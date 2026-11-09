// Oracle test: every CLighthouseDirect residual functor (value and Jacobian) vs
// lh::direct templates evaluated with double and ceres::Jet<double, 49>.
#include <ceres/jet.h>
#include <gtest/gtest.h>

#include <random>
#include <vector>

#include "oracle.hpp"
#include "tracking/residuals.hpp"

namespace {

using Jet = ceres::Jet<double, 49>;

// Original functor = {vtable, data*}; vtable slots: 2 Evaluate(params, residuals, jacobians),
// 3 Evaluate(params, residuals), 4 NumResiduals.
struct Functor {
  void** vt;
  void* data;
  int NumResiduals() { return reinterpret_cast<int (*)(Functor*)>(vt[4])(this); }
  bool Eval(const double* x, double* r) {
    const double* p[1] = {x};
    return reinterpret_cast<bool (*)(Functor*, const double* const*, double*)>(vt[3])(this, p, r);
  }
  bool EvalJ(const double* x, double* r, double* J) {
    const double* p[1] = {x};
    double* j[1] = {J};
    return reinterpret_cast<bool (*)(Functor*, const double* const*, double*, double**)>(vt[2])(this, p, r, j);
  }
};
Functor Make(uint64_t vtableAddressPoint, void* data) {
  return Functor{static_cast<void**>(oracle::At(vtableAddressPoint)), data};
}

struct ImuData { int32_t component; int32_t pad; void* object; };
struct FakeObject { char pad[0x48]; const double* points; char pad2[0x10]; const double* normals; };
struct CalHolder { void* pad; const double* begin; const double* end; };
struct SensorData { double dt; FakeObject* object; int32_t sensor; int32_t model; CalHolder* cal; };

std::mt19937 rng(42);
double U(double a = 1) { return std::uniform_real_distribution<double>(-a, a)(rng); }

void RandomState(double* x) {
  for (int i = 0; i < 49; i++) x[i] = 0.01 * U();
  x[0] = U(0.5); x[1] = U(0.5); x[2] = -2.0 - std::abs(U(1.5));
  for (int i = 3; i < 9; i++) x[i] = U(0.5);
  double q[4] = {1 + U(0.3), U(0.3), U(0.3), U(0.3)};
  const double n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
  for (int i = 0; i < 4; i++) x[9 + i] = q[i] / n * (1 + 0.01 * U());
  for (int i = 13; i < 16; i++) x[i] = U(3);
  x[16] = U(0.05); x[17] = U(0.05); x[18] = 9.80665 + U(0.1);
  for (int i = 19; i < 25; i++) x[i] = U(0.1);
  for (int i = 25; i < 28; i++) x[i] = 1 + U(0.02);
  for (int i = 28; i < 31; i++) x[i] = U(0.05);
  for (int b = 0; b < 3; b++) {
    for (int i = 0; i < 3; i++) x[31 + 6 * b + i] = U(0.3);
    for (int i = 3; i < 6; i++) x[31 + 6 * b + i] = U(0.3);
  }
}

template <typename F>
void CompareJacobian(Functor& f, const double* x, int m, F mine, const char* what) {
  std::vector<double> ro(static_cast<size_t>(m)), J(static_cast<size_t>(m) * 49);
  ASSERT_TRUE(f.EvalJ(x, ro.data(), J.data())) << what;
  Jet xj[49];
  for (int i = 0; i < 49; i++) xj[i] = Jet(x[i], i);
  std::vector<Jet> rm(static_cast<size_t>(m));
  mine(xj, rm.data());
  for (int r = 0; r < m; r++) {
    ASSERT_NEAR(ro[size_t(r)], rm[size_t(r)].a, 1e-11 * (1 + std::abs(ro[size_t(r)]))) << what << " r" << r;
    for (int c = 0; c < 49; c++) {
      const double o = J[size_t(r) * 49 + size_t(c)], v = rm[size_t(r)].v[c];
      ASSERT_NEAR(o, v, 1e-9 * (1 + std::abs(o))) << what << " J(" << r << "," << c << ")";
    }
  }
}

TEST(ResidualsOracle, Timestep) {
  for (int it = 0; it < 200; it++) {
    double x[49];
    RandomState(x);
    const double dt = std::abs(U(0.01));
    struct { void** vt; double dtPos, dtRot; } f{static_cast<void**>(oracle::At(0x472108)), dt, dt};
    Functor& ff = *reinterpret_cast<Functor*>(&f);
    double ro[49];
    const double* p[1] = {x};
    ASSERT_TRUE(reinterpret_cast<bool (*)(void*, const double* const*, double*)>(f.vt[3])(&f, p, ro));
    double rm[49];
    lh::direct::StateTimestep(x, dt, dt, rm);
    for (int i = 0; i < 49; i++) ASSERT_NEAR(ro[i], rm[i], 1e-14 * (1 + std::abs(ro[i]))) << i;
    CompareJacobian(ff, x, 49, [&](const Jet* xj, Jet* r) { lh::direct::StateTimestep(xj, dt, dt, r); }, "timestep");
  }
}

TEST(ResidualsOracle, Imu) {
  for (int it = 0; it < 200; it++) {
    double x[49];
    RandomState(x);
    ImuData d{-1, 0, nullptr};
    Functor g = Make(0x472188, &d), a = Make(0x4721c8, &d);
    ASSERT_EQ(g.NumResiduals(), 3);
    double ro[3], rm[3];
    ASSERT_TRUE(g.Eval(x, ro));
    lh::direct::ImuGyro(x, rm);
    for (int i = 0; i < 3; i++) ASSERT_NEAR(ro[i], rm[i], 1e-13) << "gyro " << i;
    ASSERT_TRUE(a.Eval(x, ro));
    lh::direct::ImuAccel(x, rm);
    for (int i = 0; i < 3; i++) ASSERT_NEAR(ro[i], rm[i], 1e-12) << "accel " << i;
    CompareJacobian(g, x, 3, [&](const Jet* xj, Jet* r) { lh::direct::ImuGyro(xj, r); }, "gyro");
    CompareJacobian(a, x, 3, [&](const Jet* xj, Jet* r) { lh::direct::ImuAccel(xj, r); }, "accel");
    // Single components (used when an axis is saturated).
    for (int c = 0; c < 3; c++) {
      ImuData dc{c, 0, nullptr};
      Functor gc = Make(0x472188, &dc);
      ASSERT_EQ(gc.NumResiduals(), 1);
      double v;
      ASSERT_TRUE(gc.Eval(x, &v));
      lh::direct::ImuGyro(x, rm);
      ASSERT_NEAR(v, rm[c], 1e-13);
    }
  }
}

TEST(ResidualsOracle, QuaternionNormalizer) {
  double x[49];
  RandomState(x);
  char dummy = 0;
  Functor q = Make(0x472148, &dummy);
  double v;
  ASSERT_TRUE(q.Eval(x, &v));
  EXPECT_NEAR(v, lh::direct::QuaternionNormalizer(x), 1e-15);
  CompareJacobian(q, x, 1, [&](const Jet* xj, Jet* r) { r[0] = lh::direct::QuaternionNormalizer(xj); }, "qnorm");
}

TEST(ResidualsOracle, SensorAngularAndNormal) {
  // vtable address points: primary <0,-1>, <-1,0>; alt 31/37/43 × axes; normal cos: primary, alt 31/37/43
  const uint64_t angular[4][2] = {{0x472208, 0x472248}, {0x472288, 0x4722c8}, {0x472308, 0x472348}, {0x472388, 0x4723c8}};
  const uint64_t normalCos[4] = {0x472408, 0x472448, 0x472488, 0x4724c8};
  for (int it = 0; it < 300; it++) {
    double x[49];
    RandomState(x);
    double pts[6], nrm[6], cal[14];
    for (double& v : pts) v = 0.05 * U();
    for (int s = 0; s < 2; s++) {
      double n[3] = {U(), U(), U()};
      const double l = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
      for (int i = 0; i < 3; i++) nrm[3 * s + i] = n[i] / l;
    }
    for (double& c : cal) c = 0.01 * U();
    FakeObject obj{};
    obj.points = pts;
    obj.normals = nrm;
    CalHolder ch{nullptr, cal, cal + 14};
    const double dt = it % 4 == 0 ? 0.0 : 0.004 * U();
    SensorData d{dt, &obj, 1, 1, &ch};
    for (int slot = 0; slot < 4; slot++) {
      const int alt = slot == 0 ? -1 : lh::direct::kAltBaseIndex[slot - 1];
      for (int axis = 0; axis < 2; axis++) {
        Functor f = Make(angular[slot][axis], &d);
        double vo;
        ASSERT_TRUE(f.Eval(x, &vo));
        const double vm = lh::direct::SensorAngular(x, dt, pts + 3, axis, cal, alt);
        ASSERT_NEAR(vo, vm, 1e-11) << "slot " << slot << " axis " << axis << " it " << it;
        CompareJacobian(f, x, 1, [&](const Jet* xj, Jet* r) { r[0] = lh::direct::SensorAngular(xj, dt, pts + 3, axis, cal, alt); },
                        "angular");
      }
      Functor nc = Make(normalCos[slot], &d);
      double vo;
      ASSERT_TRUE(nc.Eval(x, &vo));
      const double vm = lh::direct::SensorNormalCosAngle(x, dt, pts + 3, nrm + 3, alt);
      ASSERT_NEAR(vo, vm, 1e-11) << "normal slot " << slot << " it " << it;
      CompareJacobian(nc, x, 1, [&](const Jet* xj, Jet* r) { r[0] = lh::direct::SensorNormalCosAngle(xj, dt, pts + 3, nrm + 3, alt); },
                      "normal");
    }
  }
}

}  // namespace
