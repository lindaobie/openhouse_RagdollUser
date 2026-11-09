// Differential test: original CDisambiguatorTDM (from driver_lighthouse.so) vs
// lh::tdm::TdmDisambiguator on a real capture. Every frame must match.
#include <gtest/gtest.h>

#include <cmath>
#include <cstring>
#include <filesystem>

#include "tracking/disambiguator_tdm.hpp"
#include "tracking/tracking_source.hpp"
#include "lh/input_sink.hpp"
#include "oracle.hpp"
#include "raw_capture.hpp"
#include "usb_transport.hpp"

namespace {

struct OrigSample { int32_t sensor, axis; double time, angle, width; uint32_t flags, flags2; };
struct OrigFrame {
  int32_t baseSerial, axis;
  double syncTime, nextSyncTime, syncWidth, nominalSyncWidth, idealPeriod, averagePeriod, previousPeriod;
  int32_t ootxBits, frameCounter;
  uint8_t leftSyncIsReal, rightSyncIsReal, highFrameJitter, pad;
  int32_t uniqueBaseId;
  OrigSample *begin, *end, *cap;
};
static_assert(sizeof(OrigFrame) == 0x68);

struct Collect : lh::InputSinkBase {
  std::string want;
  struct Ev { int kind; double t, w; int sensor; };
  std::vector<Ev> evs;
  double last = 0;
  // Start after the last stream gap > 0.5 s (wireless protocol handshake at capture start).
  void Add(const Ev& e) {
    if (!evs.empty() && e.t - last > 0.5) evs.clear();
    last = e.t;
    evs.push_back(e);
  }
  void OnLightPulse(const lh::DeviceId& d, const lh::LightPulse& p) override {
    if (d == want) Add({0, p.time, p.width, p.sensor});
  }
  void OnImu(const lh::DeviceId& d, const lh::ImuSample& s) override {
    if (d == want) Add({1, s.time, 0, 0});
  }
};

class Original {
 public:
  explicit Original(int sensorType) {
    struct Cfg { uint8_t b[8]; uint8_t flag; uint8_t pad[7]; } cfg{};
    cfg.b[0] = 1; cfg.b[3] = 1; cfg.b[4] = 16;
    obj_ = oracle::Fn<void* (*)(Cfg*, int)>(0x323e00)(&cfg, sensorType);
    vt_ = *reinterpret_cast<void***>(obj_);
  }
  unsigned OnPulse(int s, double t, double w) { return reinterpret_cast<unsigned (*)(void*, int, double, double)>(vt_[8])(obj_, s, t, w); }
  unsigned Advance(double t) { return reinterpret_cast<unsigned (*)(void*, double)>(vt_[11])(obj_, t); }
  bool PopFrame(OrigFrame* f) { return reinterpret_cast<bool (*)(void*, OrigFrame*)>(vt_[16])(obj_, f); }
  struct OrigCfg { uint8_t valid; uint32_t id; double* b; double* e; double* c; uint8_t hw, ver; uint8_t pad[6]; };
  bool PopConfig(OrigCfg* cfg) { return reinterpret_cast<bool (*)(void*, void*, void*)>(vt_[15])(obj_, cfg, nullptr); }
 private:
  void* obj_;
  void** vt_;
};

struct Case { const char* capture; const char* device; int sensorType; bool source = false; };
class TdmDiff : public ::testing::TestWithParam<Case> {};

TEST_P(TdmDiff, FramesMatchOriginal) {
  const Case tc = GetParam();
  const std::string cap = std::string(LH_SOURCE_DIR) + "/captures/" + tc.capture;
  if (!std::filesystem::exists(cap)) GTEST_SKIP() << "no capture";
  Collect c;
  c.want = tc.device;
  lh::usb::RawCaptureReader r;
  ASSERT_TRUE(r.Open(cap));
  lh::usb::ReportDecoder dec(&c);
  r.ReadAll([&](const lh::usb::EndpointInfo& e) { dec.AddEndpoint(e); },
            [&](const lh::usb::RawReport& rep) { dec.OnReport(rep); });

  Original orig(tc.sensorType);
  lh::tdm::TdmDisambiguator mine({}, tc.sensorType);
  lh::TrackingSource source(tc.sensorType);
  const auto popConfig = [&](lh::BaseConfig* out) {
    return tc.source ? source.PopBaseConfig(out, nullptr) : mine.PopBaseConfig(out, nullptr);
  };
  const auto popFrame = [&](lh::OpticalFrame* out) {
    return tc.source ? source.PopFrame(out) : mine.PopFrame(out);
  };
  int frames = 0, configs = 0;
  double lastPulse = 0, nextAdvance = 0;
  double latestImu = 0, lastPulseImu = 0;
  for (size_t i = 0; i < c.evs.size(); i++) {
    const auto& e = c.evs[i];
    unsigned fo = 0, fm = 0;
    if (e.kind == 0) {
      fo = orig.OnPulse(e.sensor, e.t, e.w);
      if (tc.source) {
        source.OnLightPulse({e.t, e.w, e.sensor});
        lastPulseImu = latestImu;
      } else {
        fm = mine.OnPulse(e.sensor, e.t, e.w);
      }
      lastPulse = e.t;
    } else if (tc.source) {
      lh::ImuSample sample;
      sample.time = e.t;
      source.OnImu(sample);
      // Independent transcription of SwSync_OnImu's branch conditions.
      if (lastPulseImu != latestImu && (nextAdvance == 0 || e.t > nextAdvance)) {
        if (lastPulseImu + 0.02 < e.t) orig.Advance(e.t - 0.02);
        nextAdvance = e.t + 0.05;
      }
      latestImu = e.t;
      lh::ImuSample queued;
      ASSERT_TRUE(source.PopImu(&queued));
      EXPECT_EQ(queued.time, e.t);
    } else if (lastPulse != 0 && e.t > nextAdvance) {
      if (lastPulse + 0.02 < e.t) {
        fo = orig.Advance(e.t - 0.02);
        fm = mine.Advance(e.t - 0.02);
      }
      nextAdvance = e.t + 0.05;
    }
    if (!tc.source) {
      ASSERT_EQ(fo, fm) << "event " << i << " t=" << e.t;
    }
    Original::OrigCfg oc{};
    while (orig.PopConfig(&oc)) {
      lh::BaseConfig mc;
      ASSERT_TRUE(popConfig(&mc)) << "missing config at event " << i;
      EXPECT_EQ(oc.id, mc.serial);
      EXPECT_EQ(oc.hw, mc.hardwareVersion);
      EXPECT_EQ(oc.ver, mc.ootxVersion);
      ASSERT_EQ(size_t(oc.e - oc.b), mc.cal.size());
      for (size_t k = 0; k < mc.cal.size(); k++) EXPECT_EQ(oc.b[k], mc.cal[k]);
      if (configs < 2) {
        printf("base %08X hw=%d ver=%d cal:", mc.serial, mc.hardwareVersion, mc.ootxVersion);
        for (double v : mc.cal) printf(" %.6g", v);
        printf("\n");
      }
      configs++;
      oc = {};
    }
    ASSERT_FALSE(popConfig(nullptr)) << "extra config at event " << i;
    OrigFrame of;
    memset(&of, 0, sizeof of);
    while (orig.PopFrame(&of)) {
      lh::OpticalFrame mf;
      ASSERT_TRUE(popFrame(&mf)) << "missing frame at event " << i;
      EXPECT_EQ(uint32_t(of.baseSerial), mf.baseSerial);
      EXPECT_EQ(of.axis, mf.axis);
      EXPECT_EQ(of.syncTime, mf.syncTime);
      EXPECT_EQ(of.nextSyncTime, mf.nextSyncTime);
      EXPECT_EQ(of.syncWidth, mf.syncWidth);
      EXPECT_EQ(of.nominalSyncWidth, mf.nominalSyncWidth);
      EXPECT_EQ(of.idealPeriod, mf.idealPeriod);
      EXPECT_EQ(bool(of.leftSyncIsReal), mf.leftSyncIsReal);
      EXPECT_EQ(of.averagePeriod, mf.averagePeriod);
      EXPECT_EQ(of.previousPeriod, mf.previousPeriod);
      EXPECT_EQ(of.ootxBits, mf.ootxBit);
      EXPECT_EQ(of.frameCounter, mf.frameCounter);
      EXPECT_EQ(bool(of.rightSyncIsReal), mf.rightSyncIsReal);
      EXPECT_EQ(bool(of.highFrameJitter), mf.highFrameJitter);
      EXPECT_EQ(of.uniqueBaseId, mf.uniqueBaseId);
      const size_t n = size_t(of.end - of.begin);
      ASSERT_EQ(n, mf.samples.size()) << "frame " << frames;
      for (size_t k = 0; k < n; k++) {
        EXPECT_EQ(of.begin[k].sensor, mf.samples[k].sensor);
        EXPECT_EQ(of.begin[k].axis, mf.samples[k].axis);
        EXPECT_EQ(of.begin[k].flags, mf.samples[k].flags);
        // The high word at +0x24 is uninitialized in TDM's producer
        // (0x3273e0, uStack_144), not deterministic tracking output.
        EXPECT_EQ(mf.samples[k].flags2, 0u);
        EXPECT_EQ(of.begin[k].time, mf.samples[k].time);
        EXPECT_EQ(of.begin[k].angle, mf.samples[k].angle);
        EXPECT_EQ(of.begin[k].width, mf.samples[k].width);
      }
      if (HasFailure()) FAIL() << "first mismatch at frame " << frames << " event " << i;
      frames++;
      memset(&of, 0, sizeof of);
    }
    ASSERT_FALSE(popFrame(nullptr)) << "extra frame at event " << i;
  }
  EXPECT_GT(frames, 300);
  EXPECT_GT(configs, 0);
  printf("compared %d frames, %d base configs\n", frames, configs);
}

INSTANTIATE_TEST_SUITE_P(Captures, TdmDiff,
                         ::testing::Values(Case{"hmd_static_20s.lhraw", "LHR-0704E07A", 0},
                                           Case{"controller_static_10s.lhraw", "LHR-0704E07A", 0},
                                           Case{"controller_static_10s.lhraw", "LHR-13DA81D1", 9},
                                           Case{"hmd_static_20s.lhraw", "LHR-0704E07A", 0, true},
                                           Case{"controller_static_10s.lhraw", "LHR-13DA81D1", 9, true},
                                           Case{"session-20261016-142931Z-147083/input.lhraw", "LHR-13DA81D1", 9},
                                           Case{"session-20261016-142931Z-147083/input.lhraw", "LHR-13DA81D1", 9, true}));

}  // namespace
