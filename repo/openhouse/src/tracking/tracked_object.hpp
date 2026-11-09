// Tracked object model: sensor positions/normals and the mapping from hardware
// sensor channel to model point, from the device JSON config (config.json:
// lighthouse_config.{modelPoints, modelNormals, channelMap}) plus the IMU block.
// Mirrors the object the driver keeps at CLighthouseDirect+0x30 (channel map
// std::map<int,int> at +0, points at +0x48, normals at +0x60).
#pragma once

#include <array>
#include <map>
#include <string>
#include <vector>

namespace lh {

struct TrackedObjectModel {
  std::map<int, int> channelToIndex;           // hardware sensor id -> model point
  std::vector<std::array<double, 3>> points;   // object frame
  std::vector<std::array<double, 3>> normals;
  // IMU calibration / pose from the config "imu" block.
  std::array<double, 3> accBias{}, accScale{1, 1, 1}, gyroBias{}, gyroScale{1, 1, 1};
  std::array<double, 3> imuPlusX{1, 0, 0}, imuPlusZ{0, 0, 1}, imuPosition{};
  std::array<double, 4> objectFromImuRotation{1, 0, 0, 0};  // w,x,y,z
  std::string serial, deviceClass;
  int sensorType = 0;
  bool sensorTypeKnown = false;  // from "sensor_type" (device hardware id), not a guess

  // Convert optical geometry to the IMU origin and axes used by the filter.
  TrackedObjectModel InImuFrame() const;

  // Returns the model point index for a sensor channel, or -1.
  int IndexOf(int channel) const {
    auto it = channelToIndex.find(channel);
    return it == channelToIndex.end() ? -1 : it->second;
  }

  static bool FromJson(const std::string& json, TrackedObjectModel* out, std::string* error = nullptr);
};

}  // namespace lh
