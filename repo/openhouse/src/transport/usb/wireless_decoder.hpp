// Watchman wireless packet decoding (Vive wands / trackers: protocol v0, Index
// controllers and newer: "Wv2", protocol v1). Port of CWirelessDecoder (base
// vtable 0x472bf0), the legacy decoder (vtable 0x473818, Decode = 0x3fdec0,
// lightcap = 0x3fdb00) and CWirelessDecoderWv2 (vtable 0x473870, Decode = 0x3fe6d0).
// See ai/driver/wireless_protocol.md.
#pragma once

#include <cstdint>
#include <deque>
#include <vector>

namespace lh::usb {

// Output of one packet decode (fields named after their meaning; offsets refer to the
// original 0xe0-byte decoder object).
struct WirelessPacket {
  struct Pulse {          // 12-byte deque element
    uint32_t sensor = 0;  // sensor id, or >= 0xf7 for aux
    uint32_t ts = 0;      // 32-bit device ticks (reconstructed)
    uint16_t width = 0;   // ticks
  };
  struct Lh2Record {      // 0x1c-byte vector element (optical sub-protocol 1)
    int type = 0;         // 1 = beam word, 0 = sensor word
    int channel = 0;
    int sensor = -1;      // +8
    uint32_t ts24 = 0;    // +0xc
    int halfTick = 0;     // +0x10
    int bit26 = 0;        // +0x14
    int bit27 = 0;        // +0x18
  };
  std::deque<Pulse> pulses;          // +0x08
  std::vector<Lh2Record> lh2;        // +0x58
  bool hasImu = false;               // +0x80
  uint8_t imuRaw[12] = {};           // +0x70: int16 accel[3], gyro[3]
  uint32_t imuTs = 0;                // +0x7c
  bool hasRawInput = false;          // +0x88 (controller sub-protocol 1)
  std::vector<uint8_t> rawInput;     // +0xb8
  // Input event fields (+0x82..): presence flags and values.
  bool hasButtons = false, hasTrigger = false, hasTouchpad = false, hasInput8 = false, hasInputV2 = false;
  uint8_t buttons = 0, trigger = 0;  // +0xa0, +0xa1
  uint8_t inputA2 = 0;               // +0xa2
  uint32_t touchpad = 0;             // +0xa4
  uint64_t input8 = 0;               // +0xac
  // Metadata
  bool hasBattery = false;           // +0xd0
  uint8_t battery = 0;               // +0xd1
  bool versionChange = false;        // +0xd2
  uint8_t newVersion = 0;            // +0xd3
  bool opticalSubChanged = false;    // +0xd4
  bool controllerSubChanged = false; // +0xd5
  bool hasEvent = false;             // +0xd6
  uint8_t eventValue = 0;            // +0xd7
  uint32_t eventTs = 0;              // +0xd8

  void Clear();
};

// Legacy (protocol v0 / optical & controller sub-protocol 0) decoding.
class LegacyDecoder {
 public:
  // timeMsb: packet byte 0; data: [timeLsb, payload...]; len = packet byte 1.
  bool Decode(uint8_t timeMsb, const uint8_t* data, int len, WirelessPacket* out) const;
  static bool DecodeLightcap(const uint8_t* p, int n, WirelessPacket* out);
  static uint32_t ReconstructTs(uint8_t timeMsb, uint32_t ref, uint32_t ts24);
};

// Wv2 (protocol v1) decoding; keeps the sub-protocol state.
class Wv2Decoder {
 public:
  bool Decode(uint8_t timeMsb, const uint8_t* data, int len, WirelessPacket* out);
  int opticalSub = 0xff;     // +0xdc
  int controllerSub = 0xff;  // +0xe0
};

}  // namespace lh::usb
