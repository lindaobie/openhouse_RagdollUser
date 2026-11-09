// Minimal loopback TCP for the driver's telemetry and the viewer: non-blocking
// sockets, POSIX or Winsock. Only what those two need.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

namespace lh::net {

using Socket = std::intptr_t;
constexpr Socket kInvalid = -1;

// A non-blocking listener on 127.0.0.1:port; kInvalid (and *error) on failure.
Socket ListenLoopback(int port, std::string* error = nullptr);
// A pending connection, non-blocking, or kInvalid when there is none.
Socket Accept(Socket listener);
// A blocking connect to 127.0.0.1:port (the caller retries), then non-blocking.
Socket ConnectLoopback(int port);
// Bytes sent (0: would block), or -1 when the peer is gone.
long SendSome(Socket socket, const char* data, std::size_t size);
// Bytes read, 0 when nothing is waiting, or -1 when the peer closed or failed.
long RecvSome(Socket socket, char* buffer, std::size_t size);
void Close(Socket socket);

}  // namespace lh::net
