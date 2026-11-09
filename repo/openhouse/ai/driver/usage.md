> Former repository README (build, run, embedding API). Paths are relative to the repository root.

# liblighthouse — standalone Lighthouse 1.0 tracking

C++20 reconstruction of the Linux `driver_lighthouse.so` tracking pipeline, with
USB transport separated from tracking. **Experimental: not yet a verified,
functionally identical replacement for Valve's complete tracker.** Component
oracle tests are strong; whole-session pose parity remains unfinished.

## Build and test

Dependencies: CMake, C++20 compiler, Eigen, Ceres (Jet autodiff), nlohmann-json,
zlib, pthreads, and optionally hidapi-hidraw. GLFW/OpenGL enables the viewer.
GTest is needed for tests.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

Oracle tests load the repository's original `driver_lighthouse.so` **only in the
test process**. Production libraries/apps do not load it. Captures and the cached
controller geometry are required by some tests; those tests skip if fixtures are
absent. Disable proprietary-binary tests with `-DLH_BUILD_ORACLE_TESTS=OFF`.

To build tracking alone, with **no USB/hidapi, CLI, or graphics requirement**:

```sh
cmake -S . -B build-core -G Ninja -DLH_BUILD_USB=OFF -DLH_BUILD_ORACLE_TESTS=OFF
cmake --build build-core -j2
ctest --test-dir build-core --output-on-failure
```

## Run

Close SteamVR before opening USB devices with these tools. Device initialization
can change the Watchman protocol mode. Replay does not access hardware.

```sh
# Generic capture: record until Ctrl-C, with config/DB snapshots and diagnostics
./build/lhcapture
# Or choose a session directory and optional duration
./build/lhcapture ~/lh-captures/controller-session-01 --seconds 120

# Inspect captured input (the older single-file capture command also still works)
./build/lhtool stats ~/lh-captures/controller-session-01/input.lhraw

# Track a capture; JSON Lines on stdout, diagnostics on stderr
./build/lhtool track tests/data/captures/controller_static_10s.lhraw > poses.jsonl
./build/lhtool track session.lhraw --db /path/to/lighthousedb.json > poses.jsonl

# Live tracking, 30 seconds (0 = until Ctrl-C)
./build/lhtool track live 30 > live-poses.jsonl

# Interactive 3D diagnostics (if GLFW/OpenGL were found)
# liblh session runtime (shared room map, roles from device JSON, Index input):
./build/lhviz                                          # live devices (SteamVR stopped)
./build/lhviz --replay ~/lh-captures/<session>/input.lhraw  # replay a recording
./build/lhtool session ~/lh-captures/<session>/input.lhraw --rate 30 > session.jsonl

# Drop-in driver_lighthouse.so for SteamVR / Monado steamvr_lh (tracked devices only):
scripts/build-linux.sh && dist/openhouse-driver/install.sh   # uninstall.sh restores stock

# Windows x64 DLL + standalone apps (Linux cross-build; MinGW + vcpkg):
scripts/build-windows.sh              # produces dist/lh-windows-x64.zip
# See ai/driver/windows_build.md for installation and native-hardware verification limits.
```

Viewer: left-drag to orbit, wheel to zoom, **Tab** to select another device,
**R** to recenter, **Space** to pause replay, **Esc** to quit. `--speed 2` replays at
2× speed; `--db FILE` supplies base placement. Only the selected device is drawn,
so devices in different primary-base frames are not misleadingly overlaid.

Output poses refer to the **IMU origin and axes**, not SteamVR's head/grip frame.
Positions/velocities are in the primary base's coordinate frame unless an explicit
world-from-base placement was supplied (`world_space: true`). Quaternions are
`[w,x,y,z]`; angular velocity is in body coordinates. Do not compare these values
to SteamVR head/grip poses without the appropriate transforms/time alignment.
`lost` records are not valid optical tracking; inertial fallback can drift.

The bundled controller capture currently produces poses for both the Index HMD
and controller. This is a replay smoke test, **not a pose-accuracy validation**.

## Embed / replace USB

`lh::Tracker` implements `lh::IInputSink` from `include/lh/input_sink.hpp`.
Any USB, network, or replay adapter may send device configuration, clock offsets,
IMU samples, and LH1 light pulses to it. It owns one isolated source/filter per
device. Poll output without registering callbacks:

```cpp
#include <lh/tracker.hpp>

lh::Tracker tracker;
tracker.OnDeviceAttached("my-device", deviceConfigJson);
tracker.OnClockOffset("my-device", hostMinusDeviceSeconds);
tracker.OnImu("my-device", sample);
tracker.OnLightPulse("my-device", pulse);
for (const auto& pose : tracker.PollPoses()) {
    // Check pose.status before using pose.pose.
}
```

IMU times must be non-decreasing per device. Preserve the optical callback order:
HID pulse batches are not necessarily globally timestamp-sorted. Calls and output
polling are serialized internally. `Tracker::Devices()` and `PollLogs()` expose
initialization failures and diagnostics. See [runtime details](runtime.md).

Install with `cmake --install build --prefix /your/prefix`, then:

```cmake
find_package(lighthouse CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE lh::tracking) # optionally lh::usb
```

## More

See [PROGRESS](PROGRESS.md), [architecture](ARCHITECTURE.md),
[filter](lighthouse_direct.md), [solver](pose_solver.md) and [runtime](runtime.md).
LH2 tracking works: [lh2_support](lh2_support.md). For a complete, minimal program
using `lh::Session`, see `apps/example_app` (its own CMake project).
