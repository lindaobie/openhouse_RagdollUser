# CLighthouseDirect state vector (49 doubles, at object+0x448)

Verified by oracle tests of every residual/process functor (`tests/oracle/test_residuals.cpp`).
The object state is expressed in the **frame of its primary base station**.
The gravity reference is +Y before applying the tilt parameters; the base frame
itself need not be gravity-aligned. In the standalone runtime, the object's origin
and axes are the IMU's (optical geometry is transformed from config coordinates).

| Index | Meaning |
|---|---|
| 0–2 | position |
| 3–5 | velocity |
| 6–8 | acceleration (constant-acceleration model) |
| 9–12 | orientation quaternion (w, x, y, z) |
| 13–15 | angular velocity (body frame, used as q ⊗ exp(ω·dt/2)) |
| 16–17 | gravity tilt: rotation vector (x, 0, z) applied to (0, g, 0) |
| 18 | gravity magnitude g |
| 19–21 | gyro bias |
| 22–24 | accel bias |
| 25–27 | accel scale (measured = rotated / scale + bias) |
| 28–30 | IMU frame rotation (angle-axis) |
| 31–36 | alternate base 1: translation (3), angle-axis (3) — maps primary-base frame to it |
| 37–42 | alternate base 2 |
| 43–48 | alternate base 3 |

Active dimension `n` (object+0x5d0) limits the covariance/Jacobian columns used.
Base serial slots: object+0x1b8 (4 × 0x18 bytes, slot 0 = primary).
Covariance P: Eigen MatrixXd at +0x5d8; linearization point / predicted state at +0x7b0;
process Jacobian F at +0x938; process noise Q at +0x968; IMU R (7×7) at +0x998;
filter time at +0x5f0; update counter at +0x7a0.
