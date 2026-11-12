#include "tracking/pulse_sorter.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

#include "tracking/disambiguator_sob.hpp"

namespace lh {
namespace {

constexpr double kSyncMin = 56e-6, kSyncMax = 140e-6;  // sync flash widths, as the session's
constexpr double kMemory = 45.0;        // seconds a beam word's weight takes to fade by 1/e
constexpr long kReadyWords = 100;       // about 2 s of real beam words before predicting
// RecentPeriod: a rotor's speed now, for the scout. 5 s of words give it to about 0.01 ppm.
constexpr double kScoutMemory = 5.0;
constexpr long kScoutWords = 150;
constexpr double kRecentOutlier = 20e-6;  // s off the recent line (good words: about 1 us)
constexpr int kRecentSkips = 25;
constexpr double kJump = 500e-6;        // a beam word this far off: the rotor changed
constexpr double kTrackLife = 0.5;      // seconds a hit position stays useful
constexpr double kTight = 30e-6;        // a sensor's hit from one revolution to the next
constexpr double kPerRevolution = 3e-6;  // extra room per missed revolution
constexpr double kMaxWindow = 200e-6;
constexpr double kBurstMargin = 100e-6;  // a new sensor, beside the device's other hits
constexpr double kMinHalfWidth = 100e-6;
constexpr double kMaxShift = 400e-6;
constexpr double kFlashGroup = 1e-3;    // sync pulses this close are one flash
constexpr double kFlashLife = 0.02;     // no flash for this long: no 1.0 station in view
constexpr double kLh1Period = 1.0 / 60; // a 1.0 rotor's revolution
constexpr double kLh1SweepFrom = 1e-3, kLh1SweepTo = 7.4e-3;  // after a flash
constexpr double kReacquireEvery = 0.05;
constexpr double kReacquireSpan = 0.25;  // seconds of unclaimed pulses to look through
constexpr double kReacquireBin = 150e-6;
constexpr int kReacquireHits = 12;       // in one bin of the revolution
// Spread over at least this many revolutions. Neighbouring channels differ by only about 42 us
// per revolution, so a shorter run would also pass for the wrong rotor.
constexpr double kReacquireMinRevs = 8;
constexpr double kTwoPi = 6.283185307179586;
// How far a hit may land from where the pose puts it, in radians of a rotor's turn, at the least
// (ExpectedHit::window widens it while moving fast). In frames that fit, under 1% of hits are
// beyond 5 mrad; a 2.0 rotor's turn at 15 mrad is about 45 us.
constexpr double kPoseWindow = 0.015;
constexpr double kLh1PoseExtra = 0.010;   // a 1.0 station is often placed less well in the room
constexpr double kPoseClearly = 2.5;      // two fit: the better must be this much nearer
constexpr double kTrustRate = 0.01;       // about the last 100 hits timing was sure of
constexpr double kTrustMin = 0.6;

double Wrap(double x, double period) {
  x = std::fmod(x + 0.5 * period, period);
  if (x < 0) x += period;
  return x - 0.5 * period;
}

// A track's or burst's last hit is recent enough to go by.
bool Fresh(double last, double t) { return last >= 0 && t - last <= kTrackLife; }

}  // namespace

// ---- A rotor's line ---------------------------------------------------------------------------

void PulseSorter::RotorLine::Start(double t, double nominalPeriod) {
  *this = RotorLine{};
  anchor = t;
  b = nominalPeriod;
  lastWord = t;
}

long PulseSorter::RotorLine::Add(double t) {
  const long n = std::lround((t - anchor - a) / b);
  const double y = t - anchor;
  const double fade = std::exp(-(t - lastWord) / kMemory);
  s0 = s0 * fade + 1;
  sn = sn * fade + double(n);
  sy = sy * fade + y;
  snn = snn * fade + double(n) * double(n);
  sny = sny * fade + double(n) * y;
  // A word far off the recent line is a bad one, and one of a few hundred us would move a few seconds'
  // period by 1-2 ppm (one headset had some), so it's left out; many in a row are a real change.
  bool recentWord = true;
  const double qdet = q0 * qnn - qn * qn;
  if (qWords >= 50 && qdet > 0) {
    const double qb = (q0 * qny - qn * qy) / qdet;
    const double qa = (qy - qb * qn) / q0;
    if (std::fabs(y - qa - qb * double(n)) > kRecentOutlier) {
      if (++qSkipped <= kRecentSkips) {
        recentWord = false;
      } else {
        q0 = qn = qy = qnn = qny = 0;
        qWords = 0;
        qSkipped = 0;
      }
    } else {
      qSkipped = 0;
    }
  }
  if (recentWord) {
    const double recent = qWords ? std::exp(-(t - lastRecent) / kScoutMemory) : 0;
    q0 = q0 * recent + 1;
    qn = qn * recent + double(n);
    qy = qy * recent + y;
    qnn = qnn * recent + double(n) * double(n);
    qny = qny * recent + double(n) * y;
    ++qWords;
    lastRecent = t;
  }
  lastWord = t;
  ++words;
  const double det = s0 * snn - sn * sn;
  if (words >= 20 && det > 0) {
    b = (s0 * sny - sn * sy) / det;
    a = (sy - b * sn) / s0;
  }
  return n;
}

bool PulseSorter::RotorLine::RecentPeriod(double now, double* period) const {
  if (qWords < kScoutWords || now - lastRecent > 1.0) return false;
  const double det = q0 * qnn - qn * qn;
  if (!(det > 0)) return false;
  *period = (q0 * qny - qn * qy) / det;
  return true;
}

double PulseSorter::RotorLine::Phase(double t) const {
  const double n = std::floor((t - anchor - a - drift) / b);
  return t - (anchor + a + drift + b * n);
}

bool PulseSorter::Rotor::Ready() const { return line.words >= kReadyWords; }

// ---- Rotors and their words -------------------------------------------------------------------

bool PulseSorter::Ready(int channel) const {
  const auto it = rotors_.find(channel);
  return it != rotors_.end() && it->second.Ready();
}

std::vector<PulseSorter::RotorState> PulseSorter::Rotors(double now) const {
  std::vector<RotorState> out;
  for (const auto& [channel, r] : rotors_)
    out.push_back({channel, r.Ready(), now - r.line.lastWord < 1.0, r.epoch, r.line.lastWord, r.lastClaim});
  return out;
}

bool PulseSorter::RecentPeriod(int channel, double now, double* period) const {
  const auto it = rotors_.find(channel);
  return it != rotors_.end() && it->second.line.RecentPeriod(now, period);
}

void PulseSorter::SetDrift(int channel, double drift) {
  const auto it = rotors_.find(channel);
  if (it != rotors_.end()) it->second.line.drift = drift;
}

void PulseSorter::OnBeamWord(double t, int channel, int generation) {
  if (channel < 0 || channel >= sob::kChannels) return;
  Rotor& r = rotors_[channel];
  const bool gap = r.started && t - r.line.lastWord > 1.0;
  const bool jumped = r.started && r.Ready() &&
                      std::fabs(Wrap(t - r.line.anchor - r.line.a - r.line.drift, r.line.b)) > kJump;
  // A new generation (bit 27) comes at almost every visit to LH2 mode; the line starts over. So does
  // one the scout moved, which can't take real words into its sums.
  if (!r.started || r.generation != generation || jumped || (gap && r.line.drift != 0)) {
    r = Rotor{};
    r.started = true;
    r.generation = generation;
    r.epoch = ++epochs_;
    r.line.Start(t, double(sob::kPeriodTicks[size_t(channel)]) * sob::kTickSeconds);
  }
  r.nextBeam = r.line.Add(t) + 1;
}

void PulseSorter::OnSensorWord(double t, int channel, int sensor) {
  if (sensor < 0 || sensor >= 32) return;
  const auto it = rotors_.find(channel);
  if (it == rotors_.end() || !it->second.Ready()) return;
  Rotor& r = it->second;
  const double phase = r.line.Phase(t);
  Learn(r, r.line.Plane(phase), sensor, phase, t, 0, 0, false);
}

void PulseSorter::DueBeamWords(double upTo, const std::function<void(double, int, int)>& emit) {
  for (auto& [ch, r] : rotors_) {
    if (!r.Ready()) continue;
    const RotorLine& l = r.line;
    // After a long gap, only the current revolution: the 2.0 path counts the rest as missed.
    const long now = long(std::floor((upTo - l.anchor - l.a - l.drift) / l.b));
    if (now - r.nextBeam > 100) r.nextBeam = now;
    while (true) {
      const double t = l.anchor + l.a + l.drift + l.b * double(r.nextBeam);
      if (t > upTo) break;
      emit(t, ch, r.generation);
      ++r.nextBeam;
      ++counters_.beams;
    }
  }
}

// ---- Tracks and bursts, 2.0 and 1.0 alike -----------------------------------------------------

PulseSorter::TrackFit PulseSorter::Fit(const Track& track, const Burst& burst, double phase, double t, double period, bool wrap) {
  TrackFit fit;
  fit.revolutions = std::round((t - track.last) / period);
  const double off = phase - track.phase - burst.shift * fit.revolutions;
  fit.residual = wrap ? Wrap(off, period) : off;
  fit.window = std::min(kMaxWindow, kTight + 0.5 * std::fabs(burst.shift) + kPerRevolution * fit.revolutions);
  return fit;
}

// A pulse taken for this track: the track moves to it, and its burst follows, with how fast the
// device's hits move per revolution from a matched track.
void PulseSorter::Follow(Track& track, Burst& burst, double phase, double t, double residual, double revolutions,
                         bool matched, double wrapPeriod) {
  if (matched && revolutions >= 1)
    burst.shift = std::clamp(burst.shift + 0.15 * residual / revolutions, -kMaxShift, kMaxShift);
  track.phase = phase;
  track.last = t;
  if (!Fresh(burst.last, t)) {
    burst.centre = phase;
    burst.halfWidth = kMinHalfWidth;
    burst.shift = 0;
  } else {
    const double d = wrapPeriod > 0 ? Wrap(phase - burst.centre, wrapPeriod) : phase - burst.centre;
    burst.centre += 0.1 * d;
    burst.halfWidth = std::max({kMinHalfWidth, std::fabs(d), burst.halfWidth * 0.998});
  }
  burst.last = t;
}

void PulseSorter::Learn(Rotor& r, int plane, int sensor, double phase, double t, double residual, double revolutions, bool matched) {
  Follow(r.tracks[size_t(sensor)][size_t(plane)], r.bursts[size_t(plane)], phase, t, residual, revolutions, matched, r.line.b);
}

void PulseSorter::LearnLh1(int slot, int sensor, double phase, double t, double residual, double revolutions, bool matched) {
  Follow(lh1Tracks_[size_t(sensor)][size_t(slot)], lh1Bursts_[size_t(slot)], phase, t, residual, revolutions, matched, 0);
}

bool PulseSorter::InSweep(double t) const {
  const double since = t - lastFlash_;
  return lastFlash_ >= 0 && since < kFlashLife && since > kLh1SweepFrom && since < kLh1SweepTo;
}

// ---- Whose a pulse is -------------------------------------------------------------------------

PulseSorter::Result PulseSorter::Sort(double start, double width, int sensor, int* channel, double* hitTime) {
  if (width >= kSyncMin && width <= kSyncMax) {
    ++counters_.syncWidth;
    if (start - lastFlash_ > kFlashGroup) {
      lastFlash_ = start;
      ++flashIndex_;
    }
    return Result::NotOurs;
  }
  if (sensor < 0 || sensor >= 32) return Result::NotOurs;
  // The middle of a pulse lines up with where LH2 mode puts the hit (0.08 us between sensors
  // against 1.34 us using its start).
  const double t = start + 0.5 * std::max(0.0, width);
  Guess guess = ByTiming(t, sensor);
  if (poseView_) guess = ByPose(t, sensor, guess);
  return Apply(guess, t, sensor, channel, hitTime);
}

PulseSorter::Guess PulseSorter::ByTiming(double t, int sensor) const {
  Guess guess;
  // First, a position this sensor was hit at a revolution or so ago.
  double bestScore = 1;
  for (const auto& [ch, r] : rotors_) {
    if (!r.Ready()) continue;
    const double phase = r.line.Phase(t);
    const int plane = r.line.Plane(phase);
    const Track& track = r.tracks[size_t(sensor)][size_t(plane)];
    if (!Fresh(track.last, t)) continue;
    const TrackFit fit = Fit(track, r.bursts[size_t(plane)], phase, t, r.line.b, true);
    const double score = std::fabs(fit.residual) / fit.window;
    if (score < bestScore) {
      bestScore = score;
      guess.kind = Guess::Kind::Track;
      guess.channel = ch;
      guess.plane = plane;
      guess.phase = phase;
      guess.residual = fit.residual;
      guess.revolutions = fit.revolutions;
    }
  }
  // A 1.0 station's: where this sensor was hit that long after an earlier flash.
  const double lh1Phase = t - lastFlash_;
  const int lh1Slot = flashIndex_ & 1;
  const bool inSweep = InSweep(t);
  if (inSweep) {
    const Track& track = lh1Tracks_[size_t(sensor)][size_t(lh1Slot)];
    if (Fresh(track.last, t)) {
      const TrackFit fit = Fit(track, lh1Bursts_[size_t(lh1Slot)], lh1Phase, t, kLh1Period, false);
      if (std::fabs(fit.residual) / fit.window < bestScore) {
        guess = Guess{};
        guess.kind = Guess::Kind::Lh1Track;
        guess.residual = fit.residual;
        guess.revolutions = fit.revolutions;
        return guess;
      }
    }
  }
  if (guess.kind == Guess::Kind::Track) return guess;

  // Next, a sensor newly in view: beside where the rest of the device is being hit.
  int candidates = 0;
  for (const auto& [ch, r] : rotors_) {
    if (!r.Ready()) continue;
    const double phase = r.line.Phase(t);
    const int plane = r.line.Plane(phase);
    const Burst& burst = r.bursts[size_t(plane)];
    if (!Fresh(burst.last, t)) continue;
    if (std::fabs(Wrap(phase - burst.centre, r.line.b)) < burst.halfWidth + kBurstMargin) {
      ++candidates;
      guess.channel = ch;
      guess.plane = plane;
      guess.phase = phase;
    }
  }
  // The same question for the 1.0 station's hits.
  const Burst& lh1Burst = lh1Bursts_[size_t(lh1Slot)];
  const bool lh1Candidate = inSweep && Fresh(lh1Burst.last, t) &&
                            std::fabs(lh1Phase - lh1Burst.centre) < lh1Burst.halfWidth + kBurstMargin;
  if ((candidates >= 1 && lh1Candidate) || candidates > 1) guess.kind = Guess::Kind::Ambiguous;
  else if (lh1Candidate) guess.kind = Guess::Kind::Lh1Burst;
  else if (candidates == 1) guess.kind = Guess::Kind::Burst;
  return guess;
}

PulseSorter::Guess PulseSorter::ClaimRotor(int channel, double t, int sensor) const {
  const Rotor& r = rotors_.at(channel);
  Guess guess;
  guess.kind = Guess::Kind::Burst;
  guess.channel = channel;
  guess.phase = r.line.Phase(t);
  guess.plane = r.line.Plane(guess.phase);
  const Track& track = r.tracks[size_t(sensor)][size_t(guess.plane)];
  if (Fresh(track.last, t)) {
    const TrackFit fit = Fit(track, r.bursts[size_t(guess.plane)], guess.phase, t, r.line.b, true);
    // Too far from where it was to say anything about how fast hits are moving.
    if (std::fabs(fit.residual) < kMaxWindow) {
      guess.kind = Guess::Kind::Track;
      guess.residual = fit.residual;
      guess.revolutions = fit.revolutions;
    }
  }
  return guess;
}

PulseSorter::Guess PulseSorter::ClaimLh1(double t, int sensor) const {
  Guess guess;
  guess.kind = Guess::Kind::Lh1Burst;
  const int slot = flashIndex_ & 1;
  const Track& track = lh1Tracks_[size_t(sensor)][size_t(slot)];
  if (Fresh(track.last, t)) {
    const TrackFit fit = Fit(track, lh1Bursts_[size_t(slot)], t - lastFlash_, t, kLh1Period, false);
    if (std::fabs(fit.residual) < kMaxWindow) {
      guess.kind = Guess::Kind::Lh1Track;
      guess.residual = fit.residual;
      guess.revolutions = fit.revolutions;
    }
  }
  return guess;
}

PulseSorter::Guess PulseSorter::ByPose(double t, int sensor, const Guess& timing) {
  ExpectedHit expected[kMaxExpected];
  const int n = poseView_(sensor, t, expected);
  if (n <= 0) return timing;

  // Everyone the pose says this pulse could be from: channel -1 is a 1.0 station.
  int fits = 0, bestChannel = -2;
  double best = 1e300, second = 1e300;
  const auto fit = [&](int channel, double miss, double window) {
    if (miss >= window) return;
    ++fits;
    const double score = miss / window;
    if (score < best) {
      second = best;
      best = score;
      bestChannel = channel;
    } else {
      second = std::min(second, score);
    }
  };
  std::array<bool, sob::kChannels> rotorExpected{};
  std::array<bool, sob::kChannels> rotorFits{};
  bool lh1Expected = false, lh1Fits = false;
  for (const auto& [ch, r] : rotors_) {
    if (!r.Ready() || ch < 0 || ch >= sob::kChannels) continue;
    const uint32_t base = channelBase_(ch);
    if (!base) continue;
    const double phase = r.line.Phase(t);
    const int plane = r.line.Plane(phase);
    for (int i = 0; i < n; ++i) {
      const ExpectedHit& e = expected[i];
      if (!e.lh2 || e.base != base || e.axis != plane) continue;
      rotorExpected[size_t(ch)] = true;
      if (!e.facing) continue;
      const double miss = std::fabs(Wrap(phase * kTwoPi / r.line.b - e.angle, kTwoPi));
      const double window = std::max(kPoseWindow, e.window);
      rotorFits[size_t(ch)] = miss < window;
      fit(ch, miss, window);
    }
  }
  const bool inSweep = InSweep(t);
  for (int i = 0; i < n; ++i) {
    const ExpectedHit& e = expected[i];
    if (e.lh2) continue;
    lh1Expected = true;
    if (!inSweep || !e.facing) continue;
    // Either rotor's: which flash belongs to which isn't known here.
    const double miss = std::fabs(Wrap((t - lastFlash_) * kTwoPi / kLh1Period - e.angle, kTwoPi));
    const double window = std::max(kPoseWindow, e.window) + kLh1PoseExtra;
    if (miss < window) {
      if (!lh1Fits) fit(-1, miss, window);
      lh1Fits = true;
    }
  }

  const bool rotorGuess = (timing.kind == Guess::Kind::Track || timing.kind == Guess::Kind::Burst) && timing.channel >= 0 &&
                          timing.channel < sob::kChannels;
  const bool lh1Guess = timing.kind == Guess::Kind::Lh1Track || timing.kind == Guess::Kind::Lh1Burst;
  // Is it the pose that has gone wrong? It agrees with nearly all of what timing is sure of when
  // it's right, so most of that disagreeing says so, whatever one track has wandered onto.
  if (timing.kind == Guess::Kind::Track && rotorExpected[size_t(timing.channel)]) {
    poseTrust_ += kTrustRate * ((rotorFits[size_t(timing.channel)] ? 1.0 : 0.0) - poseTrust_);
  } else if (timing.kind == Guess::Kind::Lh1Track && lh1Expected) {
    poseTrust_ += kTrustRate * ((lh1Fits ? 1.0 : 0.0) - poseTrust_);
  }
  if (poseTrust_ < kTrustMin) {
    ++counters_.poseUntrusted;
    return timing;
  }

  if (fits >= 1) {
    if (fits > 1 && best * kPoseClearly > second) {
      ++counters_.poseTied;
      Guess tied;
      tied.kind = Guess::Kind::Ambiguous;
      return tied;
    }
    const bool same = bestChannel < 0 ? lh1Guess : (rotorGuess && timing.channel == bestChannel);
    ++(same ? counters_.poseAgreed : counters_.poseMoved);
    return bestChannel < 0 ? ClaimLh1(t, sensor) : ClaimRotor(bestChannel, t, sensor);
  }
  // Fits nobody the pose can speak for. Timing still speaks for a station the filter has no
  // view of yet (a 1.0 station only reaches the filter this way).
  if (rotorGuess && !rotorExpected[size_t(timing.channel)]) return timing;
  if (lh1Guess && !lh1Expected) return timing;
  if (timing.kind == Guess::Kind::Nobody && !(inSweep && lh1Expected)) return timing;
  ++counters_.poseVetoed;
  // Timing keeps following what it was sure of. If the pose is what's wrong, every hit lands
  // here, and with its tracks run out timing would have nothing left to disagree with it.
  if (timing.kind == Guess::Kind::Track) {
    Learn(rotors_[timing.channel], timing.plane, sensor, timing.phase, t, timing.residual, timing.revolutions, true);
  } else if (timing.kind == Guess::Kind::Lh1Track) {
    LearnLh1(flashIndex_ & 1, sensor, t - lastFlash_, t, timing.residual, timing.revolutions, true);
  }
  Guess vetoed;
  vetoed.kind = Guess::Kind::Ambiguous;
  return vetoed;
}

PulseSorter::Result PulseSorter::Apply(const Guess& guess, double t, int sensor, int* channel, double* hitTime) {
  const double lh1Phase = t - lastFlash_;
  const int lh1Slot = flashIndex_ & 1;
  switch (guess.kind) {
    case Guess::Kind::Lh1Track:
    case Guess::Kind::Lh1Burst:
      LearnLh1(lh1Slot, sensor, lh1Phase, t, guess.residual, guess.revolutions, guess.kind == Guess::Kind::Lh1Track);
      ++counters_.byLh1;
      return Result::NotOurs;
    case Guess::Kind::Track:
    case Guess::Kind::Burst: {
      const bool track = guess.kind == Guess::Kind::Track;
      Rotor& r = rotors_[guess.channel];
      Learn(r, guess.plane, sensor, guess.phase, t, guess.residual, guess.revolutions, track);
      r.lastClaim = t;
      ++(track ? counters_.byTrack : counters_.byBurst);
      *channel = guess.channel;
      *hitTime = t;
      return Result::Hit;
    }
    case Guess::Kind::Ambiguous:
      ++counters_.dropped;
      return Result::Dropped;
    case Guess::Kind::Nobody:
      break;
  }
  // Nobody's for now. In a 1.0 sweep, it could start that station's hits. Fast motion can also
  // lose a 2.0 station, so look for it among these.
  if (InSweep(t)) LearnLh1(lh1Slot, sensor, lh1Phase, t, 0, 0, false);
  ++counters_.notOurs;
  unclaimed_.push_back(t);
  while (!unclaimed_.empty() && t - unclaimed_.front() > kReacquireSpan) unclaimed_.pop_front();
  if (t >= nextReacquire_) {
    nextReacquire_ = t + kReacquireEvery;
    Reacquire(t);
  }
  return Result::NotOurs;
}

void PulseSorter::Reacquire(double now) {
  if (unclaimed_.size() < size_t(kReacquireHits)) return;
  for (auto& [ch, r] : rotors_) {
    if (!r.Ready()) continue;
    const double period = r.line.b;
    for (int plane = 0; plane < 2; ++plane) {
      Burst& burst = r.bursts[size_t(plane)];
      if (Fresh(burst.last, now)) continue;
      // A rotor's hits pile up in one part of its revolution. Anything else, like a 1.0
      // station's at its 60 Hz, spreads out.
      const int bins = int(0.5 * period / kReacquireBin);
      std::vector<int> counts(size_t(bins) + 1, 0);
      std::vector<double> first(size_t(bins) + 1, 1e300), latest(size_t(bins) + 1, -1e300);
      for (const double time : unclaimed_) {
        const double phase = r.line.Phase(time);
        if (r.line.Plane(phase) != plane) continue;
        const int bin = std::clamp(int((phase - plane * 0.5 * period) / kReacquireBin), 0, bins);
        ++counts[size_t(bin)];
        first[size_t(bin)] = std::min(first[size_t(bin)], time);
        latest[size_t(bin)] = std::max(latest[size_t(bin)], time);
      }
      const int best = int(std::max_element(counts.begin(), counts.end()) - counts.begin());
      if (counts[size_t(best)] < kReacquireHits) continue;
      if (latest[size_t(best)] - first[size_t(best)] < kReacquireMinRevs * period) continue;
      burst.centre = plane * 0.5 * period + (best + 0.5) * kReacquireBin;
      burst.halfWidth = 2 * kReacquireBin;
      burst.shift = 0;
      burst.last = now;
      ++counters_.reacquired;
    }
  }
}

}  // namespace lh
