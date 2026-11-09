// openhouse-compare: openhouse and SteamVR's own lighthouse driver tracking the same
// devices at the same time, side by side.
//
// Left view: openhouse (liblh, live USB). Right view: the stock driver_lighthouse from
// the SteamVR install, hosted in this process like Monado hosts it (stock_host.hpp).
// The stock driver's frame is aligned to ours through a base station both have placed.
// One camera drives both views: drag to orbit, middle-drag to pan, wheel to zoom.
//
//   openhouse-compare [--stock PATH]     (close SteamVR / Monado / WiVRn first)
//
// LH_COMPARE_PRINT=1 prints both sides once a second (and openhouse's log) on stdout;
// LH_COMPARE_STOCK_LOG=1 prints the stock driver's log on stderr.
#include <GLFW/glfw3.h>
#include <Eigen/Geometry>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_opengl2.h"
#include "imgui.h"
#include "lh/session.hpp"
#include "stock_host.hpp"

namespace {
using openhouse::compare::StockDevice;
using openhouse::compare::StockHost;

constexpr float kPanel = 330;                     // left panel width (window units)
const ImVec4 kBlue(0.35f, 0.6f, 1.0f, 1), kRed(1.0f, 0.35f, 0.3f, 1);
constexpr double kTrailSeconds = 3;
constexpr double kJump = 0.10;                    // m between two samples < 50 ms apart

struct Camera {
  double yaw = 35, pitch = 25, distance = 5;
  Eigen::Vector3d target = Eigen::Vector3d(0, 1, 0);
  Eigen::Matrix4d view = Eigen::Matrix4d::Identity(), projection = Eigen::Matrix4d::Identity();
  double width = 1, height = 1;

  Eigen::Vector3d Eye() const {
    const double y = yaw * M_PI / 180, p = pitch * M_PI / 180;
    return target + distance * Eigen::Vector3d(std::cos(p) * std::sin(y), std::sin(p), std::cos(p) * std::cos(y));
  }
  void Update(double w, double h) {
    width = std::max(w, 1.0); height = std::max(h, 1.0);
    const Eigen::Vector3d eye = Eye(), f = (target - eye).normalized();
    const Eigen::Vector3d s = f.cross(Eigen::Vector3d::UnitY()).normalized(), u = s.cross(f);
    view.setIdentity();
    view.block<1, 3>(0, 0) = s.transpose(); view.block<1, 3>(1, 0) = u.transpose(); view.block<1, 3>(2, 0) = -f.transpose();
    view(0, 3) = -s.dot(eye); view(1, 3) = -u.dot(eye); view(2, 3) = f.dot(eye);
    const double n = 0.02, fa = 200, t = n * std::tan(25 * M_PI / 180), r = t * width / height;
    projection.setZero();
    projection(0, 0) = n / r; projection(1, 1) = n / t;
    projection(2, 2) = -(fa + n) / (fa - n); projection(2, 3) = -2 * fa * n / (fa - n); projection(3, 2) = -1;
  }
  std::optional<ImVec2> Project(const Eigen::Vector3d& p) const {
    const Eigen::Vector4d c = projection * view * p.homogeneous();
    if (c.w() <= 1e-6) return std::nullopt;
    const Eigen::Vector3d n = c.head<3>() / c.w();
    if (std::abs(n.x()) > 1.1 || std::abs(n.y()) > 1.1) return std::nullopt;
    return ImVec2(float((n.x() * 0.5 + 0.5) * width), float((0.5 - n.y() * 0.5) * height));
  }
};

Eigen::Isometry3d ToIso(const lh::Pose& p) {
  Eigen::Isometry3d t = Eigen::Isometry3d::Identity();
  t.linear() = Eigen::Quaterniond(p.orientation[0], p.orientation[1], p.orientation[2], p.orientation[3]).normalized().toRotationMatrix();
  t.translation() = Eigen::Vector3d(p.position[0], p.position[1], p.position[2]);
  return t;
}

void Vertex(const Eigen::Vector3d& v) { glVertex3d(v.x(), v.y(), v.z()); }

enum class Track { Off, Tracking, Coasting, Lost };

const char* TrackName(Track t) {
  switch (t) {
    case Track::Tracking: return "Tracking";
    case Track::Coasting: return "Coasting";
    case Track::Lost: return "Lost";
    default: return "Off";
  }
}

// One side's view of the selected device, sampled once per drawn frame.
struct Side {
  Track track = Track::Off;
  bool valid = false;
  Eigen::Isometry3d pose = Eigen::Isometry3d::Identity();
  uint64_t updates = 0;
  std::vector<Eigen::Isometry3d> bases;  // placed bases, in our room frame
};

struct Stats {
  double tracking = 0, coasting = 0, lost = 0;  // s while on
  int losses = 0, jumps = 0;
  bool wasValid = false;
  std::optional<Eigen::Vector3d> last;
  double lastTime = 0;
  uint64_t updatesAt = 0;
  double rateTime = 0, rate = 0;
  std::deque<std::pair<double, Eigen::Vector3d>> trail;

  void Add(const Side& s, double now, double dt) {
    if (s.track == Track::Tracking) tracking += dt;
    else if (s.track == Track::Coasting) coasting += dt;
    else if (s.track == Track::Lost) lost += dt;
    if (wasValid && !s.valid && s.track != Track::Off) ++losses;
    if (s.valid) {
      const Eigen::Vector3d p = s.pose.translation();
      if (last && now - lastTime < 0.05 && (p - *last).norm() > kJump) ++jumps;
      last = p;
      lastTime = now;
      trail.emplace_back(now, p);
    } else {
      last.reset();
    }
    while (!trail.empty() && trail.front().first < now - kTrailSeconds) trail.pop_front();
    wasValid = s.valid;
    if (now - rateTime >= 1) {
      // (a counter below the last one: another device, or the driver restarted it)
      rate = rateTime > 0 && s.updates >= updatesAt ? double(s.updates - updatesAt) / (now - rateTime) : 0;
      updatesAt = s.updates;
      rateTime = now;
    }
  }
  double On() const { return tracking + coasting + lost; }
};

Track OurTrack(const lh::DeviceState& d) {
  if (!d.info.connected || !d.info.enabled) return Track::Off;
  switch (d.pose.state) {
    case lh::TrackingState::Tracking: return Track::Tracking;
    case lh::TrackingState::Coasting: return Track::Coasting;
    case lh::TrackingState::Disconnected: return Track::Off;
    default: return Track::Lost;
  }
}

Track StockTrack(const StockDevice& d) {
  if (!d.connected) return Track::Off;
  if (!d.valid) return Track::Lost;
  return d.result == vr::TrackingResult_Running_OK ? Track::Tracking : Track::Coasting;
}

void DrawStats(const char* title, const ImVec4& color, const Side& side, const Stats& s) {
  ImGui::Spacing();
  ImGui::TextColored(color, "%s", title);
  ImGui::Separator();
  const double on = std::max(s.On(), 1e-9);
  if (ImGui::BeginTable(title, 2, ImGuiTableFlags_SizingStretchProp)) {
    auto row = [](const char* name, const char* format, auto... v) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn(); ImGui::TextDisabled("%s", name);
      ImGui::TableNextColumn(); ImGui::Text(format, v...);
    };
    row("State", "%s", TrackName(side.track));
    row("Tracking", "%.1f %%", 100 * s.tracking / on);
    row("Coasting", "%.1f %%", 100 * s.coasting / on);
    row("Lost", "%.1f %%", 100 * s.lost / on);
    row("Losses", "%d", s.losses);
    row("Jumps", "%d", s.jumps);
    ImGui::EndTable();
  }
}

void DrawView(const Camera& camera, const Side& side, const Stats& stats, const ImVec4& color, double floor) {
  // Floor grid.
  glLineWidth(1);
  glBegin(GL_LINES);
  for (int i = -10; i <= 10; ++i) {
    glColor4f(0.6f, 0.65f, 0.7f, i == 0 ? 0.3f : 0.12f);
    glVertex3d(i * 0.5, floor, -5); glVertex3d(i * 0.5, floor, 5);
    glVertex3d(-5, floor, i * 0.5); glVertex3d(5, floor, i * 0.5);
  }
  glEnd();
  // Base stations.
  glLineWidth(1.5f);
  for (const auto& t : side.bases) {
    const double s = 0.07;
    const Eigen::Vector3d c[8] = {{-s, -s, -s}, {s, -s, -s}, {s, s, -s}, {-s, s, -s}, {-s, -s, s}, {s, -s, s}, {s, s, s}, {-s, s, s}};
    const int e[12][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
    glColor4f(0.85f, 0.85f, 0.9f, 0.9f);
    glBegin(GL_LINES);
    for (const auto& k : e) { Vertex(t * c[k[0]]); Vertex(t * c[k[1]]); }
    glEnd();
  }
  // Trail and device.
  glLineWidth(2);
  glBegin(GL_LINE_STRIP);
  for (size_t i = 0; i < stats.trail.size(); ++i) {
    glColor4f(color.x, color.y, color.z, 0.1f + 0.7f * float(i) / float(stats.trail.size()));
    Vertex(stats.trail[i].second);
  }
  glEnd();
  if (!side.valid) return;
  const Eigen::Isometry3d& t = side.pose;
  const double a = 0.12;
  glLineWidth(3);
  glBegin(GL_LINES);
  glColor4f(1, .3f, .3f, 1); Vertex(t.translation()); Vertex(t * Eigen::Vector3d(a, 0, 0));
  glColor4f(.3f, 1, .3f, 1); Vertex(t.translation()); Vertex(t * Eigen::Vector3d(0, a, 0));
  glColor4f(.3f, .5f, 1, 1); Vertex(t.translation()); Vertex(t * Eigen::Vector3d(0, 0, a));
  glEnd();
  glPointSize(12);
  glBegin(GL_POINTS);
  glColor4f(color.x, color.y, color.z, 1);
  Vertex(t.translation());
  glEnd();
  (void)camera;
}

}  // namespace

int main(int argc, char** argv) {
  std::string stockPath, only;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--stock" && i + 1 < argc) stockPath = argv[++i];
    else if (a == "--only" && i + 1 < argc) only = argv[++i];  // steamvr | openhouse: one driver alone
    else if (a == "--help" || a == "-h") {
      std::printf("usage: openhouse-compare [--stock driver_lighthouse.so]\n");
      return 0;
    } else {
      std::fprintf(stderr, "unknown option %s\n", a.c_str());
      return 2;
    }
  }

  // SteamVR's driver first: it reads every device's config over the radio as it finds
  // it, and two drivers reading at once truncate each other's stream. openhouse then
  // takes SteamVR's cached copies instead of reading them again.
  StockHost stock;
  std::string stockError;
  if (only != "openhouse") {
    if (stockPath.empty()) stockPath = StockHost::FindDriver();
    if (stockPath.empty()) stockError = "SteamVR's driver_lighthouse not found";
    else if (!stock.Start(stockPath, &stockError)) stockError = "SteamVR driver: " + stockError;
    else std::this_thread::sleep_for(std::chrono::seconds(3));
#ifdef _WIN32
    _putenv_s("LH_PREFER_STEAM_CONFIG", "1");
#else
    setenv("LH_PREFER_STEAM_CONFIG", "1", 1);
#endif
  }

  // openhouse: the user's room, never written back from here.
  lh::SessionOptions options;
  options.saveRoomMap = false;
  auto session = only == "steamvr" ? nullptr : lh::Session::Create(options);
  std::string error;
  if (session && !session->Start(&error)) { std::fprintf(stderr, "openhouse-compare: %s\n", error.c_str()); return 1; }

  if (!glfwInit()) return 1;
  glfwWindowHint(GLFW_SAMPLES, 4);
  GLFWwindow* window = glfwCreateWindow(1600, 900, "openhouse compare", nullptr, nullptr);
  if (!window) { glfwTerminate(); return 1; }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().IniFilename = nullptr;
  ImGui::StyleColorsDark();
  ImGui::GetStyle().WindowRounding = 0;
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL2_Init();

  Camera camera;
  std::string selected;
  bool follow = true, picked = false;
  Stats ours, theirs;
  double lastTime = glfwGetTime(), floor = 0;
  bool haveFloor = false;

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    if (session) for (const auto& e : session->PollEvents())
      if (std::getenv("LH_COMPARE_PRINT")) std::printf("openhouse: %s\n", e.text.c_str());
    const double now = glfwGetTime(), dt = std::min(now - lastTime, 0.1);
    lastTime = now;
    const lh::SessionSnapshot snap = session ? session->Snapshot() : lh::SessionSnapshot{};
    const std::vector<StockDevice> stockDevices = stock.Devices();

    // The stock frame into ours, through a base both placed (our origin if it can).
    std::map<std::string, Eigen::Isometry3d> ourBases, stockBases;
    for (const auto& b : snap.bases)
      if (b.known) ourBases[b.serial] = ToIso(b.worldFromBase);
    for (const auto& d : stockDevices)
      if (d.cls == vr::TrackedDeviceClass_TrackingReference && d.valid) stockBases[d.serial] = d.world;
    std::optional<Eigen::Isometry3d> oursFromStock;
    for (const auto& b : snap.bases) {
      if (!b.known || !stockBases.count(b.serial)) continue;
      if (!oursFromStock || b.origin) oursFromStock = ourBases[b.serial] * stockBases[b.serial].inverse();
    }

    // Devices either side has (bases aside).
    std::vector<std::string> serials;
    std::set<std::string> seen;
    for (const auto& d : snap.devices)
      if (d.info.role != lh::DeviceRole::BaseStation && seen.insert(d.info.serial).second) serials.push_back(d.info.serial);
    for (const auto& d : stockDevices)
      if (d.cls != vr::TrackedDeviceClass_TrackingReference && seen.insert(d.serial).second) serials.push_back(d.serial);
    // Until one is picked: the first device that's on, controllers and trackers first.
    if (!picked) {
      std::string best;
      int bestRank = 9;
      for (const auto& d : snap.devices) {
        if (d.info.role == lh::DeviceRole::BaseStation) continue;
        const int rank = (d.info.connected ? 0 : 2) + (d.info.role == lh::DeviceRole::Hmd ? 1 : 0);
        if (rank < bestRank) { bestRank = rank; best = d.info.serial; }
      }
      if (best.empty() && !serials.empty()) best = serials.front();
      if (best != selected) { selected = best; ours = {}; theirs = {}; }
    }

    Side left, right;
    for (const auto& [serial, t] : ourBases) left.bases.push_back(t);
    for (const auto& [serial, t] : stockBases) right.bases.push_back(oursFromStock ? *oursFromStock * t : t);
    for (const auto& d : snap.devices) {
      if (d.info.serial != selected) continue;
      left.track = OurTrack(d);
      left.valid = d.pose.valid && left.track != Track::Off;
      left.pose = ToIso(d.pose.worldFromImu);
      left.updates = d.pose.updates;
    }
    for (const auto& d : stockDevices) {
      if (d.serial != selected) continue;
      right.track = StockTrack(d);
      right.valid = d.valid && right.track != Track::Off;
      right.pose = oursFromStock ? *oursFromStock * d.world : d.world;
      right.updates = d.updates;
    }
    ours.Add(left, now, dt);
    theirs.Add(right, now, dt);
    // LH_COMPARE_PRINT=1: both sides once a second on stdout (debugging without a look at the window).
    static const bool print = std::getenv("LH_COMPARE_PRINT") != nullptr;
    static double printed = 0;
    if (print && now - printed >= 1) {
      printed = now;
      const double diff = left.valid && right.valid ? (left.pose.translation() - right.pose.translation()).norm() * 100 : -1;
      std::printf("%6.1f %s | openhouse %-8s %4.0f Hz bases %zu | SteamVR %-8s %4.0f Hz bases %zu | aligned %d diff %.1f cm\n", now,
                  selected.c_str(), TrackName(left.track), ours.rate, left.bases.size(), TrackName(right.track), theirs.rate,
                  right.bases.size(), oursFromStock.has_value(), diff);
      std::fflush(stdout);
    }
    if (follow && left.valid) camera.target = camera.target * 0.9 + left.pose.translation() * 0.1;
    else if (follow && right.valid) camera.target = camera.target * 0.9 + right.pose.translation() * 0.1;
    if (left.valid) {
      const double low = left.pose.translation().y() - 1.2;
      floor = haveFloor ? std::min(floor, std::max(low, floor - 0.001)) : low;
      haveFloor = true;
    }

    ImGui_ImplOpenGL2_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuiIO& io = ImGui::GetIO();

    // Camera input over either view.
    if (!io.WantCaptureMouse && io.MousePos.x > kPanel) {
      if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) || ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        camera.yaw -= io.MouseDelta.x * 0.3;
        camera.pitch = std::clamp(camera.pitch + io.MouseDelta.y * 0.3, -89.0, 89.0);
      }
      if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        const Eigen::Vector3d eye = camera.Eye(), f = (camera.target - eye).normalized();
        const Eigen::Vector3d s = f.cross(Eigen::Vector3d::UnitY()).normalized(), u = s.cross(f);
        camera.target += (-s * io.MouseDelta.x + u * io.MouseDelta.y) * camera.distance * 0.0015;
        follow = false;
      }
      if (io.MouseWheel != 0) camera.distance = std::clamp(camera.distance * std::exp(-io.MouseWheel * 0.12), 0.3, 40.0);
    }

    // Panel.
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(kPanel, io.DisplaySize.y));
    ImGui::Begin("panel", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::SetNextItemWidth(-1);
    if (ImGui::BeginCombo("##device", selected.empty() ? "No devices" : selected.c_str())) {
      for (const auto& s : serials)
        if (ImGui::Selectable(s.c_str(), s == selected)) {
          picked = true;
          if (s != selected) { selected = s; ours = {}; theirs = {}; }
        }
      ImGui::EndCombo();
    }
    ImGui::Checkbox("Follow", &follow);
    ImGui::SameLine();
    if (ImGui::Button("Reset stats")) { ours = {}; theirs = {}; }
    DrawStats("openhouse", kBlue, left, ours);
    DrawStats("SteamVR", kRed, right, theirs);
    ImGui::Spacing();
    ImGui::Separator();
    if (left.valid && right.valid && oursFromStock) {
      const double d = (left.pose.translation() - right.pose.translation()).norm();
      const double r = Eigen::AngleAxisd(left.pose.rotation().transpose() * right.pose.rotation()).angle();
      ImGui::Text("Difference  %.1f cm  %.1f deg", d * 100, r * 180 / M_PI);
    } else {
      ImGui::TextDisabled("Difference  -");
    }
    if (!stockError.empty()) ImGui::TextColored(kRed, "%s", stockError.c_str());
    else if (!oursFromStock) ImGui::TextDisabled("Waiting for a shared base station");
    ImGui::End();

    // Views.
    int fw, fh, ww, wh;
    glfwGetFramebufferSize(window, &fw, &fh);
    glfwGetWindowSize(window, &ww, &wh);
    const double scale = ww > 0 ? double(fw) / ww : 1;
    const double viewW = (ww - kPanel) / 2;
    camera.Update(viewW, wh);
    glClearColor(0.05f, 0.05f, 0.06f, 1);
    glDisable(GL_SCISSOR_TEST);
    glViewport(0, 0, fw, fh);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_LINE_SMOOTH);
    ImDrawList* bg = ImGui::GetBackgroundDrawList();
    for (int v = 0; v < 2; ++v) {
      const double x0 = kPanel + v * viewW;
      const int px = int(x0 * scale), pw = int(viewW * scale);
      glViewport(px, 0, pw, fh);
      glEnable(GL_SCISSOR_TEST);
      glScissor(px, 0, pw, fh);
      glClearColor(v == 0 ? 0.07f : 0.08f, 0.08f, v == 0 ? 0.1f : 0.08f, 1);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      glEnable(GL_DEPTH_TEST);
      glMatrixMode(GL_PROJECTION);
      glLoadMatrixd(camera.projection.data());
      glMatrixMode(GL_MODELVIEW);
      glLoadMatrixd(camera.view.data());
      DrawView(camera, v == 0 ? left : right, v == 0 ? ours : theirs, v == 0 ? kBlue : kRed, floor);
      glDisable(GL_DEPTH_TEST);
      glDisable(GL_SCISSOR_TEST);
      const ImVec4& c = v == 0 ? kBlue : kRed;
      bg->AddText(ImVec2(float(x0) + 12, 10), ImGui::ColorConvertFloat4ToU32(c), v == 0 ? "openhouse" : "SteamVR");
      if (v == 1) bg->AddLine(ImVec2(float(x0), 0), ImVec2(float(x0), float(wh)), IM_COL32(60, 60, 70, 255), 2);
    }

    ImGui::Render();
    glViewport(0, 0, fw, fh);
    ImGui_ImplOpenGL2_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
  }

  stock.Stop();
  if (session) session->Stop();
  ImGui_ImplOpenGL2_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
