#pragma once
// The chart surface: the drawing, which step is active, and the text of the chart
// it came from. A chart is edited by replacing its text, not by dragging steps:
// the routes stay request-to-response mappings with no script, which is what lets
// the surface be tested without a browser.
#include "app/sfc_session.hpp"
#include "net/portal_server.hpp"

namespace sc::app {

net::Response sfc_page(const SfcSession& session);

// Captures `session` by reference, so it must outlive the router and server.
net::PortalRouter make_sfc_router(SfcSession& session);

}  // namespace sc::app
