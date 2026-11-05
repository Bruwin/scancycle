#pragma once
// A minimal RFC 6455 server for the portal's one-way live feed. The classroom
// server only pushes short text events; it deliberately does not accept
// application messages, so there is no client-to-server protocol to learn.
#include "core/types.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

namespace sc::net {

// The Sec-WebSocket-Accept value for a client's Sec-WebSocket-Key. Exposed so
// the handshake has a known-answer test independent of a socket.
std::string websocket_accept_key(std::string_view key);

// One unmasked text frame. Server-to-client frames must not be masked.
std::string websocket_text_frame(std::string_view text);

class WebSocketServer {
public:
    WebSocketServer();
    ~WebSocketServer();

    WebSocketServer(const WebSocketServer&) = delete;
    WebSocketServer& operator=(const WebSocketServer&) = delete;

    // Binds 127.0.0.1:port and starts accepting. Port 0 asks the kernel for a
    // free port. A failed bind leaves error() set and starts no thread.
    bool listen(u16 port);
    void stop();

    u16 port() const;
    std::size_t client_count() const;

    // Sends one text frame to every connected client. A client whose socket
    // has gone away is dropped; it is never allowed to stop the server.
    void broadcast(std::string_view text);
    const std::string& error() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace sc::net
