# Bootstrap pose solver

Implementation: `src/tracking/pose_solver.{hpp,cpp}`. Original entry `0x3ab590`,
optimization loop `0x3a9c30`, objective `0x3a26c0`, sufficiency checks
`0x3a2e30` / `0x3a3370` / `0x3a2e80`. Addresses include the Ghidra image base.

## Algorithm

- Sort samples by hardware sensor channel, then axis; use the channel map to
  retrieve the object's model point. Paired axes share a functor.
- Require at least four hits on each axis, with non-coplanar sensor geometry
  (8.5 mm triangle-height / off-plane thresholds). Prefer testing sensors seen
  on both axes; otherwise check each axis's sensor set separately.
- State is `[px, py, pz, qw, qx, qy, qz]`. Default seed: `(0,0,-2)` and identity;
  an explicit initial pose is also supported.
- Residuals are calibrated LH1 angles plus `0.25 * (1 - |q|²)`.
- At most 40 Gauss–Newton iterations, using Ceres Jets for the Jacobian and
  Eigen LDLT for the normal equations. Golden-section search on `[0,1]`, up to
  32 objective evaluations per iteration; relative interval tolerance 0.01.
- The objective normalizes the candidate quaternion and penalizes `pz >= 0`
  with `(alpha + 1) * 1e50`. Position exceeding 20 m is normalized to 1 m,
  matching the original's unusual behavior, not clamped to 20 m.
- Convergence flag: cost below both `noise² * sampleCount` and that quantity
  divided by range. Stop on predicted reduction below `(cost + 1) * 1e-8`,
  exhaustion of iterations, or a worsening objective after convergence.
- Returned `cost` is summed squared residuals divided by the optical sample
  count (the quaternion residual does not add a degree of freedom).
  Returned `error = sqrt(range * cost)`. An extra square root in the initial
  port was fixed and this field is now compared against the binary.

## Verification and remaining mismatch

Run:

```sh
cmake --build build -j2
./build/lh_oracle_tests --gtest_filter='*PoseSolverOracle*'
```

The oracle uses the real Index-controller geometry from the SteamVR cached
`lhr-13da81d1/config.json`; it skips if that file is absent. Seed 3 generates
60 poses, with facing rejection, missing hits, and measurement noise. Each pose
is tested twice (default seed and a slightly perturbed supplied pose).

Observed on this checkout:

| Seed | Optimization attempted | Insufficient | Max position component delta | Max quaternion component delta | Line-search counter mismatches |
|---|---:|---:|---:|---:|---:|
| Default | 58 | 2 | 4.33e-9 m | 1.99e-9 | 3 |
| Supplied | 58 | 2 | 1.91e-14 m | 1.50e-14 | 0 |

Statuses and outer-iteration counts agree in all 120 cases. Default-seed maximum
cost delta is 9.63e-19, and error delta is 3.84e-12. The tests retain the existing
1e-8 pose tolerance and enforce line-search counter equality for supplied seeds.
Default-seed counter differences remain explicitly reported:

- trial 0: original 1136, port 1137;
- trial 26: original 442, port 443;
- trial 28: original 960, port 951.

All three run to 40 iterations. **This is numerical agreement on this fixture,
not bit-exact solver parity or proof of end-to-end tracking equivalence.**
The source of the counter differences remains unresolved. Experiments matching
more of the original's quaternion normalization/reduction ordering and explicitly
materializing the normal-matrix inverse did not eliminate them; those experiments
were not retained. The port uses a direct LDLT solve rather than reproducing the
original's materialized inverse/Jacobian products.

Standalone unit tests cover non-coplanar sufficiency, insufficient input, missing
calibration, and an exactly generated synthetic pose without a device/config cache.

## Next integration work

The solver is not yet connected to the EKF. Still needed: sample-history selection,
bootstrap state/covariance construction, gravity/velocity initialization, buffered
IMU replay, secondary-base insertion, and IMU/optical update orchestration.
