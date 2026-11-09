// Synchronous, transport-independent LH1 tracking session.
#pragma once

#include <memory>
#include <map>
#include <vector>
#include "lh/input_sink.hpp"
#include "lh/optical.hpp"

namespace lh {

struct RigidPose {
  std::array<double, 3> position{};
  std::array<double, 4> orientation{1, 0, 0, 0};  // w,x,y,z
};

enum class TrackingStatus { Uninitialized, Optical, Inertial, Lost };

struct TrackedPose {
  DeviceId device;
  DeviceTime time = 0;
  double hostTime = 0;
  uint32_t primaryBase = 0;
  bool worldSpace = false;
  // Frame is the primary base unless a world-from-base transform was supplied.
  // Object origin/orientation is the IMU, NOT SteamVR's head/grip pose.
  RigidPose pose;
  std::array<double, 3> velocity{}, angularVelocity{};
  TrackingStatus status = TrackingStatus::Uninitialized;
  uint64_t opticalUpdates = 0;  // original filter counter, seeded at 101 by bootstrap
  uint32_t flags = 0;
};

struct DeviceTrackingInfo {
  DeviceId device;
  bool attached = false, configured = false;
  TrackingStatus status = TrackingStatus::Uninitialized;
  uint32_t primaryBase = 0;
  uint64_t opticalUpdates = 0, bootstrapAttempts = 0, rejectedUpdates = 0;
  std::string error;
};

struct TrackerOptions {
  // Recovery policy, not original-driver parity: restart TDM after a long input
  // gap and declare stale poses invalid after this many seconds without light.
  double inputGapReset = 0.5;
  double opticalTimeout = 0.5;
  // Explicit diode/sensor hardware type when automatic model recognition is
  // insufficient. Types 0..11 use the recovered pulse-width offset table.
  std::map<DeviceId, int> sensorTypes;
};

class Tracker : public InputSinkBase {
 public:
  explicit Tracker(const TrackerOptions& options = {});
  ~Tracker() override;
  Tracker(const Tracker&) = delete;
  Tracker& operator=(const Tracker&) = delete;

  void OnDeviceAttached(const DeviceId&, const std::string& configJson) override;
  void OnDeviceDetached(const DeviceId&) override;
  void OnClockOffset(const DeviceId&, double offset) override;
  void OnImu(const DeviceId&, const ImuSample&) override;
  void OnLightPulse(const DeviceId&, const LightPulse&) override;
  void OnLh2BeamWord(const DeviceId&, const Lh2BeamWord&) override;
  void OnLh2SensorWord(const DeviceId&, const Lh2SensorWord&) override;

  // Optional external base calibration/placement (network/replay/DB adapters).
  void SetBaseConfig(const BaseConfig&, const BaseDynamicState& dynamic = {});
  void SetBasePose(uint32_t serial, const RigidPose& worldFromBase);
  // Also imports matching known-object IMU calibration and recorded base tilt.
  // Learned IMU calibration applies before bootstrap/on reset, not mid-track.
  bool LoadBasePoses(const std::string& databaseJson, std::string* error = nullptr);

  // Thread-safe. No callback is invoked under the session lock.
  std::vector<TrackedPose> PollPoses();
  std::vector<DeviceTrackingInfo> Devices() const;
  std::vector<std::string> PollLogs();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace lh
