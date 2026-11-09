# Standalone session, pose output, and diagnostic frontend

## API and separation

- Public API: `include/lh/tracker.hpp`, `lh::Tracker : InputSinkBase`.
- `src/tracking/tracker.cpp`: per-device lifecycle, locking, queues, clock offsets,
  optional base calibration/placement import, and normalized event routing.
- `tracking_source.*`: TDM plus original float-precision IMU queue.
- `sample_history.*`: original bootstrap sample interpolation/pruning.
- `lighthouse_direct.*`: synchronous bootstrap, primary/alternate slots, buffered
  IMU processing, and optical update orchestration on the verified EKF/residuals.
- `src/transport/usb`: optional library, with no dependency on the tracker.
- `apps/lhtool`: capture/stats plus offline/live `track` (JSON Lines output).
- `apps/lhcapture`: generic estimator-free session recorder; see `capture.md`.
- `apps/lhviz`: GLFW/OpenGL + Dear ImGui visualizer on top of liblh (`ai/driver/lh_runtime.md`).

The tracking-only CMake build does not discover or link hidapi or graphics libs.
The installed CMake package exports `lh::tracking` and, when enabled, `lh::usb`.
Only oracle executables depend on the original binary; production never dlopens it.

## Pose frames and status

Optical geometry is converted from the JSON object's frame into the IMU frame
before solving. Both legacy `trackref_from_imu` (`xyzw + translation`) and modern
`imu.{plus_x,plus_z,position}` config forms are supported. Factory IMU calibration
is loaded from top-level fields for legacy configs or the `imu` block for modern
configs. Invalid geometry, duplicate channels, non-finite values, degenerate IMU
axes, and non-positive accelerometer scales are rejected. Negative channel-map
entries mean unused points.

The state pose is **base-from-IMU**. Without placement, output is in the selected
primary base's frame, which is not necessarily gravity-aligned. With placement,
`worldFromBase * baseFromImu` is published, and velocity is rotated to that frame.
Angular velocity remains body-frame. This is not yet SteamVR head/grip output.
`optical_updates` exposes the original-style filter counter, seeded at 101 by
bootstrap; it is not a zero-based count of optical measurements.
`device_time` is unwrapped device seconds (possibly negative at startup);
`host_time = device_time + last clock offset`. An adapter which never supplies an
offset has not established real host-time correlation.

`Optical` means recently corrected by optical input, `Inertial` means light has
been absent for more than 50 ms, and `Lost` means absent beyond `opticalTimeout`
(default 0.5 s). Status aging uses observed IMU time even when a correction is
rejected; a frozen estimate must not remain labeled optically tracked forever.
The pose timestamp is the last successful filter update, not the most recent
input time. These statuses/timeouts are standalone policies, not original-driver
state-enum parity. Full disconnection requires `OnDeviceDetached`; no-data silence
without further device events cannot be timed from device timestamps alone.

## Database import

`SetBaseConfig` can supply OOTX-equivalent calibration externally. `SetBasePose`
takes an explicit **world-from-base** transform. `LoadBasePoses` accepts either:

```json
{"bases":[{"serial":2701244469,"pose":[0,0,0,1,0,0,0]}]}
```

or SteamVR `lighthousedb.json`. The minimal format uses `[qx,qy,qz,qw,x,y,z]`.
For SteamVR DBs, calibration and the last dynamic gravity vector are imported;
placements are copied literally from `known_universes[0].base_stations[].target_pose`
when `target_serial_number == 0`. It deliberately does not merge universes.
The transform direction/conventions of this convenience SteamVR placement importer
have **not** been validated against live SteamVR poses; prefer explicit placements
for controlled comparisons. Import validates everything before modifying the session.
`known_objects[].imu` supplies learned gyro bias, accel bias, and accel scale for
matching device serials; these override factory values for new/uninitialized
filters and after resets, not by mutating an already-active filter. Latest per-base
recorded pitch/roll/variance can seed bootstrap tilt and its covariance when the
recorded variance is below the initial tilt variance. Full original uncertainty
selection and stale-history policy are still not reproduced. `TrackerOptions::sensorTypes`
allows an explicit per-device hardware type (0..11) when the simple HMD/Knuckles
model-name recognition is insufficient.

No SteamVR database files are modified. There is no learned-DB persistence yet.

## Current evidence

- `SampleHistoryOracle.SelectionAndPruningMatch`: 200 frames × two history modes ×
  two reference-time choices = 800 selection comparisons, including pruning,
  missing sensors, and interpolated angles/times/widths. Matches the original.
- `DirectFilterOracle.DefaultStateAndInitialCovariance`: exact original default
  state and diagonal covariance initialization (off-diagonals preserved).
- `DirectFilterOracle.OrchestratedImuUpdate`: original `0x36ed70` vs orchestration,
  including no saturation, a saturated accelerometer, a saturated gyro, and all
  axes saturated; state/covariance within 1e-11.
- `DirectFilterOracle.OrchestratedOpticalUpdates`: original `0x36f740` vs primary
  optical orchestration with 1/2/4/6 input hits, facing rejection, grazing noise,
  and an angular outlier. State/covariance within 1e-11, times/update counts agree.
  Source clock/recorder observers are stubbed; this is not the original worker loop.
- Unit test `TrackerCore.SyntheticBootstrapOpticalImuAndLoss` exercises bootstrap,
  100 synthetic sweeps, interleaved IMU, covariance symmetry, and loss marking.
- Integration `TrackingReplay.ProducesFinitePosesWithoutSteamVRRuntime` exercises
  raw capture → wired/wireless parsing → source/TDM/OOTX → bootstrap → filter → pose
  output, on the existing HMD/controller fixture. Quaternions are normalized,
  positions finite, and each device produces >100 poses. It also checks explicit
  world-frame translation and a lost-pose notification on detach. No original pose
  oracle is involved in this test. A first HMD bootstrap pose may be inertial because
  its interpolated timestamp is already >50 ms behind the latest received IMU.
- `MotionFilterOracle.RealControllerUpdatesMatchOriginalFunctions` runs a real 94 s
  moving/occluded/one-base-hidden controller recording (from `lhcapture`) through
  both the port and the original per-function (`0x36f5b0` prediction, `0x36f740`
  optical update), starting each check from the port's state. Measured on that
  capture (8401 optical updates, 3665 on a secondary base, 4444 inertial steps):
  8241 optical updates (3505 on a secondary base) and 4444 inertial steps, 0 divergent
  (>1e-6 relative), max relative state divergence 8.8e-13, covariance 1.2e-14, 0/8241
  return-flag mismatches.

### Divergence causes found and fixed

- Facing cosine (0.3 cutoff and grazing noise term) is evaluated at the
  linearization point **without** the per-sample time offset; the port had applied
  the offset. This alone accounted for all 391 previously divergent updates.
- `0x36f740` has no pre-count gate: it predicts first, and if every sample fails the
  facing cutoff it skips the EKF update but returns `true` (x, P, time untouched).
  The port's `UnpackedResidualCount` pre-gate was removed (fixed all 14 flag
  mismatches).
- Alternate bases are only added once `updateCount > 0x104` (`0x35c330`).
- Alternate-base insertion extrapolates with constant velocity and
  `q * exp(0.5·ω·dt)` (no acceleration) and does not run a filter prediction.
  Decomp-backed but **not oracle-verified** (outside the snapshot test).
- Ruled out: measurement-noise scratch layout (the port already reproduces the
  correlated 1.579e-10 floor), factorization choice, FilterOutliers.

- Viewer was launched under Xvfb with the bundled replay and a finite frame limit.
  This verifies startup/render/input-thread/shutdown, not human visual correctness.

## Build/package checks from this continuation

- Normal build: 18 unit tests, 19 oracle tests, one end-to-end replay test, and
  three capture-CLI safety tests pass.
- AddressSanitizer + UndefinedBehaviorSanitizer build (oracle disabled): unit and
  replay suites pass with leak detection enabled. This checks our runtime, not
  the unsanitized proprietary binary.
- USB-disabled build and its unit suite pass.
- Both normal and tracking-only installations were consumed by an independent
  minimal CMake application. The tracking-only consumer needs no PkgConfig/hidapi.
- Replay with explicit base placement validates position transformation. Replay
  with the local SteamVR DB also produces finite HMD/controller poses using learned
  IMU calibration; this is still not a reference-pose comparison.

## Deliberate gaps — do not claim complete parity

1. Bootstrap still uses a simplified recent-gyro average and OOTX-derived gravity
   tilt, optionally seeded from recorded DB tilt. Full original tilt uncertainty
   selection, IMU-history replay/setup, base selection heuristics, device geometry
   scale corrections, and full config-parser equivalence are not ported/verified. The solver retains three known line-search counter
   differences (see pose_solver.md).
2. Secondary base insertion uses the recovered transform composition and covariance
   constants, but does not implement all original slot replacement/uncertainty/DB
   relationship policies; it is not whole-path oracle verified.
3. The default 0.5 s input-gap restart is an explicit recovery policy to avoid the
   known startup handshake stall. It resets source/filter; it is not Valve's full
   auto-disambiguation/restart state machine. Set `inputGapReset = 0` to disable.
4. Inertial fallback advances the direct EKF during optical outages, publishing
   drifting estimates as inertial/lost. `CImuOnlyTracker` / `CKalmanQuaternion` have
   **not** been reconstructed.
5. The IMU queue before bootstrap is capped at 2,000; original full queue/panic/reset
   policies, duty-cycle diagnostics, calibration persistence, and recorded protobuf
   replay are incomplete. Raw `.lhraw` replay is supported.
6. Live USB config fallback uses standard GET_FEATURE 0x10/0x11 and zlib with bounded
   buffers, tested on synthetic streams only. Cached configs remain preferred.
   HID read errors invalidate the device and publish a lost pose, but full original
   feature-report enable/keep-alive handling and automatic live reconnect / radio
   identity replacement while the process stays running are incomplete.
7. There is no full original-filter replay harness or live SteamVR pose comparator
   yet. Stationary capture replay alone is insufficient to establish matching
   movement, acceleration, occlusion, recovery, or accuracy.

These limitations are requirements still open for the requested faithful rewrite,
not optional future tracking improvements. The available frontend is diagnostic,
not a declaration that the complete reverse-engineering project is finished.
