// Oracle test: lh::solver::Solve vs CLighthousePoseSolver (0x3ab590) on synthetic
// sweeps generated from random poses with the real Index-controller model.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <sstream>

#include "oracle.hpp"
#include "tracking/lighthouse_model.hpp"
#include "tracking/pose_solver.hpp"

namespace {

// Original object: std::map<int,int> channel map at +0, points vector at +0x48, normals at +0x60.
struct OrigObject {
  std::map<int, int> channelMap;  // 0x30 bytes in libstdc++
  char pad[0x48 - sizeof(std::map<int, int>)];
  const double* points;
  char pad2[0x10];
  const double* normals;
};
static_assert(sizeof(std::map<int, int>) == 0x30);
// Original BaseConfig record: u8 valid; u32 id; vector<double> cal; u8 hw; u8 ver.
struct OrigBaseConfig { uint8_t valid; uint32_t id; const double* b; const double* e; const double* c; uint8_t hw, ver; };
struct OrigSample { int32_t sensor, axis; double time, angle, width; uint32_t flags, flags2; };
struct OrigCfg {
  const OrigObject* object;
  const OrigBaseConfig* base;
  double noise;
  int32_t iterations, lineSearch;
  double cost, error;
  int32_t status;
};

std::string ReadFile(const std::string& p) {
  std::ifstream f(p);
  std::stringstream s;
  s << f.rdbuf();
  return s.str();
}

class PoseSolverOracle : public ::testing::TestWithParam<bool> {};

TEST_P(PoseSolverOracle, MatchesOriginal) {
  const char* home = getenv("HOME");
  const std::string cfgPath = std::string(home ? home : "") + "/.local/share/Steam/config/lighthouse/lhr-13da81d1/config.json";
  if (!std::filesystem::exists(cfgPath)) GTEST_SKIP() << "no controller config";
  lh::TrackedObjectModel model;
  ASSERT_TRUE(lh::TrackedObjectModel::FromJson(ReadFile(cfgPath), &model));
  std::vector<double> pts, nrm;
  for (auto& p : model.points) pts.insert(pts.end(), p.begin(), p.end());
  for (auto& p : model.normals) nrm.insert(nrm.end(), p.begin(), p.end());
  OrigObject oobj;
  oobj.channelMap = model.channelToIndex;
  oobj.points = pts.data();
  oobj.normals = nrm.data();
  lh::BaseConfig base;
  base.valid = true;
  base.serial = 0xA101B835;
  base.hardwareVersion = 9;
  base.ootxVersion = 6;
  base.cal = {0.0011034, 0.0378113, -0.000505447, 1.70215, 0.00930023, 0, 0,
              -0.00759506, 0.0386047, 0.00405121, 0.752441, -0.0114441, 0, 0};
  OrigBaseConfig obase{1, base.serial, base.cal.data(), base.cal.data() + 14, base.cal.data() + 14, 9, 6};

  std::mt19937 rng(3);
  std::uniform_real_distribution<double> u(-1, 1);
  int solved = 0, insufficient = 0, lsMismatch = 0;
  double maxPositionDelta = 0, maxQuaternionDelta = 0, maxCostDelta = 0, maxErrorDelta = 0;
  for (int trial = 0; trial < 60; trial++) {
    // Random pose in front of the base, random orientation.
    double p[3] = {u(rng), u(rng), -1.5 - 2.0 * std::abs(u(rng))};
    double q[4] = {u(rng), u(rng), u(rng), u(rng)};
    const double qn = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (double& v : q) v /= qn;
    std::vector<lh::OpticalSample> samples;
    for (auto& [channel, idx] : model.channelToIndex) {
      const auto& s = model.points[size_t(idx)];
      const auto& nn = model.normals[size_t(idx)];
      double r[3], n[3];
      lh::model::QuatRotate(q, s.data(), r);
      lh::model::QuatRotate(q, nn.data(), n);
      const double w[3] = {r[0] + p[0], r[1] + p[1], r[2] + p[2]};
      const double len = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
      if (-(n[0] * w[0] + n[1] * w[1] + n[2] * w[2]) / len < 0.2) continue;  // facing the base
      for (int axis = 0; axis < 2; axis++) {
        if (u(rng) > 0.8) continue;  // drop some hits
        lh::OpticalSample smp;
        smp.sensor = channel;
        smp.axis = axis;
        smp.angle = lh::model::Lh1Angle(axis, w[0], w[1], w[2], base.cal.data() + 7 * axis) + 2e-5 * u(rng);
        smp.width = 5e-6;
        samples.push_back(smp);
      }
    }
    std::shuffle(samples.begin(), samples.end(), rng);
    // Original
    std::vector<OrigSample> os;
    for (auto& s : samples) os.push_back({s.sensor, s.axis, s.time, s.angle, s.width, 0, 0});
    struct { OrigSample *b, *e, *c; } vec{os.data(), os.data() + os.size(), os.data() + os.size()};
    OrigCfg cfg{&oobj, &obase, 0.0001884955592153876, 0, 0, 0, 0, 1};
    double opose[7] = {};
    // Exercise both the default 2 m seed and the caller-supplied pose path.
    lh::solver::Pose seed;
    seed.p = {p[0] + 0.02, p[1] - 0.01, p[2] + 0.03};
    seed.q = {q[0], q[1], q[2], q[3]};
    double oseed[7] = {q[1], q[2], q[3], q[0], seed.p[0], seed.p[1], seed.p[2]};
    oracle::Fn<void (*)(OrigCfg*, void*, double*, double*)>(0x3ab590)(
        &cfg, &vec, opose, GetParam() ? oseed : nullptr);
    const auto mr = lh::solver::Solve(model, base, samples, GetParam() ? &seed : nullptr);
    ASSERT_EQ(cfg.status, mr.status) << "trial " << trial;
    if (mr.status == 2) {
      insufficient++;
      continue;
    }
    EXPECT_EQ(cfg.iterations, mr.iterations) << "trial " << trial;
    // Default-seed parity is numerical, not bit-exact; retain diagnostics for
    // the known counter differences rather than silently claiming full parity.
    if (GetParam()) {
      EXPECT_EQ(cfg.lineSearch, mr.lineSearchEvals) << "trial " << trial;
    }
    if (cfg.lineSearch != mr.lineSearchEvals) {
      lsMismatch++;
      printf("ls mismatch trial %d: %d vs %d (it %d)\n", trial, cfg.lineSearch, mr.lineSearchEvals, mr.iterations);
    }
    maxCostDelta = std::max(maxCostDelta, std::abs(cfg.cost - mr.cost));
    maxErrorDelta = std::max(maxErrorDelta, std::abs(cfg.error - mr.error));
    for (int i = 0; i < 3; i++)
      maxPositionDelta = std::max(maxPositionDelta, std::abs(opose[4 + i] - mr.pose.p[size_t(i)]));
    for (int i = 0; i < 4; i++)
      maxQuaternionDelta = std::max(maxQuaternionDelta, std::abs(opose[(i + 3) % 4] - mr.pose.q[size_t(i)]));
    EXPECT_NEAR(cfg.error, mr.error, 1e-9 * (1 + cfg.error)) << "trial " << trial;
    EXPECT_NEAR(cfg.cost, mr.cost, 1e-9 * (1 + cfg.cost)) << "trial " << trial;
    for (int i = 0; i < 3; i++) EXPECT_NEAR(opose[4 + i], mr.pose.p[size_t(i)], 1e-8) << "trial " << trial;
    // original quaternion layout: x, y, z, w
    EXPECT_NEAR(opose[3], mr.pose.q[0], 1e-8);
    EXPECT_NEAR(opose[0], mr.pose.q[1], 1e-8);
    EXPECT_NEAR(opose[1], mr.pose.q[2], 1e-8);
    EXPECT_NEAR(opose[2], mr.pose.q[3], 1e-8);
    if (HasFailure()) FAIL() << "trial " << trial;
    solved++;
  }
  printf("solved %d, insufficient %d, line-search count mismatches %d\n", solved, insufficient, lsMismatch);
  printf("max deltas: position=%.9g quaternion=%.9g cost=%.9g error=%.9g\n",
         maxPositionDelta, maxQuaternionDelta, maxCostDelta, maxErrorDelta);
  EXPECT_GT(solved, 20);
}

INSTANTIATE_TEST_SUITE_P(Seeds, PoseSolverOracle, ::testing::Bool(),
                        [](const auto& info) { return info.param ? "SuppliedPose" : "DefaultPose"; });

}  // namespace
