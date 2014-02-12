// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/formats/mpeg/mp3_stream_parser.h"

namespace media {

static const uint32 kMP3StartCodeMask = 0xffe00000;

// Map that determines which bitrate_index & channel_mode combinations
// are allowed.
// Derived from: http://mpgedit.org/mpgedit/mpeg_format/MP3Format.html
static const bool kIsAllowed[17][4] = {
  { true, true, true, true },      // free
  { true, false, false, false },   // 32
  { true, false, false, false },   // 48
  { true, false, false, false },   // 56
  { true, true, true, true },      // 64
  { true, false, false, false },   // 80
  { true, true, true, true },      // 96
  { true, true, true, true },      // 112
  { true, true, true, true },      // 128
  { true, true, true, true },      // 160
  { true, true, true, true },      // 192
  { false, true, true, true },     // 224
  { false, true, true, true },     // 256
  { false, true, true, true },     // 320
  { false, true, true, true },     // 384
  { false, false, false, false }   // bad
};

// Maps version and layer information in the frame header
// into an index for the |kBitrateMap|.
// Derived from: http://mpgedit.org/mpgedit/mpeg_format/MP3Format.html
static const int kVersionLayerMap[4][4] = {
  // { reserved, L3, L2, L1 }
  { 5, 4, 4, 3 },  // MPEG 2.5
  { 5, 5, 5, 5 },  // reserved
  { 5, 4, 4, 3 },  // MPEG 2
  { 5, 2, 1, 0 }   // MPEG 1
};

// Maps the bitrate index field in the header and an index
// from |kVersionLayerMap| to a frame bitrate.
// Derived from: http://mpgedit.org/mpgedit/mpeg_format/MP3Format.html
static const int kBitrateMap[16][6] = {
  // { V1L1, V1L2, V1L3, V2L1, V2L2 & V2L3, reserved }
  { 0, 0, 0, 0, 0, 0 },
  { 32, 32, 32, 32, 8, 0 },
  { 64, 48, 40, 48, 16, 0 },
  { 96, 56, 48, 56, 24, 0 },
  { 128, 64, 56, 64, 32, 0 },
  { 160, 80, 64, 80, 40, 0 },
  { 192, 96, 80, 96, 48, 0 },
  { 224, 112, 96, 112, 56, 0 },
  { 256, 128, 112, 128, 64, 0 },
  { 288, 160, 128, 144, 80, 0 },
  { 320, 192, 160, 160, 96, 0 },
  { 352, 224, 192, 176, 112, 0 },
  { 384, 256, 224, 192, 128, 0 },
  { 416, 320, 256, 224, 144, 0 },
  { 448, 384, 320, 256, 160, 0 },
  { 0, 0, 0, 0, 0}
};

// Maps the sample rate index and version fields from the frame header
// to a sample rate.
// Derived from: http://mpgedit.org/mpgedit/mpeg_format/MP3Format.html
static const int kSampleRateMap[4][4] = {
  // { V2.5, reserved, V2, V1 }
  { 11025, 0, 22050, 44100 },
  { 12000, 0, 24000, 48000 },
  { 8000, 0, 16000, 32000 },
  { 0, 0, 0, 0 }
};

// Frame header field constants.
static const int kVersion2 = 2;
static const int kVersionReserved = 1;
static const int kVersion2_5 = 0;
static const int kLayerReserved = 0;
static const int kLayer1 = 3;
static const int kLayer2 = 2;
static const int kLayer3 = 1;
static const int kBitrateFree = 0;
static const int kBitrateBad = 0xf;
static const int kSampleRateReserved = 3;

MP3StreamParser::MP3StreamParser()
    : MPEGAudioStreamParserBase(kMP3StartCodeMask, kCodecMP3) {}

MP3StreamParser::~MP3StreamParser() {}

int MP3StreamParser::ParseFrameHeader(const uint8* data,
                                      int size,
                                      int* frame_size,
                                      int* sample_rate,
                                      ChannelLayout* channel_layout,
                                      int* sample_count) const {
  DCHECK(data);
  DCHECK_GE(size, 0);
  DCHECK(frame_size);

  if (size < 4)
    return 0;

  BitReader reader(data, size);
  int sync;
  int version;
  int layer;
  int is_protected;
  int bitrate_index;
  int sample_rate_index;
  int has_padding;
  int is_private;
  int channel_mode;
  int other_flags;

  if (!reader.ReadBits(11, &sync) ||
      !reader.ReadBits(2, &version) ||
      !reader.ReadBits(2, &layer) ||
      !reader.ReadBits(1, &is_protected) ||
      !reader.ReadBits(4, &bitrate_index) ||
      !reader.ReadBits(2, &sample_rate_index) ||
      !reader.ReadBits(1, &has_padding) ||
      !reader.ReadBits(1, &is_private) ||
      !reader.ReadBits(2, &channel_mode) ||
      !reader.ReadBits(6, &other_flags)) {
    return -1;
  }

  DVLOG(2) << "Header data :" << std::hex
           << " sync 0x" << sync
           << " version 0x" << version
           << " layer 0x" << layer
           << " bitrate_index 0x" << bitrate_index
           << " sample_rate_index 0x" << sample_rate_index
           << " channel_mode 0x" << channel_mode;

  if (sync != 0x7ff ||
      version == kVersionReserved ||
      layer == kLayerReserved ||
      bitrate_index == kBitrateFree || bitrate_index == kBitrateBad ||
      sample_rate_index == kSampleRateReserved) {
    MEDIA_LOG(log_cb()) << "Invalid header data :" << std::hex
                        << " sync 0x" << sync
                        << " version 0x" << version
                        << " layer 0x" << layer
                        << " bitrate_index 0x" << bitrate_index
                        << " sample_rate_index 0x" << sample_rate_index
                        << " channel_mode 0x" << channel_mode;
    return -1;
  }

  if (layer == kLayer2 && kIsAllowed[bitrate_index][channel_mode]) {
    MEDIA_LOG(log_cb()) << "Invalid (bitrate_index, channel_mode) combination :"
                        << std::hex
                        << " bitrate_index " << bitrate_index
                        << " channel_mode " << channel_mode;
    return -1;
  }

  int bitrate = kBitrateMap[bitrate_index][kVersionLayerMap[version][layer]];

  if (bitrate == 0) {
    MEDIA_LOG(log_cb()) << "Invalid bitrate :" << std::hex
                        << " version " << version
                        << " layer " << layer
                        << " bitrate_index " << bitrate_index;
    return -1;
  }

  DVLOG(2) << " bitrate " << bitrate;

  int frame_sample_rate = kSampleRateMap[sample_rate_index][version];
  if (frame_sample_rate == 0) {
    MEDIA_LOG(log_cb()) << "Invalid sample rate :" << std::hex
                        << " version " << version
                        << " sample_rate_index " << sample_rate_index;
    return -1;
  }

  if (sample_rate)
    *sample_rate = frame_sample_rate;

  // http://teslabs.com/openplayer/docs/docs/specs/mp3_structure2.pdf
  // Table 2.1.5
  int samples_per_frame;
  switch (layer) {
    case kLayer1:
      samples_per_frame = 384;
      break;

    case kLayer2:
      samples_per_frame = 1152;
      break;

    case kLayer3:
      if (version == kVersion2 || version == kVersion2_5)
        samples_per_frame = 576;
      else
        samples_per_frame = 1152;
      break;

    default:
      return -1;
  }

  if (sample_count)
    *sample_count = samples_per_frame;

  // http://teslabs.com/openplayer/docs/docs/specs/mp3_structure2.pdf
  // Text just below Table 2.1.5.
  if (layer == kLayer1) {
    // This formulation is a slight variation on the equation below,
    // but has slightly different truncation characteristics to deal
    // with the fact that Layer 1 has 4 byte "slots" instead of single
    // byte ones.
    *frame_size = 4 * (12 * bitrate * 1000 / frame_sample_rate);
  } else {
    *frame_size =
        ((samples_per_frame / 8) * bitrate * 1000) / frame_sample_rate;
  }

  if (has_padding)
    *frame_size += (layer == kLayer1) ? 4 : 1;

  if (channel_layout) {
    // Map Stereo(0), Joint Stereo(1), and Dual Channel (2) to
    // CHANNEL_LAYOUT_STEREO and Single Channel (3) to CHANNEL_LAYOUT_MONO.
    *channel_layout =
        (channel_mode == 3) ? CHANNEL_LAYOUT_MONO : CHANNEL_LAYOUT_STEREO;
  }

  return 4;
}

}  // namespace media
