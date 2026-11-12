// Not stock: which light mode a device should be in, in a room with both kinds of base station
// (ai/driver/mixed_stations.md).
//
// In LH1 mode a device reports hits from both kinds, and PulseSorter tells them apart, but only
// for 2.0 rotors it has heard real beam words from. Those come only in LH2 mode, where a 1.0
// station gives nothing. So a device starts in LH2 mode, moves to LH1 mode once it knows the
// rotors it can see, and comes back now and then. Going to LH2 mode costs about half a second
// without light, so routine visits wait for a calm moment, and the same half second is no loss
// to a device that has lost tracking, which LH2 mode handles better on hard swings.
//
// A device can stay in LH2 mode throughout, by kind (MixedLightOptions, from the SteamVR
// settings page). The headset always does.
//
// A first-generation device (the original Vive headset, Vive wands, the 2017 Vive Tracker) has no
// LH2 mode and sees only 1.0 stations, so none of this is for it. It doesn't say so, but it gives
// itself away: told to go to LH2 mode it keeps sending LH1 pulses and never a beam word, where a
// device that can stops within about 30 ms. Then it's left out (LooksLh1Only).
//
// Live only: a replay's modes are in its recording.
#pragma once

#include "lh/session.hpp"

namespace lh::runtime {

// s in LH1 mode between visits to LH2 mode. A rotor's line, learned on a visit, runs off by about
// 2.5 us (typical) to 9 us (worst tenth) over 45-60 s, against windows of 30 us and up. The second
// while the scout (below) covers every rotor the device is using.
constexpr double kVisitEvery = 45, kScoutedVisitEvery = 90;

// Light in LH1 mode, weighted by how much it scatters (LighthouseDirect::SetMixedLightScatter). From the
// frames the filter took in one session, per hit: LH2 words 0.06-0.07 mrad off the pose typically,
// sorted 2.0 hits 0.11-0.14, the 1.0 station's 0.16-0.21. Weighted as LH2 words, that was 2-2.5 times
// LH2 mode's jitter; these brought it near LH2 mode's, with no lag.
constexpr double kSortedHitScatter = 1e-6;    // s, of a sorted 2.0 hit's timing
constexpr double kLh1HitScatter = 0.5e-3;     // rad, of a 1.0 station's angle

// Whether a device of this kind reads both kinds of base station.
bool MixesLight(DeviceRole role, const MixedLightOptions& options);

struct MixedLightView {
  int mode = -1;           // 0 = LH1, 1 = LH2, -1 = not sent yet
  double inMode = 0;       // s since this mode was chosen
  double dark = 0;         // s without this mode's kind of light
  double sinceSent = 0;    // s since the mode was last sent
  bool stayLh2 = false;    // never leave LH2 mode
  int readyRotors = 0;     // rotors the sorter can predict
  int learningRotors = 0;  // heard right now, short of words
  int unnamedRotors = 0;   // ready and heard right now, with no station identified for them
  bool tracking = false;   // has a pose from light right now
  double lost = 0;         // s since it last did
  bool calm = false;       // slow enough to go half a second without light
  bool lh1Pulses = false;  // LH1 pulses within the last half second
  bool everLh2 = false;    // has sent a beam word this session
  // In use for the room's 2.0 stations, none seen yet this session: they may be off, so every device
  // (the headset too) only looks for their light now and then, tracking on the 1.0 light in between.
  bool lh2Unseen = false;
  // A 2.0 station seen, but none in the room in use yet (a room learned from 1.0 stations alone): LH2 mode
  // gives no pose until a device on the 1.0 light places one from its sorted hits. So every device, the
  // headset too, visits LH2 mode only to learn the rotors, and stays on the 1.0 light even when lost there.
  bool lh2Unplaced = false;
};

struct MixedLightStep {
  int mode = -1;  // the mode to send; -1: send nothing
  const char* why = nullptr;
};

// visitEvery: s in LH1 mode between visits to LH2 mode; 0: only when lost or dark.
MixedLightStep ChooseMixedLight(const MixedLightView& view, double visitEvery);

// A device that can't do LH2 mode: still sending LH1 pulses 8 s after being told to go to LH2 mode,
// past a re-send of the command (5 s), and never a beam word.
bool LooksLh1Only(const MixedLightView& view);

// Whether 1.0 + 2.0 is in use (Session::UpdateMixedRoom): only where there's a 2.0 station to sort.
struct MixedRoomView {
  bool enabled = false;          // the switch
  bool lh2Seen = false;          // a 2.0 station seen this session
  bool lh2InRoom = false;        // the room in use has one, and it wasn't found dark
  double inUse = -1;             // s in use so far; -1: not in use
  double sinceLh2Light = 1e300;  // s since any device last had 2.0 light
};
enum class MixedRoomStep { NotInUse, InUse, RoomDark };
// s in use for the room's 2.0 stations alone with no 2.0 light from any device: they're off or asleep, and
// devices stop looking for their light (MixedLightView::lh2Unseen). Stations SteamVR woke showed their first
// light 35-65 s after it started.
constexpr double kLh2RoomDark = 90;
// RoomDark: not in use for that room from now on (until the switch is turned on again).
MixedRoomStep ChooseMixedRoom(const MixedRoomView& view);

// The scout. Between a device's visits to LH2 mode its line for a 2.0 rotor runs off, about half of it
// because the rotor's speed changes (stations warm for minutes after waking: 1-2.5 ppm over 10-15 min,
// seen by every device alike). A device in LH2 mode all the while, the headset, measures that speed, so
// the line can follow it: the time it has run off is the integral of the rotor's relative period change
// since the line was learned. Only the change is passed on, so the two devices' clocks needn't line up
// closely. In replays, 45-60 s after a visit this took a line from 2.5 to 1.5 us off typically and 6.9
// to 4.1 us for the worst tenth, so a device whose every rotor in use has a scout visits half as often.
struct RotorScout {
  double reference = 0;  // the scout's period for the rotor when the line was learned
  double drift = 0;      // s the line has run off since
  double rate = 0;       // relative period change, as last heard
  double lastHeard = 0;  // when the scout last had the period
  double lastStep = 0;
  bool lost = false;     // the scout was quiet too long: the line is on its own until the next visit
};
RotorScout StartRotorScout(double now, double period);
// One step at `now`, with the scout's period if it has one now. Returns the drift for the line.
double StepRotorScout(RotorScout& scout, double now, const double* period);

}  // namespace lh::runtime
