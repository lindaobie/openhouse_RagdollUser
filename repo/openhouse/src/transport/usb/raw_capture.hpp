// Raw HID capture format (.lhraw).
//
// Stores HID reports byte-for-byte with host read time, so captures can be
// re-decoded by improved parsers and replayed into the tracker exactly as a live
// device would feed it.
//
// File: "LHRAW001" then records:
//   u8 type
//   type 1 (Endpoint):  u16 ep, u16 vid, u16 pid, u8 iface, u8 kind, str serial,
//                       i8 gyroRange, i8 accelRange, str configJson
//   type 2 (Report):    f64 hostTime, u16 ep, u8 lastInBatch, u16 len, bytes
// str = u32 len + bytes. Little endian.
#pragma once

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace lh::usb {

// Buttons: a wired device's input interface (iface 2; libsurvive's TRACKER*_BUTTONS).
enum class EndpointKind : uint8_t { Imu = 0, Optical = 1, Wireless = 2, Buttons = 3 };

struct EndpointInfo {
  uint16_t ep = 0;
  uint16_t vid = 0, pid = 0;
  uint8_t iface = 0;
  EndpointKind kind = EndpointKind::Imu;
  std::string serial;
  int8_t gyroRange = -1, accelRange = -1;
  std::string configJson;
};

struct RawReport {
  double hostTime = 0;
  uint16_t ep = 0;
  bool lastInBatch = false;
  std::vector<uint8_t> data;
};

class RawCaptureWriter {
 public:
  RawCaptureWriter() = default;
  RawCaptureWriter(const RawCaptureWriter&) = delete;
  RawCaptureWriter& operator=(const RawCaptureWriter&) = delete;
  ~RawCaptureWriter();
  bool Open(const std::string& path);
  void Close();
  void WriteEndpoint(const EndpointInfo& e);
  void WriteReport(double hostTime, uint16_t ep, bool last, const uint8_t* data, size_t len);
  bool IsOpen() const { return f_ != nullptr; }
  // Includes buffered write/close failures; safe to poll from another thread.
  bool Healthy() const { return !failed_.load(); }

 private:
  FILE* f_ = nullptr;
  std::atomic<bool> failed_{false};
};

class RawCaptureReader {
 public:
  RawCaptureReader() = default;
  RawCaptureReader(const RawCaptureReader&) = delete;
  RawCaptureReader& operator=(const RawCaptureReader&) = delete;
  ~RawCaptureReader();
  bool Open(const std::string& path);
  // Calls onEndpoint / onReport in file order. Returns false on format error.
  bool ReadAll(const std::function<void(const EndpointInfo&)>& onEndpoint,
               const std::function<void(const RawReport&)>& onReport);

 private:
  FILE* f_ = nullptr;
};

}  // namespace lh::usb
