#include "ootx.hpp"

#include <zlib.h>

#include <cmath>
#include <cstring>

namespace lh::ootx {
namespace {

// Bit positions > 18 that are multiples of 17 carry the '1' stuffing bit.
inline bool IsStuffing(uint16_t pos) { return pos > 0x12 && pos % 17 == 0; }

uint16_t RdU16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }

// Fault-class table indexed by hw_version - 7 (0x1a3b60).
constexpr int kFaultClass[10] = {2, 2, 1, 3, 4, 1, 5, 5, 5, 5};

}  // namespace

// ---------------------------------------------------------------- framer

void OotxFramer::Reset() {
  std::memset(bits_, 0, sizeof bits_);
  pos_ = 0;
}

bool OotxFramer::PushBit(bool bit) {
  // 0x3dfa30
  const uint16_t pos = pos_;
  if (pos < 17) {
    pos_ = bit ? 0 : pos + 1;
    return false;
  }
  if (pos == 17) {
    if (!bit) return false;
    bits_[2] = 0x40;  // the '1' ending the preamble (bit 17)
    pos_ = 18;
    return false;
  }
  if (pos >= 0x1ff) {
    Resync(bit);
    return false;
  }
  if (pos != 18 && pos % 17 == 0 && !bit) {
    Resync(false);
    return false;
  }
  bits_[pos >> 3] |= uint8_t(uint8_t(bit) << (7 - (pos & 7)));
  pos_ = pos + 1;
  if (pos_ > 0x22) {
    const uint16_t len = PayloadLength();
    const uint16_t words = uint16_t(len + 1 - ((len & 1) == 0)) >> 1;
    return uint16_t(words * 17 + 0x45) == pos_;
  }
  return false;
}

void OotxFramer::Resync(bool bit) {
  // 0x3df8c0: on a bad stuffing bit, keep the zeros seen since the last '1' as preamble.
  const uint16_t p = pos_;
  if (p == 0) return;
  if (bit) {
    Reset();
    return;
  }
  uint32_t u = p;
  uint16_t s;
  for (;;) {
    do {
      s = uint16_t(u);
      u = u - 1;
    } while (p <= uint16_t(u));
    const uint16_t ui = uint16_t(u);
    if (((bits_[ui >> 3] >> (7 - (ui & 7))) & 1) != 0) break;
    s = 0;
    if (ui == 0) break;
  }
  std::memset(bits_, 0, sizeof bits_);
  pos_ = uint16_t(p + 1 - s);
}

bool OotxFramer::Extract(uint16_t bitPos, uint16_t nBytes, uint8_t* out) const {
  // 0x3df780 (out must be zeroed by the caller)
  const uint32_t nBits = uint32_t(nBytes) * 8;
  if (uint32_t(pos_) < uint32_t(bitPos) + nBits) return false;
  if (nBytes == 0 || bitPos >= pos_) return nBytes == 0;
  while (!(bitPos < 0x13 || bitPos % 17 != 0)) {  // skip leading stuffing bits
    bitPos++;
    if (bitPos >= pos_) return false;
  }
  uint32_t k = 0;
  for (;;) {
    const uint32_t j = k++;
    const int b = (bits_[bitPos >> 3] >> (7 - (bitPos & 7))) & 1;
    out[(j & 0xffff) >> 3] |= uint8_t(b << (7 - (j & 7)));
    do {
      if (nBits <= (k & 0xffff)) return nBits == (k & 0xffff);
      bitPos++;
      if (pos_ <= bitPos) return nBits == (k & 0xffff);
    } while (IsStuffing(bitPos));
  }
}

uint16_t OotxFramer::PayloadLength() const {
  // 0x3df950
  if (pos_ <= 0x22) return 0;
  uint8_t b[2] = {0, 0};
  if (!Extract(0x12, 2, b)) return 0;
  return RdU16(b);
}

uint16_t OotxFramer::CopyPayload(uint8_t* out, uint16_t maxLen) const {
  // 0x3dfb20
  const uint16_t len = PayloadLength();
  if (len == 0) return 0;
  std::memset(out, 0, maxLen);
  const uint16_t n = maxLen < len ? maxLen : len;
  return Extract(0x23, n, out) ? n : 0;
}

bool OotxFramer::CrcOk(const uint8_t* payload, uint16_t len) const {
  // 0x3df990
  if (pos_ <= 0x44) return false;
  const uint16_t plen = PayloadLength();
  if (plen != len) return false;
  const uint16_t words = uint16_t(len + ((len & 1) ? 1 : 0)) >> 1;
  uint8_t c[4] = {0, 0, 0, 0};
  if (!Extract(uint16_t(words * 17 + 0x23), 4, c)) return false;
  const uint32_t want = uint32_t(c[0]) | uint32_t(c[1]) << 8 | uint32_t(c[2]) << 16 | uint32_t(c[3]) << 24;
  return want == uint32_t(crc32(0, payload, plen));
}

// ---------------------------------------------------------------- majority ring

void OotxMajorityRing::Init(int frameBits) {
  counts_.assign(size_t(2 * frameBits), 0);
  bits_.assign(size_t(frameBits), 0);
  pos_ = 0;
  decided_ = 0;
  pushes_ = 0;
}

void OotxMajorityRing::Reset() {
  // 0x3b7250
  pos_ = 0;
  decided_ = 0;
  pushes_ = 0;
  std::fill(counts_.begin(), counts_.end(), 0);
  std::fill(bits_.begin(), bits_.end(), 0);
}

bool OotxMajorityRing::Push(int bit) {
  // 0x3b72f0
  if (counts_.empty()) return false;
  const uint16_t z = counts_[pos_], o = counts_[pos_ + 1];
  if (bit == 0) {
    counts_[pos_]++;
    pushes_++;
  } else if (bit == 1) {
    counts_[pos_ + 1]++;
    pushes_++;
  }
  const uint16_t z2 = counts_[pos_], o2 = counts_[pos_ + 1];
  bits_[pos_ / 2] = z2 < o2;
  const bool nowDecided = z == o && z2 != o2;
  if (nowDecided) decided_++;
  else if (z != o && z2 == o2) decided_--;
  pos_ = int((pos_ + 2) % counts_.size());
  return nowDecided;
}

// ---------------------------------------------------------------- payload

float HalfToFloat(uint16_t h) {
  // 0x3df600
  uint32_t mant = h & 0x3ff;
  const uint32_t sign = uint32_t(h >> 15) << 31;
  uint32_t exp = (h >> 10) & 0x1f;
  uint32_t bits;
  if (exp == 0) {
    if (mant == 0) {
      bits = sign;
    } else {
      uint32_t e = 0, prev;
      do {
        prev = e;
        mant <<= 1;
        e = prev - 1;
      } while ((mant & 0x400) == 0);
      mant &= ~0x400u;
      bits = mant << 13 | sign | (prev + 0x70) * 0x800000u;
    }
  } else if (exp == 0x1f) {
    bits = mant ? (mant << 13 | sign | 0x7f800000) : (sign | 0x7f800000);
  } else {
    bits = mant << 13 | sign | (exp + 0x70) * 0x800000u;
  }
  float f;
  std::memcpy(&f, &bits, 4);
  return f;
}

void ParseBaseConfig(const uint8_t* p, BaseConfig* cfg) {
  // 0x3b7c60
  cfg->cal.clear();
  cfg->serial = uint32_t(p[2]) | uint32_t(p[3]) << 8 | uint32_t(p[4]) << 16 | uint32_t(p[5]) << 24;
  cfg->hardwareVersion = p[0xf];
  cfg->ootxVersion = p[0] & 0x3f;
  cfg->model = ModelForHardware(cfg->hardwareVersion);
  for (int axis = 0; axis < 2; axis++) {
    const uint8_t* b = p + 6 + 2 * axis;
    for (int off : {4, 0, 10, 0x11, 0x15, 0x1b, 0x1f}) cfg->cal.push_back(double(HalfToFloat(RdU16(b + off))));
  }
  cfg->valid = true;
}

void ParseDynamicState(const uint8_t* p, BaseDynamicState* d) {
  // 0x3b6f00
  const uint16_t v = RdU16(p);
  const uint8_t hw = p[0xf];
  const bool hwB_or_D = ((uint8_t(hw - 0xb)) & 0xfd) == 0;
  bool zeroAccel = false;
  if ((v & 0x3f) < 5) {
    d->firmwareVersion = 0;
    zeroAccel = hwB_or_D;
  } else {
    const uint32_t u3 = v >> 6;
    if ((v & 0x38) == 0) {
      d->firmwareVersion = uint16_t(u3);
      zeroAccel = hwB_or_D && u3 < 0x34a;
    } else {
      d->firmwareVersion = uint16_t((uint16_t(v >> 11) + 2) * 1000 + (u3 & 0x1f));
    }
  }
  if (zeroAccel) {
    d->accelDir = {0, 0, 0};
  } else {
    const float s = 1.0f / 127.0f;  // 0x1a93a0
    float x = float(int(int8_t(p[0x14]))) * s;
    float y = float(int(int8_t(p[0x15]))) * s;
    float z = float(int(int8_t(p[0x16]))) * s;
    d->accelDir = {x, y, z};
    const float n2 = x * x + y * y + z * z;
    if (n2 != 0.0f) {
      const float k = 1.0f / std::sqrt(n2);
      d->accelDir = {x * k, y * k, z * k};
    }
  }
  d->sysUnlockCount = p[0xe];
  d->mode = 0;
  d->faults = 0;
  d->extra = RdU16(p + 0x29);
  d->modeParam = 0;
  d->multi = false; d->multiAll = false; d->multiN = 0; d->multiSlot = -1; d->multiMask = 0;  // a channel left
  if ((v & 0x3f) < 6 || hw < 4) return;

  const uint16_t m = RdU16(p + 0x1f);
  const uint32_t u3 = m & 0x7f;
  if (hw > 0xd) {
    d->mode = 4;
    d->modeParam = int(u3);
  } else if (hw == 0xd && (v & 0x38) == 0) {
    if (u3 + 1 < 4) {
      d->mode = int(u3 + 1);
    } else {
      d->mode = 4;
      d->modeParam = int(u3) - 3 < 0 ? 0 : int(u3) - 3;
    }
  } else if (hw == 0xd && (m & 0x80)) {
    d->mode = 4;
    d->modeParam = (m & 0x7f) == 0 ? 0 : int(u3);
  } else {
    d->mode = int(u3 + 1);
    if (d->mode > 3) d->mode = 0;
    if (m & 0x80) {
      d->multi = true;
      d->multiAll = (m & 0x40) != 0;  // policy ALL: every flash carries an OOTX bit
      d->multiN = int((m >> 4) & 3) + 1;  // stations in the round (lhbs: found at runtime)
      d->multiSlot = int(m & 0x3);
      d->multiMask = p[0x21] & 0xf;  // which slots take turns (0 in a 33-byte frame: unknown)
    }
  }

  const uint32_t b = p[0x20];
  if (b & 0x80) {
    const uint32_t k = uint32_t(hw) - 7;
    uint32_t mask;
    if (k < 10 && kFaultClass[k] != 1) {
      mask = 0xffffff7f;
      if (uint32_t(kFaultClass[k] - 2) < 4) {
        uint32_t f = (b * 8 & 0x10) | (b * 4 & 4);
        if (b & 8) f |= (b & 4) == 0 ? 0x40u : 0x80u;
        d->faults = int(f);
        mask = 0xffffff70;
      }
    } else {
      mask = 0xffffff4c;
      d->faults = int((b & 0x20) | ((b >> 1) & 8) | (b * 4 & 4) | (b * 8 & 0x10));
    }
    if (b & mask) d->faults |= 2;
  }
}

bool DecodeRing(const OotxMajorityRing& ring, BaseConfig* cfg, BaseDynamicState* dyn) {
  // 0x3b8170
  const auto& bits = ring.Bits();
  const size_t n = bits.size();
  if (n == 0) return false;
  OotxFramer f;
  bool found = false;
  for (size_t i = 0; i < 2 * n; i++) {
    if (f.PushBit(bits[i % n] != 0)) {
      found = true;
      break;
    }
  }
  if (!found) return false;
  return DecodeFramer(f, cfg, dyn);
}

bool DecodeFramer(const OotxFramer& f, BaseConfig* cfg, BaseDynamicState* dyn) {
  uint8_t payload[48] = {};
  const uint16_t len = f.CopyPayload(payload, 0x2b);
  if (!f.CrcOk(payload, len)) return false;
  const int ver = payload[0] & 0x3f;
  const bool lengthOk = (len == 0xe && ver == 1) || (len == 0x10 && ver == 2) || (len == 0x17 && ver == 3) ||
                        ((ver == 4 || ver == 5) && len == 0x1f) || (len == 0x21 && ver == 6) ||
                        (len == 0x22 && ver == 6 && (payload[0x1f] & 0x80)) ||  // lhbs multi: + the round
                        (len == 0x29 && ver == 7) || (len == 0x2b && ver == 8);
  if (!lengthOk) return false;
  if (cfg) ParseBaseConfig(payload, cfg);
  if (dyn) ParseDynamicState(payload, dyn);
  return true;
}

}  // namespace lh::ootx
