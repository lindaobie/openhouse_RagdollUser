#include "sample_history.hpp"

#include <algorithm>
#include <cmath>

namespace lh {

void SampleHistory::Append(OpticalFrame frame) {
  // 0x3de790 uses the oldest frame's ideal period before pruning.
  if (!frames_.empty()) {
    const double cutoff = frame.syncTime - 2.95 * frames_.front().idealPeriod;
    while (!frames_.empty() && frames_.front().nextSyncTime < cutoff) frames_.pop_front();
  }
  frames_.push_back(std::move(frame));
}

double SampleHistory::ReferenceTime() const {
  if (frames_.empty()) return 0;
  const auto& last = frames_.back();
  for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
    if (it->axis == last.axis) continue;
    const double period = last.averagePeriod;
    const double delta = last.syncTime - it->syncTime;
    if (period <= 0 || std::abs(delta) < 0.125 * period) break;
    double start = it->syncTime;
    if (last.syncTime < start) start -= period;
    double aligned = double(int(delta / period)) * period + start;
    const double half = last.syncTime - 0.5 * period;
    if (aligned < half) aligned += period;
    if (0.125 * period <= std::abs(aligned - last.syncTime)) return half;
    break;
  }
  return last.syncTime;
}

bool SampleHistory::Select(std::vector<OpticalSample>* older, std::vector<OpticalSample>* newer,
                           double reference) const {
  if (frames_.size() < 4) return false;
  if (reference < 0) reference = ReferenceTime();
  struct Bracket { const OpticalSample *before = nullptr, *after = nullptr; bool collision = false; };
  Bracket brackets[2][32]{};
  for (auto f = frames_.rbegin(); f != frames_.rend(); ++f) {
    for (const auto& s : f->samples) {
      if (s.sensor < 0 || s.sensor >= 32 || s.axis < 0 || s.axis > 1) continue;
      if (!(std::abs(s.time - reference) <= f->idealPeriod + f->idealPeriod)) continue;
      auto& b = brackets[s.axis][s.sensor];
      const auto inside = [&](const OpticalSample* p) {
        return p && f->syncTime < p->time && p->time < f->nextSyncTime;
      };
      if (inside(b.before) || inside(b.after)) b.collision = true;
      else if (!b.collision) {
        if (s.time + s.width <= reference) {
          if (s.time < reference && (!b.before || b.before->time < s.time)) b.before = &s;
        } else if (!b.after || s.time + s.width < b.after->time) b.after = &s;
      }
    }
  }
  const auto valid = [](const Bracket& b) { return !b.collision && b.before && b.after; };
  const auto eligible = [&](int sensor) {
    return !requireBothAxes_ || (valid(brackets[0][sensor]) && valid(brackets[1][sensor]));
  };
  bool first = true;
  double lo = 0, hi = 0;
  for (int sensor = 0; sensor < 32; ++sensor) {
    if (!eligible(sensor)) continue;
    for (int axis = 0; axis < 2; ++axis) {
      const auto& b = brackets[axis][sensor];
      if (!valid(b)) continue;
      lo = first ? b.before->time : std::max(lo, b.before->time);
      hi = first ? b.after->time : std::min(hi, b.after->time);
      first = false;
    }
  }
  const auto interpolate = [&](double time) {
    std::vector<OpticalSample> result;
    for (int sensor = 0; sensor < 32; ++sensor) {
      if (!eligible(sensor)) continue;
      for (int axis = 0; axis < 2; ++axis) {
        const auto& b = brackets[axis][sensor];
        if (!valid(b)) continue;
        const auto &a = *b.before, &z = *b.after;
        const double t0 = 0.5 * a.width + a.time;
        OpticalSample s;
        s.sensor = z.sensor;
        s.axis = z.axis;
        s.width = (a.width + z.width) * 0.5;
        s.angle = ((time - t0) / (z.width * 0.5 + z.time - t0)) * (z.angle - a.angle) + a.angle;
        s.time = time - 0.5 * s.width;
        result.push_back(s);
      }
    }
    return result;
  };
  *older = interpolate(lo);
  *newer = interpolate(hi);
  return !older->empty() && !newer->empty();
}

}  // namespace lh
