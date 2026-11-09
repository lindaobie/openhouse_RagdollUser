# liblh — the lh session runtime

`liblh.so` is the driver core everything else uses: the 3D visualizer (`lhviz`),
`lhtool session`, `openhouse-compare`, the example app and the SteamVR driver. Its only public header is
`include/lh/session.hpp` (plain data types, no Eigen/transport types).

```
                 include/lh/session.hpp   (lh::Session, SessionSnapshot, events)
                              │
   ┌──────────────────────────┴──────────────────────────────────────────────┐
   │ runtime/session.cpp  SessionImpl : Session, IInputSink                    │
   │   transport (live UsbTransport | replay RawCaptureReader → ReportDecoder) │
   │   devices: role from JSON, DeviceTracker, IndexInputDecoder, battery      │
   │   base registry, OOTX identities, events, room persistence                │
   │        │                         │                                         │
   │  runtime/device_tracker      runtime/room_map  ◄── shared by all devices   │
   │   TrackingSource (TDM/OOTX)    rooms, base poses, tilt levelling,          │
   │   LighthouseDirect (verified   SteamVR import, learning/refinement,        │
   │   EKF + bootstrap)             moved-base detection, configs, OOTX frames  │
   └────────────────────────────────────────────────────────────────────────────┘
```

## Why not the stock structure

In SteamVR each device bootstraps and tracks base stations on its own. Secondary
bases are private filter states, and a device that resets (2 s without light)
throws all of that away and re-bootstraps from scratch, re-learns its IMU
calibration (another ~1 s before "CALIBRATED"), and adds a secondary base only after
260 updates plus a fresh solve. The Lighthouse DB shares base poses, but each
device still starts over.

Here the room map is shared and authoritative:

| Situation | Stock driver | lh runtime |
|---|---|---|
| Device sees a located base | own bootstrap; world pose after DB/universe selection | world pose valid at bootstrap |
| Second base | after 260 updates + solve (`0x35c330`) | added from the room map on first light |
| Re-acquire after reset | full restart incl. IMU calibration | calibration (states 18..30) carried over |
| Base identity after TDM sync loss (>5 s) or at startup | wait for a full OOTX frame (~3–6 s) | ~40 OOTX bits matched against known frames (~0.3–0.7 s) |
| Who refines base poses | DB relationships, HMD preferred | every calibrated device with converged estimates |
| Losing tracking | room survives in DB | room survives in memory and on disk; never dropped because a device is lost |

The per-device filter is the verified port of `CLighthouseDirect`
(`src/tracking/lighthouse_direct.*`), so the filter math is still the oracle-verified
EKF (the oracle tests in `tests/oracle`).

## Room map (`src/runtime/room_map.*`)

* World frame: Y up, origin at the origin base. World-from-global levelling follows
  the stock universe code (`0x353ab0`, method 0: Y = base "up", Z = global Z
  projected off the up axis; method 1: shortest rotation).
* Tilt representation as stock: base-frame up = `exp([pitch, 0, roll]) · +Y`.
* Several rooms are kept. The active one is chosen when the first base is seen:
  the room containing it with the most visible bases (ties: most recently updated).
  If no room contains it, a new room is created and its origin is set when the
  first device calibrates on a base (`EstablishOrigin`, using that device's
  filtered tilt).
* SteamVR import (`ImportSteamDatabase`): every universe becomes a room
  `steamvr-<id>`; only rooted entries (`target_serial_number == 0`) are used,
  `world_from_base = leveling(universe tilt) · base_from_global⁻¹`. Base
  calibrations from `base_stations[]` are imported too.
* Learning (`Observe`): a calibrated device whose alternate base has converged
  (variance traces below the stock RELATIONSHIP thresholds 6.4e-5 / 1.22e-5) reports
  `alt_from_primary`. If one side is unknown it is located from the other. If both
  are known, the non-origin, less certain base is blended toward the measurement
  (variance-weighted, gain clamped to [0.002, 0.5], at most one observation per
  device and base pair every 0.5 s).
* Moved bases: an OOTX reset count different from the stored one makes the stored
  pose uncertain (variance 1). Ten consecutive confident observations disagreeing
  by more than 10 cm or 3° relearn the base. The origin never moves.
* Persistence: `$XDG_CONFIG_HOME/lh/roommap.json` (`~/.config/lh/roommap.json`),
  written atomically at most every 10 s when dirty and on `Stop()`. Replays use an
  in-memory map unless `roomMapPath` is given. The file also stores base
  calibrations and one OOTX frame per base for fast identification.

## Room solver (`src/runtime/room_solver.*`)

The authoritative estimate of every non-origin base pose. Devices contribute
**keyframes**: a window of less than one LH1 cycle (newest sweep per base and axis)
in which the device saw at least two bases with both axes (>= 4 hits each), taken at
most every 0.1 s while the device moves slower than 2 m/s and 6 rad/s. Each sample's
sensor position and normal are stored in the keyframe's reference device frame *at
the sample's own time*, using the filter's pose at that sweep (the same extrapolation
the filter's residuals use, `0x376380`), so the solver needs no motion model.

`SolveRoom` is a bundle adjustment (Ceres, DENSE_SCHUR, one 7-parameter
quaternion+translation block per pose, Huber loss at 3σ with σ = 100 µrad):
unknowns are `world_from_base` for every base except the origin (gauge) and one
`world_from_device` per keyframe; residuals are the LH1 sweep angles from the same
`CSensorPositionAngular` model as the filter, with each base's factory calibration,
and the filter's facing gate (cos >= 0.3). Unknown bases are initialized from
per-keyframe bootstrap solves (largest 10 cm / 3° consensus). Keyframes whose RMS
exceeds max(4 × median, 500 µrad) are rejected and the problem is solved again.
Base covariances come from `ceres::Covariance`.

The session keeps up to 400 keyframes (near-duplicate views of the same device are
skipped) and solves every 0.1 s while a seen base is unlocated, otherwise every 1 s:
in a background thread for live sessions, inline for fast replays (deterministic).
Results are applied with `RoomMap::ApplySolved`: first placements and moves > 5 cm
at once, refinements blended in at 30 % and changes < 0.3 mm ignored, so devices
tracking from a refined base see no steps. The filter-based learning path
(`Observe`) no longer blends solved bases but still detects moved bases.

**Device filters and the room.** A room base enters a device's filter with a prior
variance of at least 4e-4 (trace) and the filter keeps refining it; filters are only
reset to the room when they disagree by more than 1 cm / 0.5°. Locking filters to the
room (variance ~1e-6) measurably increased static jitter on the recorded controller:
0.34 mm → 0.13 mm (4e-4) → 0.10 mm (1e-3) position std.

**Origin.** Set as soon as a device tracks a base, from that base's own accelerometer
(OOTX gravity, within ~0.5° of the calibrated tilt), then re-levelled once a calibrated
filter's tilt variance is clearly lower (it moved 0.14–0.99° on the recordings).

Diagnostics: every solve is logged at DEBUG level (`room solve: ...`); `LH_EVALUATE_ROOM=1` freezes the
room (no learning at all) and reports how well it explains the data (residual RMS
with the bases held fixed).

## Device tracker (`src/runtime/device_tracker.*`)

* `TrackingSource` → `LighthouseDirect`, exactly as before for every optical / IMU
  update.
* On bootstrap (cold solve), if the room knows the base or a previous track left a
  calibrated filter, the filter is re-seeded (`LighthouseDirect::SeedPose`) with the
  solved pose, the room's tilt for that base, and the carried IMU calibration.
* When a located base's light arrives that is not yet in the filter, it enters as a
  known alternate base (`AddKnownAltBase`) with the room-map relative pose (prior
  variance >= 4e-4, see above).
* While not tracking, a device does not bootstrap on an unlocated base if a located
  base was seen in the last 0.3 s (it would have no world pose), for at most 1 s.
* The stock health check (`0x358300`, ported as `LighthouseDirect::HealthCheck`:
  bad tilt, unlikely gravity, IMU misalignment, out of bounds, too fast, 2 s without
  light) decides resets. On reset the calibrated filter is kept as carry-over.
* Output: world pose = room `world_from_primary` · filter pose. States: Tracking
  (light in the last 0.25 s), Coasting (IMU only, still valid), Acquiring (no world
  frame yet), Lost, Uninitialized, Disconnected. `TrackingResult` maps to SteamVR's
  ETrackingResult values for the future driver.

## Fast base identification (`TdmDisambiguator::Identify`)

A base's OOTX frame repeats forever. When a device decodes a frame itself, the
frame's data bits (`OotxMajorityRing::Bits()`) are published as an identity. The
session shares identities with every device and stores them in the room map. A base
whose serial is still unknown on a device is labelled once its last 40 data values
(at least 32 real bits) match exactly one known frame at some cyclic shift with at
most 1 mismatch, and the runner-up is at least 6 mismatches worse. A later full
decode still overrides the label. Inactive when no identities are set, so the
bit-exact TDM oracle path is unchanged.

## Devices and input

* Devices can be disabled (`SessionOptions::disabledDevices`, `Session::SetDeviceEnabled`,
  the checkbox in lhviz, `--disable SERIAL` in lhviz/lhtool). A disabled device is
  listed with `enabled = false` but produces no poses, input or room contributions;
  re-enabling starts it fresh. Live sessions persist the choice in
  `$XDG_CONFIG_HOME/lh/settings.json`.

* Role comes from the device JSON only (`runtime/device_role.hpp`): `hmd` → HMD;
  `controller` naming an Index/Knuckles model → Controller; everything else →
  Tracker. An Index controller or Vive wand configured as `generic_tracker` stays a
  tracker.
* Receivers (dongles) without a tracking config are not reported as devices.
* The transport now emits `InputReport` / `BatteryReport` (framing only) through
  `IInputSink::OnInput/OnBattery`. Only Controller devices decode them
  (`runtime/index_input.*`, mapping from libsurvive `RFKnuckles`/`handle_input`,
  verified on the recorded Knuckles stream: legacy format with `0xA1` extended
  records). Inputs from every other device are ignored.
* Battery: bits 0..6 percent, bit 7 charging; a zero non-charging value is ignored.
* `DevicePose.imuFromHead` = `trackref_from_imu⁻¹ · trackref_from_head` (config
  `head`/`imu` blocks or `trackref_from_*` arrays) = DriverPose `qDriverFromHead`.

## Front ends

* `lhviz [--replay CAPTURE [--fast]] [--room FILE] [--steam-db FILE|-] [--select SERIAL]`:
  3D room (bases with frustums, devices with axes, sensors, rays to their filter
  bases, trails), device table, device detail with Index input visualization,
  base list, event log, room reset/save. `--screenshot FILE.ppm SECONDS` renders one
  frame and exits (used for headless checks).
* `lhtool session IN.lhraw|live [--seconds N] [--rate HZ] [--room FILE] [--realtime]`:
  the same data as JSON Lines.

## Measurements

Recordings: `session-20261016-142931Z-147083` (HMD still, controller moved/hidden),
`controller-moving-hidden` (controller moved and hidden around the room),
`controller-static-floor` (controller still on the floor).

Room building from scratch (no stored room, HMD disabled):

| | stock-style (filter alt estimate) | room solver |
|---|---|---|
| first light → origin | 2.0–2.2 s (waits for CALIBRATED) | 0.04–0.07 s |
| first light → second base | 3.5–7.0 s | 0.22–0.25 s |
| sigma of the second base after 2 s | — | 3.8 mm |

Room accuracy, held out: residual RMS of the controller sweeps with the room fixed
(lower is better). Our room was built from `controller-moving-hidden`; on the other
recording it is fully held out.

| room | session recording | controller-moving-hidden |
|---|---|---|
| SteamVR's stored universe | 2167 µrad | 1575 µrad |
| room solver, from scratch | 225 µrad | 179 µrad |
| room solver, started from SteamVR's | 229 µrad | 179 µrad |

Repeatability between the two recordings (controller only): 14.5 mm / 0.16° in the
base-to-base pose; both differ from SteamVR's stored universe by 55–61 mm / ~1°.

Static controller jitter (`controller-static-floor`, position std per axis / rotation std):
SteamVR's room 0.05–0.07 mm / 0.0096°; room solver live 0.06–0.16 mm / 0.007–0.009°.

Re-acquire after occlusion is light-limited: on `controller-moving-hidden` the
controller was hidden for 11 s; the first usable sweep pair returned at 5258.68 s,
steady light at 5259.08 s, and it re-acquired at 5259.14 s with calibration kept.

Startup: with a saved room map (OOTX frames), the first bootstrap is 3.2 s earlier
(0.43 s after TDM sync instead of after a full OOTX decode). The controller identified
both bases 0.33 s after the HMD decoded them.

## Known limitations / next steps

* The world origin is the origin base, not the SteamVR standing origin; the
  chaperone `world_from_global` transform is not applied yet.
* Re-acquire still waits for the stock 2 s reset when light comes back after a long
  occlusion with large IMU drift; a warm EKF seed from the last world pose is
  available (`SeedPose`) but not used for that case yet.
* Base identification needs an identity from some device or the saved room map.
* Non-Index controllers (Vive wands) are treated as trackers; their inputs are
  ignored by design for now.
