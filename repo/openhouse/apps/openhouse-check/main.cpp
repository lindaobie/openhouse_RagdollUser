// openhouse-check: started by SteamVR with every session (an auto-launched app the
// installer registers). If a SteamVR update replaced our driver_lighthouse with its own,
// tells the user to run the openhouse installer again. Only while the installer's
// driver_lighthouse.*.stock backup exists: after uninstalling (or renaming it by hand)
// SteamVR's driver is wanted, and this stays quiet.
//
//   openhouse-check DRIVER_DIR     (SteamVR/drivers/lighthouse/bin/<platform>)
//
// On Linux every run is logged to ~/.local/share/openhouse/check.log.
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#ifdef _WIN32
#include <windows.h>
#else
#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#endif

namespace {
#ifdef _WIN32
constexpr const char* kDriver = "driver_lighthouse.dll";
#else
constexpr const char* kDriver = "driver_lighthouse.so";
#endif
// A string only openhouse's driver contains (the installers check the same one).
constexpr const char* kMarker = "liblh session started";

const char* kTitle = "openhouse";
const char* kText =
    "A SteamVR update replaced the openhouse driver with SteamVR's own lighthouse driver.\n\n"
    "SteamVR's driver is being used right now. To get openhouse back, close SteamVR and run "
    "the openhouse installer again.";

#ifdef _WIN32
void Log(const std::string&) {}
#else
void Log(const std::string& line) {
  const char* data = std::getenv("XDG_DATA_HOME");
  const char* home = std::getenv("HOME");
  if (!data && !home) return;
  const auto t = std::time(nullptr);
  char stamp[32];
  std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
  std::ofstream(std::filesystem::path(data ? data : std::string(home) + "/.local/share") / "openhouse/check.log",
                std::ios::app)
      << stamp << " [" << getpid() << "] " << line << std::endl;
}
std::string Env(const char* name) {
  const char* v = std::getenv(name);
  return v ? v : "(unset)";
}
#endif

void Show() {
#ifdef _WIN32
  MessageBoxA(nullptr, kText, kTitle, MB_OK | MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND);
#else
  Log("DISPLAY=" + Env("DISPLAY") + " WAYLAND_DISPLAY=" + Env("WAYLAND_DISPLAY") +
      " DBUS_SESSION_BUS_ADDRESS=" + Env("DBUS_SESSION_BUS_ADDRESS") + " LD_LIBRARY_PATH=" + Env("LD_LIBRARY_PATH"));
  // SteamVR starts apps with its own libraries first in LD_LIBRARY_PATH (vrenv.sh). Its
  // own steamvr_msg.sh runs the desktop's zenity under SYSTEM_LD_LIBRARY_PATH; so do we.
  if (const char* system = std::getenv("SYSTEM_LD_LIBRARY_PATH"); system && *system) setenv("LD_LIBRARY_PATH", system, 1);
  else unsetenv("LD_LIBRARY_PATH");
  unsetenv("LD_PRELOAD");
  // Whatever dialog tool the desktop has; a notification at least; stderr at last.
  auto quoted = [](const std::string& s) {
    std::string q = "'";
    for (char c : s) q += c == '\'' ? std::string("'\\''") : std::string(1, c);
    return q + "'";
  };
  const std::string t = quoted(kTitle), m = quoted(kText);
  for (const std::string& command : {"zenity --warning --width=420 --title=" + t + " --text=" + m,
                                     "kdialog --title " + t + " --sorry " + m,
                                     "notify-send -u critical " + t + " " + m}) {
    const std::string tool = command.substr(0, command.find(' '));
    if (std::system(("command -v " + tool + " >/dev/null 2>&1").c_str()) != 0) continue;
    Log("showing with " + tool);
    const int rc = std::system((command + " >/dev/null 2>&1").c_str());
    Log(tool + " exited with " + std::to_string(rc));
    if (rc == 0) return;
  }
  std::fprintf(stderr, "%s: %s\n", kTitle, kText);
#endif
}
}  // namespace

int main(int argc, char** argv) {
  if (argc < 2) { Log("no driver folder given"); return 2; }
  // SteamVR hands over the manifest's "arguments" with their quotes, and may split a path
  // with spaces: join the pieces and drop the quotes.
  std::string folder = argv[1];
  for (int i = 2; i < argc; ++i) folder += std::string(" ") + argv[i];
  if (folder.size() >= 2 && folder.front() == '"' && folder.back() == '"') folder = folder.substr(1, folder.size() - 2);
  const std::filesystem::path driver = std::filesystem::path(folder) / kDriver;
  Log("checking " + driver.string());
  std::error_code ec;
  if (!std::filesystem::exists(driver.string() + ".stock", ec)) { Log("no .stock backup: nothing to do"); return 0; }
  std::ifstream in(driver, std::ios::binary);
  if (!in) { Log("can't read the driver"); return 0; }
  const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  if (bytes.find(kMarker) != std::string::npos) { Log("driver is openhouse's: fine"); return 0; }
  Log("driver was replaced: telling the user");
  Show();
  Log("done");
  return 1;
}
