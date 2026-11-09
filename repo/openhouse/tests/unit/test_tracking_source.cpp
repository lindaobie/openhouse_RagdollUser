#include <gtest/gtest.h>

#include "tracking/tracking_source.hpp"

TEST(TrackingSource, PreservesTimeFlagsAndOriginalFloatPrecision) {
  lh::TrackingSource source;
  lh::ImuSample input;
  input.time = -0.125;  // Unwrapped device times may be negative at startup.
  input.accel = {1.0 / 3.0, -9.80665, 0.123456789};
  input.gyro = {-1.23456789, 0, 7.89123456};
  input.saturated = 0x25;
  source.OnImu(input);
  double peek = 0;
  ASSERT_TRUE(source.PeekImuTime(&peek));
  EXPECT_DOUBLE_EQ(peek, input.time);
  // Peeking is non-consuming.
  ASSERT_TRUE(source.PeekImuTime(&peek));
  lh::ImuSample output;
  ASSERT_TRUE(source.PopImu(&output));
  EXPECT_DOUBLE_EQ(output.time, input.time);
  EXPECT_EQ(output.saturated, input.saturated);
  for (size_t i = 0; i < 3; ++i) {
    EXPECT_DOUBLE_EQ(output.accel[i], double(float(input.accel[i])));
    EXPECT_DOUBLE_EQ(output.gyro[i], double(float(input.gyro[i])));
  }
  EXPECT_FALSE(source.PopImu(&output));
  EXPECT_FALSE(source.PeekImuTime(&peek));
}

TEST(TrackingSource, DropsOldestAtOneThousandSamples) {
  lh::TrackingSource source;
  for (int i = 0; i < 1007; ++i) {
    lh::ImuSample input;
    input.time = i * 0.001;
    input.saturated = uint32_t(i);
    source.OnImu(input);
  }
  for (int i = 7; i < 1007; ++i) {
    double peek;
    ASSERT_TRUE(source.PeekImuTime(&peek));
    EXPECT_DOUBLE_EQ(peek, i * 0.001);
    lh::ImuSample output;
    ASSERT_TRUE(source.PopImu(&output));
    EXPECT_EQ(output.saturated, uint32_t(i));
    EXPECT_DOUBLE_EQ(output.time, peek);
  }
  lh::ImuSample output;
  EXPECT_FALSE(source.PopImu(&output));
}

TEST(TrackingSource, DeviceQueuesAreIndependent) {
  lh::TrackingSource hmd(0), controller(9);
  lh::ImuSample input;
  input.time = 1;
  hmd.OnImu(input);
  lh::ImuSample output;
  EXPECT_FALSE(controller.PopImu(&output));
  EXPECT_TRUE(hmd.PopImu(&output));
  lh::OpticalFrame frame;
  EXPECT_FALSE(hmd.PopFrame(&frame));
  EXPECT_FALSE(controller.PopFrame(&frame));
}

#include "tracking/ootx.hpp"

namespace {
// The lhbs firmware's OOTX encoder (basestation_firmware/src/ootx.c): 17 zeros, a 1, then
// u16 length, payload, pad, crc32, each byte MSB first, a 1 stuffed after 16 bits.
std::vector<uint8_t> LhbsFrame(const std::vector<uint8_t>& payload) {
  std::vector<uint8_t> bits(17, 0);
  bits.push_back(1);
  auto bit = [&](int b) {
    bits.push_back(uint8_t(b));
    if (bits.size() > 18 && bits.size() % 17 == 0) bits.push_back(1);
  };
  auto bytes = [&](const uint8_t* p, size_t n) {
    for (size_t i = 0; i < n; ++i)
      for (int k = 7; k >= 0; --k) bit((p[i] >> k) & 1);
  };
  const uint8_t len[2] = {uint8_t(payload.size()), 0};
  bytes(len, 2);
  bytes(payload.data(), payload.size());
  if (payload.size() & 1) { const uint8_t z = 0; bytes(&z, 1); }
  uint32_t crc = ~0u;
  for (uint8_t c : payload) {
    crc ^= c;
    for (int k = 0; k < 8; ++k) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
  }
  crc = ~crc;
  bytes(reinterpret_cast<const uint8_t*>(&crc), 4);
  return bits;
}
}  // namespace

// lhbs multi mode adds the round (which slots take turns) as payload byte 33.
TEST(Ootx, DecodesTheLhbsMultiRound) {
  std::vector<uint8_t> payload(34, 0);
  payload[0] = uint8_t((436 << 6) | 6);
  payload[1] = uint8_t(((436 << 6) | 6) >> 8);
  payload[2] = 0x40; payload[3] = 0xb8; payload[4] = 0x01; payload[5] = 0xa1;
  payload[15] = 9;
  payload[31] = 0x80 | 0x40 | (2 << 4) | 3;  // multi, policy ALL, 3 in the round, slot 3
  payload[33] = 0x0d;                        // slots 0, 2, 3
  const auto bits = LhbsFrame(payload);
  lh::ootx::OotxMajorityRing ring(int(bits.size()));
  for (uint8_t b : bits) ring.Push(b);
  ASSERT_TRUE(ring.Complete());
  lh::BaseConfig cfg;
  lh::BaseDynamicState dyn;
  ASSERT_TRUE(lh::ootx::DecodeRing(ring, &cfg, &dyn));
  EXPECT_TRUE(dyn.multi);
  EXPECT_TRUE(dyn.multiAll);
  EXPECT_EQ(dyn.multiN, 3);
  EXPECT_EQ(dyn.multiSlot, 3);
  EXPECT_EQ(dyn.multiMask, 0x0d);
  // A stock-length frame still decodes (mask unknown); a 34-byte one that isn't multi doesn't.
  payload.pop_back();
  auto stock = LhbsFrame(payload);
  lh::ootx::OotxMajorityRing r2(int(stock.size()));
  for (uint8_t b : stock) r2.Push(b);
  ASSERT_TRUE(lh::ootx::DecodeRing(r2, &cfg, &dyn));
  EXPECT_EQ(dyn.multiMask, 0);
  payload.push_back(0x0d);
  payload[31] = 0;
  auto bogus = LhbsFrame(payload);
  lh::ootx::OotxMajorityRing r3(int(bogus.size()));
  for (uint8_t b : bogus) r3.Push(b);
  EXPECT_FALSE(lh::ootx::DecodeRing(r3, &cfg, &dyn));
}
