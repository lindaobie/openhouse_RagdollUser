// Differential test: original wireless decoders (legacy 0x3fdec0 / Wv2 0x3fe6d0)
// vs lh::usb::LegacyDecoder / Wv2Decoder on every packet of a capture.
#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <map>

#include "oracle.hpp"
#include "raw_capture.hpp"
#include "wireless_decoder.hpp"

namespace {

// Original decoder object (0xe0 bytes), initialized like the IMU thread ctor (0x3cc540).
struct OrigDecoder {
  uint8_t* d;
  explicit OrigDecoder(bool wv2) {
    d = static_cast<uint8_t*>(calloc(1, 0x100));
    auto W = [&](int off, uint64_t v) { std::memcpy(d + off, &v, 8); };
    const uint64_t vt = uint64_t(oracle::At(wv2 ? 0x473870 : 0x473818));
    W(0, vt);
    uint64_t* map = static_cast<uint64_t*>(calloc(8, 8));
    uint8_t* node = static_cast<uint8_t*>(calloc(1, 0x1f8));
    uint64_t* slot = map + 3;
    *slot = uint64_t(node);
    W(0x08, uint64_t(map));
    W(0x10, 8);
    W(0x18, uint64_t(node)); W(0x20, uint64_t(node)); W(0x28, uint64_t(node) + 0x1f8); W(0x30, uint64_t(slot));
    W(0x38, uint64_t(node)); W(0x40, uint64_t(node)); W(0x48, uint64_t(node) + 0x1f8); W(0x50, uint64_t(slot));
    if (wv2) oracle::Fn<void (*)(void*)>(0x3fe420)(d);
  }
  bool Decode(uint8_t msb, const uint8_t* p, int n) {
    void** vt = *reinterpret_cast<void***>(d);
    return reinterpret_cast<bool (*)(void*, uint8_t, const uint8_t*, int)>(vt[2])(d, msb, p, n);
  }
  template <typename T> T At(int off) const { T v; std::memcpy(&v, d + off, sizeof v); return v; }
  std::vector<lh::usb::WirelessPacket::Pulse> Pulses() const {
    std::vector<lh::usb::WirelessPacket::Pulse> v;
    uint8_t* cur = At<uint8_t*>(0x18);
    uint8_t* last = At<uint8_t*>(0x28);
    uint64_t* node = At<uint64_t*>(0x30);
    uint8_t* fin = At<uint8_t*>(0x38);
    while (cur != fin) {
      lh::usb::WirelessPacket::Pulse p;
      std::memcpy(&p.sensor, cur, 4);
      std::memcpy(&p.ts, cur + 4, 4);
      std::memcpy(&p.width, cur + 8, 2);
      v.push_back(p);
      cur += 12;
      if (cur == last) {
        node++;
        cur = reinterpret_cast<uint8_t*>(*node);
        last = cur + 0x1f8;
      }
    }
    return v;
  }
};

class WirelessDiff : public ::testing::TestWithParam<const char*> {};

TEST_P(WirelessDiff, DecodersMatchOriginal) {
  const std::string cap = std::string(LH_SOURCE_DIR) + "/captures/" + GetParam();
  if (!std::filesystem::exists(cap)) GTEST_SKIP() << "no capture";
  lh::usb::RawCaptureReader r;
  ASSERT_TRUE(r.Open(cap));
  std::map<uint16_t, lh::usb::EndpointInfo> eps;
  struct EpState {
    OrigDecoder oLegacy{false}, oWv2{true};
    lh::usb::Wv2Decoder mWv2;
    int version = 2;  // captures start with a pending version request
  };
  std::map<uint16_t, EpState> st;
  int packets = 0, pulses = 0, imu = 0, legacyPackets = 0;
  r.ReadAll([&](const lh::usb::EndpointInfo& e) { eps[e.ep] = e; },
            [&](const lh::usb::RawReport& rep) {
              if (eps[rep.ep].kind != lh::usb::EndpointKind::Wireless || rep.data.size() < 30) return;
              if (rep.data[0] != 0x23 && rep.data[0] != 0x24) return;
              EpState& S = st[rep.ep];
              auto& oLegacy = S.oLegacy;
              auto& oWv2 = S.oWv2;
              auto& mWv2 = S.mWv2;
              int& version = S.version;
              for (int k = 0; k < (rep.data[0] == 0x24 ? 2 : 1); k++) {
                const uint8_t* pkt = rep.data.data() + 1 + 29 * k;
                if (version == 2) {
                  if (pkt[1] == 3 && pkt[3] == 0xe2) version = pkt[4];
                  continue;
                }
                lh::usb::WirelessPacket mp;
                bool mo, oo;
                OrigDecoder& od = version == 0 ? oLegacy : oWv2;
                if (version == 0) {
                  mo = lh::usb::LegacyDecoder().Decode(pkt[0], pkt + 2, pkt[1], &mp);
                  legacyPackets++;
                } else {
                  mo = mWv2.Decode(pkt[0], pkt + 2, pkt[1], &mp);
                }
                oo = od.Decode(pkt[0], pkt + 2, pkt[1]);
                ASSERT_EQ(oo, mo) << "packet " << packets;
                packets++;
                if (!oo) continue;
                ASSERT_EQ(bool(od.At<uint8_t>(0x80)), mp.hasImu) << "packet " << packets;
                if (mp.hasImu) {
                  EXPECT_EQ(0, std::memcmp(od.d + 0x70, mp.imuRaw, 12));
                  EXPECT_EQ(od.At<uint32_t>(0x7c), mp.imuTs);
                  imu++;
                }
                EXPECT_EQ(bool(od.At<uint8_t>(0xd2)), mp.versionChange);
                EXPECT_EQ(bool(od.At<uint8_t>(0xd4)), mp.opticalSubChanged);
                EXPECT_EQ(bool(od.At<uint8_t>(0xd5)), mp.controllerSubChanged);
                EXPECT_EQ(bool(od.At<uint8_t>(0xd0)), mp.hasBattery);
                EXPECT_EQ(bool(od.At<uint8_t>(0xd6)), mp.hasEvent);
                auto op = od.Pulses();
                ASSERT_EQ(op.size(), mp.pulses.size()) << "packet " << packets;
                for (size_t i = 0; i < op.size(); i++) {
                  EXPECT_EQ(op[i].sensor, mp.pulses[i].sensor);
                  EXPECT_EQ(op[i].ts, mp.pulses[i].ts);
                  EXPECT_EQ(op[i].width, mp.pulses[i].width);
                }
                pulses += int(op.size());
                if (mp.versionChange && version != mp.newVersion) version = mp.newVersion;
                if (HasFailure()) FAIL() << "first mismatch at packet " << packets;
              }
            });
  printf("compared %d packets (%d legacy), %d pulses, %d imu samples\n", packets, legacyPackets, pulses, imu);
  EXPECT_GT(packets, 1000);
  EXPECT_GT(pulses, 1000);
}

INSTANTIATE_TEST_SUITE_P(Captures, WirelessDiff,
                         ::testing::Values("controller_static_10s.lhraw", "controller_legacy_v0_10s.lhraw",
                                           "session-20261016-142931Z-147083/input.lhraw"));

}  // namespace
