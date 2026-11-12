// Not stock: tells which LH1 pulses are hits from 2.0 base stations, in a room with both kinds
// (ai/driver/mixed_stations.md).
//
// In LH1 light mode a device reports every laser hit as a plain pulse, 2.0 stations' too, but
// with no sign of whose it is. Each 2.0 rotor keeps a very steady speed of its own, so where
// a pulse falls within a rotor's revolution says whose it is. This turns those pulses into the
// words the LH2 path takes: a hit time per sensor, and a beam word per revolution predicted
// from the rotor. So no other part of tracking needs to know, and what isn't claimed can still
// go down the LH1 path.
//
// A rotor's speed and phase come from real beam words, so a device needs a visit to LH2 mode
// first, and again now and then (runtime/mixed_light.hpp). On a moving device a hit shifts
// between revolutions, so each sensor's position within the revolution is followed, along with
// how fast the whole channel's positions are moving.
//
// Timing alone can follow the wrong thing for seconds: nothing about a pulse says a track has
// wandered onto another station's sweep. So when tracking has a pose, SetPose lets it say where
// each sensor's hit should fall for each station. While most of what timing claims agrees, the
// pose decides; once it stops agreeing the pose is what's suspect, and timing decides again
// (better wrong light reaching the filter, which can notice, than light kept from it).
#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <vector>

namespace lh {

// Where the tracker's pose puts a sensor's hit for one plane of a base it is tracking.
struct ExpectedHit {
  uint32_t base = 0;
  bool lh2 = true;
  int axis = 0;
  double angle = 0;     // as OpticalSample::angle
  bool facing = false;  // false: from this pose this base can't hit this sensor
  double window = 0;    // radians a hit may land from `angle`; 0: the sorter's narrowest
};

class PulseSorter {
 public:
  enum class Result {
    NotOurs,  // no rotor claims it: a 1.0 station's, or unknown
    Hit,      // a 2.0 hit: channel and hitTime are set
    Dropped,  // could be more than one rotor's: better lost than wrong
  };
  static constexpr int kMaxExpected = 8;
  // Fills `out` with up to kMaxExpected entries, returns how many: none without a usable pose.
  using PoseView = std::function<int(int sensor, double t, ExpectedHit* out)>;
  using ChannelBase = std::function<uint32_t(int channel)>;

  // Real LH2 words: where rotors and hit positions are learned.
  void OnBeamWord(double t, int channel, int generation);
  void OnSensorWord(double t, int channel, int sensor);

  // An LH1 pulse.
  Result Sort(double start, double width, int sensor, int* channel, double* hitTime);
  // Calls emit(time, channel, generation) for each predicted beam word up to `upTo`.
  void DueBeamWords(double upTo, const std::function<void(double, int, int)>& emit);

  void SetPose(PoseView view, ChannelBase base) {
    poseView_ = std::move(view);
    channelBase_ = std::move(base);
  }

  struct Counters {
    uint64_t byTrack = 0, byBurst = 0, dropped = 0, notOurs = 0, syncWidth = 0, beams = 0, reacquired = 0, byLh1 = 0;
    // What the pose did to timing's answer.
    uint64_t poseAgreed = 0, poseMoved = 0, poseVetoed = 0, poseTied = 0, poseUntrusted = 0;
  };
  const Counters& Stats() const { return counters_; }
  // Whether this channel has enough real beam words to predict.
  bool Ready(int channel) const;
  // The rotors heard so far. hearing: a real beam word within the last second before `now`.
  // epoch: changes each time the rotor's line starts over. lastWord, lastClaim: device times of
  // its latest real beam word and of the latest pulse claimed for it.
  struct RotorState {
    int channel;
    bool ready, hearing;
    int epoch = 0;
    double lastWord = 0, lastClaim = -1;
  };
  std::vector<RotorState> Rotors(double now) const;

  // The scout (runtime/mixed_light.hpp). The rotor's period over the last few seconds of real beam
  // words, if it has a real beam word within a second of `now`.
  bool RecentPeriod(int channel, double now, double* period) const;
  // Seconds added to every time the rotor's line predicts, until real beam words come again (after a
  // gap, the line then starts over).
  void SetDrift(int channel, double drift);

 private:
  struct Track {  // one sensor, one plane, one channel
    double phase = 0;
    double last = -1;
  };
  struct Burst {  // one plane of one channel: where the device as a whole is in the revolution
    double centre = 0;
    double halfWidth = 0;
    double shift = 0;  // seconds per revolution
    double last = -1;
  };
  // A rotor's timing, from its real beam words: beam word n at anchor + a + drift + n * b, by fading
  // least squares, with a second fit over the last few seconds for the scout.
  struct RotorLine {
    double anchor = 0, a = 0, b = 0;
    double drift = 0;  // SetDrift
    long words = 0;
    double lastWord = 0;
    double s0 = 0, sn = 0, sy = 0, snn = 0, sny = 0;  // the line
    double q0 = 0, qn = 0, qy = 0, qnn = 0, qny = 0;  // the last few seconds' (RecentPeriod)
    long qWords = 0;
    int qSkipped = 0;       // words in a row left out of those as too far off
    double lastRecent = 0;  // the latest word taken into them
    void Start(double t, double nominalPeriod);
    long Add(double t);  // a real beam word; returns its revolution number
    bool RecentPeriod(double now, double* period) const;
    double Phase(double t) const;  // seconds into the revolution at t
    int Plane(double phase) const { return phase >= 0.5 * b ? 1 : 0; }
  };
  struct Rotor {
    bool started = false;
    int generation = -1;
    int epoch = 0;
    RotorLine line;
    double lastClaim = -1;  // latest LH1 pulse claimed for it
    long nextBeam = 0;
    std::array<std::array<Track, 2>, 32> tracks{};
    std::array<Burst, 2> bursts{};
    bool Ready() const;
  };
  // Whose a pulse is, before anything is learned from it.
  struct Guess {
    enum class Kind { Nobody, Track, Burst, Lh1Track, Lh1Burst, Ambiguous } kind = Kind::Nobody;
    int channel = -1, plane = 0;
    double phase = 0, residual = 0, revolutions = 0;
  };
  // A pulse against a sensor's track: how far it lands from where the track and its burst's shift put
  // it, how many revolutions on, and how far it may.
  struct TrackFit {
    double residual = 0, revolutions = 0, window = 0;
  };

  // period: a 2.0 rotor's revolution, whose phase wraps; a 1.0 station's time since its flash doesn't.
  static TrackFit Fit(const Track& track, const Burst& burst, double phase, double t, double period, bool wrap);
  static void Follow(Track& track, Burst& burst, double phase, double t, double residual, double revolutions, bool matched,
                     double wrapPeriod);
  void Learn(Rotor& r, int plane, int sensor, double phase, double t, double residual, double revolutions, bool matched);
  void Reacquire(double now);
  void LearnLh1(int slot, int sensor, double phase, double t, double residual, double revolutions, bool matched);
  bool InSweep(double t) const;
  Guess ByTiming(double t, int sensor) const;
  Guess ByPose(double t, int sensor, const Guess& timing);
  Guess ClaimRotor(int channel, double t, int sensor) const;
  Guess ClaimLh1(double t, int sensor) const;
  Result Apply(const Guess& guess, double t, int sensor, int* channel, double* hitTime);

  std::map<int, Rotor> rotors_;
  int epochs_ = 0;
  // A 1.0 station's hits compete for each pulse, or they'd be taken for a 2.0 hit whenever one
  // is due nearby. Timed from its flashes: two flashes a revolution, one per rotor.
  std::array<std::array<Track, 2>, 32> lh1Tracks_{};
  std::array<Burst, 2> lh1Bursts_{};
  double lastFlash_ = -1;
  int flashIndex_ = 0;
  std::deque<double> unclaimed_;  // times of recent pulses nobody claimed
  double nextReacquire_ = 0;
  PoseView poseView_;
  ChannelBase channelBase_;
  double poseTrust_ = 1;  // how much of what timing claims the pose agrees with, lately
  Counters counters_;
};

}  // namespace lh
