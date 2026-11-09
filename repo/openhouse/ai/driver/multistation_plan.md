# Plan: 3–4 Lighthouse 1.0 base stations (lhbs multi-station mode)

Status (2026-10-19): **first version implemented and working on real hardware**
(`src/tracking/disambiguator_multi.{hpp,cpp}`, capture `captures/ms-a`). The sections
below are the original plan; "Implemented" says what was actually built and where it
differs.

## Implemented

* A separate `MultiDisambiguator` runs beside the stock `TdmDisambiguator` on every
  pulse (TrackingSource). The stock port is not touched (oracle tests unchanged).
  When the multi one becomes `Active()`, frames and base configs come from it and the
  stock path's are dropped. `LH_MULTISTATION=auto|on|off` (default auto).
* Detection: flashes of one index event 250/500/750 us apart (not 417): 50 such pairs
  with > 5x more than 417 us pairs; or a lone station sweeping after < 35 % of its
  flashes; or an OOTX mode byte with bit 7. 0 false detections on all 8 stock captures
  (vrchat-01 included); ms-a detects in 0.3 s.
* Slots from a predicted master reference (period PLL from the flashes), re-anchored if
  an earlier station appears. OOTX mode byte parsed (`BaseDynamicState::multi/multiN/
  multiSlot`, hw != 0x0D); ModeLabel stays empty for multi stations (not a digit).
* **Measured on ms-a, differs from the firmware doc:** the slave flashes every index
  event (like policy ALL, although default was set), and its OOTX advances **only on
  its sweeping (skip = 0) flashes**; its other flashes carry data 0. So a slave's OOTX
  is read from its sweeping flashes only, and a scheduled sweeping flash that is missed
  pushes "no data". Master: every flash. Slave frame at N = 4: ~12 s (first identity in
  24 s; later sessions identify from the stored frame in ~5 s).
* Firmware follow-up (bs1.0rw, 2026-10-19): policy ALL implemented and default; mode
  byte bit 6 = ALL (driver reads N from bits 4-5). With ALL, or when a slave's
  non-sweeping flashes carry 1-bits, the driver reads the slave's OOTX from every flash
  (stock rate, ~3 s per frame). Not yet verified on hardware.
* Frames carry the time between their base's sweeps in `idealPeriod` (the frame layout
  is pinned to the original's by the oracle tests), which sizes the bootstrap sample
  history windows (B7). DeviceTracker coasting and LighthouseDirect::AdvanceInertial
  wait for max(stock, ~1.1-1.25 x sweep interval) of darkness.
* Results on ms-a (controller, 2 stations as slots 0+1 of N = 4): tracking from 0.7 s,
  both stations in the filter, master blocked ~7 s: tracked on the slave alone, every
  frame used. Two stock health resets remain (IMU alignment at 8 s, gravity after 10 s
  with both stations blocked) — not investigated yet.
* Tests: `MultiStation.DetectsDecodesAndTracks`, `MultiStation.StockRoomStaysStock`.

* N = 2 policy ALL on the new firmware (`captures/ms-n2-all`): slave OOTX frame every
  3 s (was 12 s), 99.9-100 % valid, 0.12 mm rms still. Either station blocked used to
  make the filter run off (the stock alternate-base update leaves the device and the
  alternate base's relative pose unconstrained once the primary is dark: 1.4 m/s);
  DeviceTracker now restarts on the visible base when the primary has had no >= 4-sensor
  frame for 0.5 s and the other is well lit (calibration kept, pose held as coasting for
  the ~70 ms; multi-station rooms only, it made vrchat-01 worse). Blocked phases:
  1-14 mm rms (the larger with a fresh room whose two base poses disagree by ~10 cm).
* Wake from standby (`captures/ms-n2-startup`): light ~49 s after wake; the master
  alone first (stock path tracks it), the slave ~5 s later; multi detected from the
  master's mode byte, no outage at the handover. Spinning-up rotors used to wedge the
  timing reference: it now re-anchors after 8 groups off the grid.
* Seen in stock captures too, not multi-specific: "IMU misalignment unreasonably
  large" resets a few seconds after the first calibration (recover in < 0.1 s).

Not done yet: pulse simulator, 3-4 station validation, room-solver keyframe window for
N = 4 with stations that never sweep in consecutive revolutions, superframe check
(5.2.6), lhviz slot display, MASTER+ACTIVE policy capture (slaves dark between sweeps).

## 1. What the firmware does (summary of bs1.0rw MULTISTATION.md)

* N ∈ {2, 3, 4} stations take whole rotor revolutions in turn: in revolution r, slot
  `r mod N` sweeps both axes; the others keep their lasers off. Slot 0 is the master.
* Station s's rotors (and flashes) run `s × 250 µs` after the master (stock 2-station:
  417 µs = 1/2400 s). A flash group spans ≤ 0.75 ms.
* Flash policy MASTER+ACTIVE (default): each index event, the master flashes, and so
  does the station sweeping this revolution. A slave therefore flashes only in its own
  revolutions (1 in N). Policy ALL: every station flashes every index event.
* Sync width bits keep their stock meaning: **skip = 0 ⇔ this station sweeps after this
  flash**; axis and OOTX data bits unchanged; OOTX frame unchanged (v6).
* OOTX byte 31 (mode): stock `mode & 7`; multi `0x80 | (N−1) << 4 | slot`.
* Per-station rate: 60/N sweeps/s per axis (15 at N=4); the room's total stays 60.
* Multi and stock channels (A/b/c) never mix in one room.

Firmware test log (2026-10-18): SteamVR's own disambiguator follows the schedule
(A101B835 every 4th revolution), and **openhouse acquired nothing (703 bootstrap
attempts, 0 poses)**. Section 2 explains why.

## 2. What breaks in our driver, and where

All of it is the stock-faithful port of `CDisambiguatorTDM` and the bootstrap's
sample history, which assume ≤ 2 stations, a 417 µs slot and a sweep each revolution.

| # | Stock assumption | Code | Effect in multi mode |
|---|---|---|---|
| B1 | ≤ 2 bases | `UpdateBases` stops at 2 (`disambiguator_tdm.cpp:335`); `SanityCheck` fails above 2 (`:820`, event 8) | stations 3–4 never become bases; > 2 resets the disambiguator |
| B2 | Non-sweeping bases sit 0/±1 slots of 1/2400 s (417 µs) from the sweeping one | `ProcessExpectedSync` snap, "Panic in TDM. basestation offset is invalid" (`kSlotRate`, `kSlot`) | slot 2 is 500 µs = 1.2 slots: **panic → Reset() every time** slot 2 or 3 sweeps |
| B3 | All bases within 1/30 period (556 µs) | `SanityCheck` (`kWindow30`) | 4 stations span 750 µs: sanity failure → reset |
| B4 | A base's syncs arrive every half period | `kMaxSyncGap = 1.1 · period` resets `syncSeen`/jitter; `avgPeriod` from consecutive syncs | a 1-in-N slave never updates its period, is "lost" between its revolutions |
| B5 | A missed sync is a missed OOTX bit | missed sync → `PushOotx(b, 0xff)` (no vote, ring still advances) | the slave's bits are spread over N× the ring positions: **its OOTX never decodes** (no serial, no calibration, no frames: `emitBeforeOotx = false`) |
| B6 | Mode byte is 0..3 (+bit 7 only for hw 0x0D) | `ootx.cpp` dynamic-state decode → mode 4 → forced to 0 with "incorrectly specifies SOB mode" | multi stations show "mode 0"; slot/N not available |
| B7 | A base sweeps every revolution | `SampleHistory` keeps 2.95 periods, brackets within ±2 periods (`sample_history.cpp:11,45`) | slave sweeps 66 ms apart (N=4): no bracket → **bootstrap never starts** on it |
| B8 | Clusters of one station's flashes are ≥ 417 µs from another's | phase clustering joins within 1/90 cycle (185 µs) | 250 µs spacing still separates, but with only ~65 µs margin for jitter |
| B9 | ModeLabel A/B/C | `driver_lighthouse.cpp` base properties | external tools may read a digit ModeLabel as an LH2 channel (1–16): don't label slots "1".."4" |

Everything downstream already handles 4 bases: `LighthouseDirect` has 4 slots (primary
+ 3 alternates, state size 49 = 31 + 3 × 6), the room map and room solver take any
number of bases, `DeviceTracker` adds located bases from the room map, and the OpenVR
layer registers every base the snapshot reports. Lower per-station sweep rates
(15/s/axis) are within what the filter handles (it already takes one base at 30/s).

## 3. Design principle: stock path stays bit-exact

The TDM port is verified bit-exact against the original
(`tests/oracle/test_tdm_diff.cpp`), and stock A/b/c rooms must behave exactly as now.
So multi-station support is a second **layout** of the same disambiguator, not edits
to the stock constants:

```cpp
struct TdmLayout {            // stock values are today's constants
  int maxBases = 2;           // 4
  double slot = 1.0 / 2400;   // 250e-6
  int maxSlotOffset = 1;      // N - 1 (snap range, in slots)
  int cadence = 1;            // N: revolutions between a slave's flashes (MASTER+ACTIVE)
  double clusterTol = 1.0/90; // 1/180 in multi (93 µs, half the 250 µs spacing ±)
  double groupWindow = 1.0/30;// (N - 1) * slot + margin, as a fraction of the period
  bool darkIsNoVote = false;  // true: a scheduled-dark revolution pushes nothing to OOTX
};
```

`TdmDisambiguator` keeps `TdmLayout::Stock()` by default and only switches to
`TdmLayout::Multi(N)` on positive evidence (section 4). Every stock branch reads the
layout field that equals today's constant, so the oracle test keeps passing unchanged,
and a new test runs the same capture with the layout API to prove it.

## 4. Detecting multi mode (no settings needed, stock rooms untouched)

Per tracked device's disambiguator (each device sees the same room):

1. **Phase evidence (before any OOTX):** UpdateBases finds a third flash cluster pair,
   or two stations whose phase offset is a multiple of 250 µs but not 417 µs. In stock
   layout this is exactly where it would log "TDM supports max of 2 basestations" or
   panic; instead it switches to `Multi(N)` with N = number of distinct clusters
   seen, rounded up by the slot spacing (slot index = offset / 250 µs).
2. **OOTX confirmation:** the master's OOTX decodes first (it flashes every index
   event, so its ring fills at the stock rate). Its mode byte `0x80 | (N−1) << 4 | 0`
   gives N exactly; each slave's frame then confirms its slot. A mismatch with the
   phase assignment → back to stock layout and reset (as the stock sanity check does).
3. **Override:** `LH_MULTISTATION=off|on|auto` (default auto) for testing.
4. A room never mixes: once any device decoded a multi master, the session tells the
   other devices' disambiguators (the same way identities are shared today:
   `SetKnownIdentities`), so they switch at once instead of rediscovering it.

## 5. Changes, by component

### 5.1 OOTX (`ootx.cpp`, `BaseDynamicState`) — small
* For hw 9 (our stations), bit 7 of byte 31 set → `multi = true`, `multiN =
  ((b >> 4) & 7) + 1`, `multiSlot = b & 0xf`; `mode` stays a stock value (0) so
  nothing that switches on stock modes changes. Hw 0x0D keeps Valve's existing meaning.
* `PushOotx`'s "SOB mode" warning only for non-multi mode 4.

### 5.2 Disambiguator (`disambiguator_tdm.{hpp,cpp}`) — the bulk
1. `TdmLayout` (section 3) threaded through every constant listed in B1–B5, B8.
2. Base discovery (B1, B8): allow `maxBases`; a slave's cluster pair has ~1/N of the
   master's hits, so the "≥ 10 % of the best pair" rule is fine at N ≤ 4 but the
   "≥ 5 hits" window must count over N× as many periods.
3. Expected-sync matching (B2, B3): snap non-sweeping bases to the 250 µs grid within
   ±(N−1) slots; group window `(N−1)·slot + margin`.
4. Cadence (B4): per base, the expected revolution of its next flash
   (`r mod N == slot`, the master every revolution). A base that is scheduled dark is
   neither "missed" nor "lost"; `dt` between its flashes is divided by the revolution
   count `round(dt / P)` before it feeds `avgPeriod` and the jitter test, and
   `kMaxSyncGap` becomes `(N + 0.1) · P` for slaves.
5. OOTX (B5): a slave pushes bits only on its own flashes (`darkIsNoVote`); a scheduled
   flash that didn't arrive still pushes 0xff (a real miss).
6. Superframe check: the master's skip = 0 flash marks `r ≡ 0 (mod N)`; if a sweep
   arrives from a station whose slot doesn't match `r mod N`, drop that frame (the
   firmware refuses to emit in that case, so it means our slot assignment is wrong:
   re-derive it).
7. Frame output: unchanged format; add `OpticalFrame::sweepInterval` (N × P for a
   slave, P for stock) for the bootstrap.

### 5.3 Bootstrap sample history (`sample_history.cpp`) — small
* B7: use the frame's `sweepInterval` instead of `idealPeriod` for the history cutoff
  (2.95 ×) and the bracket window (± 2 ×). Stock frames have `sweepInterval =
  idealPeriod`, so stock behaviour is identical. The constant-velocity interpolation
  across 66 ms is less exact than across 16 ms; the multi-start bootstrap already
  tolerates this (and the filter takes over from there).

### 5.4 Tracking, room, OpenVR — mostly verification
* `DeviceTracker`/`LighthouseDirect`: up to 4 bases already. Check the alternate-base
  bootstrap gate (`updateCount > 260`) and `CanSwitchPrimary` with 15 Hz per base.
* Room solver keyframes need ≥ 2 bases with both axes within ~50 ms: with N = 4 a
  window rarely holds two stations' sweeps (each station sweeps once per 66 ms). Widen
  the keyframe window to `2 × sweepInterval` and keep per-sample time compensation
  (it already folds the filter pose at each sample's time in, so a longer window is
  only a velocity-accuracy question).
* Room map: SteamVR's lighthousedb knows at most the 2 stations stock ever saw; the
  others are located by the room solver as new bases are now. Nothing to change.
* OpenVR (B9): ModeLabel `"M1".."M4"` (not digits); `Prop_ModeLabel` stays A/B/C for
  stock stations. Base count limits: SteamVR fine; Monado drops bases beyond its own
  limit.
* RawAlignment: the anchor must be a station in lighthousedb.json (a stock-era
  station), as today.

### 5.5 Tools
* `lhviz`: show slot/N per base, per-base sweep rate.
* `lhtool stats`: per-base flash cadence (expected 1 in N) to diagnose schedules.

## 6. Testing without four stations: a pulse simulator

The disambiguator is pure timing logic, so most of this can be built and tested before
the hardware exists:

* `tests/sim/lh1_sim.{hpp,cpp}`: given N stations (poses, OOTX frames, schedule,
  policy, stagger, jitter), a device model (sensor points/normals) moving on a path,
  emit the pulse stream a Watchman would report: per index event the flash group
  (widths from skip/axis/data, `ClassifySync` inverse), per sweep one pulse per visible
  sensor at `sync + angle·P/2π` (inverse of the disambiguator's angle formula, with
  the base calibration through `Lh1Angle`), widths by distance, optional occlusion and
  reflections.
* Tests: stock 2-station stream → identical frames with stock and auto layout
  (no false multi detection); N = 2/3/4 multi → all stations found, OOTX decoded for
  every slot, no panics/resets, frames attributed to the right station, bootstrap and
  tracking within millimetres of the simulated path; slots 0 and 2 of N = 4 only
  (firmware V2); policy ALL.
* Add it to `tools/eval_replay.sh`-style scoring once real captures exist.

## 7. Order of work

| Step | What | Proves | Needs |
|---|---|---|---|
| 1 | Pulse simulator + stock-equivalence tests | the harness is right (our stock port decodes simulated stock rooms like captures) | nothing |
| 2 | `TdmLayout` refactor, stock defaults | oracle still bit-exact | nothing |
| 3 | OOTX multi byte | slots/N decoded | simulator |
| 4 | Multi layout in the disambiguator (cadence, snap, OOTX votes, superframe) | simulated N = 2/3/4 decode | simulator |
| 5 | Detection (phase + OOTX + session sharing) | auto switch, stock never switches | simulator, existing captures |
| 6 | `sweepInterval` in history/keyframes | bootstrap + room solver on simulated N = 4 | simulator |
| 7 | Hardware V1/V2 (2 stations as N = 2, and as slots 0 + 2 of N = 4) | real timing, jitter, Watchman behaviour | captures (`lhcapture`, 1–2 min each, moving + still) |
| 8 | Hardware V4 (4 stations) | full system, occlusion benefit | 4 stations |

Captures to ask for at step 7 (all with `lhcapture`, SteamVR closed):
1. two lhbs stations in multi N = 2 (master + slot 1), a controller moved around;
2. the same as slots 0 and 2 of N = 4;
3. policy ALL with N = 4 (only if the firmware's ALL mode is used);
4. one stock A/b/c capture with the same stations reflashed or set to stock channels,
   as the regression reference.

## 8. Risks

| Risk | Mitigation |
|---|---|
| False multi detection in a stock room (reflections making a third cluster) | require a full cluster pair half a period apart with consistent widths, ≥ several seconds, and an OOTX multi byte before committing past a provisional state |
| Simulator agrees with itself but not with hardware | step 7 before any tuning; simulator parameters from the firmware's measured timing (MULTISTATION.md §1) |
| 15 Hz per station makes alternate-base learning slow | room map hands located bases to devices (already); room solver window widened (5.4) |
| Flash group timing tighter than measured (rotor phase error at 250 µs) | firmware holds ~5 µs; driver snap tolerance ±1/4 slot; log stagger statistics |
| Breaking the bit-exact TDM oracle | layout refactor first, oracle test must pass unchanged at every step |

## 9. Difficulty

**Moderate.** About 1.5–2.5 k lines including the simulator and tests; ~2–4 working
sessions of implementation, then hardware iterations. The hard part is the
disambiguator (timing logic with many interacting stock rules, and it must stay
bit-exact on the stock path); the rest is small and contained. No new mathematics:
the filter, bootstrap and room solver already handle 4 bases, and multi-station gives
them more constraints, not new ones. The real uncertainty is hardware behaviour
(flash timing jitter at 250 µs, Watchman report handling), which only captures settle.
