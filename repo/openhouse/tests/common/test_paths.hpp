// Where tests find recordings: tests/data/captures (in git, trimmed to what the tests
// need) first, then the local captures/ folder (full recordings, not in git).
#pragma once
#include <filesystem>
#include <string>

inline std::string TestCapture(const std::string& relative) {
  const std::filesystem::path root(LH_SOURCE_DIR);
  const auto bundled = root / "tests/data/captures" / relative;
  return std::filesystem::exists(bundled) ? bundled.string() : (root / "captures" / relative).string();
}
