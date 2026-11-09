#include <gtest/gtest.h>

#include <cstddef>
#include <deque>
#include <random>

#include "oracle.hpp"
#include "tracking/sample_history.hpp"

namespace {

// Original uses the same libstdc++ deque/vector ABI and these POD record layouts.
struct OriginalHistory {
  std::deque<lh::OpticalFrame> frames;
  bool requireBoth;
};
static_assert(sizeof(lh::OpticalFrame) == 0x68);
static_assert(sizeof(lh::OpticalSample) == 0x28);
static_assert(offsetof(lh::OpticalFrame, samples) == 0x50);
static_assert(offsetof(OriginalHistory, requireBoth) == 0x50);

TEST(SampleHistoryOracle, SelectionAndPruningMatch) {
  for (bool both : {false, true}) {
    OriginalHistory original{{}, both};
    lh::SampleHistory mine(both);
    std::mt19937 rng(27);
    std::uniform_real_distribution<double> u(0, 1);
    for (int step = 0; step < 200; ++step) {
      lh::OpticalFrame f;
      f.baseSerial = 123;
      f.axis = step % 2;
      f.syncTime = 1 + step / 120.0;
      f.nextSyncTime = f.syncTime + 1.0 / 120;
      f.idealPeriod = f.averagePeriod = 1.0 / 60;
      for (int sensor = 0; sensor < 12; ++sensor) {
        if (u(rng) < 0.1) continue;
        lh::OpticalSample s;
        s.sensor = sensor;
        s.axis = f.axis;
        s.width = 0.000005 + u(rng) * 0.000001;
        s.time = f.syncTime + 0.002 + sensor * 0.0001;
        s.angle = 1 + sensor * 0.01 + step * 0.001;
        f.samples.push_back(s);
      }
      oracle::Fn<void (*)(OriginalHistory*, const lh::OpticalFrame*)>(0x3def20)(&original, &f);
      mine.Append(f);
      ASSERT_EQ(mine.Size(), original.frames.size());
      ASSERT_DOUBLE_EQ(mine.ReferenceTime(),
                       oracle::Fn<double (*)(OriginalHistory*)>(0x3de850)(&original));
      for (double reference : {-1.0, f.syncTime - 0.005}) {
        std::vector<lh::OpticalSample> oa, ob, ma, mb;
        const bool ok = oracle::Fn<bool (*)(double, OriginalHistory*, void*, void*)>(0x3df2e0)(
            reference, &original, &oa, &ob);
        ASSERT_EQ(ok, mine.Select(&ma, &mb, reference)) << step << " both=" << both;
        for (int i = 0; i < 2; ++i) {
          const auto &o = i ? ob : oa, &m = i ? mb : ma;
          ASSERT_EQ(o.size(), m.size()) << step;
          for (size_t k = 0; k < o.size(); ++k) {
            EXPECT_EQ(o[k].sensor, m[k].sensor);
            EXPECT_EQ(o[k].axis, m[k].axis);
            EXPECT_DOUBLE_EQ(o[k].time, m[k].time);
            EXPECT_DOUBLE_EQ(o[k].angle, m[k].angle);
            EXPECT_DOUBLE_EQ(o[k].width, m[k].width);
          }
        }
        if (HasFailure()) return;
      }
    }
  }
}

}  // namespace
