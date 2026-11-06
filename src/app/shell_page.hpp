#pragma once
// The 1.0 shell is a local web surface: intro/menu, a teacher-only portal entry
// and an interactive projection of the procedural factory. It deliberately links
// to the existing IDE and portal rather than embedding their routes.
#include "net/portal_server.hpp"

#include <string>

namespace sc::app {

struct ShellOptions {
    std::string ide_url = "http://127.0.0.1:8090/";
    std::string free_play_url = "http://127.0.0.1:8091/";
    std::string portal_url = "http://127.0.0.1:8080/";
    // This is a local presentation switch, not an authorization boundary. The
    // portal itself remains loopback-only and unauthenticated.
    bool teacher_mode = false;
};

net::PortalRouter make_shell_router(const ShellOptions& options);

}  // namespace sc::app
