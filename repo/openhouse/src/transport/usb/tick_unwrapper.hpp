// Device timestamp unwrapping (original: 0x3ced40).
//
// Devices stamp data with a free-running 48 MHz counter truncated to 24, 28 or
// 32 bits. One unwrapper is shared by all endpoints of a device so the 32-bit
// IMU stamps keep the reference fresh for 24-bit optical stamps.
#pragma once

#include <cstdint>
#include <mutex>

namespace lh::usb {

class TickUnwrapper {
 public:
  int64_t Unwrap(uint32_t raw, int bits) {
    std::lock_guard<std::mutex> lock(mutex_);
    const int64_t span = int64_t{1} << bits;
    const int64_t half = int64_t{1} << (bits - 1);
    int64_t v = (last_ & -span) | (int64_t(raw) & (span - 1));
    if (v < last_ - half) v += span;
    if (last_ + half < v) v -= span;
    // Original only moves the reference forward, except for full 32-bit stamps.
    if (bits == 32 || last_ < v) last_ = v;
    return v;
  }

  int64_t Last() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_;
  }

 private:
  mutable std::mutex mutex_;
  int64_t last_ = 0;
};

}  // namespace lh::usb
