#pragma once
// The M4 IDE surface: a plain HTML page and POST routes. No script, so the page
// works in any browser and the routes stay request-to-response mapping that a
// test can drive without opening a port.
#include "app/ide_session.hpp"
#include "net/portal_server.hpp"

namespace sc::app {

net::Response ide_page(const IdeSession& session);

// The router captures `session` by reference, so it must outlive the router and
// the server built from it.
net::PortalRouter make_ide_router(IdeSession& session);

}  // namespace sc::app
