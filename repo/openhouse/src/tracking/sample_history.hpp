// CLighthouseDirect bootstrap history (0x3de790..0x3df5c6).
#pragma once

#include <deque>
#include "lh/optical.hpp"

namespace lh {

class SampleHistory {
 public:
  explicit SampleHistory(bool requireBothAxes = false) : requireBothAxes_(requireBothAxes) {}
  void Append(OpticalFrame frame);
  // Negative reference selects the original's most recent common sweep time.
  bool Select(std::vector<OpticalSample>* older, std::vector<OpticalSample>* newer,
              double reference = -1) const;
  size_t Size() const { return frames_.size(); }
  double ReferenceTime() const;

 private:
  std::deque<OpticalFrame> frames_;
  bool requireBothAxes_;
};

}  // namespace lh
