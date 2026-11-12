// Tests for DirectFilter's correction-rejection diagnostics.
//
// The EKF maths, the limits and their order are covered by the oracle tests; what is
// tested here is the diagnostic layer added on top of RejectCorrection: which limit
// refused a frame, and (for every limit) how far over it the correction was. A log
// that says "rejected" without that cannot tell a threshold problem from a calibration
// problem, which is exactly what it is for.
#include <gtest/gtest.h>

#include <cmath>

#include "tracking/direct_filter.hpp"

namespace {

constexpr double kDeg = 0.017453292519943295;

lh::direct::DirectFilter PreConvergence() {
  lh::direct::DirectFilter f;
  f.updateCount = 0;  // the 10 deg / 0.05 g limits
  return f;
}

lh::direct::Vec Zero() { return lh::direct::Vec::Zero(49); }

// Every refusal must leave exactly one reason and, where the limit is a value, the
// value and the limit it was compared with. A stale reason between calls is the
// failure mode this guards: a log would attribute one frame's rejection to the next.
TEST(RejectDiagnostics, AcceptsAGoodCorrectionAndClearsTheReason) {
  auto f = PreConvergence();
  const auto dx = Zero();
  EXPECT_FALSE(f.RejectCorrection(dx));
  EXPECT_EQ(f.lastRejectReason, lh::direct::kRejectNone);
  EXPECT_EQ(f.lastRejectSlot, -1);
  EXPECT_DOUBLE_EQ(f.lastRejectOver, 0.0);
  EXPECT_DOUBLE_EQ(f.lastRejectLimit, 0.0);
}

TEST(RejectDiagnostics, NamesANonFiniteCorrection) {
  auto f = PreConvergence();
  auto dx = Zero();
  dx[0] = std::nan("");
  ASSERT_TRUE(f.RejectCorrection(dx));
  EXPECT_EQ(f.lastRejectReason, lh::direct::kRejectNaN);
  EXPECT_EQ(f.lastRejectSlot, -1);
}

TEST(RejectDiagnostics, ImuRotationReportsTheAngleAndItsLimit) {
  auto f = PreConvergence();
  auto dx = Zero();
  dx[28] = 12.0 * kDeg;
  ASSERT_TRUE(f.RejectCorrection(dx));
  EXPECT_EQ(f.lastRejectReason, lh::direct::kRejectImuRot);
  EXPECT_NEAR(f.lastRejectOver, 12.0 * kDeg, 1e-9);
  EXPECT_NEAR(f.lastRejectLimit, 10.0 * kDeg, 1e-9);
}

TEST(RejectDiagnostics, ImuRotationUsesTheConvergedLimitAfterConvergence) {
  auto f = PreConvergence();
  f.updateCount = 300;  // past 0x105: the converged limits
  auto dx = Zero();
  dx[28] = 3.0 * kDeg;  // under 10 deg, over the converged 2 deg
  ASSERT_TRUE(f.RejectCorrection(dx));
  EXPECT_EQ(f.lastRejectReason, lh::direct::kRejectImuRot);
  EXPECT_NEAR(f.lastRejectOver, 3.0 * kDeg, 1e-9);
  EXPECT_NEAR(f.lastRejectLimit, 2.0 * kDeg, 1e-9);
}

TEST(RejectDiagnostics, GravityMagnitudeReportsTheOffsetAndItsLimit) {
  auto f = PreConvergence();
  auto dx = Zero();
  dx[18] = 0.6;  // m/s^2 off 9.80665
  ASSERT_TRUE(f.RejectCorrection(dx));
  EXPECT_EQ(f.lastRejectReason, lh::direct::kRejectGravMag);
  EXPECT_NEAR(f.lastRejectOver, 0.6, 1e-9);
  EXPECT_NEAR(f.lastRejectLimit, 0.4903325, 1e-9);
}

TEST(RejectDiagnostics, GravityTiltReportsTheTiltAndItsLimit) {
  auto f = PreConvergence();
  f.updateCount = 300;  // below 0xc9 the tilt test is skipped
  auto dx = Zero();
  dx[16] = 0.1;  // ~5.7 deg of tilt
  ASSERT_TRUE(f.RejectCorrection(dx));
  EXPECT_EQ(f.lastRejectReason, lh::direct::kRejectGravTilt);
  EXPECT_NEAR(f.lastRejectOver, 0.1, 1e-9);
  EXPECT_NEAR(f.lastRejectLimit, 0.06981317007977318, 1e-9);
}

TEST(RejectDiagnostics, AngularRateReportsTheRateAndItsLimit) {
  auto f = PreConvergence();
  f.omegaCorrectionLimit2 = 0.01;
  auto dx = Zero();
  dx[13] = 0.2;
  ASSERT_TRUE(f.RejectCorrection(dx));
  EXPECT_EQ(f.lastRejectReason, lh::direct::kRejectOmega);
  EXPECT_NEAR(f.lastRejectOver, 0.2, 1e-9);
  EXPECT_NEAR(f.lastRejectLimit, 0.1, 1e-9);
}

TEST(RejectDiagnostics, AlternateBasePositionReportsTheSlotAndTheDistance) {
  auto f = PreConvergence();
  f.altBaseActive[0] = true;
  auto dx = Zero();
  dx[lh::direct::kAltBaseIndex[0] + 1] = 0.2;  // 20 cm against a 10 cm limit
  ASSERT_TRUE(f.RejectCorrection(dx));
  EXPECT_EQ(f.lastRejectReason, lh::direct::kRejectAltBaseTrans);
  EXPECT_EQ(f.lastRejectSlot, 0);
  EXPECT_NEAR(f.lastRejectOver, 0.2, 1e-9);
  EXPECT_NEAR(f.lastRejectLimit, 0.1, 1e-9);
}

TEST(RejectDiagnostics, AlternateBaseRotationReportsTheSlotAndTheAngle) {
  auto f = PreConvergence();
  f.altBaseActive[0] = true;
  auto dx = Zero();
  dx[lh::direct::kAltBaseIndex[0] + 3] = 2.0 * kDeg;
  ASSERT_TRUE(f.RejectCorrection(dx));
  EXPECT_EQ(f.lastRejectReason, lh::direct::kRejectAltBaseRot);
  EXPECT_EQ(f.lastRejectSlot, 0);
  EXPECT_NEAR(f.lastRejectOver, 2.0 * kDeg, 1e-9);
  EXPECT_NEAR(f.lastRejectLimit, 1.0 * kDeg, 1e-9);
}

// The checks run in a fixed order, and the diagnostic must report the one that actually
// refused the frame, not a later one also over its limit.
TEST(RejectDiagnostics, ReportsTheFirstLimitThatRefusesTheFrame) {
  auto f = PreConvergence();
  f.altBaseActive[0] = true;
  auto dx = Zero();
  dx[28] = 12.0 * kDeg;                         // IMU rotation, checked first
  dx[lh::direct::kAltBaseIndex[0] + 3] = 2.0 * kDeg;  // also over its own limit
  ASSERT_TRUE(f.RejectCorrection(dx));
  EXPECT_EQ(f.lastRejectReason, lh::direct::kRejectImuRot);
  EXPECT_EQ(f.lastRejectSlot, -1);
}

TEST(RejectDiagnostics, ACleanFrameAfterARefusalClearsTheSlotAndTheValue) {
  auto f = PreConvergence();
  f.altBaseActive[0] = true;
  auto dx = Zero();
  dx[lh::direct::kAltBaseIndex[0] + 3] = 2.0 * kDeg;
  ASSERT_TRUE(f.RejectCorrection(dx));
  dx.setZero();
  EXPECT_FALSE(f.RejectCorrection(dx));
  EXPECT_EQ(f.lastRejectReason, lh::direct::kRejectNone);
  EXPECT_EQ(f.lastRejectSlot, -1);
  EXPECT_DOUBLE_EQ(f.lastRejectOver, 0.0);
  EXPECT_DOUBLE_EQ(f.lastRejectLimit, 0.0);
}

// A refused correction has to be printed in the unit its limit is written in: the
// rotation limits are radians in the filter, an alternate base's position is metres.
// Printing 0.03 "deg" for a radian limit - or a radian limit as "cm" - would make the
// number unreadable, which is the whole point of recording it.
TEST(RejectDiagnostics, RefusalUnitsMatchTheLimitTheyWereComparedWith) {
  using lh::direct::RejectUnitsFor;
  const auto imuRot = RejectUnitsFor(lh::direct::kRejectImuRot);
  EXPECT_DOUBLE_EQ(imuRot.scale, 57.29577951308232);
  EXPECT_STREQ(imuRot.unit, "deg");
  const auto gravTilt = RejectUnitsFor(lh::direct::kRejectGravTilt);
  EXPECT_STREQ(gravTilt.unit, "deg");
  const auto altRot = RejectUnitsFor(lh::direct::kRejectAltBaseRot);
  EXPECT_STREQ(altRot.unit, "deg");
  const auto altTrans = RejectUnitsFor(lh::direct::kRejectAltBaseTrans);
  EXPECT_DOUBLE_EQ(altTrans.scale, 100.0);
  EXPECT_STREQ(altTrans.unit, "cm");
  const auto gravMag = RejectUnitsFor(lh::direct::kRejectGravMag);
  EXPECT_DOUBLE_EQ(gravMag.scale, 1.0);
  EXPECT_STREQ(gravMag.unit, "m/s2");
  const auto omega = RejectUnitsFor(lh::direct::kRejectOmega);
  EXPECT_DOUBLE_EQ(omega.scale, 1.0);
  EXPECT_STREQ(omega.unit, "rad/s");
}

}  // namespace
