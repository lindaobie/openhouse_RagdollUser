// Lighthouse 1.0 measurement model.
//
// Port of vortex::CLighthouseModel::Functors<...>::CSensorPositionAngular, model
// type 1 (double evaluate: axis 0 = 0x376380, axis 1 = 0x375a20; Jet versions
// 0x39ab80 / 0x3983b0). Templated on the scalar so the same code yields values
// (double) and Jacobians (ceres::Jet). Operation order follows the binary.
//
// Frames: the object state is expressed in the frame of the base station that
// produced the measurement (see ai/driver/measurement_models.md).
#pragma once

#include <cmath>
#include <limits>

namespace lh::model {

// Per-axis base station calibration as stored by the driver (7 doubles per axis,
// axis 1 starts at index 7). Only the first 5 are used by the LH1 model.
enum CalIndex { kTilt = 0, kPhase = 1, kCurve = 2, kGibPhase = 3, kGibMag = 4, kOgeePhase = 5, kOgeeMag = 6 };
constexpr int kCalPerAxis = 7;

// Object state layout used by the model (CLighthouseDirect::CStateTraits).
// Index into the 49-double state: pos 0..2, vel 3..5, acc 6..8, quat (w,x,y,z) 9..12,
// angular velocity 13..15.
enum StateIndex { kPos = 0, kVel = 3, kAcc = 6, kQuat = 9, kOmega = 13 };

// Extrapolates position and orientation by dt (0x376380 prologue).
//   p' = (0.5 a) dt² + (v dt + p)
//   q' = q ⊗ [cos(|ω dt|/2), sin(|ω dt|/2) ω dt / |ω dt|]
template <typename T>
void ExtrapolatePose(const T* s, double dt, T pos[3], T quat[4]) {
  using std::sin;
  using std::cos;
  using std::sqrt;
  if (dt == 0.0) {
    for (int i = 0; i < 3; i++) pos[i] = s[kPos + i];
    for (int i = 0; i < 4; i++) quat[i] = s[kQuat + i];
    return;
  }
  const T qw = s[9], qx = s[10], qy = s[11], qz = s[12];
  T wx = s[13] * dt, wy = s[14] * dt, wz = s[15] * dt;
  T n2 = wx * wx + wy * wy + wz * wz;
  T c = T(1.0), k = T(0.5);
  if (n2 > T(0.0)) {
    T n = sqrt(n2);
    c = cos(n * 0.5);
    k = sin(n * 0.5) / n;
  }
  wz = k * wz;
  wy = wy * k;
  wx = wx * k;
  quat[0] = (qw * c - (qx * wx + wy * qy)) - wz * qz;
  quat[1] = (qy * wz + qw * wx + qx * c) - qz * wy;
  quat[2] = (qw * wy + c * qy + qz * wx) - wz * qx;
  quat[3] = (qw * wz + qx * wy + qz * c) - qy * wx;
  const double dt2 = dt * dt;
  for (int i = 0; i < 3; i++) pos[i] = (s[kAcc + i] * 0.5) * dt2 + (s[kVel + i] * dt + s[kPos + i]);
}

// Rotates p by unit quaternion q = (w,x,y,z) (0x356e80).
template <typename T, typename P>
void QuatRotate(const T q[4], const P p[3], T out[3]) {
  const T w = q[0], x = q[1], y = q[2], z = q[3];
  const T nx2 = -(x * x);
  T d = ((x * y - w * z) * p[1] - (y * y + z * z) * p[0]) + (w * y + x * z) * p[2];
  out[0] = d + d + p[0];
  d = (y * z - w * x) * p[2] + (nx2 - z * z) * p[1] + (w * z + x * y) * p[0];
  out[1] = d + d + p[1];
  T e = p[1] * (w * x + y * z) + (x * z - w * y) * p[0] + (nx2 - y * y) * p[2];
  out[2] = e + e + p[2];
}

// LH1 sweep angle of point (x,y,z) in base frame for one axis (model type 1).
// cal points at the 7 doubles of that axis.
template <typename T>
T Lh1Angle(int axis, const T& x, const T& y, const T& z, const double* cal) {
  using std::asin;
  using std::atan2;
  using std::cos;
  using std::sqrt;
  using std::tan;
  const T nz = -z;
  if (axis == 0) {
    T a = atan2(nz, x) - cal[kPhase];
    a = a - asin((tan(T(cal[kTilt])) * y) / sqrt(z * z + x * x));
    a = a - cos(a + cal[kGibPhase]) * cal[kGibMag];
    const T d = atan2(y, nz);
    return (cal[kCurve] * d) * d + a;
  }
  T a = atan2(nz, -y) - cal[kPhase];
  a = a - asin((tan(T(cal[kTilt])) * x) / sqrt(z * z + y * y));
  a = a - cos(a + cal[kGibPhase]) * cal[kGibMag];
  const T d = atan2(x, nz);
  return (cal[kCurve] * d) * d + a;
}

// LH2 (gen2, "sync on beam") plane angle of point (x,y,z) in base frame (0x3760e0 /
// 0x375780, called by CSensorPositionAngular model types 2 and 3). The base looks down
// -z like LH1. Each revolution sweeps two planes, tilted -30 deg (plane 0) and +30 deg
// (plane 1) about the rotor axis; model type 3 swaps the tilts (flip). Returns false
// when the point is outside the model's domain (asin argument beyond +-1), as stock.
// cal points at the 7 doubles of that plane (tilt, phase, curve, gibphase, gibmag,
// ogeephase, ogeemag).
constexpr double kLh2Curve[4] = {0.0076069798, 5.3685255e-06, 0.0028679863, -8.0108022e-06};  // 0x1a3700

template <typename T>
bool Lh2Angle(int plane, bool flip, const T& x, const T& y, const T& z, const double* cal, T* out) {
  using std::asin;
  using std::atan2;
  using std::cos;
  using std::sin;
  using std::sqrt;
  using std::tan;
  constexpr double kSixth = 0.5235987755982988;  // pi/6: 0x1a9770, table 0x1a2970
  const T nz = -z;
  const T b = atan2(nz, x);
  const int side = flip ? 1 - plane : plane;
  const double tilt = cal[kTilt] + (side == 0 ? -kSixth : kSixth);
  const T r2 = nz * nz + x * x;
  const T s = (T(tan(tilt)) * y) / sqrt(r2);
  if (s < T(-1.0) || s > T(1.0)) return false;
  const double sn = sin(tilt), cs = cos(tilt);
  const T u = y / (sqrt(r2 + y * y) * cs);
  if (u > T(1.0) || u < T(-1.0)) return false;
  const T e = asin(u);  // elevation of the point off the plane's sweep
  // Curve term v(e) = e^2 (c0 + c1 e + c2 e^2 + c3 e^3) and its derivative.
  const T p = ((T(kLh2Curve[3]) * e + kLh2Curve[2]) * e + kLh2Curve[1]) * e + kLh2Curve[0];
  const T dp = (T(3.0 * kLh2Curve[3]) * e + 2.0 * kLh2Curve[2]) * e + kLh2Curve[1];
  const T v = e * e * p;
  const T dv = e * e * dp + T(2.0) * e * p;
  const T ogee = sin((b + cal[kOgeePhase]) - asin(s)) * cal[kOgeeMag] + cal[kCurve];
  const T s2 = (v * ogee) / (T(cs) - ogee * dv * sn) + s;
  if (s2 > T(1.0) || s2 < T(-1.0)) return false;
  T a = b - asin(s2);
  a = a + sin(a + cal[kGibPhase]) * cal[kGibMag];
  if (plane == 1) a = a + (2.0943951023931953 - cal[kPhase]);  // 2pi/3: 0x1a9998
  *out = a + kSixth;
  return true;
}

// Angle of one base axis (LH1 sweep or LH2 plane) by model type (CSensorPositionAngular
// +0x14, evaluates 0x376380 / 0x375a20): 1 = LH1; 2-5 = LH2 variants: 3 and 5 swap the
// plane tilts, and one plane's origin sits a few mm along y (type 3 plane 1: 9.14 mm,
// 0x1a9a60; types 4 and 5 plane 0: 9.25 mm, 0x1a9b40). cal14 is the whole base
// calibration (both axes). LH2 points outside the model's domain return NaN.
template <typename T>
T BaseAngle(int model, int axis, const T& x, const T& y, const T& z, const double* cal14) {
  const double* cal = cal14 + axis * kCalPerAxis;
  if (model >= 2 && model <= 5) {
    double dy = 0;
    if (model == 3 && axis == 1) dy = 0.00914;
    if (model >= 4 && axis == 0) dy = 0.00925;
    T out;
    if (!Lh2Angle(axis, model == 3 || model == 5, x, T(y - dy), z, cal, &out))
      return T(std::numeric_limits<double>::quiet_NaN());
    return out;
  }
  return Lh1Angle(axis, x, y, z, cal);
}

// Full CSensorPositionAngular, any model type: state -> predicted angle.
template <typename T>
T PredictAngle(const T* state, double dt, const double sensor[3], int axis, const double* cal14, int model) {
  T pos[3], q[4], r[3];
  ExtrapolatePose(state, dt, pos, q);
  QuatRotate(q, sensor, r);
  return BaseAngle(model, axis, T(r[0] + pos[0]), T(r[1] + pos[1]), T(r[2] + pos[2]), cal14);
}

// Full CSensorPositionAngular model type 1: state -> predicted angle.
// sensor: sensor position in object frame; cal14: both axes' calibration.
template <typename T>
T PredictLh1Angle(const T* state, double dt, const double sensor[3], int axis, const double* cal14) {
  T pos[3], q[4], r[3];
  ExtrapolatePose(state, dt, pos, q);
  QuatRotate(q, sensor, r);
  const T x = r[0] + pos[0], y = r[1] + pos[1], z = r[2] + pos[2];
  return Lh1Angle(axis, x, y, z, cal14 + axis * kCalPerAxis);
}

}  // namespace lh::model
