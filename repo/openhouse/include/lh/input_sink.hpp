// The transport -> tracking boundary.
//
// A transport (USB/HID, network, file replay, ...) owns devices and pushes their
// data into an IInputSink. The tracking library implements IInputSink and never
// touches hardware. IMU samples for one device must be non-decreasing in time.
// Preserve optical callback order: wired report batches may group pulses by sensor
// rather than globally by timestamp. The source/disambiguator handles that order.
#pragma once

#include <string>

#include "lh/log_level.hpp"
#include "lh/types.hpp"

namespace lh {

class IInputSink {
 public:
  virtual ~IInputSink() = default;

  // Device lifecycle. configJson is the device's JSON configuration blob
  // (sensor positions/normals, IMU calibration, head_from_imu, ...), as read from
  // the device or from SteamVR's cached lhr-*/config.json.
  virtual void OnDeviceAttached(const DeviceId& dev, const std::string& configJson) = 0;
  virtual void OnDeviceDetached(const DeviceId& dev) = 0;

  // Host clock correlation: hostTime ≈ deviceTime + offset. Updated by the
  // transport's clock-sync filter (original: 0x3cf2c0).
  virtual void OnClockOffset(const DeviceId& dev, double offset) = 0;

  virtual void OnImu(const DeviceId& dev, const ImuSample& s) = 0;
  virtual void OnLightPulse(const DeviceId& dev, const LightPulse& p) = 0;
  virtual void OnLh2BeamWord(const DeviceId& dev, const Lh2BeamWord& w) = 0;
  virtual void OnLh2SensorWord(const DeviceId& dev, const Lh2SensorWord& w) = 0;
  // Demodulator activity count (report 0x27); used for automatic LH2 detection.
  virtual void OnDemodCount(const DeviceId& dev, DeviceTime t, int count) = 0;
  virtual void OnEvent(const DeviceId& dev, const DeviceEvent& e) = 0;
  virtual void OnAux(const DeviceId& dev, const AuxEvent& e) = 0;
  // Controller input / battery (wireless devices). Default: ignored.
  virtual void OnInput(const DeviceId&, const InputReport&) {}
  virtual void OnBattery(const DeviceId&, const BatteryReport&) {}
  // The transport's own messages (devices plugged in, receivers, failures). Default:
  // stderr, so a bare transport still says what it does.
  virtual void OnTransportLog(LogLevel level, const std::string& text);
};

// Convenience base with no-op handlers.
class InputSinkBase : public IInputSink {
 public:
  void OnDeviceAttached(const DeviceId&, const std::string&) override {}
  void OnDeviceDetached(const DeviceId&) override {}
  void OnClockOffset(const DeviceId&, double) override {}
  void OnImu(const DeviceId&, const ImuSample&) override {}
  void OnLightPulse(const DeviceId&, const LightPulse&) override {}
  void OnLh2BeamWord(const DeviceId&, const Lh2BeamWord&) override {}
  void OnLh2SensorWord(const DeviceId&, const Lh2SensorWord&) override {}
  void OnDemodCount(const DeviceId&, DeviceTime, int) override {}
  void OnEvent(const DeviceId&, const DeviceEvent&) override {}
  void OnAux(const DeviceId&, const AuxEvent&) override {}
};

}  // namespace lh
