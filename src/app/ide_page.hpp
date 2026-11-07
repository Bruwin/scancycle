#pragma once
// The M4 IDE surface: a plain HTML page and POST routes. No script, so the page
// works in any browser and the routes stay request-to-response mapping that a
// test can drive without opening a port.
#include "app/ide_session.hpp"
#include "net/portal_server.hpp"

#include <string>

namespace sc::app {

// The regular IDE and Free Play share one page and one session. Free Play adds
// a banner, a Reset route backed by the starter source, and nothing that can
// grade, persist, or reach hardware.
struct IdePageOptions {
    bool free_play = false;

    // Captured once at startup. Reset compiles this source and therefore also
    // discards the running application, its scan count and its forces.
    std::string starter_source;
};

net::Response ide_page(const IdeSession& session, const IdePageOptions& options = {});

// The router captures `session` by reference, so it must outlive the router and
// the server built from it. Reset is registered only in Free Play.
net::PortalRouter make_ide_router(IdeSession& session, const IdePageOptions& options = {});

}  // namespace sc::app
