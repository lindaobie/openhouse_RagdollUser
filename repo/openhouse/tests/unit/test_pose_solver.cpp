#include <gtest/gtest.h>

#include "tracking/lighthouse_model.hpp"
#include "tracking/pose_solver.hpp"

#include <Eigen/Geometry>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {

lh::TrackedObjectModel Object(bool planar = false) {
  lh::TrackedObjectModel obj;
  obj.points = {{{0, 0, 0}}, {{0.1, 0, 0}}, {{0, 0.1, 0}}, {{0, 0, planar ? 0 : 0.1}}};
  for (int i = 0; i < 4; ++i) obj.channelToIndex[i] = i;
  return obj;
}

std::vector<lh::OpticalSample> Samples(const lh::TrackedObjectModel& obj) {
  std::vector<lh::OpticalSample> samples;
  const double cal[7] = {};
  for (int i = 0; i < 4; ++i) {
    const auto& p = obj.points[size_t(i)];
    for (int axis = 0; axis < 2; ++axis) {
      lh::OpticalSample s;
      s.sensor = i;
      s.axis = axis;
      s.angle = lh::model::Lh1Angle(axis, p[0], p[1], p[2] - 2, cal);
      samples.push_back(s);
    }
  }
  return samples;
}

TEST(PoseSolver, SufficiencyRequiresBothAxesAndNonCoplanarGeometry) {
  const auto obj = Object();
  auto samples = Samples(obj);
  EXPECT_TRUE(lh::solver::SamplesSufficient(obj, samples));
  samples.pop_back();
  EXPECT_FALSE(lh::solver::SamplesSufficient(obj, samples));
  const auto planar = Object(true);
  EXPECT_FALSE(lh::solver::SamplesSufficient(planar, Samples(planar)));
  EXPECT_FALSE(lh::solver::SamplesSufficient(obj, {}));
}

TEST(PoseSolver, ExactSyntheticPoseAndErrorDefinition) {
  const auto obj = Object();
  lh::BaseConfig base;
  base.cal.resize(14);
  const auto result = lh::solver::Solve(obj, base, Samples(obj));
  ASSERT_EQ(result.status, 0);
  EXPECT_NEAR(result.pose.p[0], 0, 1e-10);
  EXPECT_NEAR(result.pose.p[1], 0, 1e-10);
  EXPECT_NEAR(result.pose.p[2], -2, 1e-10);
  EXPECT_NEAR(result.pose.q[0], 1, 1e-10);
  EXPECT_LT(result.cost, 1e-20);
  const double range = std::hypot(result.pose.p[0], result.pose.p[1], result.pose.p[2]);
  EXPECT_DOUBLE_EQ(result.error, std::sqrt(range * result.cost));
}

TEST(PoseSolver, NonFiniteLineSearchCostBecomesAnOrderedBehindPenalty) {
  using lh::solver::detail::LineSearchCostOrPenalty;
  constexpr double kBehindPenalty = 1e50;
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();

  EXPECT_DOUBLE_EQ(LineSearchCostOrPenalty(2.5, 0.4), 2.5);
  EXPECT_DOUBLE_EQ(LineSearchCostOrPenalty(nan, 0.382), 1.382 * kBehindPenalty);
  EXPECT_DOUBLE_EQ(LineSearchCostOrPenalty(inf, 0.5), 1.5 * kBehindPenalty);
  EXPECT_TRUE(std::isfinite(LineSearchCostOrPenalty(nan, 0.618)));
  EXPECT_GT(LineSearchCostOrPenalty(nan, 0.618), LineSearchCostOrPenalty(nan, 0.382));
}

TEST(PoseSolver, RejectsInsufficientInputBeforeOptimization) {
  const auto obj = Object();
  lh::BaseConfig base;
  base.cal.resize(14);
  auto result = lh::solver::Solve(obj, base, {});
  EXPECT_EQ(result.status, 2);
  EXPECT_EQ(result.iterations, 0);
  base.cal.resize(13);
  result = lh::solver::Solve(obj, base, Samples(obj));
  EXPECT_EQ(result.status, 2);
  EXPECT_EQ(result.lineSearchEvals, 0);
}

// A puck-like device lying on its side, as on a bed: sensors around a ring and on
// both faces, with outward normals.
lh::TrackedObjectModel Puck() {
  lh::TrackedObjectModel obj;
  for (int i = 0; i < 18; ++i) {
    const double a = i * M_PI / 9;
    const double z = (i % 3 == 0) ? 0.025 : (i % 3 == 1 ? -0.025 : 0.0);
    const double r = 0.04 + 0.02 * std::fmod(i * 0.618, 1.0);  // irregular, like a real device
    obj.points.push_back({{r * std::cos(a), 0.8 * r * std::sin(a), z}});
    Eigen::Vector3d n(std::cos(a), std::sin(a), z * 20);
    n.normalize();
    obj.normals.push_back({{n.x(), n.y(), n.z()}});
    obj.channelToIndex[i] = i;
  }
  obj.points.push_back({{0, 0, 0.03}}); obj.normals.push_back({{0, 0, 1}}); obj.channelToIndex[18] = 18;
  obj.points.push_back({{0, 0, -0.03}}); obj.normals.push_back({{0, 0, -1}}); obj.channelToIndex[19] = 19;
  return obj;
}

// Lit sensors of `obj` at `pose` (facing the base by more than cos 0.3), exact angles.
std::vector<lh::OpticalSample> Seen(const lh::TrackedObjectModel& obj, const Eigen::Isometry3d& pose, std::vector<int>* away) {
  std::vector<lh::OpticalSample> samples;
  const double cal[7] = {};
  for (size_t i = 0; i < obj.points.size(); ++i) {
    const Eigen::Vector3d p = pose * Eigen::Vector3d(obj.points[i].data());
    const Eigen::Vector3d n = pose.rotation() * Eigen::Vector3d(obj.normals[i].data());
    const double facing = n.dot(-p.normalized());
    if (facing < 0.3) { if (facing < -0.1 && away) away->push_back(int(i)); continue; }
    for (int axis = 0; axis < 2; ++axis) {
      lh::OpticalSample s;
      s.sensor = int(i);
      s.axis = axis;
      s.angle = lh::model::Lh1Angle(axis, p.x(), p.y(), p.z(), cal);
      samples.push_back(s);
    }
  }
  return samples;
}

// The stock solve starts 2 m straight ahead, unrotated; a device lying on its side
// below the base is out of its reach. Multi-start finds it, and a reflected hit on a
// sensor that faces away from the base (the sheet under it) is left out.
TEST(PoseSolver, MultiStartFindsADeviceOnItsSideAndLeavesOutReflections) {
  const auto obj = Puck();
  lh::BaseConfig base;
  base.cal.resize(14);
  Eigen::Isometry3d truth = Eigen::Isometry3d::Identity();
  truth.linear() = (Eigen::AngleAxisd(1.9, Eigen::Vector3d::UnitX()) * Eigen::AngleAxisd(2.4, Eigen::Vector3d::UnitZ())).toRotationMatrix();
  truth.translation() = Eigen::Vector3d(0.4, -1.1, -1.5);
  std::vector<int> away;
  auto samples = Seen(obj, truth, &away);
  ASSERT_GE(samples.size(), 10u);
  ASSERT_FALSE(away.empty());
  ASSERT_TRUE(lh::solver::SamplesSufficient(obj, samples));
  // The reflection: a sensor on the far side "sees" the sweep, a few degrees off.
  const int reflected = away.front();
  const Eigen::Vector3d rp = truth * Eigen::Vector3d(obj.points[size_t(reflected)].data());
  const double cal[7] = {};
  for (int axis = 0; axis < 2; ++axis) {
    lh::OpticalSample s;
    s.sensor = reflected; s.axis = axis;
    s.angle = lh::model::Lh1Angle(axis, rp.x(), rp.y(), rp.z(), cal) + (axis ? 0.04 : -0.03);
    samples.push_back(s);
  }
  const auto stock = lh::solver::Solve(obj, base, samples);
  const bool stockRight = stock.status == 0 && (Eigen::Vector3d(stock.pose.p.data()) - truth.translation()).norm() < 0.01;
  EXPECT_FALSE(stockRight) << "the fixture should defeat the stock start";
  bool ambiguous = true;
  std::vector<int> dropped;
  const auto found = lh::solver::SolveMultiStart(obj, base, samples, &ambiguous, &dropped);
  ASSERT_EQ(found.status, 0);
  EXPECT_FALSE(ambiguous);
  EXPECT_LT((Eigen::Vector3d(found.pose.p.data()) - truth.translation()).norm(), 0.002);
  const Eigen::Quaterniond q(found.pose.q[0], found.pose.q[1], found.pose.q[2], found.pose.q[3]);
  EXPECT_LT(q.angularDistance(Eigen::Quaterniond(truth.rotation())), 0.005);
  EXPECT_NE(std::find(dropped.begin(), dropped.end(), reflected), dropped.end());
}

}  // namespace
