#include <lh/session.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <string>
#include <thread>

namespace {

std::atomic<bool> running{true};

void PrintEvent(const lh::SessionEvent& e) {
  switch (e.type) {
    case lh::SessionEventType::DeviceConnected:
    case lh::SessionEventType::DeviceDisconnected:
    case lh::SessionEventType::DeviceActivated:
    case lh::SessionEventType::TrackingStateChanged:
    case lh::SessionEventType::BaseStationSeen:
    case lh::SessionEventType::BaseStationLocated:
    case lh::SessionEventType::RoomChanged:
      std::printf("[event] %s\n", e.text.c_str());
      break;
    default:
      break;
  }
}

void PrintDevices(const lh::SessionSnapshot& snapshot) {
  std::printf("\n%-14s %-11s %-14s %s\n", "device", "role", "state", "position (m)");
  for (const auto& d : snapshot.devices) {
    if (!d.info.connected) continue;

    const auto& p = d.pose.worldFromHead.position;
    std::printf("%-14s %-11s %-14s", d.info.serial.c_str(), lh::ToString(d.info.role), lh::ToString(d.pose.state));

    if (d.pose.valid) std::printf(" %+.3f %+.3f %+.3f", p[0], p[1], p[2]);

    std::printf("\n");
  }
}

std::string RoomState(const lh::SessionSnapshot& snapshot) {
  std::string state = snapshot.roomId;
  for (const auto& b : snapshot.bases) state += " " + b.serial + (b.learned ? "+" : b.known ? "~" : "-");

  return state;
}

void PrintRoom(const lh::SessionSnapshot& snapshot) {
  std::printf("\nroom %s\n", snapshot.roomId.empty() ? "(not started yet)" : snapshot.roomId.c_str());

  for (const auto& b : snapshot.bases) {
    std::printf("  %s channel %-2s %s", b.serial.c_str(), b.channel.c_str(), b.learned ? "learned" : b.known ? "learning" : "not placed");

    if (b.known) std::printf("  at %+.2f %+.2f %+.2f", b.worldFromBase.position[0], b.worldFromBase.position[1], b.worldFromBase.position[2]);

    std::printf("\n");
  }
}

}

int main(int argc, char** argv) {
  lh::SessionOptions options;
  options.roomMapPath = "room.json";

  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--replay" && i + 1 < argc) options.replayPath = argv[++i];
    else options.roomMapPath = arg;
  }

  options.saveRoomMap = true;         // the learned room is written back to roomMapPath
  options.steamDatabasePath = "-";    // do not import SteamVR room map

  auto session = lh::Session::Create(options);
  std::string error;

  if (!session->Start(&error)) {
    std::fprintf(stderr, "could not start: %s\n", error.c_str());
    return 1;
  }

  std::printf("tracking, room map %s (Ctrl+C to quit)\n", options.roomMapPath.c_str());

  std::signal(SIGINT, [](int) { running = false; });
  std::string lastRoom;
  auto lastPrint = std::chrono::steady_clock::now();

  while (running && !session->Finished()) {
    for (const auto& event : session->PollEvents()) PrintEvent(event);

    const lh::SessionSnapshot snapshot = session->Snapshot();
    if (const std::string room = RoomState(snapshot); room != lastRoom) {  // a base was placed or learned
      lastRoom = room;
      PrintRoom(snapshot);
    }

    if (std::chrono::steady_clock::now() - lastPrint >= std::chrono::seconds(1)) {
      lastPrint = std::chrono::steady_clock::now();
      PrintDevices(snapshot);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }

  session->Stop();  // also saves the room map
  std::printf("stopped, room saved to %s\n", options.roomMapPath.c_str());
  return 0;
}
