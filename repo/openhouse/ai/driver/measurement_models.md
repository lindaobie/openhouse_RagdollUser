# Measurement and process models of CLighthouseDirect

Port: `src/tracking/residuals.hpp` (templated: double / ceres::Jet<double,49>).
Each original functor is `CCeresFunctor<F, double, 49>`; vtable slot 2 = Evaluate with
Jacobians (autodiff), slot 3 = values, slot 4 = residual count.

| Functor | double Eval | Model |
|---|---|---|
| CStateTimestep | 0x34bbb0 | p += (½a·dt + v)·dt; v += a·dt; q ← q ⊗ exp(ω·dt/2); rest constant |
| CQuaternionNormalizer | 0x349f60 | 0.25·(1 − ‖q‖²), measured 0 |
| CImuGyro | 0x34ec90 | AngleAxisRotate(imuRot, ω) + gyroBias |
| CImuAccel | 0x34e720 | AngleAxisRotate(imuRot, q⁻¹·(acc + g)) / scale + bias, g = AA((tx,0,tz),(0,g,0)) |
| CSensorPositionAngular<0,-1> / <-1,0> | 0x376380 / 0x375a20 | LH1 angle (axis 0 / 1) of the sensor in base frame |
| … AlternateBase<31/37/43> | 0x375500 … | same, sensor point mapped by compose(altPose, objectPose) |
| CSensorNormalCosAngle (+ alternate) | 0x34e0e0, 0x34c2b0 … | −cos(normal_world, sensor_dir); used to drop back-facing samples |

Sensor positions are extrapolated by the sample's time offset `dt` from the filter time
(position with constant acceleration, orientation with ω); normals use the
non-extrapolated orientation.

LH1 angle (calibration per axis: tilt, phase, curve, gibphase, gibmag):
```
axis 0: a = atan2(−z, x) − phase − asin(tan(tilt)·y/√(z²+x²)); a −= cos(a+gibphase)·gibmag; a += curve·atan2(y, −z)²
axis 1: a = atan2(−z, −y) − phase − asin(tan(tilt)·x/√(z²+y²)); a −= cos(a+gibphase)·gibmag; a += curve·atan2(x, −z)²
```

## Process noise (0x355990), diagonal Q plus blocks
- pos/vel/acc per axis: q·G·Gᵀ, G = [dt²/2, dt, 1], q = 0.25 (0x354560)
- quaternion/ω: s = 1.0966227·dt; Q_qq = s·dt²/12 · (I·‖q‖² − q qᵀ) form, Q_qω = s·dt/4 · Ξ(q), Q_ωω = s·I (0x3556d0)
- gravity tilt 8.4616e-8·dt, gyro bias 3.3846e-9·dt, accel bias xy 6.6785e-9·dt / z 2.6714e-8·dt,
  accel scale 1.1111e-11·dt; +1e-11 on the whole diagonal.

## Update rejection (0x34f570) on the correction dx
NaN; |dIMUrot| > 10° (2° after 260 updates); |dg| > 0.05 g (0.01 g after 260 updates);
|dtilt| > 4°; |dω|² > limit (object+0x408); alternate bases: |dt| > 0.1 m or |drot| > 1°.
After an accepted update the quaternion is renormalized.
