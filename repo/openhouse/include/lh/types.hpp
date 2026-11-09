// Core data types shared by transports and the tracking library.
//
// These are the *normalized* events that cross the transport/tracking boundary.
// They mirror the callbacks CLighthouseOpticalThread / CLighthouseIMUThread deliver
// to CSwSyncDetectSource in driver_lighthouse.so (see ai/driver/interfaces.md), so any
// transport (USB, network, replay) that can produce them can drive the tracker.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace lh {

// Device time in seconds (device 48 MHz tick counter, unwrapped to 64 bits and
// scaled by 1/48e6). Each device has its own time base.
using DeviceTime = double;

// Stable per-device identifier chosen by the transport (e.g. "LHR-0704E07A").
using DeviceId = std::string;

constexpr double kTicksPerSecond = 48e6;

struct ImuSample {
  DeviceTime time = 0;
  std::array<double, 3> accel{};  // m/s^2, raw (uncalibrated) device frame
  std::array<double, 3> gyro{};   // rad/s, raw (uncalibrated) device frame
  uint32_t saturated = 0;         // bit i: component i (ax,ay,az,gx,gy,gz) clipped
};

// LH1-style light pulse: one photodiode envelope (report 0x21/0x25, or Watchman).
struct LightPulse {
  DeviceTime time = 0;   // start of pulse
  double width = 0;      // seconds; < 0 if unknown
  int sensor = 0;        // 0..31
};

// LH2 demodulated beam word, report 0x28 word with bit1 == 0.
// Delivered to IDisambiguator slot 9 in the original.
struct Lh2BeamWord {
  DeviceTime time = 0;
  int channel = 0;  // LH2 channel set by preceding control byte
  int bit26 = 0;
  int bit27 = 0;
};

// LH2 sensor word, report 0x28 word with bit1 == 1.
// Delivered to IDisambiguator slot 10 in the original.
struct Lh2SensorWord {
  DeviceTime time = 0;
  int channel = 0;
  int value = 0;  // bits 27..31 of the word
};

enum class DeviceEventType : int {
  PulseWidthUnknown = 3,      // width field 0xffff
  ImuReportComplete = 0xb,
  Lh2ChannelConflict = 0xe,   // "two bases share light channel %d"
  SyncPulseSeen = 0xf,        // pulse with LH1 sync-like width
};

struct DeviceEvent {
  DeviceTime time = 0;
  int type = 0;  // DeviceEventType or other raw value
  int arg = 0;
};

// Auxiliary sensor events (sensor ids >= 0xf7 on LH1 reports, 0x28 control 3).
struct AuxEvent {
  DeviceTime time = 0;
  int id = 0;
  int value = 0;
};


// Controller input as framed by the wireless protocol. Only the framing is decoded
// here; what the bits mean depends on the device (see runtime/index_input.hpp).
enum class InputFormat : int {
  Legacy = 0,   // controller sub-protocol 0: buttons / trigger / 2 axes / 8-byte extended
  Raw = 1,      // controller sub-protocol 1: undecoded bytes in `raw`
  Compact = 2,  // controller sub-protocol 2: buttons, trigger, extra, int8 axes
};

struct InputReport {
  DeviceTime time = 0;
  InputFormat format = InputFormat::Legacy;
  bool hasButtons = false, hasTrigger = false, hasAxes = false, hasExtended = false;
  uint8_t buttons = 0, trigger = 0, extra = 0;
  int16_t axisX = 0, axisY = 0;          // first/second 16-bit word of the axis block
  std::array<uint8_t, 8> extended{};     // libsurvive "gen 2" record (type byte first)
  std::vector<uint8_t> raw;              // Raw format only
};

// Battery status byte: bits 0..6 percent, bit 7 charging (libsurvive handle_battery).
struct BatteryReport {
  DeviceTime time = 0;
  uint8_t raw = 0;
};

}  // namespace lh
