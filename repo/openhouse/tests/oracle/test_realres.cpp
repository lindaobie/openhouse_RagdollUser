// Focused: our residual/Jacobian vs original functor at the real trajectory state,
// using the real Index-controller geometry from config.json.
#include <gtest/gtest.h>
#include <ceres/jet.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include "oracle.hpp"
#include "tracking/residuals.hpp"
#include "tracking/tracked_object.hpp"

namespace {
struct Functor {
  void** vt; void* data;
  bool Eval(const double* x, double* r) {
    const double* p[1] = {x};
    return reinterpret_cast<bool (*)(Functor*, const double* const*, double*)>(vt[3])(this, p, r);
  }
};
Functor Make(uint64_t vtableAddressPoint, void* data) {
  return Functor{static_cast<void**>(oracle::At(vtableAddressPoint)), data};
}
struct FakeObject { char pad[0x48]; const double* points; char pad2[0x10]; const double* normals; };
struct CalHolder { void* pad; const double* begin; const double* end; };
struct SensorData { double dt; FakeObject* object; int32_t sensor; int32_t model; CalHolder* cal; };
}

TEST(RealResidual, MatchesOriginalAtTrajectoryState) {
  const char* home = getenv("HOME");
  std::ifstream cf(std::string(home ? home : "") + "/.local/share/Steam/config/lighthouse/lhr-13da81d1/config.json");
  if (!cf) GTEST_SKIP() << "no controller config";
  std::stringstream ss; ss << cf.rdbuf();
  lh::TrackedObjectModel m;
  ASSERT_TRUE(lh::TrackedObjectModel::FromJson(ss.str(), &m));
  const auto imu = m.InImuFrame();
  std::vector<double> pts, nrm;
  for (auto& p : imu.points) pts.insert(pts.end(), p.begin(), p.end());
  for (auto& n : imu.normals) nrm.insert(nrm.end(), n.begin(), n.end());
  FakeObject obj{}; obj.points = pts.data(); obj.normals = nrm.data();
  std::vector<double> cal(14);
  for (int i = 0; i < 14; ++i) cal[i] = 0.001 * (i - 7);
  CalHolder ch{nullptr, cal.data(), cal.data() + 14};
  double x[49];
  { std::ifstream f("/tmp/lh_check0_xlin.txt"); for (int i = 0; i < 49; ++i) f >> x[i]; }
  int valueBad = 0, jacBad = 0, tested = 0;
  for (int sensor = 0; sensor < int(imu.points.size()); ++sensor) {
    for (int axis = 0; axis < 2; ++axis) {
      SensorData d{0.003, &obj, sensor, 1, &ch};
      Functor f = Make(axis == 0 ? 0x472208 : 0x472248, &d);
      double vo; ASSERT_TRUE(f.Eval(x, &vo));
      const double vm = lh::direct::SensorAngular(x, 0.003, pts.data() + 3 * sensor, axis, cal.data(), -1);
      ++tested;
      if (std::abs(vo - vm) > 1e-12) { ++valueBad; printf("VALUE sensor %d axis %d orig %.17g mine %.17g diff %.3g\n", sensor, axis, vo, vm, vo - vm); }
      ceres::Jet<double, 49> xj[49];
      for (int i = 0; i < 49; ++i) xj[i] = ceres::Jet<double, 49>(x[i], i);
      const auto rj = lh::direct::SensorAngular(xj, 0.003, pts.data() + 3 * sensor, axis, cal.data(), -1);
      for (int j = 0; j < 49; ++j) {
        if (j == 9 || j == 10 || j == 11 || j == 12) continue;  // quat renormalized in orig path
        double xp[49]; std::copy(x, x + 49, xp); xp[j] += 1e-7;
        double vp; f.Eval(xp, &vp);
        const double fd = (vp - vo) / 1e-7;
        if (std::abs(fd - rj.v[j]) > 1e-4 * (1 + std::abs(fd))) { ++jacBad; printf("JAC sensor %d axis %d j %d fd %.6g mine %.6g\n", sensor, axis, j, fd, rj.v[j]); }
      }
    }
  }
  printf("RealResidual: tested %d, valueBad %d, jacBad %d\n", tested, valueBad, jacBad);
  EXPECT_EQ(valueBad, 0);
  EXPECT_EQ(jacBad, 0);
}
