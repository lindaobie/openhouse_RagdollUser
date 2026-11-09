// Lighthouse 1.0 OOTX ("omnidirectional optical transmitter") decoding.
//
// Three layers, as in the driver:
//  * OotxMajorityRing (0x3b72f0 & co): every sync carries one data bit; the ring
//    accumulates per-slot 0/1 votes over a fixed frame length so the payload can be
//    reconstructed through dropouts.
//  * OotxFramer (0x3df8a0/0x3dfa30/0x3df780): the bit-level packet format
//    (17-zero preamble, '1', then 16-bit words each followed by a '1' stuffing bit).
//  * DecodeOotxPayload (0x3b8170/0x3b7c60/0x3b6f00): CRC check, version/length
//    validation and field extraction.
#pragma once

#include <cstdint>
#include <vector>

#include "lh/optical.hpp"

namespace lh::ootx {

class OotxFramer {
 public:
  void Reset();
  // Pushes one bit; returns true when a complete packet is buffered.
  bool PushBit(bool bit);
  uint16_t PayloadLength() const;  // 0 if not yet known
  // Copies up to maxLen payload bytes; returns the number copied (0 on failure).
  uint16_t CopyPayload(uint8_t* out, uint16_t maxLen) const;
  bool CrcOk(const uint8_t* payload, uint16_t len) const;

 private:
  bool Extract(uint16_t bitPos, uint16_t nBytes, uint8_t* out) const;
  void Resync(bool bit);
  uint8_t bits_[64] = {};  // raw bitstream incl. preamble, MSB first
  uint16_t pos_ = 0;
};

class OotxMajorityRing {
 public:
  explicit OotxMajorityRing(int frameBits = 0) { Init(frameBits); }
  void Init(int frameBits);
  void Reset();
  // bit: 0, 1, or anything else for "sync without data" (still advances).
  // Returns true when a slot becomes decided.
  bool Push(int bit);
  bool Complete() const { return decided_ != 0 && decided_ == int(bits_.size()); }
  const std::vector<uint8_t>& Bits() const { return bits_; }

 private:
  std::vector<uint16_t> counts_;  // [zeros, ones] per slot
  std::vector<uint8_t> bits_;     // majority bit per slot
  int pos_ = 0;                   // index into counts_ (steps by 2)
  int decided_ = 0;
  int pushes_ = 0;
};

// Searches the ring's bits (circularly) for a valid OOTX packet and decodes it.
bool DecodeRing(const OotxMajorityRing& ring, BaseConfig* cfg, BaseDynamicState* dyn);
// Decodes the packet a framer just completed (PushBit returned true).
bool DecodeFramer(const OotxFramer& framer, BaseConfig* cfg, BaseDynamicState* dyn);

// Field extraction from a CRC-checked payload (exposed for tests).
void ParseBaseConfig(const uint8_t* payload, BaseConfig* cfg);
void ParseDynamicState(const uint8_t* payload, BaseDynamicState* dyn);

float HalfToFloat(uint16_t h);

}  // namespace lh::ootx
