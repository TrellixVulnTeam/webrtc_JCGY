/*
 *  Copyright (c) 2013 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef WEBRTC_VIDEO_ENGINE_OVERUSE_FRAME_DETECTOR_H_
#define WEBRTC_VIDEO_ENGINE_OVERUSE_FRAME_DETECTOR_H_

#include "webrtc/base/constructormagic.h"
#include "webrtc/base/criticalsection.h"
#include "webrtc/base/scoped_ptr.h"
#include "webrtc/base/exp_filter.h"
#include "webrtc/base/thread_annotations.h"
#include "webrtc/base/thread_checker.h"
#include "webrtc/modules/interface/module.h"

namespace webrtc {

class Clock;

// CpuOveruseObserver is called when a system overuse is detected and
// VideoEngine cannot keep up the encoding frequency.
class CpuOveruseObserver {
 public:
  // Called as soon as an overuse is detected.
  virtual void OveruseDetected() = 0;
  // Called periodically when the system is not overused any longer.
  virtual void NormalUsage() = 0;

 protected:
  virtual ~CpuOveruseObserver() {}
};

struct CpuOveruseOptions {
  CpuOveruseOptions()
      : enable_encode_usage_method(true),
        low_encode_usage_threshold_percent(55),
        high_encode_usage_threshold_percent(85),
        enable_extended_processing_usage(true),
        frame_timeout_interval_ms(1500),
        min_frame_samples(120),
        min_process_count(3),
        high_threshold_consecutive_count(2) {}

  // Method based on encode time of frames.
  bool enable_encode_usage_method;
  int low_encode_usage_threshold_percent;  // Threshold for triggering underuse.
  int high_encode_usage_threshold_percent; // Threshold for triggering overuse.
  bool enable_extended_processing_usage;  // Include a larger time span (in
                                          // addition to encode time) for
                                          // measuring the processing time of a
                                          // frame.
  // General settings.
  int frame_timeout_interval_ms;  // The maximum allowed interval between two
                                  // frames before resetting estimations.
  int min_frame_samples;  // The minimum number of frames required.
  int min_process_count;  // The number of initial process times required before
                          // triggering an overuse/underuse.
  int high_threshold_consecutive_count; // The number of consecutive checks
                                        // above the high threshold before
                                        // triggering an overuse.
};

struct CpuOveruseMetrics {
  CpuOveruseMetrics()
      : avg_encode_time_ms(-1),
        encode_usage_percent(-1) {}

  int avg_encode_time_ms;   // The average encode time in ms.
  int encode_usage_percent; // The average encode time divided by the average
                            // time difference between incoming captured frames.
};

class CpuOveruseMetricsObserver {
 public:
  virtual ~CpuOveruseMetricsObserver() {}
  virtual void CpuOveruseMetricsUpdated(const CpuOveruseMetrics& metrics) = 0;
};


// Use to detect system overuse based on the send-side processing time of
// incoming frames.
class OveruseFrameDetector : public Module {
 public:
  OveruseFrameDetector(Clock* clock,
                       const CpuOveruseOptions& options,
                       CpuOveruseObserver* overuse_observer,
                       CpuOveruseMetricsObserver* metrics_observer);
  ~OveruseFrameDetector();

  // Called for each captured frame.
  void FrameCaptured(int width, int height, int64_t capture_time_ms);

  // Called for each encoded frame.
  void FrameEncoded(int encode_time_ms);

  // Called for each sent frame.
  void FrameSent(int64_t capture_time_ms);

  // Only public for testing.
  int LastProcessingTimeMs() const;
  int FramesInQueue() const;

  // Implements Module.
  int64_t TimeUntilNextProcess() override;
  int32_t Process() override;

 private:
  class EncodeTimeAvg;
  class SendProcessingUsage;
  class FrameQueue;

  void UpdateCpuOveruseMetrics() EXCLUSIVE_LOCKS_REQUIRED(crit_);

  // TODO(asapersson): This method is only used on one thread, so it shouldn't
  // need a guard.
  void AddProcessingTime(int elapsed_ms) EXCLUSIVE_LOCKS_REQUIRED(crit_);

  // TODO(asapersson): This method is always called on the processing thread.
  // If locking is required, consider doing that locking inside the
  // implementation and reduce scope as much as possible.  We should also
  // see if we can avoid calling out to other methods while holding the lock.
  bool IsOverusing() EXCLUSIVE_LOCKS_REQUIRED(crit_);
  bool IsUnderusing(int64_t time_now) EXCLUSIVE_LOCKS_REQUIRED(crit_);

  bool FrameTimeoutDetected(int64_t now) const EXCLUSIVE_LOCKS_REQUIRED(crit_);
  bool FrameSizeChanged(int num_pixels) const EXCLUSIVE_LOCKS_REQUIRED(crit_);

  void ResetAll(int num_pixels) EXCLUSIVE_LOCKS_REQUIRED(crit_);

  // Protecting all members except const and those that are only accessed on the
  // processing thread.
  // TODO(asapersson): See if we can reduce locking.  As is, video frame
  // processing contends with reading stats and the processing thread.
  mutable rtc::CriticalSection crit_;

  const CpuOveruseOptions options_;

  // Observer getting overuse reports.
  CpuOveruseObserver* const observer_;

  // Stats metrics.
  CpuOveruseMetricsObserver* const metrics_observer_;
  CpuOveruseMetrics metrics_ GUARDED_BY(crit_);

  Clock* const clock_;
  int64_t next_process_time_;  // Only accessed on the processing thread.
  int64_t num_process_times_ GUARDED_BY(crit_);

  int64_t last_capture_time_ GUARDED_BY(crit_);

  // These six members are only accessed on the processing thread.
  int64_t last_overuse_time_;
  int checks_above_threshold_;
  int num_overuse_detections_;

  int64_t last_rampup_time_;
  bool in_quick_rampup_;
  int current_rampup_delay_ms_;

  // Number of pixels of last captured frame.
  int num_pixels_ GUARDED_BY(crit_);

  int64_t last_encode_sample_ms_;  // Only accessed by one thread.

  // TODO(asapersson): Can these be regular members (avoid separate heap
  // allocs)?
  const rtc::scoped_ptr<EncodeTimeAvg> encode_time_ GUARDED_BY(crit_);
  const rtc::scoped_ptr<SendProcessingUsage> usage_ GUARDED_BY(crit_);
  const rtc::scoped_ptr<FrameQueue> frame_queue_ GUARDED_BY(crit_);

  int64_t last_sample_time_ms_;  // Only accessed by one thread.

  rtc::ThreadChecker processing_thread_;

  DISALLOW_COPY_AND_ASSIGN(OveruseFrameDetector);
};

}  // namespace webrtc

#endif  // WEBRTC_VIDEO_ENGINE_OVERUSE_FRAME_DETECTOR_H_
