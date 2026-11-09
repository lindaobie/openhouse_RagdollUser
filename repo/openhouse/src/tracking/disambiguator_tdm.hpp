// Lighthouse 1.0 TDM disambiguator — port of vortex::(anon)::CDisambiguatorTDM
// (vtable 0x4711d0, factory 0x323e00). See ai/driver/disambiguator_tdm.md.
//
// Input: raw light pulses (sensor, start time, width) from one tracked device.
// Output: OpticalFrames (one per base-station sweep, with per-sensor angles),
// decoded base configs (OOTX), events and log lines.
//
// Algorithm summary:
//  1. Every pulse is stored per sensor. Pulses whose width is a valid LH1 sync width
//     are clustered by phase (fmod(t, 1/60) / (1/60)).
//  2. Every 0.125 s, pairs of clusters half a period apart (the two axis syncs of one
//     base) that are seen consistently become base stations ("sync timing lock").
//  3. Sync flashes are predicted every half period; candidates (sync-width pulses seen
//     by several sensors at the same time) near the prediction are matched to bases,
//     update their timing (period, jitter) and feed their OOTX data bit.
//  4. The base whose sync says "not skip" sweeps this half period: every pulse between
//     sync+P/12 and sync+5P/12 becomes a sample with angle 2π·(mid−sync)/(next−sync).
#pragma once

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "lh/optical.hpp"
#include "tracking/ootx.hpp"

namespace lh::tdm {

// Disambiguator configuration bytes (built by 0x3cb080 from the
// "usedisambiguation"/"disambiguationdebug" settings).
struct TdmConfig {
  bool rightSync = true;          // +8: measure the "right" sync (frame end) instead of predicting
  bool emitBeforeOotx = false;    // +9: emit frames for bases whose serial is still unknown
  bool debugLog = false;          // +0xa
  bool flag3 = true;              // +0xb
  int maxQueue = 16;              // +0xc: capacity of frame/config/event/log queues
  bool flag10 = false;            // +0x10
};

// Sync pulse classification (0x3e2fc0).
struct SyncInfo {
  int status = 2;          // 0 ok, 1 too long, 2 too short, 3 between classes, 4 unclassified
  bool notSkip = false;    // this base sweeps during this half period
  int axis = -1;
  int data = 0xff;         // OOTX data bit
  int base = 0xff;
  double nominal = 0;      // center of the width class
};

// Per-sensor-type width offset (0x3e2e20).
double SensorTypeWidthOffset(int sensorType);
// Which sensor type's width offset explains these observed sync widths best
// (the stock driver probes the device before it knows its type). 0 if none.
int InferSensorTypeFromSyncWidths(const std::vector<double>& widths);
bool ClassifySync(double width, double widthOffset, bool absolute, SyncInfo* out);

struct TdmEvent {
  double time = 0;
  int type = 0;  // 8: >2 bases, 9: bases phase-locked, 10: two slave bases
};

struct TdmStats {
  int invalidLong = 0, badSensor = 0, runt = 0, total = 0, syncMiss = 0, guardBand = 0;
};

class TdmDisambiguator {
 public:
  explicit TdmDisambiguator(const TdmConfig& cfg = {}, int sensorType = 0);

  void SetSensorType(int sensorType);
  int SensorType() const { return sensorType_; }

  // IDisambiguator slot 8 / 11. Returns flags: bit0 = new base config, bit1 = new frame.
  unsigned OnPulse(int sensor, double t, double width);
  unsigned Advance(double t);

  bool PopFrame(OpticalFrame* out);
  bool PopBaseConfig(BaseConfig* cfg, BaseDynamicState* dyn);
  bool PopEvent(TdmEvent* out);
  bool PopLog(std::string* out);
  bool HasOutput() const;
  double EarliestPendingTime() const;  // slot 19
  void Reset();                        // slot 13
  const TdmStats& Stats() const { return stats_; }

  // ---- fast base identification (extension, inactive unless identities are set) --
  // A base's OOTX frame repeats forever, so ~40 data bits already pin down which
  // known base is sweeping. Identities learned by any device (or a previous session)
  // let this device label a base long before it could decode a whole frame.
  struct Identity {
    BaseConfig config;
    BaseDynamicState dynamic;
    std::vector<uint8_t> frame;  // one full OOTX frame of data bits (cyclic)
  };
  void SetKnownIdentities(std::vector<Identity> identities) { known_ = std::move(identities); }
  // Re-derive the width offset from widths observed so far when the configured
  // type yields no valid classification (see ai/driver/sensor_type.md).
  int RetrySensorType();
  bool PopIdentity(Identity* out);  // frames this disambiguator decoded itself
  std::string StatsString(bool reset);

 private:
  struct Cluster {
    double sum = 0;
    int count = 0;
    double cachedAvg = 0;
    int cachedCount = 0;
    double lastTime = 0;
    double Avg();
  };
  struct Candidate {
    double start = 0, end = 0;
    uint32_t mask = 0, mask2 = 0;
    SyncInfo sync;
  };
  struct PulseStore {
    std::vector<double> starts, ends;
    size_t cap = 0x100;
  };
  struct StatAcc {
    int n = 0;
    double sum = 0, sumsq = 0;
    void Add(double v) { n++; sum += v; sumsq += v * v; }
  };
  struct Base {
    uint8_t syncSeen[2] = {0, 0};
    uint8_t jitter[2] = {0, 0};
    double lastSync[2] = {0, 0};
    double period = 0;
    double avgPeriod[2] = {0, 0};
    double prevDt[2] = {0, 0};
    ootx::OotxMajorityRing ring[2];
    uint32_t serial = 0;
    std::deque<uint8_t> recentBits;  // for Identify (raw data values, >1 = no data)
    uint8_t uniqueId = 0;
    int mode = 0;
    int counterBase = -1;
    double counterT0 = 0, counterRate = 0;
    StatAcc periodStats[2], syncSensors[2], multihit[2], sampleSensors[2], widthDelta[2];
    int Counter(double t) const;
  };

  void CheckSyncLost(double t);          // 0x324ba0
  void UpdateBases(double t);            // 0x325260
  void ScanSyncCandidates(double t);     // 0x323430
  unsigned ProcessExpectedSync(double t);  // 0x3273e0
  bool SanityCheck(double t);            // 0x324c00
  void Gather(double lo, double hi, std::vector<Candidate>* out) const;  // 0x322e50
  void PendingInWindow(double lo, double hi, std::vector<Candidate>* out) const;  // 0x3224d0
  unsigned PushOotx(Base& b, int bit);   // 0x326c60
  unsigned Identify(Base& b, int bit);
  void Log(const std::string& s);
  void PushEvent(double t, int type);
  template <typename T>
  void PushBounded(std::deque<T>& q, T v) {
    if (int(q.size()) >= cfg_.maxQueue && !q.empty()) q.pop_front();
    q.push_back(std::move(v));
  }

  TdmConfig cfg_;
  int sensorType_ = 0;
  double widthOffset_ = 0;
  double idealPeriod_ = 1.0 / 60.0;       // +0x18
  double period_ = 1.0 / 60.0;            // +0x20
  std::vector<Cluster> clusters_;         // +0x28
  double lastClusterCheck_ = 0;           // +0x40
  double lastSyncTime_ = 0;               // +0x48
  uint8_t nextUniqueId_ = 0;              // +0x50
  double nextExpectedSync_ = 0;           // +0x58
  double scanTime_ = 0;                   // +0x60
  std::vector<Candidate> pending_;        // +0x68
  PulseStore stores_[32];                 // +0x80
  std::vector<Base> bases_;               // +0x780
  float avgNotSkip_ = 0;                  // +0x7a8
  int syncCount_ = 0;                     // +0x7ac
  std::deque<std::pair<BaseConfig, BaseDynamicState>> configs_;  // +0x7b8
  std::deque<OpticalFrame> frames_;       // +0x808
  std::deque<std::string> logs_;          // +0x858
  std::deque<TdmEvent> events_;           // +0x8a8
  TdmStats stats_;                        // +0x8f8..
  std::vector<Identity> known_;
  std::deque<Identity> learned_;
  std::vector<double> widthSample_;  // recent sync-width candidates
};

}  // namespace lh::tdm
