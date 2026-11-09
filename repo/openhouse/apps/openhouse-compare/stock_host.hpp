// SteamVR's own lighthouse driver (driver_lighthouse.so / .dll), run in this process the
// way Monado's steamvr_lh runs it: a minimal OpenVR host that takes every device it
// adds and keeps its latest pose. Nothing of the stock driver is shipped: it's loaded
// from the user's SteamVR install.
#pragma once
#include <openvr_driver.h>

#include <Eigen/Geometry>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace openhouse::compare {

struct StockDevice {
  std::string serial;
  vr::ETrackedDeviceClass cls = vr::TrackedDeviceClass_Invalid;
  bool connected = false, valid = false;
  vr::ETrackingResult result = vr::TrackingResult_Uninitialized;
  Eigen::Isometry3d world = Eigen::Isometry3d::Identity();  // the host's frame (worldFromDriver * pose)
  uint64_t updates = 0;
};

class StockHost {
 public:
  StockHost();
  ~StockHost();
  // The stock library in SteamVR's install (our installer's .stock copy, else the
  // original); "" if none was found.
  static std::string FindDriver();
  bool Start(const std::string& path, std::string* error);
  void Stop();
  std::vector<StockDevice> Devices() const;

  // Host state, public for the interface implementations in stock_host.cpp.
  struct Impl;

 private:
  std::unique_ptr<Impl> impl_;
};

}  // namespace openhouse::compare
