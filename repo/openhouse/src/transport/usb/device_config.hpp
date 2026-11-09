// Read the standard Lighthouse compressed configuration feature stream.
// Injectable feature reader keeps protocol tests independent of hardware/hidapi.
#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace lh::usb {
using FeatureReader = std::function<int(uint8_t* report, size_t capacity)>;
bool ReadDeviceConfig(const FeatureReader& read, std::string* json, std::string* error = nullptr);
}  // namespace lh::usb
