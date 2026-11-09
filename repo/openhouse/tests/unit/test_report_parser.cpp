#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "report_parser.hpp"
#include "usb_transport.hpp"

namespace {

struct Recorder : lh::InputSinkBase {
  std::vector<lh::ImuSample> imu;
  std::vector<lh::LightPulse> pulses;
  std::vector<lh::BatteryReport> batteries;
  int attached = 0, detached = 0;
  void OnDeviceAttached(const lh::DeviceId&, const std::string&) override { ++attached; }
  void OnDeviceDetached(const lh::DeviceId&) override { ++detached; }
  void OnBattery(const lh::DeviceId&, const lh::BatteryReport& b) override { batteries.push_back(b); }
  void OnImu(const lh::DeviceId&, const lh::ImuSample& s) override { imu.push_back(s); }
  void OnLightPulse(const lh::DeviceId&, const lh::LightPulse& p) override { pulses.push_back(p); }
};

template <typename T>
void Put(uint8_t* p, T v) { std::memcpy(p, &v, sizeof v); }

TEST(TickUnwrapper, Wraps24Bit) {
  lh::usb::TickUnwrapper u;
  EXPECT_EQ(u.Unwrap(0xfffff0, 32), 0xfffff0);
  EXPECT_EQ(u.Unwrap(0x000010, 24), 0x1000010);  // wrapped forward
  EXPECT_EQ(u.Unwrap(0xffffe0, 24), 0xffffe0);   // slightly older, no wrap
}

TEST(ImuParser, ScalesAndOrders) {
  auto dev = std::make_shared<lh::usb::DeviceState>();
  dev->id = "X";
  dev->gyroRange = 0;
  dev->accelRange = 0;
  Recorder rec;
  lh::usb::ImuReportParser p(dev, &rec);
  uint8_t r[52] = {0x20};
  const uint32_t ts[3] = {300, 100, 200};  // ring order: oldest is index 1
  for (int i = 0; i < 3; i++) {
    uint8_t* s = r + 1 + 17 * i;
    Put<int16_t>(s + 0, 16384);   // ax = 1 g
    Put<int16_t>(s + 6, 32767);   // gx saturated
    Put<uint32_t>(s + 12, ts[i]);
  }
  ASSERT_TRUE(p.Parse(1.0, r, sizeof r, false));
  ASSERT_EQ(rec.imu.size(), 3u);
  EXPECT_NEAR(rec.imu[0].time, 100 / 48e6, 1e-15);
  EXPECT_NEAR(rec.imu[2].time, 300 / 48e6, 1e-15);
  EXPECT_NEAR(rec.imu[0].accel[0], 9.80665, 1e-9);
  EXPECT_EQ(rec.imu[0].saturated, 1u << 3);
  // Same report again: nothing new.
  ASSERT_TRUE(p.Parse(1.0, r, sizeof r, false));
  EXPECT_EQ(rec.imu.size(), 3u);
}

TEST(OpticalParser, Report25) {
  auto dev = std::make_shared<lh::usb::DeviceState>();
  Recorder rec;
  lh::usb::OpticalReportParser p(dev, &rec);
  uint8_t r[64] = {0x25};
  for (int i = 0; i < 9; i++) Put<uint16_t>(r + 1 + 7 * i + 1, 0xffff);
  r[1] = 5;
  Put<uint16_t>(r + 2, 480);       // 10 us
  Put<uint32_t>(r + 4, 48000);     // 1 ms
  ASSERT_TRUE(p.Parse(0, r, sizeof r));
  ASSERT_EQ(rec.pulses.size(), 1u);
  EXPECT_EQ(rec.pulses[0].sensor, 5);
  EXPECT_NEAR(rec.pulses[0].width, 1e-5, 1e-15);
  EXPECT_NEAR(rec.pulses[0].time, 1e-3, 1e-15);
}

TEST(WirelessReconnect, ResetsProtocolClockAndImuEpoch) {
  Recorder rec;
  lh::usb::ReportDecoder decoder(&rec);
  lh::usb::EndpointInfo endpoint;
  endpoint.ep = 1; endpoint.kind = lh::usb::EndpointKind::Wireless;
  endpoint.serial = "LHR-TEST"; endpoint.gyroRange = endpoint.accelRange = 0;
  decoder.AddEndpoint(endpoint);
  auto imu = [&](uint8_t msb, double host) {
    uint8_t r[30] = {0x23, msb, 15, 0, 0xe8, 1};
    Put<int16_t>(r + 6, 16384);
    decoder.OnReport(host, 1, true, r, sizeof r);
  };
  uint8_t malformed[30] = {0x23, 100, 255};
  decoder.OnReport(0.9, 1, true, malformed, sizeof malformed);
  EXPECT_TRUE(rec.imu.empty()); EXPECT_TRUE(rec.batteries.empty());
  imu(100, 1.0);
  ASSERT_EQ(rec.imu.size(), 1u);
  const double oldTime = rec.imu.back().time;
  decoder.SetVersionNegotiation(1);
  uint8_t transition[30] = {0x23, 0, 3, 0, 0xe2, 1};
  decoder.OnReport(1.1, 1, true, transition, sizeof transition);
  ASSERT_EQ(decoder.Wireless(1)->ProtocolVersion(), 1);
  const uint8_t off[] = {0x26};
  decoder.OnReport(1.2, 1, true, off, sizeof off);
  EXPECT_EQ(rec.detached, 1);
  EXPECT_EQ(decoder.WirelessGeneration(1), 1u);
  EXPECT_EQ(decoder.Wireless(1)->ProtocolVersion(), 2);  // wait, don't misdecode v0 as Wv2
  uint8_t battery[30] = {0x23, 1, 3, 0, 0xe1, 77};
  decoder.OnReport(1.3, 1, true, battery, sizeof battery);
  EXPECT_TRUE(rec.batteries.empty());
  transition[5] = 0;
  decoder.OnReport(1.4, 1, true, transition, sizeof transition);
  battery[5] = 42;
  decoder.OnReport(1.5, 1, true, battery, sizeof battery);
  ASSERT_EQ(rec.batteries.size(), 1u);
  EXPECT_EQ(rec.batteries[0].raw, 42);
  imu(1, 1.6);
  ASSERT_EQ(rec.imu.size(), 2u);
  EXPECT_LT(rec.imu.back().time, oldTime);  // rebooted timestamp must not be dropped as old
  EXPECT_EQ(rec.attached, 2);
  // Missing 0x26: a long radio-data gap must also create a fresh epoch.
  imu(2, 4.0);
  EXPECT_EQ(rec.detached, 2);
  EXPECT_EQ(decoder.WirelessGeneration(1), 2u);
  EXPECT_EQ(decoder.Wireless(1)->ProtocolVersion(), 2);
}

}  // namespace
