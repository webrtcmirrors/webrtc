/*
 *  Copyright (c) 2012 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "api/video/encoded_image.h"

#include <string.h>

namespace webrtc {

// FFmpeg's decoder, used by H264DecoderImpl, requires up to 8 bytes padding due
// to optimized bitstream readers. See avcodec_decode_video2.
const size_t EncodedImage::kBufferPaddingBytesH264 = 8;

size_t EncodedImage::GetBufferPaddingBytes(VideoCodecType codec_type) {
  switch (codec_type) {
    case kVideoCodecH264:
      return kBufferPaddingBytesH264;
    default:
      return 0;
  }
}

EncodedImage::EncodedImage() : EncodedImage(nullptr, 0, 0) {}

EncodedImage::EncodedImage(EncodedImage&&) = default;
EncodedImage::EncodedImage(const EncodedImage&) = default;

EncodedImage::EncodedImage(uint8_t* buffer, size_t size, size_t capacity)
    : size_(size), buffer_(buffer), capacity_(capacity) {}

EncodedImage::~EncodedImage() = default;

EncodedImage& EncodedImage::operator=(EncodedImage&&) = default;
EncodedImage& EncodedImage::operator=(const EncodedImage&) = default;

void EncodedImage::Retain() {
  if (buffer_) {
    encoded_data_ = std::vector<uint8_t>(size_);
    memcpy(encoded_data_.data(), buffer_, size_);
    buffer_ = nullptr;
  }
}

void EncodedImage::SetEncodeTime(int64_t encode_start_ms,
                                 int64_t encode_finish_ms) {
  timing_.encode_start_ms = encode_start_ms;
  timing_.encode_finish_ms = encode_finish_ms;
}
}  // namespace webrtc
