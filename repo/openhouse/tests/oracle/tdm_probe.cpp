// Feeds pulses from a .lhraw capture into the ORIGINAL CDisambiguatorTDM
// (instantiated from driver_lighthouse.so) and prints the frames it produces.
//   tdm_probe CAPTURE.lhraw [DEVICE_SERIAL] [MAX_FRAMES]
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "lh/input_sink.hpp"
#include "oracle.hpp"
#include "raw_capture.hpp"
#include "usb_transport.hpp"

namespace {

// Original frame layout (0x68 bytes), see ai/driver/disambiguator_tdm.md.
struct OrigSample {
  int32_t sensor, axis;
  double time, angle, width;
  uint32_t flags, flags2;
};
struct OrigFrame {
  int32_t baseSerial, axis;
  double syncTime, nextSyncTime, syncWidth, nominalSyncWidth, idealPeriod, averagePeriod, previousPeriod;
  int32_t ootxBits, frameCounter;
  uint8_t leftSyncIsReal, rightSyncIsReal, highFrameJitter, pad;
  int32_t uniqueBaseId;
  OrigSample* begin;
  OrigSample* end;
  OrigSample* cap;
};
static_assert(sizeof(OrigFrame) == 0x68);

struct Collect : lh::InputSinkBase {
  std::string want;
  struct Ev { int kind; double t; double w; int sensor; };  // 0 pulse, 1 imu
  std::vector<Ev> evs;
  void OnLightPulse(const lh::DeviceId& d, const lh::LightPulse& p) override {
    if (d == want) evs.push_back({0, p.time, p.width, p.sensor});
  }
  void OnImu(const lh::DeviceId& d, const lh::ImuSample& s) override {
    if (d == want) evs.push_back({1, s.time, 0, 0});
  }
};

}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) return 2;
  Collect c;
  c.want = argc > 2 ? argv[2] : "LHR-0704E07A";
  int maxFrames = argc > 3 ? atoi(argv[3]) : 40;
  lh::usb::RawCaptureReader r;
  if (!r.Open(argv[1])) return 1;
  lh::usb::ReportDecoder dec(&c);
  r.ReadAll([&](const lh::usb::EndpointInfo& e) { dec.AddEndpoint(e); },
            [&](const lh::usb::RawReport& rep) { dec.OnReport(rep); });
  fprintf(stderr, "%zu events\n", c.evs.size());

  // Config bytes as built by 0x3cb080 with disambiguationdebug = 0.
  struct Cfg { uint8_t b[8]; uint8_t flag; uint8_t pad[7]; } cfg{};
  cfg.b[0] = 1; cfg.b[3] = 1; cfg.b[4] = 16;
  using Factory = void* (*)(Cfg*, int);
  void* tdm = oracle::Fn<Factory>(0x323e00)(&cfg, 0 /*sensorType HMD*/);
  void** vt = *reinterpret_cast<void***>(tdm);
  using OnPulseFn = unsigned (*)(void*, int, double, double);
  using AdvanceFn = unsigned (*)(void*, double);
  using PopFrameFn = bool (*)(void*, OrigFrame*);
  auto onPulse = reinterpret_cast<OnPulseFn>(vt[8]);
  auto advance = reinterpret_cast<AdvanceFn>(vt[11]);
  auto popFrame = reinterpret_cast<PopFrameFn>(vt[16]);

  int frames = 0;
  double lastPulse = 0, nextAdvance = 0;
  for (auto& e : c.evs) {
    if (e.kind == 0) {
      onPulse(tdm, e.sensor, e.t, e.w);
      lastPulse = e.t;
    } else if (lastPulse != 0 && e.t > nextAdvance) {
      if (lastPulse + 0.02 < e.t) advance(tdm, e.t - 0.02);
      nextAdvance = e.t + 0.05;
    }
    OrigFrame f;
    memset(&f, 0, sizeof f);
    while (popFrame(tdm, &f)) {
      if (frames < maxFrames) {
        printf("frame base=%08X ub=%d axis=%d sync=%.6f next=%.6f sw=%.2fus per=%.6f avg=%.6f prev=%.6f ootx=%d cnt=%d real=%d/%d jit=%d n=%zu\n",
               f.baseSerial, f.uniqueBaseId, f.axis, f.syncTime, f.nextSyncTime, f.syncWidth * 1e6, f.idealPeriod,
               f.averagePeriod, f.previousPeriod, f.ootxBits, f.frameCounter, f.leftSyncIsReal, f.rightSyncIsReal,
               f.highFrameJitter, size_t(f.end - f.begin));
        for (OrigSample* s = f.begin; s != f.end && s - f.begin < 3; s++)
          printf("   s%d axis=%d t=%.6f angle=%.6f w=%.2fus flags=%x/%x\n", s->sensor, s->axis, s->time, s->angle,
                 s->width * 1e6, s->flags, s->flags2);
      }
      frames++;
      memset(&f, 0, sizeof f);
    }
  }
  printf("total frames %d\n", frames);
}
