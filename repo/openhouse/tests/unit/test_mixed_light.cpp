#include <gtest/gtest.h>

#include <string>

#include "runtime/mixed_light.hpp"

using lh::runtime::ChooseMixedLight;
using lh::runtime::MixedLightStep;
using lh::runtime::MixedLightView;

namespace {

constexpr double kVisits = 45;

// A device in LH2 mode, tracking, knowing its rotors.
MixedLightView InLh2(double inMode) {
  MixedLightView v;
  v.mode = 1;
  v.inMode = inMode;
  v.sinceSent = inMode;
  v.readyRotors = 3;
  v.tracking = true;
  v.calm = true;
  return v;
}

// A device in LH1 mode, tracking, with light.
MixedLightView InLh1(double inMode) {
  MixedLightView v = InLh2(inMode);
  v.mode = 0;
  return v;
}

int Mode(const MixedLightView& v) { return ChooseMixedLight(v, kVisits).mode; }

}  // namespace

TEST(MixedLight, StartsInLh2) {
  MixedLightView v;
  EXPECT_EQ(Mode(v), 1);
  v.stayLh2 = true;
  EXPECT_EQ(Mode(v), 1);
}

TEST(MixedLight, LeavesLh2OnceItKnowsItsRotors) {
  EXPECT_EQ(Mode(InLh2(1.0)), -1);  // however ready, a visit lasts long enough to hear a new rotor
  EXPECT_EQ(Mode(InLh2(2.5)), 0);
}

TEST(MixedLight, WaitsForARotorStillBeingLearned) {
  MixedLightView v = InLh2(5.0);
  v.learningRotors = 1;
  EXPECT_EQ(Mode(v), -1);
  v.inMode = 8.0;
  EXPECT_EQ(Mode(v), 0);  // but not for ever
}

TEST(MixedLight, GivesAStationTimeToBeIdentified) {
  MixedLightView v = InLh2(20.0);
  v.unnamedRotors = 1;
  EXPECT_EQ(Mode(v), -1);
  v.inMode = 25.0;
  EXPECT_EQ(Mode(v), 0);
}

TEST(MixedLight, WithNoPoseStaysLongerToIdentifyAStation) {
  MixedLightView v = InLh2(59.0);
  v.unnamedRotors = 1;
  v.tracking = false;
  EXPECT_EQ(Mode(v), -1);
  v.inMode = 60.0;
  EXPECT_EQ(Mode(v), 0);
}

TEST(MixedLight, WaitsForTrackingToComeBack) {
  MixedLightView v = InLh2(5.0);
  v.tracking = false;
  EXPECT_EQ(Mode(v), -1);
  v.inMode = 8.0;
  EXPECT_EQ(Mode(v), 0);
}

TEST(MixedLight, StationsWakingAreWaitedFor) {
  MixedLightView v = InLh2(29.0);
  v.readyRotors = 0;
  v.tracking = false;
  v.dark = 29.0;
  EXPECT_NE(Mode(v), 0);  // LH1 mode now would see the 1.0 station first
  v.dark = v.inMode = 30.0;
  EXPECT_EQ(Mode(v), 0);
}

TEST(MixedLight, AVisitInDarknessEndsEarly) {
  MixedLightView v = InLh2(4.0);
  v.tracking = false;
  v.dark = 4.0;
  EXPECT_EQ(Mode(v), 0);
  v.inMode = 2.0;  // the switch itself takes half a second
  v.dark = 2.0;
  EXPECT_EQ(Mode(v), -1);
}

TEST(MixedLight, VisitsWhenCalm) {
  EXPECT_EQ(Mode(InLh1(44.0)), -1);
  EXPECT_EQ(Mode(InLh1(45.0)), 1);
}

TEST(MixedLight, AVisitWaitsOutMovementButNotForEver) {
  MixedLightView v = InLh1(60.0);
  v.calm = false;
  EXPECT_EQ(Mode(v), -1);
  v.inMode = 90.0;
  EXPECT_EQ(Mode(v), 1);
}

TEST(MixedLight, VisitsCanBeTurnedOff) {
  EXPECT_EQ(ChooseMixedLight(InLh1(1000.0), 0).mode, -1);
  MixedLightView lost = InLh1(1000.0);
  lost.lost = 2.0;
  EXPECT_EQ(ChooseMixedLight(lost, 0).mode, 1);  // going for help still happens
}

TEST(MixedLight, LostInLh1GoesToLh2) {
  MixedLightView v = InLh1(10.0);
  v.tracking = false;
  v.lost = 0.5;
  EXPECT_EQ(Mode(v), -1);  // long enough to recover where it is
  v.lost = 1.0;
  EXPECT_EQ(Mode(v), 1);
  v.inMode = 2.0;
  EXPECT_EQ(Mode(v), -1);  // not straight back after arriving lost
}

TEST(MixedLight, NoLightInLh1GoesToLh2) {
  MixedLightView v = InLh1(10.0);
  v.dark = 5.0;
  EXPECT_EQ(Mode(v), 1);
}

TEST(MixedLight, LooksForRotorsAgainWhenItKnowsNone) {
  MixedLightView v = InLh1(14.0);
  v.readyRotors = 0;
  EXPECT_EQ(Mode(v), -1);
  v.inMode = 15.0;
  EXPECT_EQ(Mode(v), 1);
}

TEST(MixedLight, ResendsItsModeWhileDark) {
  MixedLightView v = InLh2(10.0);
  v.readyRotors = 0;
  v.tracking = false;
  v.dark = 10.0;
  v.sinceSent = 4.0;
  EXPECT_EQ(Mode(v), -1);
  v.sinceSent = 5.0;
  const MixedLightStep step = ChooseMixedLight(v, kVisits);
  EXPECT_EQ(step.mode, 1);
  EXPECT_EQ(std::string(step.why), "re-send");
}

TEST(MixedLight, AHeadsetStaysInLh2) {
  MixedLightView v = InLh2(1000.0);
  v.stayLh2 = true;
  EXPECT_EQ(Mode(v), -1);
  v.dark = 5.0;
  v.sinceSent = 5.0;
  EXPECT_EQ(Mode(v), 1);  // re-sent, the same mode
  v.mode = 0;
  v.dark = 0;
  EXPECT_EQ(Mode(v), 1);
  v.mode = 1;
  v.tracking = false;
  v.lost = 100;
  EXPECT_EQ(Mode(v), -1);
}

TEST(MixedLight, EachKindOfDeviceHasItsOwnSwitch) {
  using lh::DeviceRole;
  using lh::runtime::MixesLight;
  lh::MixedLightOptions options;  // the defaults: controllers and trackers, never the headset
  EXPECT_FALSE(options.enabled);
  EXPECT_TRUE(MixesLight(DeviceRole::Controller, options));
  EXPECT_FALSE(MixesLight(DeviceRole::Hmd, options));
  EXPECT_TRUE(MixesLight(DeviceRole::Tracker, options));
  EXPECT_FALSE(MixesLight(DeviceRole::BaseStation, options));
  options.controllers = false;
  EXPECT_FALSE(MixesLight(DeviceRole::Controller, options));
  EXPECT_TRUE(MixesLight(DeviceRole::Tracker, options));
  options.trackers = false;
  EXPECT_FALSE(MixesLight(DeviceRole::Tracker, options));
}

namespace {
// A rotor slowing steadily, as one warming up: 0.005 ppm more each second.
constexpr double kPeriod = 0.02, kRamp = 0.005e-6;
double Warming(double t) { return kPeriod * (1 + kRamp * t); }
}  // namespace

TEST(RotorScout, FollowsTheRotorsSpeed) {
  auto s = lh::runtime::StartRotorScout(0, kPeriod);
  double drift = 0;
  for (double t = 0.25; t <= 60 + 1e-9; t += 0.25) {
    const double p = Warming(t);
    drift = lh::runtime::StepRotorScout(s, t, &p);
  }
  EXPECT_NEAR(drift, kRamp * 60 * 60 / 2, 0.05e-6);  // 9 us: the integral of the change
  EXPECT_FALSE(s.lost);
}

TEST(RotorScout, KeepsTheRateThroughAHoleThenLetsGo) {
  auto s = lh::runtime::StartRotorScout(0, kPeriod);
  double drift = 0;
  for (double t = 0.25; t <= 40 + 1e-9; t += 0.25) {
    const double p = Warming(t);
    drift = lh::runtime::StepRotorScout(s, t, &p);
  }
  const double at40 = drift, rate = s.rate;  // 0.2 ppm
  for (double t = 40.25; t <= 60 + 1e-9; t += 0.25) drift = lh::runtime::StepRotorScout(s, t, nullptr);
  EXPECT_FALSE(s.lost);
  EXPECT_NEAR(drift, at40 + rate * 20, 0.01e-6);  // the last rate kept through 20 s without the scout
  for (double t = 60.25; t <= 65 + 1e-9; t += 0.25) drift = lh::runtime::StepRotorScout(s, t, nullptr);
  EXPECT_TRUE(s.lost);
  const double frozen = drift;
  for (double t = 65.25; t <= 90 + 1e-9; t += 0.25) {
    const double p = Warming(t);
    drift = lh::runtime::StepRotorScout(s, t, &p);
  }
  EXPECT_TRUE(s.lost);  // and then the line is on its own, even when the scout comes back
  EXPECT_DOUBLE_EQ(drift, frozen);
}

TEST(RotorScout, ABadPeriodCountsAsNone) {
  auto s = lh::runtime::StartRotorScout(0, kPeriod);
  double drift = 0;
  for (double t = 0.25; t <= 40 + 1e-9; t += 0.25) {
    const double p = Warming(t);
    drift = lh::runtime::StepRotorScout(s, t, &p);
  }
  const double at40 = drift, rate = s.rate;
  const double wild = kPeriod * (1 + 5e-6);
  for (double t = 40.25; t <= 45 + 1e-9; t += 0.25) drift = lh::runtime::StepRotorScout(s, t, &wild);
  EXPECT_NEAR(drift, at40 + rate * 5, 0.01e-6);  // the rate kept, not 5 ppm
}

TEST(RotorScout, AJumpInTheScoutsPeriodIsFollowedOnlySlowly) {
  auto s = lh::runtime::StartRotorScout(0, kPeriod);
  const double jumped = kPeriod * (1 + 2e-6);  // within the 3 ppm limit, but no rotor changes so fast
  double drift = 0;
  for (double t = 0.25; t <= 5 + 1e-9; t += 0.25) drift = lh::runtime::StepRotorScout(s, t, &jumped);
  EXPECT_NEAR(s.rate, 0.25e-6, 0.01e-6);  // 0.05 ppm a second
  EXPECT_LT(drift, 1e-6);  // against 10 us if taken at once
}

TEST(MixedLight, SwitchedOffInLh1GoesToLh2AtOnce) {
  MixedLightView v = InLh1(10.0);
  v.calm = false;  // even mid-swing: it was asked for
  v.stayLh2 = true;
  const MixedLightStep step = ChooseMixedLight(v, kVisits);
  EXPECT_EQ(step.mode, 1);
  EXPECT_EQ(std::string(step.why), "stays in LH2");
}

// The original Vive headset, Vive wands, the 2017 Vive Tracker: no LH2 mode, so told to go there
// they keep sending LH1 pulses, and never a beam word.
TEST(MixedLight, TellsA1Point0OnlyDeviceApart) {
  using lh::runtime::LooksLh1Only;
  MixedLightView v;
  v.mode = 1;
  v.inMode = 8.0;
  v.lh1Pulses = true;
  EXPECT_TRUE(LooksLh1Only(v));
  v.inMode = 7.9;
  EXPECT_FALSE(LooksLh1Only(v)) << "not before the command was sent again";
  v.inMode = 30.0;
  v.everLh2 = true;
  EXPECT_FALSE(LooksLh1Only(v)) << "it has sent 2.0 light: the command was lost, the device can";
  v.everLh2 = false;
  v.lh1Pulses = false;
  EXPECT_FALSE(LooksLh1Only(v)) << "dark in LH2 mode: no 2.0 station in view, or one waking";
  v.lh1Pulses = true;
  v.mode = 0;
  EXPECT_FALSE(LooksLh1Only(v)) << "in LH1 mode, LH1 pulses are what every device sends";
  v.mode = 1;
  v.stayLh2 = true;  // a headset
  EXPECT_TRUE(LooksLh1Only(v));
}

// In use for the room's 2.0 stations, none of which has been seen: they may be off, so every device, the
// headset too, takes a short look in LH2 mode and goes back to the 1.0 light, rather than waiting blind.
TEST(MixedLight, OnlyLooksForUnseen2Point0Stations) {
  MixedLightView v = InLh2(0);
  v.readyRotors = 0;
  v.tracking = false;
  v.lh2Unseen = true;
  v.inMode = v.dark = v.sinceSent = 2.9;
  EXPECT_EQ(Mode(v), -1);
  v.inMode = v.dark = v.sinceSent = 3.0;
  EXPECT_EQ(Mode(v), 0) << "nothing seen in 3 s: back to the 1.0 light";
  v.stayLh2 = true;  // a headset
  EXPECT_EQ(Mode(v), 0);
  v.lh2Unseen = false;
  EXPECT_EQ(Mode(v), -1) << "once one is seen, the headset stays in LH2 mode";
  v.stayLh2 = false;
  EXPECT_EQ(Mode(v), -1) << "and the others wait for stations still waking";

  MixedLightView lh1 = InLh1(14.9);
  lh1.readyRotors = 0;
  lh1.lh2Unseen = true;
  lh1.stayLh2 = true;
  EXPECT_EQ(Mode(lh1), -1) << "a headset on the 1.0 light while none is seen";
  lh1.inMode = 15.0;
  EXPECT_EQ(Mode(lh1), 1) << "until the next look";
}

// A 2.0 station seen in a room learned from 1.0 stations alone, where none is placed yet: LH2 mode gives no
// pose, so a device only learns the rotors there and stays on the 1.0 light, where it can place them.
// Live, without this every device cycled 8 s blind in LH2 mode and 3 s in LH1 mode (too short to
// pick up the 1.0 light), the headset stayed in LH2 mode, and tracking never came back.
TEST(MixedLight, With2Point0StationsNotInTheRoomStaysOnThe1Point0Light) {
  MixedLightView v = InLh2(2.5);
  v.tracking = false;
  v.lost = 999;
  EXPECT_EQ(Mode(v), -1) << "placed, it would wait for a pose";
  v.lh2Unplaced = true;
  EXPECT_EQ(Mode(v), 0) << "rotors learned: back to the 1.0 light";
  v.stayLh2 = true;  // a headset
  EXPECT_EQ(Mode(v), 0);
  v.inMode = v.sinceSent = 1.0;
  EXPECT_EQ(Mode(v), -1) << "a visit still lasts long enough to learn the rotors";

  MixedLightView lh1 = InLh1(10.0);
  lh1.tracking = false;
  lh1.lost = 10.0;
  EXPECT_EQ(Mode(lh1), 1) << "placed, LH2 mode helps a lost device";
  lh1.lh2Unplaced = true;
  EXPECT_EQ(Mode(lh1), -1) << "unplaced it can't: stay and pick up the 1.0 light";
  lh1.stayLh2 = true;
  EXPECT_EQ(Mode(lh1), -1) << "the headset too";
  lh1.inMode = kVisits;
  EXPECT_EQ(Mode(lh1), 1) << "visits go on, to keep the rotors";
}

// In use only where there's a 2.0 station to sort, and not for a room whose 2.0 stations stay dark
// (switched off, or not woken): there devices would go on looking for their light.
TEST(MixedLight, InUseOnlyWithA2Point0StationToSort) {
  using lh::runtime::ChooseMixedRoom;
  using lh::runtime::MixedRoomStep;
  using lh::runtime::MixedRoomView;
  MixedRoomView v;
  v.enabled = true;
  EXPECT_EQ(ChooseMixedRoom(v), MixedRoomStep::NotInUse) << "no 2.0 station known: a 1.0-only room";
  v.lh2InRoom = true;
  EXPECT_EQ(ChooseMixedRoom(v), MixedRoomStep::InUse);
  v.enabled = false;
  EXPECT_EQ(ChooseMixedRoom(v), MixedRoomStep::NotInUse) << "switched off";
  v.enabled = true;

  v.inUse = 89;
  EXPECT_EQ(ChooseMixedRoom(v), MixedRoomStep::InUse) << "the stations may still be waking";
  v.inUse = 90;
  EXPECT_EQ(ChooseMixedRoom(v), MixedRoomStep::RoomDark);
  v.sinceLh2Light = 10;
  EXPECT_EQ(ChooseMixedRoom(v), MixedRoomStep::InUse) << "a device had 2.0 light lately";
  v.inUse = 30;
  v.sinceLh2Light = 1e300;
  EXPECT_EQ(ChooseMixedRoom(v), MixedRoomStep::InUse) << "counted from when it came into use";

  v.inUse = 600;
  v.lh2Seen = true;
  EXPECT_EQ(ChooseMixedRoom(v), MixedRoomStep::InUse) << "a 2.0 station seen this session: it's lit";
  v.lh2InRoom = false;
  EXPECT_EQ(ChooseMixedRoom(v), MixedRoomStep::InUse);
}
