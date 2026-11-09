#include "lh/log_level.hpp"

#include <cstdio>

#include "lh/input_sink.hpp"
#include "lh/telemetry.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>

namespace lh {
namespace {
bool Contains(std::string_view text, std::string_view part) { return text.find(part) != std::string_view::npos; }

std::string Lower(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return char(std::tolower(c)); });
  return out;
}
}  // namespace

const char* LogLevelName(LogLevel level) {
  switch (level) {
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Info: return "INFO";
    case LogLevel::Warn: return "WARN";
    case LogLevel::Error: return "ERR";
  }
  return "INFO";
}

bool ParseLogLevel(std::string_view text, LogLevel* level) {
  const std::string s = Lower(text);
  if (s == "debug") *level = LogLevel::Debug;
  else if (s == "info") *level = LogLevel::Info;
  else if (s == "warn" || s == "warning") *level = LogLevel::Warn;
  else if (s == "err" || s == "error") *level = LogLevel::Error;
  else return false;
  return true;
}

LogLevel ClassifyLog(std::string_view text) {
  // Light decoding and per-device bookkeeping: useful when digging, noise otherwise.
  static constexpr std::array<std::string_view, 14> kDebug = {
      "[multi]", "[lh2]", "added base", "-> coasting", "coasting ->", "handed over to base", "relationship taken",
      "loosened their relationship", "Room solver: base", "room solve:", "sync stats:", "protocol versioning",
      "set up in", "haptic "};
  static constexpr std::array<std::string_view, 15> kWarn = {
      "Resetting tracking", "disagrees", "knocked", "is stale", "out of bounds", "-> lost", "rejected",
      "moved by", "relearning", "dropped a malformed", "keeps contradicting", "two bases", "same serial",
      "out of range", "not half a turn"};
  static constexpr std::array<std::string_view, 6> kError = {"cannot", "failed", "error", "Error", "unable", "refused"};
  for (const auto& k : kError) if (Contains(text, k)) return LogLevel::Error;
  for (const auto& k : kWarn) if (Contains(text, k)) return LogLevel::Warn;
  for (const auto& k : kDebug) if (Contains(text, k)) return LogLevel::Debug;
  return LogLevel::Info;
}

LogLevel MinimumLogLevelFromEnvironment() {
  LogLevel level = LogLevel::Info;
  if (const char* v = std::getenv("LH_LOG_LEVEL")) ParseLogLevel(v, &level);
  return level;
}

namespace telemetry {
int PortFromEnvironment() {
  const char* v = std::getenv("LH_TELEMETRY_PORT");
  if (!v || !*v) return kDefaultPort;
  const int port = std::atoi(v);
  return port >= 0 && port < 65536 ? port : kDefaultPort;
}
}  // namespace telemetry

void IInputSink::OnTransportLog(LogLevel level, const std::string& text) {
  if (level >= MinimumLogLevelFromEnvironment()) std::fprintf(stderr, "lh: [%s] %s\n", LogLevelName(level), text.c_str());
}

}  // namespace lh
