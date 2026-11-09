#include "device_config.hpp"

#include <zlib.h>
#include <chrono>
#include <thread>
#include <cstdio>
#include <array>
#include <vector>

namespace lh::usb {

bool ReadDeviceConfig(const FeatureReader& rawRead, std::string* json, std::string* error) {
  const auto fail = [&](const char* text) { if (error) *error = text; return false; };
  if (!rawRead || !json) return fail("invalid config reader/output");
  // As stock HidDevice_GetFeature(..., 100 ms): a request the radio can't answer yet
  // fails (wireless devices behind a dongle); retry it for up to 100 ms.
  const auto read = [&](uint8_t* data, size_t size) {
    const uint8_t id = data[0];
    int n = -1;
    for (int attempt = 0; attempt < 20; ++attempt) {
      data[0] = id;
      n = rawRead(data, size);
      if (n > 0) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return n;
  };
  std::array<uint8_t, 64> report{};
  report[0] = 0x10;  // start/reset the configuration stream (GET_FEATURE)
  int n = read(report.data(), report.size());
  if (n < 3 || n > int(report.size()) || report[0] != 0x10) {
    static thread_local char text[96];
    std::snprintf(text, sizeof text, "config readmode failed (%d bytes, %02x %02x %02x)", n, report[0], report[1], report[2]);
    return fail(text);
  }
  const size_t expected = size_t(report[1]) * 256 + report[2];
  std::vector<uint8_t> compressed;
  for (int chunks = 0; chunks < 2048; ++chunks) {
    report.fill(0); report[0] = 0x11;
    n = read(report.data(), report.size());
    if (n < 2 || n > int(report.size()) || report[0] != 0x11 || report[1] > n - 2)
      return fail("invalid config chunk");
    const size_t length = report[1];
    if (length == 0) {
      if (compressed.empty() || compressed.size() < expected) return fail("truncated config stream");
      std::string output(65536, '\0');
      uLongf size = output.size();
      const int result = uncompress(reinterpret_cast<Bytef*>(output.data()), &size,
                                    compressed.data(), compressed.size());
      if (result != Z_OK) return fail("invalid/oversized compressed config");
      output.resize(size);
      const auto begin = output.find_first_not_of(" \t\r\n");
      if (begin == std::string::npos || output[begin] != '{') return fail("config is not a JSON object");
      *json = std::move(output);
      if (error) error->clear();
      return true;
    }
    if (compressed.size() + length > 65536) return fail("config stream too large");
    compressed.insert(compressed.end(), report.begin() + 2, report.begin() + 2 + length);
  }
  return fail("config stream did not terminate");
}

}  // namespace lh::usb
