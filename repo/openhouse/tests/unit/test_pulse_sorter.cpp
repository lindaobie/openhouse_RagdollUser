#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <vector>

#include "tracking/disambiguator_sob.hpp"
#include "tracking/pulse_sorter.hpp"
#include "tracking/tracking_source.hpp"

using lh::PulseSorter;

TEST(PulseSorterRotors, SaysWhichAreReadyAndWhichAreBeingHeard) {
  PulseSorter sorter;
  const double period = lh::sob::kPeriodTicks[0] * lh::sob::kTickSeconds;
  EXPECT_TRUE(sorter.Rotors(0).empty());
  double t = 0;
  for (int i = 0; i < 50; ++i, t += period) sorter.OnBeamWord(t, 0, 0);
  ASSERT_EQ(sorter.Rotors(t).size(), 1u);
  EXPECT_EQ(sorter.Rotors(t)[0].channel, 0);
  EXPECT_FALSE(sorter.Rotors(t)[0].ready);
  EXPECT_TRUE(sorter.Rotors(t)[0].hearing);
  for (int i = 0; i < 50; ++i, t += period) sorter.OnBeamWord(t, 0, 0);
  EXPECT_TRUE(sorter.Rotors(t)[0].ready);
  EXPECT_FALSE(sorter.Rotors(t + 2)[0].hearing);
  EXPECT_TRUE(sorter.Rotors(t + 2)[0].ready);  // a rotor doesn't stop being known
}

TEST(PulseSorterRotors, ANewGenerationStartsTheLineOver) {
  PulseSorter sorter;
  const double period = lh::sob::kPeriodTicks[0] * lh::sob::kTickSeconds;
  double t = 0;
  for (int i = 0; i < 150; ++i, t += period) sorter.OnBeamWord(t, 0, 0);
  ASSERT_TRUE(sorter.Ready(0));
  const int epoch = sorter.Rotors(t)[0].epoch;
  sorter.OnBeamWord(t, 0, 1);
  EXPECT_FALSE(sorter.Ready(0));
  EXPECT_NE(sorter.Rotors(t)[0].epoch, epoch);
}

TEST(PulseSorterScout, RecentPeriodIsTheRotorsSpeedNow) {
  PulseSorter sorter;
  const double nominal = lh::sob::kPeriodTicks[0] * lh::sob::kTickSeconds;
  double t = 0, period = 0;
  for (int i = 0; i < 100; ++i, t += nominal) sorter.OnBeamWord(t, 0, 0);
  EXPECT_FALSE(sorter.RecentPeriod(0, t, &period));  // not enough words yet
  // 2 ppm slower for 20 s: the recent period follows, the 45 s line lags behind.
  const double slower = nominal * (1 + 2e-6);
  for (int i = 0; i < 1000; ++i, t += slower) sorter.OnBeamWord(t, 0, 0);
  ASSERT_TRUE(sorter.RecentPeriod(0, t, &period));
  EXPECT_NEAR(period / nominal - 1, 2e-6, 0.05e-6);
  EXPECT_FALSE(sorter.RecentPeriod(0, t + 2, &period));  // not heard for 2 s
  EXPECT_FALSE(sorter.RecentPeriod(1, t, &period));
}

TEST(PulseSorterScout, ABadWordDoesntMoveTheRecentPeriod) {
  PulseSorter sorter;
  const double nominal = lh::sob::kPeriodTicks[0] * lh::sob::kTickSeconds;
  double t = 0, period = 0;
  for (int i = 0; i < 400; ++i, t += nominal) sorter.OnBeamWord(t, 0, 0);
  sorter.OnBeamWord(t + 300e-6, 0, 0);  // one word 300 us late: about 1.5 ppm on 5 s of them
  t += nominal;
  for (int i = 0; i < 10; ++i, t += nominal) sorter.OnBeamWord(t, 0, 0);
  ASSERT_TRUE(sorter.RecentPeriod(0, t, &period));
  EXPECT_NEAR(period / nominal - 1, 0, 0.01e-6);
  // But many in a row are the rotor: the recent line starts over on them.
  for (int i = 0; i < 200; ++i, t += nominal) sorter.OnBeamWord(t + 100e-6, 0, 0);
  ASSERT_TRUE(sorter.RecentPeriod(0, t, &period));
  EXPECT_NEAR(period / nominal - 1, 0, 0.01e-6);
}

TEST(PulseSorterScout, DriftMovesThePredictedBeamWords) {
  PulseSorter sorter;
  const double period = lh::sob::kPeriodTicks[0] * lh::sob::kTickSeconds;
  double t = 0;
  for (int i = 0; i < 150; ++i, t += period) sorter.OnBeamWord(t, 0, 0);
  std::vector<double> plain, moved;
  sorter.DueBeamWords(t + 0.1, [&](double w, int, int) { plain.push_back(w); });
  PulseSorter other;
  double u = 0;
  for (int i = 0; i < 150; ++i, u += period) other.OnBeamWord(u, 0, 0);
  other.SetDrift(0, 7e-6);
  other.DueBeamWords(u + 0.1, [&](double w, int, int) { moved.push_back(w); });
  ASSERT_EQ(plain.size(), moved.size());
  ASSERT_FALSE(plain.empty());
  for (size_t i = 0; i < plain.size(); ++i) EXPECT_NEAR(moved[i] - plain[i], 7e-6, 1e-9);
}

TEST(PulseSorterScout, AMovedLineStartsOverWhenRealWordsReturn) {
  PulseSorter sorter;
  const double period = lh::sob::kPeriodTicks[0] * lh::sob::kTickSeconds;
  double t = 0;
  for (int i = 0; i < 150; ++i, t += period) sorter.OnBeamWord(t, 0, 0);
  const int epoch = sorter.Rotors(t)[0].epoch;
  sorter.OnBeamWord(t + 3 * period, 0, 0);  // a short gap, no drift: the same line
  EXPECT_EQ(sorter.Rotors(t)[0].epoch, epoch);
  sorter.SetDrift(0, 2e-6);
  t += 3 * period + 100 * period;  // 2 s on: a visit, same generation
  sorter.OnBeamWord(t, 0, 0);
  EXPECT_NE(sorter.Rotors(t)[0].epoch, epoch);
  EXPECT_FALSE(sorter.Rotors(t)[0].ready);  // learns again, as at a new generation
}

namespace {

// Two rotors at channels 0 and 1, with each sensor hit at a fixed place per revolution.
struct Room {
  static constexpr double kWidth = 10e-6;
  PulseSorter sorter;
  double period(int channel) const { return lh::sob::kPeriodTicks[size_t(channel)] * lh::sob::kTickSeconds; }

  // Real LH2 mode: beam words and sensor words, so the sorter learns the rotors and positions.
  void TeachLh2(double from, double to) {
    for (int channel : {0, 1}) {
      for (double t = from; t < to; t += period(channel)) {
        sorter.OnBeamWord(t, channel, 0);
        sorter.OnSensorWord(t + HitPhase(channel, 3), channel, 3);
      }
    }
  }
  static double HitPhase(int channel, int sensor) { return (channel == 0 ? 2e-3 : 5e-3) + sensor * 40e-6; }
};

}  // namespace

TEST(PulseSorter, NothingBeforeItHasSeenARotor) {
  PulseSorter sorter;
  int channel = -1;
  double t = 0;
  EXPECT_EQ(sorter.Sort(1.0, 10e-6, 3, &channel, &t), PulseSorter::Result::NotOurs);
  EXPECT_FALSE(sorter.Ready(0));
}

TEST(PulseSorter, SyncFlashesAreNeverClaimed) {
  Room room;
  room.TeachLh2(100.0, 110.0);
  ASSERT_TRUE(room.sorter.Ready(0));
  int channel = -1;
  double t = 0;
  // A sync-width pulse exactly where a 2.0 hit is due is still a flash.
  const double at = 100.0 + 501 * room.period(0) + Room::HitPhase(0, 3) - 45e-6;
  EXPECT_EQ(room.sorter.Sort(at, 90e-6, 3, &channel, &t), PulseSorter::Result::NotOurs);
}

TEST(PulseSorter, ClaimsAHitForTheRightRotorUsingThePulseCentre) {
  Room room;
  room.TeachLh2(100.0, 110.0);
  const double period = room.period(1);
  const double centre = 100.0 + 501 * period + Room::HitPhase(1, 3);
  int channel = -1;
  double t = 0;
  ASSERT_EQ(room.sorter.Sort(centre - 0.5 * Room::kWidth, Room::kWidth, 3, &channel, &t), PulseSorter::Result::Hit);
  EXPECT_EQ(channel, 1);
  EXPECT_NEAR(t, centre, 1e-9);
}

TEST(PulseSorter, LeavesPulsesAwayFromAnyRotorForTheLh1Path) {
  Room room;
  room.TeachLh2(100.0, 110.0);
  int channel = -1;
  double t = 0;
  // Well off both rotors' hits on this sensor and away from the device's other hits: a 1.0
  // station's sweep, say.
  const double at = 100.0 + 501 * room.period(0) + 8e-3;
  EXPECT_EQ(room.sorter.Sort(at, Room::kWidth, 3, &channel, &t), PulseSorter::Result::NotOurs);
}

TEST(PulseSorter, A1Point0HitKeepsItsOwnPlaceEvenBesideA2Point0One) {
  Room room;
  room.TeachLh2(100.0, 110.0);
  const double period = room.period(0);
  int channel = -1, claimed = 0;
  double t = 0;
  // A 1.0 station flashing at 120 Hz, hitting sensor 3 a fixed time after each flash.
  const double flashPeriod = 1.0 / 120;
  for (int i = 0; i < 40; ++i) {
    const double flash = 110.05 + i * flashPeriod;
    room.sorter.Sort(flash, 90e-6, 7, &channel, &t);
    const double hit = flash + 4e-3 + (i % 2) * 1e-3;
    if (room.sorter.Sort(hit - 0.5 * Room::kWidth, Room::kWidth, 3, &channel, &t) == PulseSorter::Result::Hit) ++claimed;
  }
  (void)period;
  // A few may fall on a 2.0 hit's place as the two drift past each other, but it's learned to
  // be the 1.0 station's: not most.
  EXPECT_LT(claimed, 5);
  EXPECT_GT(room.sorter.Stats().byLh1, 20u);
}

TEST(PulseSorter, FollowsAHitThatDriftsBetweenRevolutions) {
  Room room;
  room.TeachLh2(100.0, 110.0);
  const double period = room.period(0);
  int channel = -1, claimed = 0;
  double t = 0;
  // Moves 20 us per revolution, about 1 m/s at 2 m: 30 revolutions move it 600 us, far outside
  // a window around where it started.
  for (int i = 0; i < 30; ++i) {
    const double at = 100.0 + (501 + i) * period + Room::HitPhase(0, 3) + i * 20e-6;
    if (room.sorter.Sort(at, Room::kWidth, 3, &channel, &t) == PulseSorter::Result::Hit && channel == 0) ++claimed;
  }
  EXPECT_EQ(claimed, 30);
}

TEST(PulseSorter, PredictsBeamWordsOnceForEachRevolution) {
  Room room;
  room.TeachLh2(100.0, 110.0);
  std::vector<double> beams;
  const auto collect = [&](double t, int channel, int generation) {
    EXPECT_EQ(generation, 0);
    if (channel == 0) beams.push_back(t);
  };
  room.sorter.DueBeamWords(111.0, collect);
  room.sorter.DueBeamWords(111.0, collect);  // nothing twice
  ASSERT_GT(beams.size(), 30u);
  EXPECT_LT(beams.size(), 70u);
  for (size_t i = 1; i < beams.size(); ++i) EXPECT_NEAR(beams[i] - beams[i - 1], room.period(0), 1e-7);
}

TEST(PulseSorter, FindsARotorAgainAfterLosingIt) {
  Room room;
  room.TeachLh2(100.0, 110.0);
  const double period = room.period(0);
  int channel = -1;
  double t = 0;
  // A second later every remembered position is stale. Hits at one place in the revolution,
  // on a sensor it has never seen, should bring the rotor back.
  PulseSorter::Result last = PulseSorter::Result::NotOurs;
  const auto hit = [&](int revolution, int sensor) {
    const double at = 100.0 + revolution * period + 3e-3 + sensor * 10e-6;
    last = room.sorter.Sort(at, Room::kWidth, sensor, &channel, &t);
  };
  for (int revolution = 600; revolution < 612; ++revolution)
    for (int sensor : {10, 11, 12}) hit(revolution, sensor);
  EXPECT_GE(room.sorter.Stats().reacquired, 1u);
  hit(612, 13);
  EXPECT_EQ(last, PulseSorter::Result::Hit);
  EXPECT_EQ(channel, 0);
}

namespace {

// The same room, with a tracker's pose: channel 0 is base 100 and channel 1 base 200, and it
// expects each hit where the room puts it, unless told otherwise.
struct PoseRoom : Room {
  static constexpr double kTwoPi = 6.283185307179586;
  bool knows[2] = {true, true};
  bool facing = true;
  double window = 0;
  std::function<double(int channel, int sensor, double angle)> adjust;

  double Angle(int channel, int sensor) const { return kTwoPi * HitPhase(channel, sensor) / period(channel); }
  void UsePose() {
    sorter.SetPose(
        [this](int sensor, double, lh::ExpectedHit* out) {
          int n = 0;
          for (int channel : {0, 1}) {
            if (!knows[channel]) continue;
            for (int axis : {0, 1}) {
              lh::ExpectedHit& e = out[n++];
              e.base = channel == 0 ? 100 : 200;
              e.lh2 = true;
              e.axis = axis;
              e.angle = Angle(channel, sensor);
              if (adjust) e.angle = adjust(channel, sensor, e.angle);
              e.facing = facing;
              e.window = window;
            }
          }
          return n;
        },
        [](int channel) { return channel == 0 ? 100u : channel == 1 ? 200u : 0u; });
  }
  PulseSorter::Result Hit(int channel, int revolution, int sensor, int* who = nullptr) {
    int found = -1;
    double t = 0;
    const auto result = sorter.Sort(100.0 + revolution * period(channel) + HitPhase(channel, sensor) - 0.5 * kWidth, kWidth, sensor, &found, &t);
    if (who) *who = found;
    return result;
  }
};

}  // namespace

TEST(PulseSorterPose, NamesAStationTimingKnowsNothingAbout) {
  PoseRoom room;
  room.TeachLh2(100.0, 110.0);
  room.UsePose();
  int channel = -1;
  // Sensor 25 has never been hit and this is well away from where the others are.
  EXPECT_EQ(room.Hit(1, 501, 25, &channel), PulseSorter::Result::Hit);
  EXPECT_EQ(channel, 1);
}

TEST(PulseSorterPose, WithoutAPoseTimingDecidesAsBefore) {
  PoseRoom room;
  room.TeachLh2(100.0, 110.0);
  room.sorter.SetPose([](int, double, lh::ExpectedHit*) { return 0; }, [](int) { return 100u; });
  int channel = -1;
  EXPECT_EQ(room.Hit(1, 501, 3, &channel), PulseSorter::Result::Hit);
  EXPECT_EQ(channel, 1);
  EXPECT_EQ(room.Hit(1, 502, 25), PulseSorter::Result::NotOurs);
}

TEST(PulseSorterPose, DropsATrackThatHasWanderedOffThePose) {
  PoseRoom room;
  room.TeachLh2(100.0, 110.0);
  // 50 mrad off for sensor 3 alone: a track on something else. The other sensors keep agreeing,
  // so it's that track that's wrong and not the pose.
  room.adjust = [](int, int sensor, double angle) { return sensor == 3 ? angle + 0.05 : angle; };
  room.UsePose();
  int agreed = 0, wandered = 0, total = 0;
  for (int revolution = 501; revolution < 620; ++revolution, ++total) {
    int channel = -1;
    // The two rotors' hits pass each other once in a while, and then it can't tell: those few
    // are dropped.
    for (int sensor : {4, 5, 6, 7}) agreed += room.Hit(0, revolution, sensor, &channel) == PulseSorter::Result::Hit && channel == 0;
    wandered += room.Hit(0, revolution, 3, &channel) == PulseSorter::Result::Hit && channel == 0;
  }
  EXPECT_GT(agreed, total * 4 * 95 / 100);
  EXPECT_EQ(wandered, 0);
  EXPECT_EQ(room.sorter.Stats().poseUntrusted, 0u);
}

TEST(PulseSorterPose, GivesAHitToTheRotorThePoseFits) {
  PoseRoom room;
  room.TeachLh2(100.0, 110.0);
  // This pulse is where sensor 3 was on channel 0, so timing says channel 0. The pose puts
  // that sensor elsewhere for channel 0, and exactly here for channel 1.
  const double at = 100.0 + 501 * room.period(0) + Room::HitPhase(0, 3);
  const double phase1 = std::fmod(at - 100.0, room.period(1));
  room.adjust = [&](int channel, int, double angle) {
    return channel == 1 ? PoseRoom::kTwoPi * phase1 / room.period(1) : angle + 0.05;
  };
  room.UsePose();
  int channel = -1;
  double t = 0;
  ASSERT_EQ(room.sorter.Sort(at - 0.5 * Room::kWidth, Room::kWidth, 3, &channel, &t), PulseSorter::Result::Hit);
  EXPECT_EQ(channel, 1);
  EXPECT_EQ(room.sorter.Stats().poseMoved, 1u);
}

TEST(PulseSorterPose, AWrongPoseLosesTheSayAndLightGetsThrough) {
  PoseRoom room;
  room.TeachLh2(100.0, 110.0);
  room.adjust = [](int, int, double angle) { return angle + 0.05; };  // everything disagrees
  room.UsePose();
  int dropped = 0, last = 0;
  // One sensor is the slowest case: its track must outlive the time trust takes to fall.
  for (int revolution = 501; revolution < 620; ++revolution) {
    if (room.Hit(0, revolution, 3) == PulseSorter::Result::Dropped) {
      ++dropped;
      last = revolution;
    }
  }
  EXPECT_GT(dropped, 10);
  EXPECT_LT(last, 600) << "the pose never lost its say";
  EXPECT_GT(room.sorter.Stats().poseUntrusted, 0u);
}

TEST(PulseSorterPose, AWiderWindowLetsThroughWhatAFastSwingMisses) {
  PoseRoom room;
  room.TeachLh2(100.0, 110.0);
  room.adjust = [](int, int, double angle) { return angle + 0.05; };
  room.UsePose();
  EXPECT_EQ(room.Hit(0, 501, 3), PulseSorter::Result::Dropped) << "50 mrad off when still";
  room.window = 0.08;
  int channel = -1;
  EXPECT_EQ(room.Hit(0, 502, 3, &channel), PulseSorter::Result::Hit) << "the pose may be that far off";
  EXPECT_EQ(channel, 0);
}

TEST(PulseSorterPose, TimingStillSpeaksForAStationThePoseHasNoViewOf) {
  PoseRoom room;
  room.TeachLh2(100.0, 110.0);
  room.knows[0] = false;  // not in the filter yet
  room.UsePose();
  int channel = -1;
  EXPECT_EQ(room.Hit(0, 501, 3, &channel), PulseSorter::Result::Hit);
  EXPECT_EQ(channel, 0);
}

TEST(PulseSorterPose, ASensorFacingAwayIsNotThatStationsHit) {
  PoseRoom room;
  room.TeachLh2(100.0, 110.0);
  room.facing = false;
  room.UsePose();
  EXPECT_EQ(room.Hit(0, 501, 3), PulseSorter::Result::Dropped);
}

TEST(PulseSorterPose, Checks1Point0HitsAgainstThePoseToo) {
  PulseSorter sorter;
  const double kFlash = 200.0, kAt = 3e-3;
  sorter.SetPose(
      [&](int, double, lh::ExpectedHit* out) {
        out[0].base = 300;
        out[0].lh2 = false;
        out[0].axis = 0;
        out[0].angle = 6.283185307179586 * kAt * 60;
        out[0].facing = true;
        return 1;
      },
      [](int) { return 0u; });
  int channel = -1;
  double t = 0;
  ASSERT_EQ(sorter.Sort(kFlash, 90e-6, 7, &channel, &t), PulseSorter::Result::NotOurs);
  EXPECT_EQ(sorter.Sort(kFlash + kAt - 5e-6, 10e-6, 3, &channel, &t), PulseSorter::Result::NotOurs) << "where the pose expects it";
  EXPECT_EQ(sorter.Sort(kFlash + 5e-3 - 5e-6, 10e-6, 3, &channel, &t), PulseSorter::Result::Dropped) << "in the sweep, but nowhere near";
}

namespace {

// A device in a room of both kinds, through TrackingSource: a 2.0 station on channel 0, timed
// from LH2 words for 3 s, then the same hits as LH1 pulses for 0.5 s. Returns the frames that
// come out of the pulses, and whether each sample of them is marked sorted.
struct SortedRun {
  int lh2Frames = 0, sortedSamples = 0, otherSamples = 0;
};
SortedRun RunMixed(bool sorting) {
  lh::TrackingSource source;
  lh::BaseConfig cfg;
  cfg.valid = true;
  cfg.serial = 0x12345678;
  cfg.cal.assign(14, 0.0);
  cfg.model = 5;
  lh::BaseDynamicState dyn;
  dyn.sobChannel = 0;
  source.SetKnownSobChannels({{0, {cfg, dyn}}});
  source.SetSorting(sorting, sorting);
  const double period = lh::sob::kPeriodTicks[0] * lh::sob::kTickSeconds;
  double t = 1.0;
  lh::OpticalFrame frame;
  for (int k = 0; k < 150; ++k, t += period) {
    source.OnLh2BeamWord({t, 0, 0, 0});
    source.OnLh2SensorWord({t + 0.2 * period, 0, 3});
    source.OnLh2SensorWord({t + 0.7 * period, 0, 4});
  }
  const double lh1From = t;
  for (int k = 0; k < 25; ++k, t += period) {
    source.OnLightPulse({t + 0.2 * period - 5e-6, 10e-6, 3});
    source.OnLightPulse({t + 0.7 * period - 5e-6, 10e-6, 4});
  }
  source.OnImu({t + 0.5, {}, {}, 0});  // the last revolution overdue: out it comes
  SortedRun run;
  while (source.PopFrame(&frame)) {
    if (frame.baseSerial != cfg.serial || frame.syncTime < lh1From - 1e-6) continue;
    ++run.lh2Frames;
    for (const auto& s : frame.samples) ++((s.flags2 & lh::kSampleSorted) ? run.sortedSamples : run.otherSamples);
  }
  return run;
}

}  // namespace

TEST(TrackingSourceSorting, Lh1PulsesFromA2Point0StationBecomeItsFrames) {
  const SortedRun run = RunMixed(true);
  EXPECT_GE(run.lh2Frames, 40);  // both planes, nearly every revolution
  EXPECT_GE(run.sortedSamples, 40);
  EXPECT_EQ(run.otherSamples, 0);
}

TEST(TrackingSourceSorting, OffByDefaultAndThenTheLh1PathAsBefore) {
  lh::TrackingSource source;
  EXPECT_FALSE(source.Sorting());
  EXPECT_TRUE(source.Rotors(0).empty());
  const SortedRun run = RunMixed(false);
  EXPECT_EQ(run.lh2Frames, 0);
}

TEST(TrackingSourceSorting, TurnedOffItForgetsTheRotors) {
  lh::TrackingSource source;
  source.SetSorting(true, true);
  const double period = lh::sob::kPeriodTicks[0] * lh::sob::kTickSeconds;
  double t = 1.0;
  for (int k = 0; k < 150; ++k, t += period) source.OnLh2BeamWord({t, 0, 0, 0});
  ASSERT_EQ(source.Rotors(t).size(), 1u);
  EXPECT_TRUE(source.Rotors(t)[0].ready);
  source.SetSorting(false, false);
  EXPECT_TRUE(source.Rotors(t).empty());
  source.SetSorting(true, true);
  EXPECT_TRUE(source.Rotors(t).empty());  // learned again from LH2 words
}

// Learning only (the switch on, before it's in use): rotors are learned and LH1 pulses go down the
// LH1 path as usual; sorting from then on needs nothing more learned.
TEST(TrackingSourceSorting, LearnsBeforeItSortsAndIsReadyWhenItDoes) {
  lh::TrackingSource source;
  source.SetSorting(true, false);
  EXPECT_FALSE(source.Sorting());
  const double period = lh::sob::kPeriodTicks[0] * lh::sob::kTickSeconds;
  double t = 1.0;
  for (int k = 0; k < 150; ++k, t += period) source.OnLh2BeamWord({t, 0, 0, 0});
  ASSERT_EQ(source.Rotors(t).size(), 1u);
  EXPECT_TRUE(source.Rotors(t)[0].ready);
  source.SetSorting(true, true);
  EXPECT_TRUE(source.Sorting());
  ASSERT_EQ(source.Rotors(t).size(), 1u);
  EXPECT_TRUE(source.Rotors(t)[0].ready);  // the same line, kept
}
