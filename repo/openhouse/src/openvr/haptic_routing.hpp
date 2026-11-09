#pragma once
#include <openvr_driver.h>

namespace lh::openvr {
// Modern actions identify their component handle, which is unique across the host:
// that alone decides. Monado's steamvr_lh sends the device's own index as the
// container (Activate(n) gets container n + 1 from TrackedDeviceToPropertyContainer,
// but its events say n) and n - 1 as the device index, so neither may veto a
// matching component. Legacy Identify/TriggerHapticPulse may identify only a
// tracked-device index, or omit the container. Never broadcast an untargeted or
// explicitly mismatched event to another controller.
inline bool MatchesHaptic(const vr::VREvent_t& event, uint32_t id,
                          vr::PropertyContainerHandle_t container, vr::VRInputComponentHandle_t component) {
  if (event.eventType != vr::VREvent_Input_HapticVibration || !component) return false;
  const auto& h = event.data.hapticVibration;
  if (h.componentHandle) return h.componentHandle == component;
  if (h.containerHandle && h.containerHandle != container) return false;
  return id != vr::k_unTrackedDeviceIndexInvalid && event.trackedDeviceIndex == id;
}
}
