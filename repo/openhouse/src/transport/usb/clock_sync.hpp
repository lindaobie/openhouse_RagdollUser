// Host <-> device clock offset estimator (original: 0x3cf2c0).
//
// offset ≈ hostTime - deviceTime. Follows decreases (lower latency) 10x faster
// than increases, so it tracks the minimum transport latency.
#pragma once

#include <cmath>
#include <mutex>

namespace lh::usb {

class ClockSync {
 public:
  static constexpr double kUsbLatency = 0.0009;    // 0x1a9a20
  static constexpr double kResetError = 0.05;      // 0x1a96e8
  static constexpr double kStaleAfter = 0.5;       // 0x1a9b90
  static constexpr double kGainDown = 0.025;       // 0x1a9640
  static constexpr double kGainUp = 0.0025;        // 0x1a95c8

  // hostT: host read time minus transport latency (0.9 ms wired, 4.4 ms wireless);
  // deviceTime: newest sample stamp.
  void Update(double hostT, double deviceTime) {
    std::lock_guard<std::mutex> lock(mutex_);
    const double err = (hostT - deviceTime) - offset_;
    if (offset_ == 0.0 || hostT - lastUpdate_ > kStaleAfter || std::fabs(err) > kResetError) {
      offset_ = hostT - deviceTime;
    } else {
      offset_ += (err < 0.0 ? kGainDown : kGainUp) * err;
    }
    lastUpdate_ = hostT;
  }

  double Offset() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return offset_;
  }

 private:
  mutable std::mutex mutex_;
  double offset_ = 0.0;
  double lastUpdate_ = 0.0;
};

}  // namespace lh::usb
