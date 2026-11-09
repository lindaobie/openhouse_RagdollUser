#include "tracked_object.hpp"

#include <nlohmann/json.hpp>
#include <Eigen/Geometry>
#include <cmath>
#include <algorithm>

namespace lh {
namespace {
bool Vec3(const nlohmann::json& j, std::array<double, 3>* out) {
  if (!j.is_array() || j.size() != 3) return false;
  for (int i = 0; i < 3; i++) {
    (*out)[size_t(i)] = j[size_t(i)].get<double>();
    if (!std::isfinite((*out)[size_t(i)])) return false;
  }
  return true;
}
}  // namespace

bool TrackedObjectModel::FromJson(const std::string& text, TrackedObjectModel* out, std::string* error) {
  if (!out) { if (error) *error = "null tracked-object output"; return false; }
  try {
    const auto j = nlohmann::json::parse(text);
    TrackedObjectModel m;
    if (j.contains("device_serial_number")) m.serial = j["device_serial_number"].get<std::string>();
    m.deviceClass = j.value("device_class", std::string{});
    // Sensor type drives the TDM sync-width offset (0x3e2e20). SteamVR reports it
    // per device ("SwSyncDetect ... SensorType %d"); the device JSON carries the
    // model under model_number (Knuckles) or model_name (Vive Tracker / wand).
    if (j.contains("sensor_type")) {
      m.sensorType = j.at("sensor_type").get<int>();
      m.sensorTypeKnown = true;
    } else {
      const std::string model = j.value("model_number", j.value("model_name", std::string{}));
      m.sensorType = model.find("Knuckles") != std::string::npos ? 9 : 0;
    }
    const auto& lc = j.at("lighthouse_config");
    for (const auto& p : lc.at("modelPoints")) {
      std::array<double, 3> v{};
      if (!Vec3(p, &v)) throw std::runtime_error("bad modelPoints entry");
      m.points.push_back(v);
    }
    for (const auto& p : lc.at("modelNormals")) {
      std::array<double, 3> v{};
      if (!Vec3(p, &v)) throw std::runtime_error("bad modelNormals entry");
      m.normals.push_back(v);
    }
    if (m.points.empty() || m.points.size() != m.normals.size() || m.points.size() > 32)
      throw std::runtime_error("optical model needs 1..32 matching points and normals");
    if (lc.contains("channelMap")) {
      const auto& cm = lc["channelMap"];
      if (cm.size() != m.points.size()) throw std::runtime_error("channelMap size mismatch");
      for (size_t i = 0; i < cm.size(); i++) {
        const int channel = cm[i].get<int>();
        if (channel < 0) continue;
        if (channel >= 32 || !m.channelToIndex.emplace(channel, int(i)).second)
          throw std::runtime_error("invalid or duplicate optical channel");
      }
    } else {
      for (size_t i = 0; i < m.points.size(); i++) m.channelToIndex[int(i)] = int(i);
    }
    const auto& imu = j.contains("imu") ? j["imu"] : j;
    const auto read = [&](const char* key, std::array<double, 3>* value) {
      if (imu.contains(key) && !Vec3(imu[key], value))
        throw std::runtime_error(std::string("bad IMU field: ") + key);
    };
    read("acc_bias", &m.accBias); read("acc_scale", &m.accScale);
    read("gyro_bias", &m.gyroBias); read("gyro_scale", &m.gyroScale);
    read("plus_x", &m.imuPlusX); read("plus_z", &m.imuPlusZ); read("position", &m.imuPosition);
    for (double scale : m.accScale)
      if (scale <= 0) throw std::runtime_error("acc_scale must be positive");
    if (j.contains("trackref_from_imu")) {
      const auto& pose = j["trackref_from_imu"];
      if (!pose.is_array() || pose.size() != 7) throw std::runtime_error("bad trackref_from_imu");
      m.objectFromImuRotation = {pose[3].get<double>(), pose[0].get<double>(), pose[1].get<double>(), pose[2].get<double>()};
      m.imuPosition = {pose[4].get<double>(), pose[5].get<double>(), pose[6].get<double>()};
    } else {
      Eigen::Vector3d x(m.imuPlusX.data()), z(m.imuPlusZ.data());
      if (x.norm() < 1e-6 || z.norm() < 1e-6 || x.cross(z).norm() < 1e-6)
        throw std::runtime_error("degenerate IMU axes");
      x.normalize(); z.normalize();
      Eigen::Matrix3d rotation;
      rotation.col(0) = x; rotation.col(1) = z.cross(x).normalized();
      rotation.col(2) = x.cross(rotation.col(1)).normalized();
      const Eigen::Quaterniond q(rotation);
      m.objectFromImuRotation = {q.w(), q.x(), q.y(), q.z()};
    }
    Eigen::Quaterniond q(m.objectFromImuRotation[0], m.objectFromImuRotation[1],
                         m.objectFromImuRotation[2], m.objectFromImuRotation[3]);
    if (!q.coeffs().allFinite() || q.norm() < 1e-6) throw std::runtime_error("bad IMU rotation");
    for (double t : m.imuPosition)
      if (!std::isfinite(t)) throw std::runtime_error("bad IMU position");
    q.normalize();
    m.objectFromImuRotation = {q.w(), q.x(), q.y(), q.z()};
    *out = std::move(m);
    if (error) error->clear();
    return true;
  } catch (const std::exception& e) {
    if (error) *error = e.what();
    return false;
  }
}

TrackedObjectModel TrackedObjectModel::InImuFrame() const {
  TrackedObjectModel result = *this;
  const auto& r = objectFromImuRotation;
  const Eigen::Quaterniond q = Eigen::Quaterniond(r[0], r[1], r[2], r[3]).conjugate();
  const Eigen::Vector3d t(imuPosition.data());
  for (auto& p : result.points) {
    const Eigen::Vector3d transformed = q * (Eigen::Vector3d(p.data()) - t);
    std::copy(transformed.data(), transformed.data() + 3, p.begin());
  }
  for (auto& n : result.normals) {
    const Eigen::Vector3d transformed = q * Eigen::Vector3d(n.data());
    std::copy(transformed.data(), transformed.data() + 3, n.begin());
  }
  result.imuPosition = {};
  result.objectFromImuRotation = {1, 0, 0, 0};
  return result;
}

}  // namespace lh
