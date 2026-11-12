// Disambiguated optical data and base-station information.
//
// OpticalFrame is one sweep of one base station axis: every sensor hit during that
// sweep with its angle. It mirrors the driver's 0x68-byte frame record (see
// ai/driver/disambiguator_tdm.md) and the lighthouse.pb.COpticalFrame recording format.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "lh/types.hpp"

namespace lh {

struct OpticalSample {
  int sensor = 0;
  int axis = 0;
  DeviceTime time = 0;   // pulse start
  double angle = 0;      // radians, 2π · (pulse mid − sync) / (nextSync − sync)
  double width = 0;      // seconds
  uint32_t flags = 0;
  uint32_t flags2 = 0;   // kSampleSorted
};

// flags2 (not stock): a 2.0 station's hit picked out of LH1 pulses (tracking/pulse_sorter.hpp),
// in a room with both kinds of base station. Its timing scatters more than an LH2 sensor word's.
constexpr uint32_t kSampleSorted = 1u << 0;

struct OpticalFrame {
  uint32_t baseSerial = 0;   // OOTX id (0 until the base's OOTX has been decoded)
  int axis = 0;
  DeviceTime syncTime = 0;       // "left" sync
  DeviceTime nextSyncTime = 0;   // "right" sync (measured or predicted)
  double syncWidth = 0;
  double nominalSyncWidth = 0;   // width class center
  double idealPeriod = 0;
  double averagePeriod = 0;
  double previousPeriod = 0;
  int ootxBit = 0;               // data bit carried by this sync
  int frameCounter = 0;
  bool leftSyncIsReal = true;
  bool rightSyncIsReal = false;
  bool highFrameJitter = false;
  int uniqueBaseId = 0;          // small id assigned when the base was first seen
  std::vector<OpticalSample> samples;
};

// Base station factory calibration decoded from OOTX (vortex base config record).
// cal: 7 values per axis: tilt, phase, curve, gibphase, gibmag, ogeephase, ogeemag.
struct BaseConfig {
  bool valid = false;
  uint32_t serial = 0;
  std::vector<double> cal;   // 14 values
  uint8_t hardwareVersion = 0;
  uint8_t ootxVersion = 0;   // fw_version & 0x3f
  // Measurement model (CSensorPositionAngular type): 1 = LH1 sweeps, 2-5 = LH2 planes.
  int model = 1;
};

// Measurement model type per base hardware version (CSensorPositionAngular +0x14, set by
// the functor factories 0x34a940/0x34b110 from table 0x1a2840 at hw - 7): hardware 9
// (LH1) and 12 -> 1, 7/8 -> 2, 10 -> 3, 11 -> 4, 13-16 -> 5 (2.0 base stations report
// 14/15), anything else -> 1.
inline int ModelForHardware(int hardwareVersion) {
  static constexpr int kTable[10] = {2, 2, 1, 3, 4, 1, 5, 5, 5, 5};
  const unsigned i = unsigned(hardwareVersion - 7);
  return i < 10 ? kTable[i] : 1;
}

// Dynamic base state decoded from OOTX (0x3b6f00).
struct BaseDynamicState {
  uint16_t firmwareVersion = 0;
  uint8_t sysUnlockCount = 0;
  std::array<float, 3> accelDir{};  // normalized gravity direction in base frame
  int mode = 0;                     // 1 = A, 2 = B, 3 = C, 4 = SOB, 0 = unknown
  int faults = 0;
  uint16_t extra = 0;               // payload bytes 0x29..0x2a
  int modeParam = 0;
  // lhbs multi-station mode (mode byte 0x80 | all << 6 | (N-1) << 4 | slot on hw != 0x0D).
  bool multi = false, multiAll = false;
  int multiN = 0, multiSlot = -1;
  int multiMask = 0;  // lhbs: slots in the round (bit s = slot s), 0 unknown
  int sobChannel = -1;  // LH2: channel 0-15 (SteamVR shows 1-16), -1 unknown
  // Recalled from a stored frame (fast identification), not decoded now: its gravity
  // direction is from an earlier session.
  bool cached = false;
};

}  // namespace lh
