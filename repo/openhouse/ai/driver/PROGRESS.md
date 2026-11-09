# Porting progress

Legend: ✅ ported + verified against the original, 🟡 ported (not yet diff-tested),
🔍 reverse engineered/documented, ⬜ not started.

## Transport (lh_usb) — `src/transport/usb`
| Item | Original | Status |
|---|---|---|
| hidapi enumeration / read loop | 0x3cb310 | 🟡 runs on Index HMD + dongles |
| Tick unwrapping | 0x3ced40 | ✅ unit test |
| Clock sync (host↔device offset) | 0x3cf2c0 | 🟡 |
| IMU report 0x20, range modes (feature 0x01) | 0x3d0a00, 0x3d07c0, 0x3ce260 | ✅ unit test, 1000 Hz live |
| Optical reports 0x21/0x25/0x27/0x28 | 0x3cf250 | ✅ (0x25 verified via TDM diff) |
| Raw capture (.lhraw) | new | ✅ round-trip/replay + buffered I/O failure tests |
| Generic session recorder (`lhcapture`) | new | ✅ raw capture, optional notes, config/DB snapshots, live counters; CLI/round-trip/failure tests; see capture.md |
| Device config via feature 0x10/0x11 + zlib | standard protocol | 🟡 bounded reader + synthetic tests; fallback when cached config absent, not hardware verified |
| Wireless Watchman legacy v0 + Wv2 decoders | 0x3fdec0, 0x3fe6d0 | ✅ bit-exact (Index controller, both protocols) |
| Wireless device info / protocol handshake / device serial | 0x3dd8a0, 0x3cc9a0 | 🟡 works live |
| HMD keep-alive / mode feature reports | 0x337390, 0x3cb9a0.. | 🔍 partially |

## Tracking (lh_tracking) — `src/tracking`
| Item | Original | Status |
|---|---|---|
| LH1 measurement model (CSensorPositionAngular type 1) | 0x376380 / 0x375a20 | ✅ oracle 1e-12 |
| TDM disambiguator | CDisambiguatorTDM | ✅ bit-exact (HMD sensorType 0, Index controller sensorType 9) |
| OOTX decode (ring, framer, config, dynamic state) | 0x3b72f0, 0x3dfa30, 0x3b8170 | ✅ bit-exact, matches lighthousedb |
| Framer disambiguator (start-up alt) | CDisambiguatorFramer | ⬜ |
| SwSyncDetect source (IMU queue, advance timing, TDM outputs) | CSwSyncDetectSource | 🟡 LH1 source implemented; frame/config capture parity; full original source not instantiated (source_layer.md) |
| Lighthouse DB / universes / base poses | CLighthouseDB | 🟡 explicit placements + read-only JSON importer; original DB selection/persistence not ported |
| Bootstrap pose solver | 0x3ab590, 0x3a9c30 | 🟡 120 oracle cases: status/iterations agree, poses within 1e-8; 3 default-seed line-search counter mismatches remain (pose_solver.md) |
| CLighthouseDirect residual functors (timestep, IMU, angular, facing, alt bases) | 0x34bbb0, 0x34e720, 0x376380, … | ✅ values+Jacobians vs oracle |
| CLighthouseDirect EKF core (Q, predict, update, rejection) | 0x355990, 0x366610, 0x36cae0, 0x34f570 | ✅ 50-step sequence vs oracle |
| CLighthouseDirect orchestration (IMU/optical update, bootstrap, slots) | 0x36ed70, 0x36f740, 0x370450 | 🟡 per-update motion oracle **verified** on the real motion capture: 8241 optical (3505 secondary) + 4444 inertial updates match original to 1e-12, 0 flag mismatches; bootstrap/slot-insertion and base-switch policy not yet oracle-verified |
| Bootstrap sample-history selection/pruning | 0x3de790..0x3df5c6 | ✅ 800 oracle selections |
| Default state / initial covariance | 0x3511a0, 0x350280 | ✅ exact vs oracle |
| IMU-only fallback | CImuOnlyTracker, CKalmanQuaternion | ⬜ original; 🟡 standalone EKF outage policy exists |
| Public multi-device session / pose output | new | 🟡 `lh::Tracker`, JSONL CLI; raw replay integration test |
| Frontend (viewer) | new | 🟡 `lhviz` (liblh + Dear ImGui): 3D room, devices, Index input, events; screenshot-checked on replay |
| Installable tracking-only CMake package | new | 🟡 `lh::tracking`, USB/graphics optional |

## lh runtime (liblh) — `src/runtime`, design: `ai/driver/lh_runtime.md`
Our own system on top of the verified filter (not a copy of the stock structure).
| Item | Status |
|---|---|
| Public session API (`include/lh/session.hpp`, `liblh.so`) | ✅ live USB + replay; runtime tests |
| Shared room map: SteamVR import, room selection, origin, learning/refinement, moved-base detection, persistence | ✅ unit tests + replay (base distance 4.92 m = SteamVR's) |
| Device tracker: room-seeded bootstrap tilt, known alt bases from the map, calibration carry-over, stock health check (0x358300) | ✅ replay: reacquire 0.06–1.15 s after reset with calibration kept |
| Fast base identification from ~40 OOTX bits (shared + persisted identities) | ✅ replay: startup bootstrap 3.2 s earlier with saved map |
| Room solver: bundle adjustment over keyframes from all devices | ✅ simulated test (< 2 mm), held-out residual 10× lower than SteamVR's stored room; second base 0.22 s after first light |
| Device enable/disable (persisted) | ✅ lhviz checkbox, `--disable` |
| Device roles from JSON `device_class` (Index = controller, rest = tracker) | ✅ unit tests |
| Index controller input + battery (libsurvive mapping) | ✅ decoded on the recorded and live Knuckles |
| Stock fixes: CALIBRATED covariance re-seed (0x350450), interpolated bootstrap gyro (0x350600) | 🟡 decoded from the binary; not oracle-tested end to end |
| Sensor type from hardware id (report 0x05, receivers need retries); width inference only as fallback | ✅ live: HMD 0, controller 9, tracker 4 (matches inference and docs) |
| Bootstrap range gate (0x35bf00: z <= -0.1 m, <= 7 m) | ✅ ported; replays unchanged |
| Bootstrap leave-one-sensor-out retry (not in stock) | ✅ live + `captures/floor-reflection`: tracker/controller on the floor never bootstrapped (1 sensor ~2.3 mrad off); now both track, static std < 0.7 mm; `BootstrapsDespiteOneBadSensor` |
| Drop-in OpenVR `driver_lighthouse.so` (tracked devices only) | 🟡 actual SteamVR/Sniper replay: driver loads, valid poses, skeletal action/bones read, haptic event delivered; actual Monado: controller + chaperone discovered, then no-HMD system-creation limit; full live host parity unverified |
| Steam Runtime dependency packaging | tested: Ceres + transitive libraries bundled with relative RPATH; installer backs up stock; actual vrserver loads without `libceres.so.4` error |
| Index estimated skeleton | tested: both-hand finite/unit/mirrored geometry; actual SteamVR reads skeleton. Asset-driven curl synthesis, not stock estimator; visual correctness unverified |
| Index haptics / SteamVR Identify | ✅ `HapticReport` ports stock 0x3f4e50; matches a usbmon capture of stock Identify (duration 0, 200 Hz, amp 0.125 → `ff 8f 08 00 f4 01 f4 01 01 00 ff` every frame). User-confirmed 2026-10-18: tone + LED flash (the LED flash needs this exact pattern). Zero-amplitude events ignored. Stock framing: 8-byte payload with trailing ff, on/off order and stop fields (0x3cb9a0/0x3cba30), replacing incorrect old libsurvive packet; replay routing + wire tests pass |
| Wireless controller power cycles | fixed stale-state path: handle 0x26/data gap, reset protocol/timestamps/clock/IMU dedup and recreate tracker; room retained. Rejected IMU calibration is never carried over (was a fail/carry loop), and replayed pre-reboot light timestamps no longer stall the optical timeout. Regressions pass on Linux and Wine; hardware retest pending |
| Direct haptics diagnostic | `lhtool haptics` bypasses the OpenVR path and print actual HID write results; real controller vibrated on every packet shape tried, so receiver output works |
| SteamVR device Turn Off | ported: capability property + per-device standby → queued stock `off!` command; mock host/packet tests pass; physical power-down/reconnect unverified |
| Windows x64 | built: self-contained driver DLL + liblh/apps; pinned vcpkg/MinGW build and ZIP. 8/8 suites pass under Wine, including DLL mock host and power-cycle replay; viewer screenshot checked. Native Windows SteamVR/USB unverified; see windows_build.md |
| Host raw-frame alignment after room rebuild | synthetic test: shared-base transform + reference universe; no changes to host room files; genuinely new rooms still require room setup |

Latest driver checks: 9/9 CTest suites pass with installed hand assets; USB-disabled
build and its 2 test suites pass (replay explicitly skipped without capture support).
Installer round-trip restores the stock binary byte-for-byte. The updated packaged
driver is installed locally, with the original at `driver_lighthouse.so.stock`.
See `ai/driver/openvr_driver.md` for actual-host results and remaining verification gaps.

## Resume checkpoint

- Generic user capture tool: `./build/lhcapture` records until Ctrl-C into a new
  timestamped session directory. No prescribed motion sequence or estimator runs.
  Keep the whole directory for analysis. Instructions: `ai/driver/capture.md`.

- `ai/driver/pose_solver.md` records the solver's measured deltas and unresolved counter
  differences. Its reported error estimate now matches the binary (extra square
  root removed). Do not describe this solver as bit-exact.
- Source, history, bootstrap, and filter are connected via `lh::Tracker`. Both HMD
  and Index controller now produce finite, normalized replay poses. `lhtool track`
  and `lhviz` are usable; see the root README for commands.
- New primary-path oracle tests cover original IMU saturation handling and optical
  update orchestration (facing, grazing noise, angular outliers, update counts).
- This table is the port's history (what came from where). The driver has since been
  released as openhouse 1.0; current work and open issues are in `ai/README.md`, and
  live comparisons against the stock driver run in `openhouse-compare`.
- Regression command: `cmake --build build -j16 && ctest --test-dir build --output-on-failure`.
  Oracle fixtures require the original binary, captures, and cached controller config;
  missing captures/config can skip their tests. No new hardware capture is needed
  for the current solver/source work.
