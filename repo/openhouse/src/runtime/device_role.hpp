// Device role from the device JSON. Only `device_class` (plus the model name for
// Index controllers) decides: physical hardware never overrides the user's mapping.
#pragma once

#include <string>

#include "lh/session.hpp"

namespace lh::runtime {

// hmd -> Hmd; controller whose model/render model/input profile names an Index
// ("Knuckles") controller or a Vive wand -> Controller; everything else -> Tracker.
DeviceRole RoleFromConfig(const std::string& configJson);
// Index or Vive wand, for a config RoleFromConfig makes a Controller.
ControllerType ControllerTypeFromConfig(const std::string& configJson);

}  // namespace lh::runtime
