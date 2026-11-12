#pragma once
// SteamVR's watchdog: starts SteamVR when a device is turned on (watchdog.cpp).
#include <openvr_driver.h>

namespace lh::openvr {
vr::IVRWatchdogProvider* WatchdogProvider();
}
