// Residual / process functions of vortex::CLighthouseDirect, templated on the
// scalar so they give values (double) and Jacobians (ceres::Jet<double, 49>).
// Each mirrors one CCeresFunctor<...> "double Evaluate" (vtable slot 3); operation
// order follows the binary. See ai/driver/lighthouse_direct.md for the state layout.
#pragma once

#include <cmath>

#include "lighthouse_model.hpp"

namespace lh::direct {

constexpr int kStateSize = 49;  // CStateTraits: 31 core + 3 alternate bases × 6
enum S {
  kPos = 0, kVel = 3, kAcc = 6, kQuat = 9, kOmega = 13,
  kGravTilt = 16,   // 2: gravity tilt as rotation vector (x, 0, z) of +Y (y-up world)
  kGravMag = 18,
  kGyroBias = 19, kAccelBias = 22, kAccelScale = 25,
  kImuRot = 28,     // 3: angle-axis rotation of the IMU frame
  kAltBase0 = 31,   // 6 each: translation(3), angle-axis(3); bases at 31, 37, 43
};
constexpr int kAltBaseIndex[3] = {31, 37, 43};

// ceres::AngleAxisRotatePoint as inlined in the functors (threshold DBL_EPSILON).
template <typename T>
void AngleAxisRotate(const T aa[3], const T p[3], T out[3]) {
  using std::cos;
  using std::sin;
  using std::sqrt;
  const T theta2 = aa[0] * aa[0] + aa[1] * aa[1] + aa[2] * aa[2];
  if (theta2 > T(2.220446049250313e-16)) {
    const T theta = sqrt(theta2);
    const T c = cos(theta), s = sin(theta);
    const T inv = T(1.0) / theta;
    const T w[3] = {aa[0] * inv, aa[1] * inv, aa[2] * inv};
    const T tmp = (w[0] * p[0] + w[1] * p[1] + w[2] * p[2]) * (T(1.0) - c);
    out[0] = (w[1] * p[2] - w[2] * p[1]) * s + c * p[0] + w[0] * tmp;
    out[1] = (w[2] * p[0] - w[0] * p[2]) * s + p[1] * c + w[1] * tmp;
    out[2] = (w[0] * p[1] - w[1] * p[0]) * s + p[2] * c + w[2] * tmp;
  } else {
    out[0] = (aa[1] * p[2] + p[0]) - p[1] * aa[2];
    out[1] = (aa[2] * p[0] + p[1]) - aa[0] * p[2];
    out[2] = (aa[0] * p[1] + p[2]) - p[0] * aa[1];
  }
}

// CStateTimestep (0x34bbb0): constant acceleration / angular velocity propagation.
template <typename T>
void StateTimestep(const T* x, double dtPos, double dtRot, T* out) {
  using std::cos;
  using std::sin;
  using std::sqrt;
  for (int i = 0; i < kStateSize; i++) out[i] = x[i];
  for (int i = 0; i < 3; i++) {
    out[kPos + i] = (dtPos * 0.5 * x[kAcc + i] + x[kVel + i]) * dtPos + x[kPos + i];
    out[kVel + i] = dtPos * x[kAcc + i] + x[kVel + i];
    out[kAcc + i] = x[kAcc + i];
  }
  T wx = dtRot * x[13], wy = dtRot * x[14], wz = dtRot * x[15];
  T n2 = wx * wx + wy * wy + wz * wz;
  T c = T(1.0), k = T(0.5);
  if (T(0.0) < n2) {
    const T n = sqrt(n2);
    c = cos(0.5 * n);
    k = sin(0.5 * n) / n;
  }
  wz = k * wz;
  wx = wx * k;
  wy = wy * k;
  const T qw = x[9], qx = x[10], qy = x[11], qz = x[12];
  out[9] = (qw * c - qz * wz) - (qx * wx + qy * wy);
  out[10] = qw * wx + qx * c + (qy * wz - qz * wy);
  out[11] = qw * wy + qy * c + (qz * wx - qx * wz);
  out[12] = wz * qw + wy * qx + (c * qz - wx * qy);
}

// CQuaternionNormalizer (0x349f60)
template <typename T>
T QuaternionNormalizer(const T* x) {
  return 0.25 * (1.0 - (x[9] * x[9] + x[10] * x[10] + x[11] * x[11] + x[12] * x[12]));
}

// CImuGyro (0x34ec90): measured = AngleAxisRotate(imuRot, omega) + gyroBias.
template <typename T>
void ImuGyro(const T* x, T out[3]) {
  const T aa[3] = {x[28], x[29], x[30]};
  const T w[3] = {x[13], x[14], x[15]};
  T r[3];
  AngleAxisRotate(aa, w, r);
  out[0] = r[0] + x[19];
  out[1] = x[20] + r[1];
  out[2] = r[2] + x[21];
}

// CImuAccel (0x34e720): specific force (acc + gravity) rotated into the body frame,
// then by the IMU rotation, divided by the per-axis scale, plus bias.
template <typename T>
void ImuAccel(const T* x, T out[3]) {
  // World is y-up: gravity = AngleAxisRotate((tiltX, 0, tiltZ), (0, g, 0)).
  const T tilt[3] = {x[16], T(0.0), x[17]};
  const T gz[3] = {T(0.0), x[18], T(0.0)};
  T g[3];
  AngleAxisRotate(tilt, gz, g);
  const T f[3] = {g[0] + x[6], g[1] + x[7], g[2] + x[8]};
  // conj(q) * f
  const T qw = x[9], qx = x[10], qy = x[11], qz = x[12];
  const T a17 = qw * -qy, a15 = -qy * qy, a11 = qw * -qz, a16 = qw * -qx, a13 = -qx * qx, a7 = qz * -qz;
  T b3 = (qx * qy + a11) * f[0] + (a7 + a13) * f[1] + (qy * qz - a16) * f[2];
  T b7 = (a15 + a7) * f[0] + (qx * qy - a11) * f[1] + (a17 + qx * qz) * f[2];
  T b10 = f[0] * (qx * qz - a17) + f[1] * (qy * qz + a16) + (a13 + a15) * f[2];
  const T body[3] = {b7 + b7 + f[0], b3 + b3 + f[1], b10 + b10 + f[2]};
  const T aa[3] = {x[28], x[29], x[30]};
  T r[3];
  AngleAxisRotate(aa, body, r);
  out[0] = r[0] / x[25] + x[22];
  out[1] = r[1] / x[26] + x[23];
  out[2] = r[2] / x[27] + x[24];
}

// Pose of the object at time offset dt: position (const accel) and q ⊗ exp(ω dt / 2).
template <typename T>
void ExtrapolatePoseFull(const T* x, double dt, T pos[3], T q[4]) {
  model::ExtrapolatePose(x, dt, pos, q);
}

// Alternate base pose [t, q] from state (0x34bac0): angle-axis → quaternion.
template <typename T>
void AltBasePose(const T* x, int idx, T pose[7]) {
  using std::cos;
  using std::sin;
  using std::sqrt;
  pose[0] = x[idx];
  pose[1] = x[idx + 1];
  pose[2] = x[idx + 2];
  const T n2 = x[idx + 3] * x[idx + 3] + x[idx + 4] * x[idx + 4] + x[idx + 5] * x[idx + 5];
  T k = T(0.5);
  if (n2 <= T(0.0)) {
    pose[3] = T(1.0);
  } else {
    const T n = sqrt(n2);
    k = sin(n * 0.5) / n;
    pose[3] = cos(n * 0.5);
  }
  pose[4] = x[idx + 3] * k;
  pose[5] = x[idx + 4] * k;
  pose[6] = k * x[idx + 5];
}

// Pose composition (0x34bff0): result = A ∘ B, poses as [t(3), q(w,x,y,z)].
template <typename T>
void ComposePose(const T a[7], const T b[7], T out[7]) {
  using std::sqrt;
  const T inv = T(1.0) / sqrt(a[3] * a[3] + a[4] * a[4] + a[5] * a[5] + a[6] * a[6]);
  const T w = a[3] * inv, qx = a[4] * inv, qy = a[5] * inv, qz = a[6] * inv;
  const T nx2 = -(qx * qx);
  T d = ((qx * qy - w * qz) * b[1] - (qy * qy + qz * qz) * b[0]) + (w * qy + qx * qz) * b[2];
  const T r0 = d + d + b[0];
  T e = (w * qx + qy * qz) * b[1] + (qx * qz - w * qy) * b[0] + (nx2 - qy * qy) * b[2];
  T f = (w * qz + qx * qy) * b[0] + (nx2 - qz * qz) * b[1] + (qy * qz - w * qx) * b[2];
  const T r2 = e + e + b[2];
  const T r1 = f + f + b[1];
  out[0] = a[0] + r0;
  out[1] = a[1] + r1;
  out[2] = r2 + a[2];
  out[3] = (a[3] * b[3] - a[6] * b[6]) - (a[4] * b[4] + a[5] * b[5]);
  out[4] = a[3] * b[4] + a[4] * b[3] + (a[5] * b[6] - a[6] * b[5]);
  out[5] = a[3] * b[5] + a[5] * b[3] + (a[6] * b[4] - a[4] * b[6]);
  out[6] = a[3] * b[6] + a[4] * b[5] + (a[6] * b[3] - a[5] * b[4]);
}

// Sensor position in the frame of the base that produced the measurement.
// altIdx < 0: primary base (object state is in its frame); else alternate base slot.
template <typename T>
void SensorInBase(const T* x, double dt, const double sensor[3], int altIdx, T out[3]) {
  T pos[3], q[4];
  model::ExtrapolatePose(x, dt, pos, q);
  if (altIdx < 0) {
    T r[3];
    model::QuatRotate(q, sensor, r);
    out[0] = r[0] + pos[0];
    out[1] = r[1] + pos[1];
    out[2] = r[2] + pos[2];
    return;
  }
  // 0x372170: compose(altBase, objectPose), then transform the sensor point.
  T alt[7], obj[7], c[7];
  AltBasePose(x, altIdx, alt);
  obj[0] = pos[0]; obj[1] = pos[1]; obj[2] = pos[2];
  obj[3] = q[0]; obj[4] = q[1]; obj[5] = q[2]; obj[6] = q[3];
  ComposePose(alt, obj, c);
  const T w = c[3], qx = c[4], qy = c[5], qz = c[6];
  const T nx2 = -(qx * qx);
  T d = ((qx * qy - w * qz) * sensor[1] - (qy * qy + qz * qz) * sensor[0]) + (w * qy + qx * qz) * sensor[2];
  out[0] = d + d + c[0] + sensor[0];
  d = (w * qz + qx * qy) * sensor[0] + (nx2 - qz * qz) * sensor[1] + (qy * qz - w * qx) * sensor[2];
  out[1] = d + d + c[1] + sensor[1];
  d = (qx * qz - w * qy) * sensor[0] + sensor[1] * (qy * qz + w * qx) + (nx2 - qy * qy) * sensor[2];
  out[2] = d + d + sensor[2] + c[2];
}

// CSensorPositionAngular, any base slot; baseModel 1 = LH1 (stock path), 2-5 = LH2.
template <typename T>
T SensorAngular(const T* x, double dt, const double sensor[3], int axis, const double* cal14, int altIdx,
                int baseModel = 1) {
  T p[3];
  SensorInBase(x, dt, sensor, altIdx, p);
  return model::BaseAngle(baseModel, axis, p[0], p[1], p[2], cal14);
}

// CSensorNormalCosAngle (0x34e0e0): −cos between the rotated sensor normal and the
// direction from the base to the sensor (positive when the sensor faces the base).
// Note: the normal is rotated with the non-extrapolated orientation.
template <typename T>
T SensorNormalCosAngle(const T* x, double dt, const double sensor[3], const double normal[3], int altIdx) {
  using std::sqrt;
  using std::cos;
  using std::sin;
  T p[3];
  SensorInBase(x, dt, sensor, altIdx, p);
  T w = x[9], qx = x[10], qy = x[11], qz = x[12];
  if (altIdx >= 0) {
    // 0x34c2b0: normal rotated by q_alt ⊗ q_obj.
    const T n2a = x[altIdx + 3] * x[altIdx + 3] + x[altIdx + 4] * x[altIdx + 4] + x[altIdx + 5] * x[altIdx + 5];
    T c = T(1.0), k = T(0.5);
    if (T(0.0) < n2a) {
      const T n = sqrt(n2a);
      k = sin(0.5 * n) / n;
      c = cos(0.5 * n);
    }
    const T ax = x[altIdx + 3] * k, ay = x[altIdx + 4] * k, az = x[altIdx + 5] * k;
    const T ow = w, ox = qx, oy = qy, oz = qz;
    w = (ow * c - oz * az) - (ox * ax + oy * ay);
    qx = (oz * ay - oy * az) + ox * c + ow * ax;
    qy = (ox * az - oz * ax) + oy * c + ow * ay;
    qz = (ow * az - ay * ox) + oy * ax + oz * c;
  }
  const T nx2 = -(qx * qx);
  T a = ((qx * qy - w * qz) * normal[1] - (qy * qy + qz * qz) * normal[0]) + (w * qy + qx * qz) * normal[2];
  T b = (w * qz + qx * qy) * normal[0] + (nx2 - qz * qz) * normal[1] + (qy * qz - w * qx) * normal[2];
  T c = (nx2 - qy * qy) * normal[2] + normal[1] * (qy * qz + w * qx) + normal[0] * (qx * qz - w * qy);
  const T n0 = a + a + normal[0], n1 = b + b + normal[1], n2 = c + c + normal[2];
  return -((n0 * p[0] + p[1] * n1 + p[2] * n2) * (1.0 / sqrt(p[1] * p[1] + p[0] * p[0] + p[2] * p[2])));
}

}  // namespace lh::direct
