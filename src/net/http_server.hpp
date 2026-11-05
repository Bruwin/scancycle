#pragma once
// The portal's TCP transport. Loopback only and no TLS: a classroom tool should
// not expose student work to the corridor, and the teacher portal is served from
// the machine that runs the simulation. Message parsing lives in net/http, so a
// failed connection is the only thing this class reports as an error.
#include "core/types.hpp"
#include "net/portal_server.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace sc::net {

class HttpServer {
public:
    explicit HttpServer(const PortalRouter& router);
    ~HttpServer();

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    // Binds 127.0.0.1:port and listens. Port 0 asks the kernel for a free port,
    // which port() then reports; that is how tests avoid fighting over 8080.
    bool listen(u16 port);

    // Accepts one connection and answers it. Blocking; false means the listening
    // socket itself failed, which is the signal to stop serving.
    bool serve_one();

    u16 port() const { return port_; }
    const std::string& error() const { return error_; }

private:
    void close_listener();

    const PortalRouter* router_ = nullptr;
    std::intptr_t listener_ = -1;  // OS handle; -1 when closed
    u16 port_ = 0;
    std::string error_;
};

// Connects to 127.0.0.1:port, writes `raw`, reads until the peer closes. The
// server's own smoke check and the transport tests use it, so both sides of the
// wire are exercised by the same code path a browser would take.
std::string http_round_trip(u16 port, std::string_view raw, std::string& error);

}  // namespace sc::net
