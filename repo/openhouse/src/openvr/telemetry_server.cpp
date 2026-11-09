#include "telemetry_server.hpp"

#include <chrono>
#include <nlohmann/json.hpp>

#include "lh/telemetry.hpp"
#include "lh/version.hpp"

#ifdef _WIN32
#include <process.h>
#define LH_GETPID _getpid
#else
#include <unistd.h>
#define LH_GETPID getpid
#endif

namespace lh::openvr {
namespace {
using json = nlohmann::json;

const char* RoleName(DeviceRole role) {
  switch (role) {
    case DeviceRole::Hmd: return "hmd";
    case DeviceRole::Controller: return "controller";
    case DeviceRole::Tracker: return "tracker";
    case DeviceRole::BaseStation: return "base";
  }
  return "tracker";
}

const char* ResultName(TrackingResult result) {
  switch (result) {
    case TrackingResult::CalibratingInProgress: return "calibrating";
    case TrackingResult::CalibratingOutOfRange: return "calibrating, out of range";
    case TrackingResult::RunningOk: return "ok";
    case TrackingResult::RunningOutOfRange: return "out of range";
    default: return "uninitialized";
  }
}

const char* StateName(TrackingState state) {
  switch (state) {
    case TrackingState::Disconnected: return "disconnected";
    case TrackingState::Uninitialized: return "uninitialized";
    case TrackingState::Acquiring: return "acquiring";
    case TrackingState::Tracking: return "tracking";
    case TrackingState::Coasting: return "coasting";
    case TrackingState::Lost: return "lost";
  }
  return "uninitialized";
}

std::string BaseName(uint32_t id) {
  char text[16];
  std::snprintf(text, sizeof text, "LHB-%08X", id);
  return text;
}

json Vec(const std::array<double, 3>& v) { return json::array({v[0], v[1], v[2]}); }
}  // namespace

bool TelemetryServer::Start(int port, std::string host, std::string* error) {
  if (thread_.joinable() || port <= 0) return false;
  listener_ = net::ListenLoopback(port, error);
  if (listener_ == net::kInvalid) return false;
  host_ = std::move(host);
  stop_ = false;
  thread_ = std::thread([this] { Main(); });
  return true;
}

void TelemetryServer::Stop() {
  stop_ = true;
  wake_.notify_all();
  if (thread_.joinable()) thread_.join();
  std::lock_guard lock(mutex_);
  for (auto& c : clients_) net::Close(c.socket);
  clients_.clear();
  net::Close(listener_);
  listener_ = net::kInvalid;
}

void TelemetryServer::Broadcast(const std::string& line) {
  for (auto& c : clients_) {
    if (c.out.size() > kMaxQueued) continue;  // dropped on the next write pass
    c.out += line;
  }
  if (!clients_.empty()) wake_.notify_all();
}

void TelemetryServer::Log(double time, LogLevel level, const std::string& serial, const std::string& text) {
  if (!thread_.joinable()) return;
  const std::string line =
      json{{"type", "log"}, {"t", time}, {"level", LogLevelName(level)}, {"serial", serial}, {"text", text}}.dump(
          -1, ' ', false, json::error_handler_t::replace) + "\n";
  std::lock_guard lock(mutex_);
  backlog_.push_back(line);
  while (backlog_.size() > kBacklog) backlog_.pop_front();
  Broadcast(line);
}

void TelemetryServer::PublishState(double time, const SessionSnapshot& snapshot, const std::vector<DeviceUpdate>& updates,
                                   const std::vector<Session::Receiver>& receivers, const RoomStatus& room) {
  if (!thread_.joinable()) return;
  {
    std::lock_guard lock(mutex_);
    if (clients_.empty()) return;  // nobody watching: skip the work
  }
  json bases = json::array();
  for (const auto& b : snapshot.bases) {
    const auto& p = b.worldFromBase.position;
    const auto& q = b.worldFromBase.orientation;
    bases.push_back({{"serial", b.serial}, {"channel", b.channel}, {"mode", b.mode}, {"seen", b.seen}, {"placed", b.known},
                     {"origin", b.origin}, {"locked", b.locked}, {"observations", b.observations},
                     {"sigma", b.sigma}, {"learned", b.known && b.learned},
                     {"status", !b.seen && !b.known ? "not seen" : !b.known ? "not placed" : b.learned ? "learned" : "learning"},
                     {"p", json::array({p[0], p[1], p[2]})}, {"q", json::array({q[0], q[1], q[2], q[3]})},
                     {"faults", b.faults}});
  }
  int learned = 0, seenBases = 0;
  for (const auto& b : snapshot.bases) { seenBases += b.seen; learned += b.seen && b.known && b.learned; }
  json devices = json::array();
  for (const auto& d : snapshot.devices) {
    if (d.info.role == DeviceRole::BaseStation) continue;
    const DeviceUpdate* u = nullptr;
    for (const auto& x : updates) if (x.serial == d.info.serial) u = &x;
    const DevicePose& pose = u ? u->pose : d.pose;
    const auto& p = pose.worldFromHead.position;
    const auto& q = pose.worldFromHead.orientation;
    json coverage = json::array();
    int seeing = 0;
    for (const auto& c : pose.coverage) {
      coverage.push_back({{"base", BaseName(c.base)}, {"frames", c.frames}, {"sensors", c.sensors}});
      seeing += c.frames > 0;
    }
    json inFilter = json::array();
    for (uint32_t b : pose.bases) inFilter.push_back(BaseName(b));
    const auto battery = u ? u->batteryPercent : d.info.batteryPercent;
    devices.push_back({{"serial", d.info.serial}, {"role", RoleName(d.info.role)}, {"model", d.info.model},
                       {"hand", d.info.controllerRole}, {"connected", u ? u->connected : d.info.connected},
                       {"enabled", d.info.enabled}, {"battery", battery ? json(*battery) : json(nullptr)},
                       {"charging", u ? u->charging : d.info.charging}, {"state", StateName(pose.state)},
                       {"valid", pose.valid}, {"p", json::array({p[0], p[1], p[2]})},
                       {"q", json::array({q[0], q[1], q[2], q[3]})}, {"v", Vec(pose.velocity)},
                       {"primary", pose.primaryBase ? BaseName(pose.primaryBase) : ""}, {"bases", inFilter},
                       {"coverage", coverage}, {"basesSeen", seeing}, {"result", ResultName(pose.result)}});
  }
  json rx = json::array();
  for (const auto& r : receivers) rx.push_back({{"serial", r.serial}, {"connected", r.connected}});
  const std::string line =
      json{{"type", "state"}, {"t", time},
           {"room", {{"id", snapshot.roomId}, {"settled", room.settled}, {"epoch", room.epoch}, {"settleSigma", room.settleSigma},
                     {"resets", room.resets}, {"learned", learned}, {"seen", seenBases}}},
           {"bases", bases}, {"devices", devices}, {"receivers", rx}}
          .dump(-1, ' ', false, json::error_handler_t::replace) + "\n";
  std::lock_guard lock(mutex_);
  Broadcast(line);
}

void TelemetryServer::Main() {
  while (!stop_) {
    {
      std::unique_lock lock(mutex_);
      for (net::Socket s; (s = net::Accept(listener_)) != net::kInvalid;) {
        Client c{s, json{{"type", "hello"}, {"protocol", telemetry::kProtocolVersion}, {"name", "openhouse"}, {"version", lh::kVersion}, {"host", host_},
                         {"pid", int(LH_GETPID())}}.dump() + "\n"};
        for (const auto& line : backlog_) c.out += line;
        clients_.push_back(std::move(c));
      }
      for (auto it = clients_.begin(); it != clients_.end();) {
        bool drop = it->out.size() > kMaxQueued;
        while (!drop && !it->out.empty()) {
          const long n = net::SendSome(it->socket, it->out.data(), it->out.size());
          if (n < 0) drop = true;
          else if (n == 0) break;
          else it->out.erase(0, size_t(n));
        }
        char sink[256];  // clients send nothing; this notices a closed one
        if (!drop && net::RecvSome(it->socket, sink, sizeof sink) < 0) drop = true;
        if (drop) { net::Close(it->socket); it = clients_.erase(it); }
        else ++it;
      }
      wake_.wait_for(lock, std::chrono::milliseconds(20));
    }
  }
}

}  // namespace lh::openvr
