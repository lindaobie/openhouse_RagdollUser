#include <gtest/gtest.h>

#include <nlohmann/json.hpp>
#include "lh/tracker.hpp"
#include "tracking/lighthouse_direct.hpp"
#include "tracking/lighthouse_model.hpp"

namespace {

lh::TrackedObjectModel Object() {
  lh::TrackedObjectModel model;
  model.points = {{{-0.08, -0.07, -0.02}}, {{0.08, -0.07, 0.02}},
                  {{-0.08, 0.07, 0.03}}, {{0.08, 0.07, -0.03}},
                  {{0, 0, 0.08}}, {{0.04, 0.01, -0.06}}};
  for (size_t i = 0; i < model.points.size(); ++i) {
    model.normals.push_back({0, 0, 1});
    model.channelToIndex[int(i)] = int(i);
  }
  return model;
}

std::string Config() {
  const auto m = Object();
  return nlohmann::json{{"device_class", "hmd"}, {"lighthouse_config", {
    {"modelPoints", m.points}, {"modelNormals", m.normals}}}}.dump();
}

lh::OpticalFrame Frame(const lh::TrackedObjectModel& m, int step, uint32_t serial = 123) {
  lh::OpticalFrame f;
  f.baseSerial = serial; f.axis = step % 2;
  f.syncTime = 1 + step / 120.0;
  f.nextSyncTime = f.syncTime + 1.0 / 120;
  f.idealPeriod = f.averagePeriod = 1.0 / 60;
  f.leftSyncIsReal = f.rightSyncIsReal = true;
  const double cal[7]{};
  for (size_t i = 0; i < m.points.size(); ++i) {
    const auto& p = m.points[i];
    lh::OpticalSample s;
    s.sensor = int(i); s.axis = f.axis; s.width = 5e-6;
    s.time = f.syncTime + 0.004 + i * 1e-5;
    s.angle = lh::model::Lh1Angle(s.axis, p[0], p[1], p[2] - 2, cal);
    f.samples.push_back(s);
  }
  return f;
}

TEST(TrackerCore, SyntheticBootstrapOpticalImuAndLoss) {
  const auto model = Object();
  lh::LighthouseDirect tracker(model);
  lh::BaseConfig base;
  base.valid = true; base.serial = 123; base.cal.resize(14);
  lh::BaseDynamicState dynamic;
  dynamic.accelDir = {0, 1, 0};
  tracker.OnBaseConfig(base, dynamic);
  double imuTime = 1;
  for (int step = 0; step < 100; ++step) {
    const auto f = Frame(model, step);
    while (imuTime < f.nextSyncTime) {
      lh::ImuSample imu;
      imu.time = imuTime; imu.accel = {0, 9.80665, 0};
      tracker.OnImu(imu); imuTime += 0.001;
    }
    tracker.OnFrame(f);
  }
  const auto pose = tracker.Pose();
  ASSERT_TRUE(pose);
  EXPECT_EQ(pose->status, lh::TrackingStatus::Optical);
  EXPECT_GT(pose->opticalUpdates, 150u);
  EXPECT_NEAR(pose->pose.position[0], 0, 1e-5);
  EXPECT_NEAR(pose->pose.position[1], 0, 1e-5);
  EXPECT_NEAR(pose->pose.position[2], -2, 1e-5);
  const auto& filter = tracker.Filter();
  EXPECT_TRUE(filter.P.allFinite());
  EXPECT_LT((filter.P - filter.P.transpose()).cwiseAbs().maxCoeff(), 1e-10);
  for (int i = 0; i < 700; ++i) {
    lh::ImuSample imu;
    imu.time = imuTime; imu.accel = {0, 9.80665, 0};
    tracker.OnImu(imu); tracker.AdvanceInertial(imuTime - 0.02);
    imuTime += 0.001;
  }
  ASSERT_TRUE(tracker.Pose());
  EXPECT_EQ(tracker.Pose()->status, lh::TrackingStatus::Lost);
}

TEST(TrackerCore, AddsASecondaryBaseWithoutChangingPrimaryFrame) {
  const auto model = Object();
  lh::LighthouseDirect tracker(model);
  lh::BaseConfig base;
  base.valid = true; base.serial = 123; base.cal.resize(14);
  tracker.OnBaseConfig(base, {});
  base.serial = 456; tracker.OnBaseConfig(base, {});
  // The original only adds an alternate base once updateCount > 260.
  for (int step = 0; step < 200; ++step) tracker.OnFrame(Frame(model, step));
  ASSERT_TRUE(tracker.Pose());
  ASSERT_EQ(tracker.Pose()->primaryBase, 123u);
  ASSERT_GT(tracker.Filter().updateCount, 0x104u);
  for (int step = 200; step < 220; ++step) {
    auto frame = Frame(model, step, 456);
    for (auto& s : frame.samples) {
      const auto& p = model.points[size_t(s.sensor)];
      s.angle = lh::model::Lh1Angle(s.axis, p[0] + 0.3, p[1] - 0.1, p[2] - 2.4, base.cal.data());
    }
    tracker.OnFrame(frame);
  }
  EXPECT_EQ(tracker.Filter().n, 37);
  EXPECT_TRUE(tracker.Filter().altBaseActive[0]);
  EXPECT_EQ(tracker.Pose()->primaryBase, 123u);
  EXPECT_NEAR(tracker.Filter().x[31], 0.3, 1e-4);
  EXPECT_NEAR(tracker.Filter().x[32], -0.1, 1e-4);
  EXPECT_NEAR(tracker.Filter().x[33], -0.4, 1e-4);
  EXPECT_TRUE(tracker.Filter().P.allFinite());
}

TEST(TrackerCore, RecordedTiltSeedsStateAndUncertainty) {
  const auto model = Object();
  lh::LighthouseDirect tracker(model);
  lh::BaseConfig base;
  base.valid = true; base.serial = 123; base.cal.resize(14);
  tracker.OnBaseConfig(base, {});
  tracker.SetRecordedTilt(123, {0.1, -0.2}, 1e-5);
  for (int step = 0; step < 20 && !tracker.Pose(); ++step) tracker.OnFrame(Frame(model, step));
  ASSERT_TRUE(tracker.Pose());
  EXPECT_DOUBLE_EQ(tracker.Filter().x[16], 0.1);
  EXPECT_DOUBLE_EQ(tracker.Filter().x[17], -0.2);
  EXPECT_DOUBLE_EQ(tracker.Filter().P(16, 16), 1e-5);
  EXPECT_DOUBLE_EQ(tracker.Filter().P(17, 17), 1e-5);
}

TEST(TrackerSession, ConfigLifecycleAndDatabaseValidation) {
  lh::Tracker tracker;
  tracker.OnDeviceAttached("A", "{}");
  ASSERT_EQ(tracker.Devices().size(), 1u);
  EXPECT_FALSE(tracker.Devices()[0].configured);
  EXPECT_FALSE(tracker.Devices()[0].error.empty());
  tracker.OnDeviceAttached("A", Config());
  EXPECT_TRUE(tracker.Devices()[0].configured);
  EXPECT_TRUE(tracker.Devices()[0].error.empty());
  tracker.OnDeviceAttached("A", "");  // second endpoint must not reset configuration
  EXPECT_TRUE(tracker.Devices()[0].configured);
  std::string error;
  EXPECT_TRUE(tracker.LoadBasePoses(R"({"bases":[{"serial":123,"pose":[0,0,0,1,1,2,3]}]})", &error));
  EXPECT_FALSE(tracker.LoadBasePoses(R"({"bases":[{"serial":123,"pose":[0,0,0,0,1,2,3]}]})", &error));
  EXPECT_FALSE(error.empty());
  const auto learned = R"({"bases":[],"known_objects":[{"serialNumber":"A","imu":{"gyro_bias":[0.1,0.2,0.3],"acc_bias":[0,0,0],"acc_scale":[1,1,1]}}]})";
  EXPECT_TRUE(tracker.LoadBasePoses(learned, &error));
  auto badCalibration = nlohmann::json::parse(learned);
  badCalibration["known_objects"][0]["imu"]["acc_scale"][1] = 0;
  EXPECT_FALSE(tracker.LoadBasePoses(badCalibration.dump(), &error));
  EXPECT_THROW(tracker.SetBasePose(0, {}), std::invalid_argument);
  tracker.OnDeviceDetached("A");
  EXPECT_FALSE(tracker.Devices()[0].attached);
  EXPECT_TRUE(tracker.PollPoses().empty());
  tracker.OnDeviceAttached("A", Config());
  EXPECT_TRUE(tracker.Devices()[0].configured);
}

TEST(TrackedObject, LegacyImuPoseAndValidation) {
  auto j = nlohmann::json::parse(Config());
  j["acc_bias"] = {1, 2, 3};
  j["trackref_from_imu"] = {0, 0, 0, 1, 0.1, 0.2, 0.3};
  lh::TrackedObjectModel model;
  ASSERT_TRUE(lh::TrackedObjectModel::FromJson(j.dump(), &model));
  EXPECT_EQ(model.accBias[1], 2);
  const auto imu = model.InImuFrame();
  EXPECT_NEAR(imu.points[0][0], model.points[0][0] - 0.1, 1e-15);
  j["lighthouse_config"]["modelNormals"] = nlohmann::json::array();
  EXPECT_FALSE(lh::TrackedObjectModel::FromJson(j.dump(), &model));
}

}  // namespace
