#include "raw_capture.hpp"

#include <cstring>
#include <cmath>
#include <limits>

namespace lh::usb {
namespace {
constexpr char kMagic[8] = {'L', 'H', 'R', 'A', 'W', '0', '0', '1'};

template <typename T>
void W(FILE* f, T v) { fwrite(&v, sizeof v, 1, f); }
void WS(FILE* f, const std::string& s) {
  W<uint32_t>(f, uint32_t(s.size()));
  fwrite(s.data(), 1, s.size(), f);
}
template <typename T>
bool R(FILE* f, T& v) { return fread(&v, sizeof v, 1, f) == 1; }
bool RS(FILE* f, std::string& s) {
  uint32_t n;
  if (!R(f, n) || n > (64u << 20)) return false;
  s.resize(n);
  return fread(s.data(), 1, n, f) == n;
}
}  // namespace

RawCaptureWriter::~RawCaptureWriter() { Close(); }

bool RawCaptureWriter::Open(const std::string& path) {
  Close();
  failed_ = false;
  f_ = fopen(path.c_str(), "wb");
  if (!f_) { failed_ = true; return false; }
  if (fwrite(kMagic, 1, 8, f_) != 8) { failed_ = true; Close(); return false; }
  return true;
}

void RawCaptureWriter::Close() {
  if (f_ && fclose(f_) != 0) failed_ = true;
  f_ = nullptr;
}

void RawCaptureWriter::WriteEndpoint(const EndpointInfo& e) {
  if (!f_) return;
  W<uint8_t>(f_, 1);
  W(f_, e.ep); W(f_, e.vid); W(f_, e.pid); W(f_, e.iface); W(f_, uint8_t(e.kind));
  WS(f_, e.serial);
  W(f_, e.gyroRange); W(f_, e.accelRange);
  WS(f_, e.configJson);
  if (ferror(f_)) failed_ = true;
}

void RawCaptureWriter::WriteReport(double hostTime, uint16_t ep, bool last, const uint8_t* data, size_t len) {
  if (!f_) return;
  if (len > std::numeric_limits<uint16_t>::max()) { failed_ = true; return; }
  W<uint8_t>(f_, 2);
  W(f_, hostTime); W(f_, ep); W<uint8_t>(f_, last ? 1 : 0); W<uint16_t>(f_, uint16_t(len));
  fwrite(data, 1, len, f_);
  if (ferror(f_)) failed_ = true;
}

RawCaptureReader::~RawCaptureReader() {
  if (f_) fclose(f_);
}

bool RawCaptureReader::Open(const std::string& path) {
  if (f_) fclose(f_);
  f_ = fopen(path.c_str(), "rb");
  if (!f_) return false;
  char m[8];
  if (fread(m, 1, 8, f_) == 8 && std::memcmp(m, kMagic, 8) == 0) return true;
  fclose(f_); f_ = nullptr;
  return false;
}

bool RawCaptureReader::ReadAll(const std::function<void(const EndpointInfo&)>& onEndpoint,
                               const std::function<void(const RawReport&)>& onReport) {
  if (!f_) return false;
  uint8_t type;
  while (R(f_, type)) {
    if (type == 1) {
      EndpointInfo e;
      uint8_t kind;
      if (!(R(f_, e.ep) && R(f_, e.vid) && R(f_, e.pid) && R(f_, e.iface) && R(f_, kind) && RS(f_, e.serial) &&
            R(f_, e.gyroRange) && R(f_, e.accelRange) && RS(f_, e.configJson)))
        return false;
      if (kind > uint8_t(EndpointKind::Buttons) || e.gyroRange < -1 || e.gyroRange > 3 ||
          e.accelRange < -1 || e.accelRange > 3) return false;
      e.kind = EndpointKind(kind);
      onEndpoint(e);
    } else if (type == 2) {
      RawReport r;
      uint8_t last;
      uint16_t len;
      if (!(R(f_, r.hostTime) && R(f_, r.ep) && R(f_, last) && R(f_, len))) return false;
      if (!std::isfinite(r.hostTime) || last > 1) return false;
      r.lastInBatch = last != 0;
      r.data.resize(len);
      if (fread(r.data.data(), 1, len, f_) != len) return false;
      onReport(r);
    } else {
      return false;
    }
  }
  return !ferror(f_);
}

}  // namespace lh::usb
