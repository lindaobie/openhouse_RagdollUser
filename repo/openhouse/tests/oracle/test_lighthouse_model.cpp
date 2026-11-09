// Compares lh::model::PredictLh1Angle against the original
// CSensorPositionAngular<0,-1> (0x376380) / <-1,0> (0x375a20) double Evaluate.
#include <gtest/gtest.h>

#include <random>

#include "oracle.hpp"
#include "tracking/lighthouse_model.hpp"

namespace {

// Memory layout the original functor reads (see ai/driver/measurement_models.md).
struct CalHolder { void* pad; const double* begin; const double* end; };
struct FakeObject { char pad[0x48]; const double* sensors; };
struct FunctorData {
  double dt;
  FakeObject* object;
  int32_t sensor;
  int32_t model;
  CalHolder* cal;
};
struct FakeFunctor { void* vtbl; FunctorData* data; };

using EvalFn = bool (*)(FakeFunctor*, double**, double*);

double Original(int axis, const double* state, double dt, const double* sensors, int sensor, const double* cal14) {
  FakeObject obj{};
  obj.sensors = sensors;
  CalHolder ch{nullptr, cal14, cal14 + 14};
  FunctorData d{dt, &obj, sensor, 1, &ch};
  FakeFunctor f{nullptr, &d};
  double* params[1] = {const_cast<double*>(state)};
  double out = 0;
  auto fn = oracle::Fn<EvalFn>(axis == 0 ? 0x376380 : 0x375a20);
  EXPECT_TRUE(fn(&f, params, &out));
  return out;
}

TEST(LighthouseModelOracle, Lh1AngleMatchesOriginal) {
  std::mt19937 rng(1234);
  std::uniform_real_distribution<double> u(-1, 1);
  int n = 0;
  for (int iter = 0; iter < 2000; iter++) {
    double state[49] = {};
    state[0] = u(rng);
    state[1] = u(rng);
    state[2] = -2.0 - 2.0 * std::abs(u(rng));  // in front of the base (-z)
    for (int i = 3; i < 9; i++) state[i] = 0.5 * u(rng);
    double q[4] = {1 + u(rng), 0.3 * u(rng), 0.3 * u(rng), 0.3 * u(rng)};
    double qn = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int i = 0; i < 4; i++) state[9 + i] = q[i] / qn;
    for (int i = 13; i < 16; i++) state[i] = 3.0 * u(rng);
    double sensors[6] = {0.05 * u(rng), 0.05 * u(rng), 0.05 * u(rng), 0.05 * u(rng), 0.05 * u(rng), 0.05 * u(rng)};
    double cal[14];
    for (double& c : cal) c = 0.01 * u(rng);
    const double dt = iter % 3 == 0 ? 0.0 : 0.005 * u(rng);
    for (int axis = 0; axis < 2; axis++) {
      const double want = Original(axis, state, dt, sensors, 1, cal);
      const double got = lh::model::PredictLh1Angle(state, dt, sensors + 3, axis, cal);
      ASSERT_NEAR(got, want, 1e-12) << "iter " << iter << " axis " << axis;
      n++;
    }
  }
  EXPECT_EQ(n, 4000);
}

}  // namespace

namespace {

double OriginalModel(int axis, int model, const double* state, double dt, const double* sensors, const double* cal14,
                     bool* ok) {
  FakeObject obj{};
  obj.sensors = sensors;
  CalHolder ch{nullptr, cal14, cal14 + 14};
  FunctorData d{dt, &obj, 1, model, &ch};
  FakeFunctor f{nullptr, &d};
  double* params[1] = {const_cast<double*>(state)};
  double out = 0;
  *ok = oracle::Fn<EvalFn>(axis == 0 ? 0x376380 : 0x375a20)(&f, params, &out);
  return out;
}

// LH2 planes (model types 2-5) against the original evaluate.
TEST(LighthouseModelOracle, Lh2AngleMatchesOriginal) {
  std::mt19937 rng(99);
  std::uniform_real_distribution<double> u(-1, 1);
  int n = 0, invalid = 0;
  for (int iter = 0; iter < 2000; iter++) {
    double state[49] = {};
    state[0] = 1.5 * u(rng);
    state[1] = 1.5 * u(rng);
    state[2] = -1.0 - 3.0 * std::abs(u(rng));
    for (int i = 3; i < 9; i++) state[i] = 0.5 * u(rng);
    double q[4] = {1 + u(rng), 0.3 * u(rng), 0.3 * u(rng), 0.3 * u(rng)};
    double qn = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int i = 0; i < 4; i++) state[9 + i] = q[i] / qn;
    for (int i = 13; i < 16; i++) state[i] = 3.0 * u(rng);
    double sensors[6] = {0.05 * u(rng), 0.05 * u(rng), 0.05 * u(rng), 0.05 * u(rng), 0.05 * u(rng), 0.05 * u(rng)};
    double cal[14];
    for (int a = 0; a < 2; a++) {  // realistic LH2 magnitudes (lighthousedb)
      double* c = cal + 7 * a;
      c[0] = 0.05 * u(rng); c[1] = a ? 0.01 * u(rng) : 0.0; c[2] = 0.5 * u(rng); c[3] = 3.0 * u(rng);
      c[4] = 0.01 * u(rng); c[5] = 3.0 * u(rng); c[6] = 0.7 * u(rng);
    }
    const double dt = iter % 3 == 0 ? 0.0 : 0.005 * u(rng);
    for (int model : {2, 3, 4, 5})
      for (int axis = 0; axis < 2; axis++) {
        bool ok = false;
        const double want = OriginalModel(axis, model, state, dt, sensors, cal, &ok);
        const double got = lh::model::PredictAngle(state, dt, sensors + 3, axis, cal, model);
        if (!ok) { EXPECT_TRUE(std::isnan(got)) << "iter " << iter; ++invalid; continue; }
        ASSERT_NEAR(got, want, 1e-12) << "iter " << iter << " model " << model << " axis " << axis;
        n++;
      }
  }
  EXPECT_GT(n, 14000);
  std::printf("LH2 model: %d matched, %d outside the domain on both\n", n, invalid);
}

}  // namespace
