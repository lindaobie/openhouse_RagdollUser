#include <gtest/gtest.h>
#include <zlib.h>
#include <cstring>
#include <algorithm>
#include <vector>
#include "device_config.hpp"

TEST(DeviceConfig, ReadsAndInflatesFeatureStream) {
  const std::string text = R"({"lighthouse_config":{"modelPoints":[[1,2,3]]}})";
  std::vector<uint8_t> compressed(compressBound(text.size()));
  uLongf size = compressed.size();
  ASSERT_EQ(compress(compressed.data(), &size, reinterpret_cast<const Bytef*>(text.data()), text.size()), Z_OK);
  compressed.resize(size);
  size_t offset = 0;
  auto read = [&](uint8_t* report, size_t capacity) {
    EXPECT_EQ(capacity, 64u);
    if (report[0] == 0x10) {
      offset = 0; report[1] = uint8_t(size >> 8); report[2] = uint8_t(size); return 3;
    }
    EXPECT_EQ(report[0], 0x11);
    const size_t n = std::min(size_t(13), compressed.size() - offset);
    report[1] = uint8_t(n);
    std::memcpy(report + 2, compressed.data() + offset, n); offset += n;
    return int(n + 2);
  };
  std::string result, error;
  ASSERT_TRUE(lh::usb::ReadDeviceConfig(read, &result, &error)) << error;
  EXPECT_EQ(text, result);
}

TEST(DeviceConfig, RejectsTruncationAndInvalidLengthsWithoutChangingOutput) {
  std::string output = "unchanged";
  const auto truncated = [](uint8_t* report, size_t) {
    if (report[0] == 0x10) { report[1] = 0; report[2] = 10; return 3; }
    report[1] = 0; return 2;
  };
  EXPECT_FALSE(lh::usb::ReadDeviceConfig(truncated, &output));
  EXPECT_EQ(output, "unchanged");
  const auto malformed = [](uint8_t* report, size_t) {
    if (report[0] == 0x10) { report[1] = 0; report[2] = 0; return 3; }
    report[1] = 63; return 64;
  };
  EXPECT_FALSE(lh::usb::ReadDeviceConfig(malformed, &output));
  EXPECT_EQ(output, "unchanged");
}
