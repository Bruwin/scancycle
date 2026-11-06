#pragma once
// The graphical ladder surface: one SVG per rung, a click to invert a contact,
// and forms for the edits a drawing cannot express with a click alone. No
// script, so the routes stay request-to-response mappings a test can drive.
#include "app/ladder_session.hpp"
#include "net/portal_server.hpp"

namespace sc::app {

net::Response ladder_page(const LadderSession& session);

// Captures `session` by reference, so it must outlive the router and server.
net::PortalRouter make_ladder_router(LadderSession& session);

}  // namespace sc::app
