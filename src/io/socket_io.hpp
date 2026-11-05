#pragma once
// Blocking IPv4 TCP primitives for the in-house Modbus transport. The types are
// deliberately narrow: these helpers only open a loopback listener, connect a
// client, and move exact byte ranges. Modbus framing stays in io/modbus.
#include "core/types.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace sc::io {

using SocketHandle = std::intptr_t;
inline constexpr SocketHandle kInvalidSocket = -1;

bool socket_startup(std::string& error);

// Connects to host:port. timeout_ms bounds sends and receives after the
// connection is established; a failed connect is reported as a value.
SocketHandle connect_tcp(std::string_view host, u16 port, int timeout_ms,
                         std::string& error);

// Binds 127.0.0.1:port. Port 0 asks the kernel for a free port.
SocketHandle listen_loopback(u16 port, u16& bound_port, std::string& error);

// Waits up to timeout_ms for one client. A timeout is too ordinary to be an
// error: `timed_out` says so and leaves `error` empty. Once accepted, the
// client gets the same send/receive timeout.
SocketHandle accept_tcp(SocketHandle listener, int timeout_ms, bool& timed_out,
                        std::string& error);

bool socket_send(SocketHandle socket, std::span<const u8> data, std::string& error);
bool socket_receive(SocketHandle socket, std::span<u8> data, std::string& error);

void socket_shutdown(SocketHandle socket);
void socket_close(SocketHandle socket);

}  // namespace sc::io
