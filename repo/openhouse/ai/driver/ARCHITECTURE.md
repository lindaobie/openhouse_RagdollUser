# driver_lighthouse.so — reverse-engineered architecture

Target: SteamVR `driver_lighthouse.so` (x86-64 Linux, stripped, BuildID
`95b76c32315b26ca21bedc60544d6b91b4a255da`, built with GCC 9.3.1 SteamRT, no FMA
instructions). Ghidra image base is `0x100000`; every address in these docs is a
Ghidra address (file VA + 0x100000).

Internal source tree (from embedded paths): `/data/src/vortexlib/*` (tracking core,
namespace `vortex`), `/data/src/drivers/lighthouse/*` (SteamVR glue), `/data/src/aitcamlib`
(camera, out of scope).

Statically linked third-party code (not ported, replaced by system libs):
libstdc++, protobuf 2.6.1, Ceres Solver, Eigen, jsoncpp, nlohmann::json, tinygltf.

## Data flow (vortex namespace)

```
 USB HID (libudev + hidraw)
   CHidDevice ── CLighthouseUsbThread
        │                 │
        │          CWirelessDecoder / CWirelessDecoderWv2   (Watchman dongle packets)
        ▼
   CLighthouseIMUThread  ── IImuCallback ──► IMU samples
   CLighthouseOpticalThread ── ICallback ──► raw light pulses
        ▼
   CLighthouseSource / CSwSyncDetectSource     (per tracked object)
        │   IDisambiguator:
        │     CDisambiguatorTDM        (LH1, sync-pulse + sweep, A/B/C TDM)
        │     CDisambiguatorFramer     (LH1 framer mode)
        │     CDisambiguatorSyncOnBeam (LH2 / "SOB")
        │   OOTX decode → base station calibration (fcal)
        ▼
   CLighthouseDB / CLighthouseDBUniverse  (lighthousedb.json, base poses, universes)
        ▼
   IPoseTracker:
     CLighthouseDirect  — custom EKF with Ceres Jet Jacobians, 49-parameter state,
                          residual functors in CLighthouseModel::Functors<>
     CLighthousePoseSolver — 7-parameter (pos+quat) bootstrap solver
     CImuOnlyTracker / CKalmanQuaternion — IMU-only fallback
        ▼
   pose → vr::DriverPose_t (CLighthouseHmdDriver / ...)
```

Recording/replay: `CLighthouseRecorder`/`CLighthouseReplayer` stream
`lighthouse.pb.CRecord` messages (see `proto/lighthouse_debug.proto`), triggered by
the debug request `start_tracking_recording %d %d %d` / `stop_tracking_recording`.

See `re/names.tsv` / `re/fn_calls.tsv` for addresses, `state_vector.md` and `measurement_models.md`
for the filter, and `PROGRESS.md` for porting status.

## Standalone implementation

`lh_usb` (optional) → `IInputSink` → `lh::Tracker` → per-device `TrackingSource`
→ TDM/OOTX + float IMU queue → `SampleHistory` / `LighthouseDirect` → `TrackedPose`.
`lhtool track` emits JSON Lines.

On top of that, `liblh.so` (`include/lh/session.hpp`, `src/runtime/`) is the
session runtime: one room map shared by all devices, per-device trackers built on the
verified filter, roles from the device JSON, Index controller input, persistence.
`lhviz` (3D visualizer) and `lhtool session` use only that API. See `lh_runtime.md`.
Transport, tracking, and frontend are separate targets; tracking alone builds
without hidapi or graphics. See `runtime.md` for implemented paths, tests, and
important remaining parity gaps. Poses are IMU-origin poses, not DriverPose_t.

## Scope decision (2026-10-16)
Only **Lighthouse 1.0** tracking is reimplemented: TDM / Framer disambiguators, LH1 OOTX,
LH1 angle model (CSensorPositionAngular model type 1). The Sync-on-Beam disambiguator and
LH2 angle models (types 2–5, 0x3760e0/0x375780) are documented but not ported. The USB
layer still decodes LH2 report 0x28 into Lh2* events, which the tracker ignores.

## What runs on the reference setup (from ~/.local/share/Steam/logs/vrserver.txt)
`Source: auto disambiguator (right sync)` → `SwSyncDetect Start, primary: tdm, alt: framer`
→ `SwSyncDetect selected tdm` on every device (HMD SensorType 0, controllers SensorType 9).
So CDisambiguatorTDM is the production path; Framer only competes during start-up.
The log also prints BOOTSTRAPPED/CALIBRATED lines (distance, base pitch/roll) that serve as
a sanity reference for the port (bases A101B835 pitch≈137°, E236DC40 pitch≈50°).
