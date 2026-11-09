// Per-device LH1 source: normalized transport events -> tracker input queues.
// Port of the TDM path of CSwSyncDetectSource; no USB or host clock conversion.
// LH2 words go to the sync-on-beam disambiguator (disambiguator_sob.hpp).
#pragma once

#include <deque>
#include <mutex>

#include "lh/types.hpp"
#include "tracking/disambiguator_multi.hpp"
#include "tracking/disambiguator_sob.hpp"
#include "tracking/disambiguator_tdm.hpp"

namespace lh {

class TrackingSource {
 public:
  // inferSensorType: re-derive the type from measured sync widths while it is 0.
  // Off when the type came from the device's hardware id (report 0x05).
  explicit TrackingSource(int sensorType = 0, const tdm::TdmConfig& config = {}, bool inferSensorType = true);

  void OnImu(const ImuSample& sample);
  void OnLightPulse(const LightPulse& pulse);
  // LH2 (2.0 base stations): device-decoded beam and sensor words.
  void OnLh2BeamWord(const Lh2BeamWord& word);
  void OnLh2SensorWord(const Lh2SensorWord& word);

  bool PopImu(ImuSample* out);
  bool PeekImuTime(DeviceTime* out) const;
  bool PopFrame(OpticalFrame* out);
  const tdm::TdmStats& Stats() const { return tdm_.Stats(); }
  bool PopBaseConfig(BaseConfig* config, BaseDynamicState* dynamic);
  bool PopEvent(tdm::TdmEvent* out);
  bool PopLog(std::string* out);
  // Fast base identification (see TdmDisambiguator::Identity).
  void SetKnownIdentities(std::vector<tdm::TdmDisambiguator::Identity> identities);
  bool PopIdentity(tdm::TdmDisambiguator::Identity* out);
  void SetKnownSobChannels(const std::map<int, std::pair<BaseConfig, BaseDynamicState>>& channels);

 private:
  // The original stores float components, even though transport callbacks use doubles.
  struct QueuedImu {
    double time;
    std::array<float, 3> accel, gyro;
    uint32_t saturated;
  };
  mutable std::mutex mutex_;
  bool inferSensorType_ = true;
  tdm::TdmDisambiguator tdm_;
  // Runs beside the stock disambiguator; takes over once it detects lhbs multi mode.
  tdm::MultiDisambiguator multi_;
  // LH2 light; a device reports either LH1 pulses or LH2 words (its light mode).
  sob::SobDisambiguator sob_;
  std::deque<QueuedImu> imu_;
  double latestImuTime_ = 0;
  double lastPulseImuTime_ = 0;
  double nextAdvanceCheck_ = 0;
};

}  // namespace lh
