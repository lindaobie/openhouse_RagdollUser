// SteamVR's dongle pairing menu, answered by our driver (which owns the dongles; the
// stock driver loaded for a headset never sees them, stock_hmd.hpp).
//
// SteamVR talks to the lighthouse driver through the private IVRMailbox_002 interface:
// a mailbox named "lighthouse_pairing" receives JSON requests and the reply goes to the
// request's "returnAddress" with its "message_id" (stock CLighthousePairing, handlers
// registered at 0x3bbcf0):
//  * discover_dongles -> discover_dongles_response (0x3b9460):
//      {"isPairing": bool, "expectedControllerType": "...",
//       "dongles": [{"serial", "firmwareVersion", "connectedSerial"}]}  (receivers only)
//  * start_pairing {"serial", "timeoutSeconds" (default 10), "sendStatusUpdates"}
//      (0x3ba930): receiver into pairing mode; "pairing_result" (0x3b9db0) with "result"
//      "success" + "connectedSerial" once a device connects (0x3ba1d0, each RunFrame),
//      "timeout", or "failed_to_start".
//  * stop_pairing {"serial"} (0x3ba500): pairing mode off, result "canceled".
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <openvr_driver.h>

namespace lh { class Session; }

namespace lh::openvr {

// IVRMailbox_002 as the stock driver calls it (vrmailbox.cpp; not in the public headers).
// Slot 1 (unregister) is never called by it; kept for the vtable layout.
class IVRMailbox {
 public:
  virtual int RegisterMailbox(const char* name, uint64_t* handle, char* address, uint32_t addressSize) = 0;
  virtual int UndoRegisterMailbox(uint64_t handle) = 0;
  virtual int SendMessage(uint64_t handle, const char* address, const char* message) = 0;
  virtual int ReadMessage(uint64_t handle, char* buffer, uint32_t size, uint32_t* needed) = 0;
};
inline constexpr const char* kMailboxVersion = "IVRMailbox_002";
inline constexpr const char* kPairingMailbox = "lighthouse_pairing";

// The host's mailbox for the stock driver: everything passes, but it cannot take the
// pairing mailbox (ours answers it).
class StockMailbox : public IVRMailbox {
 public:
  explicit StockMailbox(IVRMailbox* real) : real_(real) {}
  int RegisterMailbox(const char* name, uint64_t* handle, char* address, uint32_t addressSize) override;
  int UndoRegisterMailbox(uint64_t handle) override { return real_->UndoRegisterMailbox(handle); }
  int SendMessage(uint64_t handle, const char* address, const char* message) override {
    return real_->SendMessage(handle, address, message);
  }
  int ReadMessage(uint64_t handle, char* buffer, uint32_t size, uint32_t* needed) override {
    return real_->ReadMessage(handle, buffer, size, needed);
  }

 private:
  IVRMailbox* real_;
};

class PairingMailbox {
 public:
  using LogFn = void (*)(const std::string&);
  // Registers "lighthouse_pairing" (before the stock driver loads). False if the host
  // has no mailboxes (Monado).
  bool Open(vr::IVRDriverContext* context, LogFn log);
  // Host thread, each RunFrame: requests, and the running pairing's outcome.
  // expectedControllerType: the headset's Prop_ExpectedControllerType_String ("" if none).
  void Poll(Session& session, const std::string& expectedControllerType);

 private:
  struct Pairing {
    std::string receiver, returnAddress, before;
    double messageId = 0;
    bool haveMessageId = false, sendStatus = false, sawEmpty = false;
    double deadline = 0;
  };
  void Reply(const std::string& address, const std::string& json);
  std::string Discover(Session& session, const std::string& expected, const std::string* messageId) const;
  void Finish(Session& session, const std::string& result, const std::string& connected, const std::string& expected);

  IVRMailbox* mailbox_ = nullptr;
  uint64_t handle_ = 0;
  LogFn log_ = nullptr;
  std::optional<Pairing> pairing_;
  std::vector<char> buffer_ = std::vector<char>(16384);
};

}  // namespace lh::openvr
