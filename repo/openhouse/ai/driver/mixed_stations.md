# 1.0 and 2.0 base stations together

Not stock. Off by default: "Enable Mixed Basestations (1.0 and 2.0 together, EXPERIMENTAL, might change the room map)", at the end of the SteamVR
settings page (`driver_openhouse/mixedLight`), `MixedLightOptions` in `lh/session.hpp`, `lhtool session
... --mixed-light` for replays. Off, or with no 2.0 station known, every device is on the usual
light-mode choice (`Session::UpdateLightMode`) with no sorter: the output is the same as without this code.

Built and tested in one room: one stock 1.0 station (channel A, no sync cable), three 2.0 stations, a
Bigscreen Beyond (wired) and two Index controllers. One person's hands, so treat numbers as first readings.

## Why it needs anything

A device has two light modes, and neither sees both kinds of station as they are:

| Stations on | LH2 mode (controller, per s) | LH1 mode |
|---|---|---|
| 2.0 only | 850 sensor words | 1652 short pulses, no sync flashes |
| 1.0 only | nothing at all | 120 sync flashes, 602 short pulses |
| both | 996 sensor words | 124 sync flashes, 2110 short pulses |

LH1 mode reports every 2.0 hit too, as a plain pulse with nothing to say whose it is: about 72 % of a
controller's pulses in that room (85 % on the headset). The stock LH1 path never tracked through them,
and switching modes back and forth can't work: going to LH2 mode takes about 0.5 s to the first sensor
word (LH1 mode: 10-20 ms).

## How

**Sorting** (`tracking/pulse_sorter.hpp`, run by `TrackingSource::SetSorting`). Each 2.0 rotor keeps a
very steady speed of its own (LH1 pulses alone give its period to within 0.4-0.9 ppm). The sorter learns
each rotor's line (beam word n at a + n b) from real beam words in LH2 mode, then claims an LH1 pulse for
a rotor by where it falls in that rotor's revolution: per sensor and plane where it was hit a revolution
or so ago, else beside where the rest of the device is being hit, with a reacquire for rotors lost in
fast motion. The 1.0 station's hits, timed from its flashes, compete for each pulse. Claimed pulses go to
`SobDisambiguator` as sensor words (`kSampleSorted`) with predicted beam words, so the filter and the LH2
path are unchanged; the rest goes down the LH1 path. `TrackingSource::PopFrame` merges the two in time
order (the filter drops a frame older than the last it used). The pulse's middle lines up with LH2 mode's
hit time (0.08 us between sensors, against 1.34 us using its start).

**The pose helps** (`LighthouseDirect::ExpectedHits`: the filter state carried forward through the IMU, as
the output is). Timing alone followed the wrong station's sweep for seconds, and the filter then crept to
a confident wrong pose (10-33 % of moving samples in one recording). A hit within 15 mrad of where the pose
puts a station's hit is that station's (25 mrad for a 1.0 station), widened by half of how far the device
has gone since light last corrected the filter, at most 150 mrad (hard swings). Two within reach: the much
nearer, or neither. The pose has a say only within 0.25 s of a used frame of four sensors or more, and only
while at least 60 % of timing's recent sure claims fit it; otherwise timing decides again.

**Light modes** (`runtime/mixed_light.hpp`, `Session::UpdateMixedLight`). A device starts in LH2 mode, goes
to LH1 mode once it knows the rotors it hears, the stations they belong to, and has a pose (up to 8 s, 25 s
to identify a station, 60 s without a pose), and visits LH2 mode every 45 s, waiting for a calm moment (under
0.5 m/s and 2 rad/s) for up to 90 s. A visit costs 0.2-0.5 s without light. A device lost for 1 s in LH1
mode goes to LH2 mode at once, which handles hard swings better. The sync generation (beam word bit 27)
flips at nearly every visit, and the rotor's line then starts over, as stock does ("new sync generation").

Kinds of device: controllers and trackers each have a switch (on by default). The headset stays in LH2
mode: the stock driver, loaded for its display, runs its own tracker for it and puts it back in LH2 mode
about 11 s after each LH1 command ("SwSyncDetect Restart ... enabling Sync on Beam"), re-bootstrapping
its own universe as it does. Using 1.0 light on the headset would need that command kept from the headset.
Switching it off sends every device that was mixing to LH2 mode, 2.0 light alone, for the usual choice to
go on from. Choosing afresh puts the headset in LH1 mode in a room of both kinds ("default"); leaving the
controllers in LH1 mode leaves them on unsorted light (below). Live, the headset lost tracking after every
switch-off either way. With this, switched off mid-session, it kept tracking (100 % over the next 30 s).

**Unsorted light starves the other devices.** In a room with 2.0 stations lit, a device in LH1 mode that
isn't sorting (master's choice in such a room, or any 1.0-only device) gets mostly 2.0 hits it can't use. It
keeps restarting its tracking, each restart a fresh pose solve, all under the session's one lock. That held
up the other devices' USB reads enough that the headset's reports were dropped: its IMU, about 1011 samples
a second, fell to 444-867 while an Index controller sat unsorted in LH1 mode (1550-2030 pulses a second),
and to 601-726 while a wand was on, and the headset lost tracking. Sorted, the same controller at 1300
pulses a second cost the headset nothing. Replays don't show it (they never drop a report): replayed alone,
the wand took 35 s of processor time for about 80 s of its light, and the controller 263 restarts. The
proper fix is outside this: reading USB off that lock, or cheaper restarts.

**1.0-only devices** (the original Vive headset, Vive wands, the 2017 Vive Tracker) can't use any of this:
they have no LH2 mode and see only 1.0 stations, so there are no beam words to learn a rotor from. Told to
go to LH2 mode they keep sending LH1 pulses, where a device that can stops within about 30 ms. A device
still doing that 8 s on (past a re-send), having never sent a beam word, is left out
(`runtime::LooksLh1Only`): no sorting, no weighting, and its light is ignored while this is in use
(`Session::OnLightPulse`), with a warning in the log. With 2.0 stations lit, that light is mostly 2.0 hits
it can't tell apart: a Vive wand on its own dongle tracked about 11 % of a replay, kept failing its IMU
calibration, and starved the headset (below). It's asked for LH2 mode again every 30 s, and a beam word
from it lets it back in, in case it was a device that can whose command was lost. To use one, switch
1.0 + 2.0 off and the 2.0 stations off too.

**How long a visit holds.** Between visits the line is carried forward, and 1 us of timing error turns every
angle from that station by 0.31 mrad, about 1 mm at 3 m. A line fitted on one visit, against the next
visit's, by time since (four sessions, one device at a time):

| Since the visit | 0-10 s | 20-30 s | 45-60 s | 60-90 s | 90-150 s |
|---|---|---|---|---|---|
| Typical | 0.1 us | 0.8 us | 2.5 us | 6.7 us | 11 us |
| Worst 10 % | 0.7 us | 3.5 us | 9.2 us | 28 us | 41 us |

About half of that drift is the 2.0 stations' rotors slowing as they warm up after waking (1-2.5 ppm
over 10-15 minutes, which every device sees alike), half each device's own. The 1.0 station's flashes
keep a steady period, but a 5 s window of them is good to only about 0.2 ppm, too little to correct with.

**The scout** (`RotorScout`, `Session::UpdateScout`, switch "Mixed Basestations: headset times the 2.0 stations",
on). The headset is in LH2 mode throughout, so it has each rotor's period over the last 5 s of real words
(`PulseSorter::RecentPeriod`, words more than 20 us off left out). When a device comes back to LH1 mode
with a new line, the line follows the change in the headset's period since (`PulseSorter::SetDrift`),
at most 0.05 ppm/s; without the headset's period the last rate is kept for 20 s, then the line is on
its own. Only the change is passed on, so the two clocks needn't line up. Replayed, 45-60 s after a visit
it took a line from 2.5 to 1.5 us off typically and from 6.9 to 4.1 us for the worst tenth, better in 86
of 121 cases. While every station a device is claiming hits for has a scout it visits every 90 s instead
of 45 (logged). The headset often doesn't see a station a controller uses, so that was 3-31 % of the time.

**Weighting** (`LighthouseDirect::SetMixedLightScatter`, while in use). Light in LH1 mode sits about twice
as far from where the pose puts it as LH2 words do, per hit in the frames the filter took: LH2 words
0.06-0.07 mrad typically (worst 10 % 0.24-0.27), sorted 2.0 hits 0.11-0.14 (0.50-0.60), the 1.0 station
0.16-0.21 (0.65-0.82). Taken as just as good, that came through as 2-2.5 times LH2 mode's jitter
(controllers live: 0.37-0.48 mm rms against 0.18-0.19). 1 us more timing scatter for a sorted hit and
0.5 mrad more for a 1.0 hit brought it near LH2 mode's. Weighting one kind alone didn't help: the other
then counted for more.

**lhbs multi-station** auto-detection waits while sorting (`TrackingSource::UseMulti`), as it was off
(`LH_MULTISTATION=off`) for every test here. The room's lone stock 1.0 station (channel A, no cable) tripped
it even with the 2.0 stations off: "multi-station layout detected (0 multi / 0 stock flash pairs)", through
the rule for a lone station sweeping after far fewer than half its flashes. Sweeps were then dropped as out
of sequence, and the headset never tracked, against 93 % with detection off. Seen in three runs, on channel A
only; the third was live with the switch off (4.5 min, the headset never tracked), so master does it too. Not
looked into further. `LH_MULTISTATION=on` still forces it. Multi-station rooms with 2.0 stations
are untested.

## Results

Offline, controller moved by hand at about 1-3 m/s, LH1 stretches, % of samples (a wrong pose: more than
0.3 m outside where the same motion went in LH2 mode):

| Recording, light used | Timing alone: in place / wrong / not tracked | With the pose |
|---|---|---|
| 2.0 only, sorted | 80, 96 / 11, 4 / 10, 0 | 98.5, 96 / 1.3, 4 / 0.2, 0 |
| 1.0 on, both together | 76, 84 / 23, 10 / 1, 7 | 98.2, 100 / 1.7, 0 / 0.2, 0 |
| LH2 mode, for reference | 99-100 / 0 / 0-1 | |

Hard swings, offline (Beat Saber-like, peaks near 20 m/s): one recording tracked 100 % with the window
sized by speed, but over three recordings LH1 mode averaged about 68 % tracked (8-100 %, up to 16 % wrong),
against 88-100 % in LH2 mode. A cold start mid-swing is the weak spot.

Live in SteamVR, on the build this was cut from (two sessions of 5.5 and 6.5 minutes, controllers on
both kinds, the headset in LH2 mode, before the scout and the weighting):

| | Good pose | Longest dropout |
|---|---|---|
| Headset | 99.6-99.9 % | 0.09-1.0 s |
| Left controller | 98.7-99.2 % | 0.6-1.4 s |
| Right controller | 98.9-99.5 % | 0.4-1.3 s |

Two later live sessions (the second with the weighting, which the user found steadier),
replayed on this code (`--mixed-light`; the light modes are the recording's, the room map the one from
before each session). Tracked: every trace sample after the first
20 s, so time a controller was off or out of sight counts against both (in the first, the left controller
had a 70 s hole). Jitter: how far each pose sits from a quadratic through the poses within 0.1 s, while moving
under 0.15 m/s.

| | Controllers tracked, the same light without this | With it | Jitter in LH1 mode, rms (LH2 mode) |
|---|---|---|---|
| First, 6.5 min | 35 %, 49 % | 78 %, 95 % | 0.18-0.21 mm (0.16-0.17 mm) |
| Second, 4 min | 14 %, 58 % | 86 %, 95 % | 0.12-0.17 mm (0.08-0.14 mm) |

Without the weighting the same replays gave 0.28-0.42 mm and 0.18-0.20 mm. The headset, in LH2 mode, is
the same with and without. With the switch off, both replays give the same poses as without this code,
byte for byte.

Most dropouts are the visits. Swings as for Beat Saber felt fine to the user; very hard ones overshoot,
about 16 cm above 6 m/s, settling in 0.2 s. That goes with the accelerometer clipping (78 m/s2 per axis)
in either light mode (LH2 mode with clipping: 12-52 cm), so it isn't this code's; not fixed.

## Limits and open items

* 1.0-only devices (original Vive headset, Vive wands, 2017 Vive Tracker) get nothing from this, and are
  ignored once they show themselves (above). The detection worked live on a Vive wand; an original Vive
  headset, which would then lose tracking, wasn't at hand.
* With the switch off, master's light-mode choice runs, and in a room of both kinds it puts devices in LH1
  mode unsorted, starving the others (above). So with the switch off a 1.0-only device turned on there
  breaks the headset's tracking, as it does on master; with it on, it's ignored once detected (live: the
  headset recovered within a few seconds of turning a wand on).
* Trackers, several 1.0 stations, sync cables and lhbs multi-station alongside 2.0 stations: untested.
* Hard swings from a cold start are less reliable than in LH2 mode (above).
* A rotor needs a visit with its station in view before its hits can be used.
* Replays don't exercise the light-mode choice: a recording's modes are fixed.
* Meant for rooms with both kinds. It's in use only where there's a 2.0 station to sort
  (`Session::UpdateMixedRoom`): one in the room in use, or seen this session. Before any base is seen no
  room is chosen, and it isn't in use either: SteamVR's database often has 2.0 stations of other rooms (the
  `ms-a` test capture's does), and in a 1.0-only room devices would otherwise start in LH2 mode, where no
  1.0 station can be seen, and wait 30 s for 2.0 stations to wake. So in a 1.0-only room it should never be
  in use (`MixedLight.NeverInUseInARoomWithout2Point0Stations`, a replay of an lhbs multi-station room).
  Live, in a room of two stock 1.0 stations (a Beyond, an Index controller, a Vive wand), switching it on
  mid-session left it out of use, and the headset and the wand kept tracking (99-100 % over the 30 s that
  followed). Off, the default, a 1.0-only setup gets the same poses as on master, byte for byte. In a
  2.0-only room it still sorts, with visits and their dropouts, for nothing gained.
* A room of both kinds with its 2.0 stations off or asleep (without the radio-mode fix SteamVR doesn't
  wake them on a setup like this one) comes into use, since the room in use has them. Waiting there in LH2
  mode for stations that may be waking would leave every device blind, the headset most (it stays in LH2
  mode): live, a minute at the start of a 1.0-only session, and again on switching on. So until a 2.0
  station is seen, every device, the headset too, only looks for their light, 3 s in LH2 mode, and goes back
  to the 1.0 light for 15 s before the next look (`MixedLightView::lh2Unseen`). With no 2.0 light from any
  device for 90 s after coming into use, it stands down for that room (`runtime::ChooseMixedRoom`, a warning
  in the log), and the usual choice keeps every device on the 1.0 light, as on master. Stations SteamVR woke
  showed their first light 35-65 s after it started. 2.0 stations turned on after that aren't seen this
  session, as on master; turning the switch off and on tries again.
* With the switch on, 2.0 stations turned off mid-session, after one has been seen, leave the headset in
  LH2 mode with no light, so it doesn't track until they're back or the switch is turned off.
* A 1.0 station moved or turned since the room was learned, in a room of both kinds: with the 2.0 stations
  lit their light outweighs its, but with only the 1.0 stations lit they disagree and the play space shifts
  between them. That's the room map's (it keeps a locked base where it was), as on master; resetting the room
  relearns it.
* The first time, a room needs a 2.0 station seen, or in the room openhouse has, before this is in use. If
  the room in use was learned without them, devices beside a lit 1.0 station stay in LH1 mode and never see
  the 2.0 stations, as on master. Turning the 1.0 station off once lets them be seen; after that the room
  has both.
* Once one is seen there, it's in use, but no 2.0 station is in the room yet, so LH2 mode gives no pose (a
  device bootstraps on one and stays "acquiring"). Until one is placed, every device, the headset too, visits
  LH2 mode only to learn the rotors and stays on the 1.0 light, even when lost there
  (`MixedLightView::lh2Unplaced`): its sorted hits then place the 2.0 stations from the 1.0 ones. Replayed
  against a room of the two 1.0 stations alone, a controller on the 1.0 light placed the first 2.0 station
  0.5 s after it was seen, the next 7 s later. Live, before this, every device cycled 8 s blind in LH2
  mode and 3 s in LH1 mode, too short to pick up the 1.0 light, the headset stayed in LH2 mode, and tracking
  never came back.
