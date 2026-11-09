# vortex::CLighthouseDirect (the tracker)

Ctor 0x35a0d0 `(this, ILighthouseSource* src, DB* db, TrackedObjectConfig* cfg, {int preferredBase; u16}, ?)`;
vtables 0x471fc0 / 0x472038 / 0x4720a0. One instance per tracked device.
Ported pieces: `src/tracking/residuals.hpp` (all functors ✅), `src/tracking/direct_filter.*`
(EKF core ✅). `src/tracking/lighthouse_direct.*` now connects these to bootstrap,
IMU/optical updates, and slots. The sections below describe the **original**;
the port still differs in bootstrap history initialization, secondary/DB policy,
and outage/restart behavior. See `runtime.md` for the exact evidence and gaps.
Original default state / covariance are now oracle-verified, and primary-path
IMU and optical orchestration have focused original-function oracle coverage.

## Members (offsets)
| Off | Meaning |
|---|---|
| 0x30 | tracked object model (sensor points +0x48, normals +0x60, sensor set) |
| 0x38 | ILighthouseSource |
| 0x48 | CLighthouseDB, 0x50 DB/universe handle |
| 0xf8 | flags (8 frame seen, 0x100 duty warn, 0x200 correction rejected, 0x400 IMU off scale) |
| 0xfc | rejected correction count |
| 0x1a8 | IMU-disabled flag |
| 0x1b8 + 0x18·k | base slot k: {u32 serial; double lastTime (+8); u8 (+0x10)}; k=0 primary |
| 0x218 | bootstrapped |
| 0x330 / 0x338 | pending optical frame flag / frame copy (thread loop) |
| 0x3a8 | std::map<serial, sample history> for bootstrap |
| 0x3d8 | last optical time |
| 0x3e0..0x400 | optical noise parameters (see OpticalUpdate) |
| 0x408 | |dω|² correction limit |
| 0x410/0x418/0x420, 0x428.. | gyro / accel noise (R), 0x440 quat-norm noise |
| 0x448 | state x (49) — see state_vector.md |
| 0x5d0 | active dimension n |
| 0x5d8 | covariance P (MatrixXd 49×49) |
| 0x5f0 | filter time |
| 0x7a0 | update count (≥ 101 once bootstrapped; "CALIBRATED" log at 200) |
| 0x7b0 | linearization point xLin |
| 0x938 | process Jacobian (row-major) |
| 0x950 | predicted covariance Ppred (n×n) |
| 0x968 | process noise Q |
| 0x980 | big measurement-noise scratch matrix |
| 0x998 | IMU R (7×7) |
| 0x9b0.. | deque of IMU samples awaiting processing ({double t; float acc[3]; float gyro[3]; u32 flags}) |

## EKF core (verified, tests/oracle/test_direct_filter.cpp)
- Predict (0x366610): `xLin = f(x)`, `Ppred = F_n P_n F_nᵀ + Q_n`; x and P untouched.
- Update (0x36cae0): `K = Ppred Hᵀ (H Ppred Hᵀ + R)⁻¹`, `dx = K(z − h(xLin))`; reject (0x34f570)
  → flags |= 0x200; else `x = xLin + dx`, normalize q, `P_n = (I−KH)Ppred(I−KH)ᵀ + KRKᵀ`.

## Thread loop (0x3705a0) — replaced by a synchronous driver in the port
Wait(100) → drain events (0x357fa0) → base configs (0x350240) → IMU (0x353710 queues into
0x9b0) → optical frame: PredictTo(frame time) applies queued IMU (0x36f5b0) → ProcessFrame
(0x370450) → 0x358300/0x350c90 → publish (0x351700).

## IMU update (0x36ed70)
Only when updateCount ≥ 101 and not IMU-disabled; `dt = t − filterTime ∈ [0, 0.25]`.
Measurements: gyro (3) and accel (3) for non-saturated axes, z = sample values (float→double),
plus quaternion norm (z = 0). Noise: gyro +0x410.., accel +0x428.., qnorm +0x440, or the
precomputed 7×7 at +0x998 when nothing is saturated. ProcessNoise(dt) → Predict(dt) → Update
(mode 0); on success filterTime = t. Saturated → "IMU went off scale." (flag 0x400).

## ProcessFrame (0x370450, with bootstrap merged from 0x35c5b0)
1. flags |= 8; base = DB.Lookup(frame.serial) (0x340090) else ignore; DB.OnFrame(sync +
   clock offset) (0x3438c0); ignore unless base calibrated (+300).
2. Base already in a slot: if left & right sync real → OpticalUpdate(frame, baseCal = base+0xe8);
   then 0x353380.
3. Otherwise (left & right real, no high jitter): append frame to the per-base sample history
   (0x3def20). Bootstrap needs contiguous samples (0x3df2e0, "Not enough contiguous samples…"),
   then CLighthousePoseSolver (0x3ab590, 7-parameter pos+quat functors).
   - Not yet bootstrapped (updateCount < 101): base selection (0x35c4b0, 0x35bf00 "Trying to %s
     %08X: %s"), primary slot = serial, state pos/quat = solved pose, updateCount = 101, times =
     frame time, init P (0x350280), ω from gyro average, velocity from a second solve on older
     samples, gravity tilt from base accel (OOTX) or recorded value ("base/recorded pitch ~ roll ~"),
     replay queued IMU (0x356560), log "BOOTSTRAPPED base …".
   - Already bootstrapped: secondary base (0x35c330): alt slot pose = solved pose composed with
     current object pose, stored as translation + angle-axis at 31+6k; P init for slot (0x34b810);
     DB notified (0x347bb0); log "SECONDARY base …".

## OpticalUpdate (0x36f740)
frame time = min over samples of (time + width/2); gather samples (0x35b380: one hit per
sensor/axis, sensor in model set, width in [1.06 µs, 132.6 µs]) (0x35e420 further filtering);
PredictTo(frame time); dt clamp 0.25; ProcessNoise + Predict; for each sample build the facing
functor of the matching base slot and drop samples with cos < 0.3 (0x1a9820); build angular
functors (0x350580) + quat norm; R diagonal from distance d to base (0x351ce0):
`base = p3e0 + p3e8 + atan(p3f0/d)² + atan(p3f8/d)² + p400`, plus atan(k·(0.5 − cos))² for grazing
samples; EkfUpdate(mode 1); success → updateCount++, times, 0x354070, "CALIBRATED" at 200.
