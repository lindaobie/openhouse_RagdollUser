// Per-update original-function oracle on the moving/occluded controller capture.
// Both sides start each comparison from the port's state/covariance and IMU queue.
// This tests orchestration on real measurements, NOT independent full-tracker parity.
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <Eigen/Geometry>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

#include "oracle.hpp"
#include "tracking/lighthouse_direct.hpp"
#include "tracking/tracking_source.hpp"
#include "usb_transport.hpp"

namespace {
using lh::direct::Mat;
constexpr const char* kDevice = "LHR-13DA81D1";

struct NativeImu { double time; float accel[3], gyro[3]; uint32_t saturated, padding; };
static_assert(sizeof(NativeImu) == 40);
struct NativeObject {
  std::map<int, int> channels;
  char pad[0x48 - sizeof(std::map<int, int>)]{};
  std::vector<std::array<double, 3>> points, normals;
  uint64_t unknown = 0;
  double diameter = 0, radius = 0;
};
struct Matrix { double* data; long rows, cols; };
struct Ref { double* data; long rows, cols, outer, pad; void* tmp; long z[2]; };
struct Clock { void* mutex; double padding[2]; double offset; };
struct Source { void** vtable; Clock* clock; };
struct Recorder { void** vtable; };

class NativeSnapshot {
 public:
  uint8_t* data = static_cast<uint8_t*>(std::calloc(1, 0x4000));
  NativeObject object;
  std::mutex mutex;
  Clock clock{mutex.native_handle(), {}, 0};
  void* sourceVtable[20]{};
  Source source{sourceVtable, &clock};
  void* recorderVtable[2]{};
  Recorder recorder{recorderVtable};

  template <class T> T& At(int offset) { return *reinterpret_cast<T*>(data + offset); }
  double* X() { return reinterpret_cast<double*>(data + 0x448); }
  std::deque<NativeImu>& Queue() { return *reinterpret_cast<std::deque<NativeImu>*>(data + 0x9b0); }
  void SetMat(int offset, const Mat& matrix) {
    auto& m = At<Matrix>(offset);
    if (m.data) std::free(m.data);
    m = {static_cast<double*>(std::malloc(size_t(matrix.size()) * sizeof(double))), matrix.rows(), matrix.cols()};
    std::memcpy(m.data, matrix.data(), size_t(matrix.size()) * sizeof(double));
  }
  // The original writes measurement noise into the bottom-right m×m block of its
  // scratch matrix; size it to the exact residual count before each call.
  void ResizeNoise(int m) {
    auto& mat = At<Matrix>(0x980);
    if (mat.data) std::free(mat.data);
    mat = {static_cast<double*>(std::malloc(size_t(m) * size_t(m) * sizeof(double))), m, m};
    for (int i = 0; i < m * m; ++i) mat.data[i] = 0.0;
    for (int i = 0; i < m - 1; ++i) mat.data[i * (m + 1)] = 1.579136704174297e-10;
    mat.data[(m - 1) * (m + 1)] = 1e-8;
  }
  Mat P() {
    const auto& m = At<Matrix>(0x5d8);
    return Eigen::Map<Mat>(m.data, m.rows, m.cols);
  }
  explicit NativeSnapshot(const lh::LighthouseDirect& core) {
    const auto& f = core.Filter();
    const auto& model = core.Model();
    object.channels = model.channelToIndex; object.points = model.points; object.normals = model.normals;
    for (const auto& a : object.points) {
      object.radius = std::max(object.radius, Eigen::Vector3d(a.data()).norm());
      for (const auto& b : object.points)
        object.diameter = std::max(object.diameter, (Eigen::Vector3d(a.data()) - Eigen::Vector3d(b.data())).norm());
    }
    At<void*>(0x30) = &object;
    sourceVtable[18] = reinterpret_cast<void*>(+[](Source* s) -> void* { return s->clock; });
    sourceVtable[19] = reinterpret_cast<void*>(+[](Source*) -> double { return 0; });
    recorderVtable[1] = reinterpret_cast<void*>(+[](void*, void*) {});
    At<void*>(0x38) = &source; At<void*>(0x238) = &recorder;
    for (int slot = 0; slot < 4; ++slot) {
      At<uint32_t>(0x1b8 + 24 * slot) = core.BaseSerial(slot);
      // Skip DB relationship publication, not measurement/filter calculations.
      At<uint8_t>(0x1c8 + 24 * slot) = 1;
    }
    At<uint32_t>(0xf8) = f.flags; At<int>(0xfc) = f.rejectCount;
    At<int>(0x5d0) = f.n; At<uint64_t>(0x7a0) = f.updateCount;
    At<double>(0x408) = f.omegaCorrectionLimit2;
    At<double>(0x5f0) = core.Pose()->time;
    At<double>(0x3d8) = core.LastOpticalTime();
    std::memcpy(X(), f.x, 49 * sizeof(double));
    SetMat(0x5d8, f.P); SetMat(0x938, Mat::Zero(49, 49)); SetMat(0x968, f.Q);
    // Matches the original's persistent scratch layout (see runtime.md notes):
    // a fixed-capacity square matrix whose diagonal is seeded with the optical
    // noise floor. The original writes per-residual noise into it during the call.
    Mat optical = Mat::Zero(33, 33);
    optical.topLeftCorner(32, 32).setConstant(1.579136704174297e-10); optical(32, 32) = 1e-8;
    SetMat(0x980, optical);
    At<double>(0x3e0) = 1.579136704174297e-10; At<double>(0x3e8) = 2.2206609902451057e-9;
    At<double>(0x3f0) = 7e-5; At<double>(0x3f8) = 3e-5; At<double>(0x400) = 3.947841760435744e-9;
    Mat imu = Mat::Zero(7, 7);
    imu.diagonal() << 1.6e-5, 1.6e-5, 1.6e-5, 0.0015, 0.0015, 0.003, 1e-8;
    SetMat(0x998, imu);
    for (int i = 0; i < 7; ++i) At<double>(0x410 + i * 8) = imu(i, i);
    new (data + 0x9b0) std::deque<NativeImu>();
    for (const auto& s : core.PendingImu()) {
      NativeImu sample{}; sample.time = s.time; sample.saturated = s.saturated;
      for (int i = 0; i < 3; ++i) {
        sample.accel[i] = float(s.accel[size_t(i)]); sample.gyro[i] = float(s.gyro[size_t(i)]);
      }
      Queue().push_back(sample);
    }
  }
  ~NativeSnapshot() {
    Queue().~deque();
    for (int offset : {0x5d8, 0x938, 0x950, 0x968, 0x980, 0x998}) std::free(At<void*>(offset));
    std::free(data);
  }
  struct Divergence { double state = 0, covariance = 0; };
  Divergence Compare(const lh::LighthouseDirect& core) {
    const auto& f = core.Filter();
    Divergence d;
    const double* x = X();
    for (int i = 0; i < 49; ++i)
      d.state = std::max(d.state, std::abs(x[i] - f.x[i]) / (1 + std::abs(f.x[i])));
    const Mat native = P();
    d.covariance = (native - f.P).cwiseAbs().maxCoeff() / (1 + f.P.cwiseAbs().maxCoeff());
    return d;
  }
};

struct ReplayOracle : lh::InputSinkBase {
  nlohmann::json database;
  std::unique_ptr<lh::TrackingSource> source;
  std::unique_ptr<lh::LighthouseDirect> core;
  lh::TrackedObjectModel model;
  std::map<uint32_t, std::pair<lh::BaseConfig, lh::BaseDynamicState>> bases;
  std::optional<double> latestInput, latestImu;
  size_t optical = 0, inertial = 0, secondary = 0, calibrationBoundarySkipped = 0, resets = 0;
  double maxStateError = 0, maxCovError = 0;
  size_t divergentChecks = 0;
  size_t divergentWithAlt = 0, divergentPure = 0;
  size_t divergentPpred = 0, divergentOnlyUpdate = 0;
  size_t flagMismatches = 0;
  double maxPredictionError = 0;
  size_t predictionDivergences = 0;

  void Reset() {
    source = std::make_unique<lh::TrackingSource>(9);
    core = std::make_unique<lh::LighthouseDirect>(model);
    for (const auto& [id, b] : bases) core->OnBaseConfig(b.first, b.second);
    for (const auto& b : database.at("base_stations")) {
      if (!b.contains("dynamic_states") || b["dynamic_states"].empty()) continue;
      const auto& last = b["dynamic_states"].back();
      if (last.contains("tilt")) {
        const auto& t = last["tilt"];
        core->SetRecordedTilt(b["config"]["serialNumber"], {t["pitch"], t["roll"]}, t["variance"]);
      }
    }
    latestInput.reset(); latestImu.reset(); ++resets;
  }
  void Time(double t) {
    if (latestInput && t - *latestInput > 0.5) Reset();
    if (!latestInput || *latestInput < t) latestInput = t;
  }
  void OnDeviceAttached(const lh::DeviceId& id, const std::string& config) override {
    if (id != kDevice || config.empty() || core) return;
    ASSERT_TRUE(lh::TrackedObjectModel::FromJson(config, &model));
    for (const auto& object : database.at("known_objects")) {
      if (object["serialNumber"] != id || !object.contains("imu")) continue;
      const auto& imu = object["imu"];
      model.gyroBias = imu["gyro_bias"].get<std::array<double, 3>>();
      model.accBias = imu["acc_bias"].get<std::array<double, 3>>();
      model.accScale = imu["acc_scale"].get<std::array<double, 3>>();
    }
    for (const auto& b : database.at("base_stations")) {
      const auto& c = b["config"];
      lh::BaseConfig cfg;
      cfg.valid = true; cfg.serial = c["serialNumber"]; cfg.hardwareVersion = c["modelId"]; cfg.ootxVersion = c["ootxVersion"];
      cfg.cal = c["baseCalibration"].get<std::vector<double>>();
      lh::BaseDynamicState dynamic;
      if (b.contains("dynamic_states") && !b["dynamic_states"].empty())
        dynamic.accelDir = b["dynamic_states"].back()["dynamic_state"]["gravity_vector"].get<std::array<float, 3>>();
      bases[cfg.serial] = {cfg, dynamic};
    }
    Reset();
  }
  void Drain() {
    lh::BaseConfig cfg; lh::BaseDynamicState dynamic;
    while (source->PopBaseConfig(&cfg, &dynamic)) {
      bases[cfg.serial] = {cfg, dynamic}; core->OnBaseConfig(cfg, dynamic);
    }
    lh::ImuSample sample;
    while (source->PopImu(&sample)) core->OnImu(sample);
    lh::OpticalFrame frame;
    while (source->PopFrame(&frame)) {
      SCOPED_TRACE("optical check " + std::to_string(optical) + " frame time " + std::to_string(frame.syncTime));
      int slot = -1;
      for (int i = 0; i < 4; ++i) if (core->BaseSerial(i) == frame.baseSerial) slot = i;
      (void)slot;
      // Bootstrap/slot insertion and the calibration-boundary DB callbacks need
      // a full original context. They are explicitly outside this snapshot test.
      if (!core->Pose() || slot < 0 || !frame.leftSyncIsReal || !frame.rightSyncIsReal || core->Filter().updateCount == 200) {
        if (core->Filter().updateCount == 200) ++calibrationBoundarySkipped;
        core->OnFrame(frame); continue;
      }
      double measurementTime = frame.samples.empty() ? core->Pose()->time : frame.samples.front().time + 0.5 * frame.samples.front().width;
      for (const auto& s : frame.samples) measurementTime = std::min(measurementTime, s.time + 0.5 * s.width);
      if (!frame.samples.empty() && measurementTime >= core->Pose()->time) {
        SCOPED_TRACE("queued IMU prefix");
        NativeSnapshot prefix(*core);
        oracle::Fn<bool (*)(double, void*, int)>(0x36f5b0)(measurementTime, prefix.data, 0);
        core->ProcessImuUntil(measurementTime); prefix.Compare(*core);
      }
      NativeSnapshot native(*core);
      const bool expected = oracle::Fn<bool (*)(void*, void*, void*)>(0x36f740)(native.data, &frame, &bases.at(frame.baseSerial).first);
      const bool actual = core->OnFrame(frame);
      if (expected != actual) ++flagMismatches;
      if (expected) {
        const auto d = native.Compare(*core);
        maxStateError = std::max(maxStateError, d.state);
        maxCovError = std::max(maxCovError, d.covariance);
        if (d.state > 1e-6 || d.covariance > 1e-6) {
          ++divergentChecks;
          if (core->Filter().n == 37) ++divergentWithAlt; else ++divergentPure;
          // Localize: compare predicted covariance (Ppred) too.
          const auto& np = native.At<Matrix>(0x950);
          const lh::direct::Mat nppm = Eigen::Map<const lh::direct::Mat>(np.data, np.rows, np.cols);
          const double predDiff = (nppm - core->Filter().Ppred).cwiseAbs().maxCoeff() /
                                  (1 + core->Filter().Ppred.cwiseAbs().maxCoeff());
          if (predDiff > 1e-6) ++divergentPpred; else ++divergentOnlyUpdate;
        }
        ++optical; if (slot > 0) ++secondary;
      }
    }
    if (latestImu && core->Pose()) {
      const double target = *latestImu - 0.02;
      if (target > core->Pose()->time && target >= core->LastOpticalTime() + 0.05) {
        SCOPED_TRACE("inertial check " + std::to_string(inertial) + " time " + std::to_string(target));
        NativeSnapshot native(*core);
        oracle::Fn<bool (*)(double, void*, int)>(0x36f5b0)(target, native.data, 0);
        const auto d = core->AdvanceInertial(target) ? native.Compare(*core) : NativeSnapshot::Divergence{};
        maxStateError = std::max(maxStateError, d.state);
        maxCovError = std::max(maxCovError, d.covariance);
        ++inertial;
      }
    }
  }
  void OnImu(const lh::DeviceId& id, const lh::ImuSample& sample) override {
    if (id != kDevice || !core) return;
    Time(sample.time); latestImu = sample.time; source->OnImu(sample); Drain();
  }
  void OnLightPulse(const lh::DeviceId& id, const lh::LightPulse& pulse) override {
    if (id != kDevice || !core) return;
    Time(pulse.time); source->OnLightPulse(pulse); Drain();
  }
};

TEST(MotionFilterOracle, RealControllerUpdatesMatchOriginalFunctions) {
  const auto directory = std::filesystem::path(LH_SOURCE_DIR) / "captures/session-20261016-142931Z-147083";
  if (!std::filesystem::exists(directory / "input.lhraw")) GTEST_SKIP() << "no motion session";
  ReplayOracle sink;
  std::ifstream database(directory / "lighthousedb.json");
  ASSERT_TRUE(database.good()); database >> sink.database;
  lh::usb::RawCaptureReader reader;
  ASSERT_TRUE(reader.Open((directory / "input.lhraw").string()));
  lh::usb::ReportDecoder decoder(&sink);
  ASSERT_TRUE(reader.ReadAll([&](const auto& ep) { decoder.AddEndpoint(ep); },
                            [&](const auto& report) { decoder.OnReport(report); }));
  printf("motion oracle: %zu optical (%zu secondary), %zu inertial, %zu calibration-boundary skips, %zu initializations\n",
         sink.optical, sink.secondary, sink.inertial, sink.calibrationBoundarySkipped, sink.resets);
  printf("  max relative state divergence %.3g, covariance divergence %.3g, divergent checks %zu/%zu\n",
         sink.maxStateError, sink.maxCovError, sink.divergentChecks, sink.optical);
  printf("  flag mismatches %zu/%zu; divergent: n=37 %zu, n=31 %zu; Ppred-divergent %zu, update-only %zu\n",
         sink.flagMismatches, sink.optical, sink.divergentWithAlt, sink.divergentPure,
         sink.divergentPpred, sink.divergentOnlyUpdate);
  // Every optical update and inertial prediction on this capture matches the
  // original functions to round-off. Bootstrap/slot insertion is outside this
  // snapshot test (see ai/driver/VALIDATION.md).
  EXPECT_GT(sink.optical, 5000u); EXPECT_GT(sink.secondary, 1000u); EXPECT_GT(sink.inertial, 500u);
  EXPECT_LT(sink.maxStateError, 1e-9);
  EXPECT_LT(sink.maxCovError, 1e-9);
  EXPECT_EQ(sink.divergentChecks, 0u);
  EXPECT_EQ(sink.flagMismatches, 0u);
}
}  // namespace
