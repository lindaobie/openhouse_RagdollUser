#include <gtest/gtest.h>
#include <filesystem>
#include <stdexcept>
#include <cstdlib>
#include <fstream>
#include <string>
#include "platform/platform.hpp"
#include "raw_capture.hpp"

namespace {
// A fresh file in the temp directory (also on Windows, where there's no mkstemp).
struct TemporaryFile {
  std::string path;
  TemporaryFile() {
    static int counter = 0;
    path = (std::filesystem::temp_directory_path() / ("lh-capture-test-" + std::to_string(lh::platform::ProcessId()) +
                                                      "-" + std::to_string(++counter)))
               .string();
    if (!std::ofstream(path, std::ios::binary)) throw std::runtime_error("cannot create " + path);
  }
  ~TemporaryFile() { std::error_code error; std::filesystem::remove(path, error); }
};

TEST(RawCapture, RoundTripAndHealth) {
  TemporaryFile file;
  lh::usb::RawCaptureWriter writer;
  ASSERT_TRUE(writer.Open(file.path));
  lh::usb::EndpointInfo endpoint;
  endpoint.ep = 3; endpoint.serial = "test-controller";
  endpoint.kind = lh::usb::EndpointKind::Wireless;
  endpoint.gyroRange = 3; endpoint.accelRange = 2; endpoint.configJson = "{}";
  writer.WriteEndpoint(endpoint);
  const uint8_t bytes[] = {0x23, 0x12, 0x34};
  writer.WriteReport(123.5, 3, true, bytes, sizeof bytes);
  writer.Close();
  EXPECT_TRUE(writer.Healthy());
  lh::usb::RawCaptureReader reader;
  ASSERT_TRUE(reader.Open(file.path));
  int endpoints = 0, reports = 0;
  ASSERT_TRUE(reader.ReadAll([&](const auto& e) {
    ++endpoints; EXPECT_EQ(e.serial, endpoint.serial); EXPECT_EQ(e.configJson, "{}");
    EXPECT_EQ(e.gyroRange, 3); EXPECT_EQ(e.accelRange, 2);
  }, [&](const auto& r) {
    ++reports; EXPECT_EQ(r.hostTime, 123.5); EXPECT_EQ(r.ep, 3);
    EXPECT_TRUE(r.lastInBatch); EXPECT_EQ(r.data, std::vector<uint8_t>(bytes, bytes + sizeof bytes));
  }));
  EXPECT_EQ(endpoints, 1); EXPECT_EQ(reports, 1);
}

TEST(RawCapture, ReportsBufferedWriteFailuresOnClose) {
  if (!std::filesystem::exists("/dev/full")) GTEST_SKIP() << "Linux /dev/full required";
  lh::usb::RawCaptureWriter writer;
  writer.Open("/dev/full");
  writer.Close();
  EXPECT_FALSE(writer.Healthy());
}
}  // namespace
