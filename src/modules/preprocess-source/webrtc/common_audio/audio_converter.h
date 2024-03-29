/*
 *  Copyright (c) 2014 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_COMMON_AUDIO_AUDIO_CONVERTER_H_
#define WEBRTC_COMMON_AUDIO_AUDIO_CONVERTER_H_

// #include "webrtc/base/constructormagic.h"
// #include "webrtc/base/scoped_ptr.h"
#include "../base/constructormagic.h"
#include "../base/scoped_ptr.h"

namespace webrtc_ecnr {

// Format conversion (remixing and resampling) for audio. Only simple remixing
// conversions are supported: downmix to mono (i.e. |dst_channels| == 1) or
// upmix from mono (i.e. |src_channels == 1|).
//
// The source and destination chunks have the same duration in time; specifying
// the number of frames is equivalent to specifying the sample rates.
class AudioConverter {
 public:
  // Returns a new AudioConverter, which will use the supplied format for its
  // lifetime. Caller is responsible for the memory.
  static rtc::scoped_ptr<AudioConverter> Create(int src_channels,
                                                size_t src_frames,
                                                int dst_channels,
                                                size_t dst_frames);
  virtual ~AudioConverter() {};

  // Convert |src|, containing |src_size| samples, to |dst|, having a sample
  // capacity of |dst_capacity|. Both point to a series of buffers containing
  // the samples for each channel. The sizes must correspond to the format
  // passed to Create().
  virtual void Convert(const float* const* src, size_t src_size,
                       float* const* dst, size_t dst_capacity) = 0;

  int src_channels() const { return src_channels_; }
  size_t src_frames() const { return src_frames_; }
  int dst_channels() const { return dst_channels_; }
  size_t dst_frames() const { return dst_frames_; }

 protected:
  AudioConverter();
  AudioConverter(int src_channels, size_t src_frames, int dst_channels,
                 size_t dst_frames);

  // Helper to RTC_CHECK that inputs are correctly sized.
  void CheckSizes(size_t src_size, size_t dst_capacity) const;

 private:
  const int src_channels_;
  const size_t src_frames_;
  const int dst_channels_;
  const size_t dst_frames_;

  RTC_DISALLOW_COPY_AND_ASSIGN(AudioConverter);
};

}  // namespace webrtc_ecnr

#endif  // WEBRTC_COMMON_AUDIO_AUDIO_CONVERTER_H_
