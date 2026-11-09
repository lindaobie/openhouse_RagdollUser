#include <gtest/gtest.h>
#include "platform/platform.hpp"
#include <fstream>
#include <sstream>

namespace {
struct ScopedEnv {
  std::string key, old;
  bool present;
  ScopedEnv(std::string name, const std::string& value) : key(std::move(name)), old(lh::platform::Env(key.c_str())), present(std::getenv(key.c_str()) != nullptr) {
#ifdef _WIN32
    _putenv_s(key.c_str(), value.c_str());
#else
    setenv(key.c_str(), value.c_str(), 1);
#endif
  }
  ~ScopedEnv() {
#ifdef _WIN32
    _putenv_s(key.c_str(), present ? old.c_str() : "");
#else
    if (present) setenv(key.c_str(), old.c_str(), 1); else unsetenv(key.c_str());
#endif
  }
};

TEST(Platform, UsesPlatformConfigurationAndSteamOverrides) {
#ifdef _WIN32
  ScopedEnv config("APPDATA", "C:/lh-test-config");
  EXPECT_EQ(lh::platform::ConfigDirectory(), "C:/lh-test-config/lh");
#else
  ScopedEnv config("XDG_CONFIG_HOME", "/tmp/lh-test-config");
  EXPECT_EQ(lh::platform::ConfigDirectory(), "/tmp/lh-test-config/lh");
#endif
  ScopedEnv root("LH_STEAM_ROOT", "test-steam-root");
  ScopedEnv explicitConfig("LH_STEAM_CONFIG_DIR", "test-lighthouse-config");
  ScopedEnv vr("STEAMVR_PATH", "test-steamvr-path");
  EXPECT_EQ(lh::platform::SteamRoot(), "test-steam-root");
  EXPECT_EQ(lh::platform::SteamConfigDirectory(), "test-lighthouse-config");
  EXPECT_EQ(lh::platform::SteamVRDirectory(), "test-steamvr-path");
  EXPECT_GT(lh::platform::ProcessId(), 0u);
}

TEST(Platform, ReplacesExistingFileWithoutDeletingItOnFailure) {
  namespace fs = std::filesystem;
  const auto dir = fs::temp_directory_path() / ("lh-atomic-" + std::to_string(lh::platform::ProcessId()));
  fs::create_directories(dir);
  const auto target = dir / "settings.json", temp = dir / "settings.tmp";
  { std::ofstream f(target); f << "old"; }
  { std::ofstream f(temp); f << "new"; }
  ASSERT_NO_THROW(lh::platform::ReplaceFile(temp, target));
  auto text = [&] { std::ifstream f(target); std::ostringstream s; s << f.rdbuf(); return s.str(); };
  EXPECT_EQ(text(), "new");
  EXPECT_FALSE(fs::exists(temp));
  EXPECT_THROW(lh::platform::ReplaceFile(dir / "missing", target), std::system_error);
  EXPECT_EQ(text(), "new");
  fs::remove_all(dir);
}
}
