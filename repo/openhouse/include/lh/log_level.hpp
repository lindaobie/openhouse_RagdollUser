// Log levels shared by the runtime, the OpenVR driver and the viewer.
//
// Every session event carries one. Where the code knows it, it says so; the rest is
// classified from the message (ClassifyLog), so the levels stay consistent no matter
// where a message comes from. Sinks filter: the driver's log keeps Info and up unless
// LH_LOG_LEVEL lowers it; the viewer receives everything and filters itself.
#pragma once
#include <string>
#include <string_view>

namespace lh {

enum class LogLevel : int { Debug = 0, Info = 1, Warn = 2, Error = 3 };

const char* LogLevelName(LogLevel level);  // "DEBUG", "INFO", "WARN", "ERR"
// "debug", "info", "warn"/"warning", "err"/"error" (any case); false if none of them.
bool ParseLogLevel(std::string_view text, LogLevel* level);
// The level of a message that didn't say: internal chatter (light decoding, filter
// bookkeeping) is Debug, recoveries and disagreements Warn, failures Error.
LogLevel ClassifyLog(std::string_view text);
// LH_LOG_LEVEL, else Info.
LogLevel MinimumLogLevelFromEnvironment();

}  // namespace lh
