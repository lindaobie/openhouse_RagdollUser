// End-to-end replay smoke test, NOT a ground-truth pose comparison.
#include "common/test_paths.hpp"
#include <gtest/gtest.h>
#include <filesystem>
#include <cmath>
#include <map>

#include "lh/tracker.hpp"
#include "raw_capture.hpp"
#include "usb_transport.hpp"

TEST(TrackingReplay, ProducesFinitePosesWithoutSteamVRRuntime) {
  const auto path = TestCapture("controller_static_10s.lhraw");
  if (!std::filesystem::exists(path)) GTEST_SKIP() << "no capture fixture";
  lh::Tracker tracker, world;
  lh::RigidPose placement;
  placement.position = {1, 2, 3};
  world.SetBasePose(0xA101B835, placement); world.SetBasePose(0xE236DC40, placement);
  struct Tee : lh::InputSinkBase {
    lh::Tracker &a, &b;
    Tee(lh::Tracker& first, lh::Tracker& second) : a(first), b(second) {}
    void OnDeviceAttached(const lh::DeviceId& id, const std::string& c) override { a.OnDeviceAttached(id, c); b.OnDeviceAttached(id, c); }
    void OnClockOffset(const lh::DeviceId& id, double t) override { a.OnClockOffset(id, t); b.OnClockOffset(id, t); }
    void OnImu(const lh::DeviceId& id, const lh::ImuSample& s) override { a.OnImu(id, s); b.OnImu(id, s); }
    void OnLightPulse(const lh::DeviceId& id, const lh::LightPulse& p) override { a.OnLightPulse(id, p); b.OnLightPulse(id, p); }
  } tee(tracker, world);
  lh::usb::RawCaptureReader reader;
  ASSERT_TRUE(reader.Open(path));
  lh::usb::ReportDecoder decoder(&tee);
  std::map<std::string, size_t> counts;
  std::map<std::string, double> times;
  const auto drain = [&] {
    const auto local = tracker.PollPoses(), global = world.PollPoses();
    ASSERT_EQ(local.size(), global.size());
    for (size_t index = 0; index < local.size(); ++index) {
      const auto& pose = local[index];
      EXPECT_FALSE(pose.worldSpace); EXPECT_TRUE(global[index].worldSpace);
      EXPECT_EQ(pose.time, global[index].time);
      // Publishing a world pose normalizes the quaternion product again.
      for (size_t axis = 0; axis < 4; ++axis)
        EXPECT_NEAR(pose.pose.orientation[axis], global[index].pose.orientation[axis], 1e-14);
      for (size_t axis = 0; axis < 3; ++axis)
        EXPECT_NEAR(global[index].pose.position[axis], pose.pose.position[axis] + placement.position[axis], 1e-12);
      ++counts[pose.device];
      if (times.contains(pose.device)) {
        EXPECT_LE(times[pose.device], pose.time);
      }
      times[pose.device] = pose.time;
      double q2 = 0;
      for (double p : pose.pose.position) EXPECT_TRUE(std::isfinite(p));
      for (double q : pose.pose.orientation) { EXPECT_TRUE(std::isfinite(q)); q2 += q * q; }
      EXPECT_NEAR(q2, 1, 1e-10);
      if (pose.status != lh::TrackingStatus::Optical) {
        // Bootstrap interpolates an older common time: the HMD's first pose can
        // already be >50 ms behind the newest received IMU sample.
        EXPECT_EQ(pose.status, lh::TrackingStatus::Inertial);
        EXPECT_EQ(pose.opticalUpdates, 101u);
      }
    }
  };
  ASSERT_TRUE(reader.ReadAll([&](const auto& e) { decoder.AddEndpoint(e); },
                            [&](const auto& r) { decoder.OnReport(r); drain(); }));
  drain();
  EXPECT_GT(counts["LHR-0704E07A"], 100u);
  EXPECT_GT(counts["LHR-13DA81D1"], 100u);
  tracker.OnDeviceDetached("LHR-13DA81D1");
  const auto detached = tracker.PollPoses();
  ASSERT_EQ(detached.size(), 1u);
  EXPECT_EQ(detached[0].device, "LHR-13DA81D1");
  EXPECT_EQ(detached[0].status, lh::TrackingStatus::Lost);
}
