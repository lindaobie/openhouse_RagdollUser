// Oracle test: lh::direct::DirectFilter (process noise, predict, update) vs the
// original functions run on a fake CLighthouseDirect object, over 50 steps that
// alternate IMU-style and optical-style updates.
#include <ceres/jet.h>
#include <gtest/gtest.h>

#include <cstring>
#include <random>
#include <mutex>

#include "oracle.hpp"
#include "tracking/direct_filter.hpp"
#include "tracking/lighthouse_direct.hpp"
#include "tracking/lighthouse_model.hpp"

namespace {

using lh::direct::Mat;
using lh::direct::Vec;

struct EigenMat { double* data; long rows, cols; };
struct Ref { double* data; long rows, cols, outer, pad; void* tmp; long z[2]; };
struct Fn { void** vt; void* data; };

class FakeDirect {
 public:
  uint8_t* o;
  FakeDirect() { o = static_cast<uint8_t*>(calloc(1, 0x4000)); }
  ~FakeDirect() {
    for (int offset : {0x5d8, 0x938, 0x950, 0x968, 0x980, 0x998}) free(At<void*>(offset));
    free(o);
  }
  FakeDirect(const FakeDirect&) = delete;
  FakeDirect& operator=(const FakeDirect&) = delete;
  double* X() { return reinterpret_cast<double*>(o + 0x448); }
  double* XLin() { return reinterpret_cast<double*>(o + 0x7b0); }
  template <typename T> T& At(int off) { return *reinterpret_cast<T*>(o + off); }
  void SetMat(int off, const Mat& m) {
    EigenMat em{static_cast<double*>(malloc(sizeof(double) * size_t(m.size()))), m.rows(), m.cols()};
    std::memcpy(em.data, m.data(), sizeof(double) * size_t(m.size()));
    std::memcpy(o + off, &em, sizeof em);
  }
  Mat GetMat(int off) {
    EigenMat em;
    std::memcpy(&em, o + off, sizeof em);
    return Eigen::Map<Mat>(em.data, em.rows, em.cols);
  }
};

struct FakeObject { char pad[0x48]; const double* points; char pad2[0x10]; const double* normals; };
struct CalHolder { void* pad; const double* begin; const double* end; };
struct SensorData { double dt; FakeObject* object; int32_t sensor; int32_t model; CalHolder* cal; };
struct ImuData { int32_t comp, pad; void* o; };

TEST(DirectFilterOracle, DefaultStateAndInitialCovariance) {
  FakeDirect orig;
  lh::direct::DirectFilter mine;
  oracle::Fn<void (*)(double*)>(0x3511a0)(orig.X());
  mine.DefaultState();
  for (int i = 0; i < 49; ++i) EXPECT_EQ(orig.X()[i], mine.x[i]) << i;
  // Off-diagonals must survive initialization unchanged.
  mine.P = Mat::Constant(49, 49, 0.0123);
  orig.SetMat(0x5d8, mine.P);
  oracle::Fn<void (*)(void*)>(0x350280)(orig.o);
  mine.InitialCovariance();
  EXPECT_EQ((orig.GetMat(0x5d8) - mine.P).cwiseAbs().maxCoeff(), 0);
}

TEST(DirectFilterOracle, OrchestratedImuUpdate) {
  lh::TrackedObjectModel object;
  object.points = {{{-0.1, 0, 0}}, {{0.1, 0, 0}}, {{0, 0.1, 0}}, {{0, 0, 0.1}}};
  object.normals.resize(4, {0, 0, 1});
  for (int i = 0; i < 4; ++i) object.channelToIndex[i] = i;
  lh::LighthouseDirect tracker(object);
  lh::BaseConfig base;
  base.valid = true; base.serial = 123; base.cal.resize(14);
  tracker.OnBaseConfig(base, {});
  for (int step = 0; step < 8; ++step) {
    lh::OpticalFrame frame;
    frame.baseSerial = 123; frame.axis = step % 2;
    frame.syncTime = 1 + step / 120.0; frame.nextSyncTime = frame.syncTime + 1.0 / 120;
    frame.idealPeriod = frame.averagePeriod = 1.0 / 60;
    frame.rightSyncIsReal = true;
    for (int sensor = 0; sensor < 4; ++sensor) {
      const auto& p = object.points[size_t(sensor)];
      lh::OpticalSample s;
      s.sensor = sensor; s.axis = frame.axis; s.time = frame.syncTime + 0.004; s.width = 5e-6;
      s.angle = lh::model::Lh1Angle(frame.axis, p[0], p[1], p[2] - 2, base.cal.data());
      frame.samples.push_back(s);
    }
    tracker.OnFrame(frame);
  }
  ASSERT_TRUE(tracker.Pose());
  for (uint32_t saturated : {0u, 1u, 8u, 0x3fu}) {
    FakeDirect orig;
    const auto& filter = tracker.Filter();
    std::memcpy(orig.X(), filter.x, 49 * sizeof(double));
    orig.At<int>(0x5d0) = filter.n;
    orig.At<uint64_t>(0x7a0) = filter.updateCount;
    orig.At<double>(0x408) = filter.omegaCorrectionLimit2;
    orig.At<double>(0x5f0) = tracker.Pose()->time;
    orig.SetMat(0x5d8, filter.P);
    orig.SetMat(0x938, Mat::Zero(49, 49));
    orig.SetMat(0x968, Mat::Zero(49, 49));
    Mat R = Mat::Zero(7, 7);
    R.diagonal() << 1.6e-5, 1.6e-5, 1.6e-5, 0.0015, 0.0015, 0.003, 1e-8;
    orig.SetMat(0x998, R);
    for (int i = 0; i < 7; ++i) orig.At<double>(0x410 + 8 * i) = R(i, i);
    struct Imu { double time; float accel[3], gyro[3]; uint32_t saturated, pad; } sample{};
    static_assert(sizeof(Imu) == 40);
    sample.time = tracker.Pose()->time + 0.06; sample.saturated = saturated;
    double accel[3], gyro[3];
    lh::direct::ImuAccel(filter.x, accel); lh::direct::ImuGyro(filter.x, gyro);
    lh::ImuSample input;
    input.time = sample.time; input.saturated = saturated;
    for (int i = 0; i < 3; ++i) {
      input.accel[size_t(i)] = sample.accel[i] = float(accel[i] + 0.001);
      input.gyro[size_t(i)] = sample.gyro[i] = float(gyro[i] + 0.0001);
    }
    ASSERT_TRUE((oracle::Fn<bool (*)(void*, void*)>(0x36ed70)(orig.o, &sample)));
    tracker.OnImu(input);
    ASSERT_TRUE(tracker.AdvanceInertial(input.time + 0.000001));
    EXPECT_EQ(tracker.Pose()->time, orig.At<double>(0x5f0));
    for (int i = 0; i < 49; ++i) EXPECT_NEAR(orig.X()[i], tracker.Filter().x[i], 1e-11) << i;
    EXPECT_LT((orig.GetMat(0x5d8) - tracker.Filter().P).cwiseAbs().maxCoeff(), 1e-11);
  }
}

TEST(DirectFilterOracle, OrchestratedOpticalUpdates) {
  lh::TrackedObjectModel object;
  object.points = {{{-0.1, 0, 0}}, {{0.1, 0, 0}}, {{0, 0.1, 0}}, {{0, 0, 0.1}},
                   {{0, -0.1, -0.05}}, {{0.05, 0.02, 0.02}}};
  object.normals.resize(6, {0, 0, 1});
  object.normals[0] = {std::sqrt(1 - 0.4 * 0.4), 0, 0.4};  // grazing noise
  object.normals[1] = {1, 0, 0};  // faces away; must be rejected
  for (int i = 0; i < 6; ++i) object.channelToIndex[i] = i;
  lh::LighthouseDirect tracker(object);
  lh::BaseConfig base;
  base.valid = true; base.serial = 123; base.cal.resize(14); base.hardwareVersion = 9;
  tracker.OnBaseConfig(base, {});
  for (int step = 0; step < 8; ++step) {
    lh::OpticalFrame frame;
    frame.baseSerial = 123; frame.axis = step % 2;
    frame.syncTime = 1 + step / 120.0; frame.nextSyncTime = frame.syncTime + 1.0 / 120;
    frame.idealPeriod = frame.averagePeriod = 1.0 / 60; frame.rightSyncIsReal = true;
    for (int sensor = 0; sensor < 4; ++sensor) {
      const auto& p = object.points[size_t(sensor)];
      lh::OpticalSample s;
      s.sensor = sensor; s.axis = frame.axis; s.time = frame.syncTime + 0.004; s.width = 5e-6;
      s.angle = lh::model::Lh1Angle(frame.axis, p[0], p[1], p[2] - 2, base.cal.data());
      frame.samples.push_back(s);
    }
    tracker.OnFrame(frame);
  }
  ASSERT_TRUE(tracker.Pose());
  struct OriginalObject {
    std::map<int, int> channels;
    char pad[0x48 - sizeof(std::map<int, int>)]{};
    std::vector<std::array<double, 3>> points, normals;
    uint64_t unknown = 0;
    double diameter = 0, radius = 0;
  } obj;
  ASSERT_EQ(reinterpret_cast<char*>(&obj.points) - reinterpret_cast<char*>(&obj), 0x48);
  ASSERT_EQ(reinterpret_cast<char*>(&obj.diameter) - reinterpret_cast<char*>(&obj), 0x80);
  obj.channels = object.channelToIndex; obj.points = object.points; obj.normals = object.normals;
  for (const auto& a : obj.points) {
    obj.radius = std::max(obj.radius, Eigen::Vector3d(a.data()).norm());
    for (const auto& b : obj.points)
      obj.diameter = std::max(obj.diameter, (Eigen::Vector3d(a.data()) - Eigen::Vector3d(b.data())).norm());
  }
  // Publish diagnostics call source clock accessors and a recording callback.
  // Stub those observers; do not bypass the actual optical update function.
  static std::mutex mutex;
  struct Clock { void* mutex; double pad[2]; double offset; };
  static Clock clock{mutex.native_handle(), {}, 0};
  void* sourceVtable[20]{};
  sourceVtable[18] = reinterpret_cast<void*>(+[](void*) -> void* { return &clock; });
  sourceVtable[19] = reinterpret_cast<void*>(+[](void*) -> double { return 0; });
  struct Stub { void** vtable; } source{sourceVtable};
  void* recorderVtable[2]{};
  recorderVtable[1] = reinterpret_cast<void*>(+[](void*, void*) {});
  Stub recorder{recorderVtable};
  for (int hits : {1, 2, 4, 6}) {
  FakeDirect orig;
  const auto& filter = tracker.Filter();
  std::memcpy(orig.X(), filter.x, 49 * sizeof(double));
  orig.At<void*>(0x38) = &source; orig.At<void*>(0x238) = &recorder;
  orig.At<void*>(0x30) = &obj; orig.At<uint32_t>(0x1b8) = 123;
  orig.At<int>(0x5d0) = filter.n; orig.At<uint64_t>(0x7a0) = filter.updateCount;
  orig.At<double>(0x408) = filter.omegaCorrectionLimit2;
  orig.At<double>(0x5f0) = tracker.Pose()->time;
  orig.SetMat(0x5d8, filter.P);
  orig.SetMat(0x938, Mat::Zero(49, 49)); orig.SetMat(0x968, Mat::Zero(49, 49));
  Mat R = Mat::Zero(33, 33);
  R.topLeftCorner(32, 32).setConstant(1.579136704174297e-10); R(32, 32) = 1e-8;
  orig.SetMat(0x980, R);
  orig.At<double>(0x3e0) = 1.579136704174297e-10;
  orig.At<double>(0x3e8) = 2.2206609902451057e-9;
  orig.At<double>(0x3f0) = 7e-5; orig.At<double>(0x3f8) = 3e-5;
  orig.At<double>(0x400) = 3.947841760435744e-9;
  lh::OpticalFrame frame;
  frame.baseSerial = 123; frame.rightSyncIsReal = true;
  for (int sensor = 0; sensor < hits; ++sensor) {
    lh::OpticalSample sample;
    sample.sensor = sensor; sample.axis = 0;
    sample.time = tracker.Pose()->time + 0.002 + sensor * 0.00001; sample.width = 5e-6;
    sample.angle = lh::direct::SensorAngular(filter.x, 0, object.points[size_t(sensor)].data(), 0, base.cal.data(), -1) + 2e-5;
    if (sensor == 5) sample.angle += 0.9;  // angular outlier
    frame.samples.push_back(sample);
  }
  ASSERT_TRUE((oracle::Fn<bool (*)(void*, void*, void*)>(0x36f740)(orig.o, &frame, &base)));
  ASSERT_TRUE(tracker.OnFrame(frame));
  for (int i = 0; i < 49; ++i) EXPECT_NEAR(orig.X()[i], tracker.Filter().x[i], 1e-11) << i;
  EXPECT_LT((orig.GetMat(0x5d8) - tracker.Filter().P).cwiseAbs().maxCoeff(), 1e-11);
  EXPECT_EQ(orig.At<uint64_t>(0x7a0), tracker.Filter().updateCount);
  EXPECT_EQ(orig.At<double>(0x5f0), tracker.Pose()->time);
  }
}

TEST(DirectFilterOracle, PredictUpdateSequence) {
  std::mt19937 rng(7);
  std::uniform_real_distribution<double> u(-1, 1);
  FakeDirect orig;
  lh::direct::DirectFilter mine;
  // Initial state: object 2 m in front of the base, small motion.
  double x0[49] = {};
  x0[0] = 0.2; x0[1] = -0.1; x0[2] = -2.0;
  x0[3] = 0.05; x0[4] = -0.02; x0[5] = 0.01;
  x0[9] = 0.95; x0[10] = 0.1; x0[11] = 0.2; x0[12] = -0.05;
  double qn = std::sqrt(0.9025 + 0.01 + 0.04 + 0.0025);
  for (int i = 9; i < 13; i++) x0[i] /= qn;
  x0[13] = 0.2; x0[14] = -0.1; x0[15] = 0.3;
  x0[18] = 9.80665;
  x0[25] = x0[26] = x0[27] = 1.0;
  std::memcpy(orig.X(), x0, sizeof x0);
  std::memcpy(mine.x, x0, sizeof x0);
  const int n = 31;
  orig.At<int>(0x5d0) = n;
  mine.n = n;
  orig.At<uint64_t>(0x7a0) = 1000;
  mine.updateCount = 1000;
  orig.At<double>(0x408) = 1e300;
  Mat A = Mat::Random(49, 49);
  Mat P0 = (A * A.transpose()) * 1e-4 + Mat::Identity(49, 49) * 1e-3;
  orig.SetMat(0x5d8, P0);
  mine.P = P0;
  orig.SetMat(0x938, Mat::Zero(49, 49));
  orig.SetMat(0x968, Mat::Zero(49, 49));

  double pts[3 * 8], nrm[3 * 8], cal[14];
  for (double& v : pts) v = 0.05 * u(rng);
  for (double& v : nrm) v = u(rng);
  for (double& c : cal) c = 0.005 * u(rng);
  FakeObject obj{};
  obj.points = pts;
  obj.normals = nrm;
  CalHolder ch{nullptr, cal, cal + 14};

  for (int step = 0; step < 50; step++) {
    const double dt = 0.001 + 0.004 * std::abs(u(rng));
    // ---- process noise + predict
    oracle::Fn<void (*)(double, void*, void*)>(0x355990)(dt, orig.o, orig.o + 0x448);
    mine.ComputeProcessNoise(dt);
    Mat Qo = orig.GetMat(0x968);
    ASSERT_LT((Qo - mine.Q).cwiseAbs().maxCoeff(), 1e-18) << "Q step " << step;
    struct { void** vt; double a, b; } ts{static_cast<void**>(oracle::At(0x472108)), dt, dt};
    EigenMat qm;
    std::memcpy(&qm, orig.o + 0x968, sizeof qm);
    Ref qref{qm.data, qm.rows, qm.cols, qm.cols, 0, nullptr, {0, 0}};
    oracle::Fn<void (*)(void*, void*, void*)>(0x366610)(orig.o, &ts, &qref);
    mine.Predict(dt, dt);
    for (int i = 0; i < 49; i++) ASSERT_NEAR(orig.XLin()[i], mine.xLin[i], 1e-12 * (1 + std::abs(mine.xLin[i]))) << "xLin " << i;
    Mat Po = orig.GetMat(0x950);
    ASSERT_LT((Po - mine.Ppred).cwiseAbs().maxCoeff(), 1e-12 * (1 + Po.cwiseAbs().maxCoeff())) << "Ppred step " << step;

    // ---- update: IMU style on even steps, optical (4 sensors, both axes) on odd steps
    std::vector<Fn> fns;
    std::vector<ImuData> imuData(1, ImuData{-1, 0, nullptr});
    std::vector<SensorData> sd;
    sd.reserve(8);
    std::vector<lh::direct::Residual> res;
    static char dummy;
    std::vector<double> z;
    if (step % 2 == 0) {
      fns.push_back({static_cast<void**>(oracle::At(0x472188)), &imuData[0]});
      fns.push_back({static_cast<void**>(oracle::At(0x4721c8)), &imuData[0]});
      res.push_back({3, [](const double* x, double* r) { lh::direct::ImuGyro(x, r); },
                     [](const lh::direct::Jet* x, lh::direct::Jet* r) { lh::direct::ImuGyro(x, r); }});
      res.push_back({3, [](const double* x, double* r) { lh::direct::ImuAccel(x, r); },
                     [](const lh::direct::Jet* x, lh::direct::Jet* r) { lh::direct::ImuAccel(x, r); }});
      double h[6];
      lh::direct::ImuGyro(mine.xLin, h);
      lh::direct::ImuAccel(mine.xLin, h + 3);
      for (int i = 0; i < 6; i++) z.push_back(h[i] + 0.002 * u(rng));
    } else {
      for (int s = 0; s < 4; s++) {
        for (int axis = 0; axis < 2; axis++) {
          const double sdt = 0.002 * u(rng);
          sd.push_back(SensorData{sdt, &obj, s, 1, &ch});
          fns.push_back({static_cast<void**>(oracle::At(axis == 0 ? 0x472208 : 0x472248)), &sd.back()});
          const double* sp = pts + 3 * s;
          res.push_back({1, [=](const double* x, double* r) { r[0] = lh::direct::SensorAngular(x, sdt, sp, axis, cal, -1); },
                         [=](const lh::direct::Jet* x, lh::direct::Jet* r) {
                           r[0] = lh::direct::SensorAngular(x, sdt, sp, axis, cal, -1);
                         }});
          z.push_back(lh::direct::SensorAngular(mine.xLin, sdt, sp, axis, cal, -1) + 1e-4 * u(rng));
        }
      }
    }
    fns.push_back({static_cast<void**>(oracle::At(0x472148)), &dummy});
    res.push_back({1, [](const double* x, double* r) { r[0] = lh::direct::QuaternionNormalizer(x); },
                   [](const lh::direct::Jet* x, lh::direct::Jet* r) { r[0] = lh::direct::QuaternionNormalizer(x); }});
    z.push_back(0.0);
    const int m = int(z.size());
    Mat R = Mat::Identity(m, m) * (step % 2 == 0 ? 1e-4 : 1e-6);
    R(m - 1, m - 1) = 1e-6;
    std::vector<void*> list;
    for (auto& f : fns) list.push_back(&f);
    struct { void** vt; void** b; void** e; void** c; } fs{static_cast<void**>(oracle::At(0x4720c0)), list.data(),
                                                         list.data() + list.size(), list.data() + list.size()};
    Ref rref{R.data(), m, m, m, 0, nullptr, {0, 0}};
    struct { double* d; long n; } zref{z.data(), m};
    const bool okO = oracle::Fn<bool (*)(void*, int, void*, void*, void*)>(0x36cae0)(orig.o, step % 2 == 0 ? 0 : 1, &fs, &rref, &zref);
    const bool okM = mine.Update(res, R, Eigen::Map<Vec>(z.data(), m));
    ASSERT_EQ(okO, okM) << "step " << step;
    for (int i = 0; i < 49; i++) ASSERT_NEAR(orig.X()[i], mine.x[i], 1e-10 * (1 + std::abs(mine.x[i]))) << "x " << i << " step " << step;
    Mat Pn = orig.GetMat(0x5d8);
    ASSERT_LT((Pn - mine.P).cwiseAbs().maxCoeff(), 1e-11 * (1 + Pn.cwiseAbs().maxCoeff())) << "P step " << step;
  }
}

}  // namespace
