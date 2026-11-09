#pragma once
#include "lh/session.hpp"
#include "runtime/room_map.hpp"

namespace lh::openvr {
// Keep the host's raw/chaperone frame distinct from our optimizable room frame.
// Align through one shared physical base; never rewrite the host's room files.
class RawAlignment {
 public:
  bool Load(const std::string& path, std::string* error);
  bool Update(const SessionSnapshot& snapshot);
  const Eigen::Isometry3d& RawFromWorld() const { return rawFromWorld_; }
  const std::string& RoomId() const { return roomId_; }
 private:
  runtime::RoomMap reference_;
  uint32_t anchor_ = 0;
  Eigen::Isometry3d rawFromWorld_ = Eigen::Isometry3d::Identity();
  std::string roomId_;
  static constexpr double kGrossLevelRad = 5.0 * 0.017453292519943295;
  bool levelDecided_ = false;  // once per session (Load)
  Eigen::Matrix3d levelFix_ = Eigen::Matrix3d::Identity();
};
}
