#include "harness.hpp"

#include "app/ladder_page.hpp"
#include "app/ladder_session.hpp"
#include "net/http.hpp"
#include "net/portal_server.hpp"
#include "plc/ladder_text.hpp"
#include "ui/fbd_svg.hpp"

#include <string>

using namespace sc;

namespace {

const char* kChart = R"ld(VAR
    a : BOOL;
    b : BOOL;
    out : BOOL;
END_VAR
out = (a | b)
)ld";

std::string draw(const char* source) {
    plc::LadderParseResult parsed = plc::parse_ladder_source(source);
    CHECK(parsed.ok);
    if (!parsed.ok || parsed.source.rungs.empty()) {
        return {};
    }
    return ui::fbd_rung_svg(parsed.source.rungs[0], {0, true});
}

}  // namespace

TEST(fbd_draws_a_junction_as_a_block) {
    const std::string svg = draw(kChart);
    CHECK(svg.find("<svg class=\"fbd\"") != std::string::npos);
    CHECK(svg.find("&gt;=1") != std::string::npos);
    CHECK(svg.find(">a</text>") != std::string::npos);
    CHECK(svg.find("&amp;path=0\"") != std::string::npos);
    CHECK(svg.find("&amp;path=1\"") != std::string::npos);
    CHECK(svg.find("/swap?rung=0") != std::string::npos);
}

TEST(fbd_draws_an_and_block_for_series) {
    const std::string svg = draw("VAR\n    a : BOOL;\n    b : BOOL;\n    out : BOOL;\n"
                                 "END_VAR\nout = a & b\n");
    CHECK(svg.find(">&amp;</text>") != std::string::npos);
}

TEST(fbd_bubbles_a_negated_contact) {
    const std::string svg = draw("VAR\n    a : BOOL;\n    out : BOOL;\nEND_VAR\nout = !a\n");
    CHECK(svg.find("class=\"neg\"") != std::string::npos);
}

TEST(fbd_labels_a_timer_preset) {
    const std::string svg = draw("VAR\n    a : BOOL;\n    out : BOOL;\nEND_VAR\n"
                                 "out = TON(a, 2s)\n");
    CHECK(svg.find("TON 2s") != std::string::npos);
    CHECK(svg.find(">out</text>") != std::string::npos);
}

TEST(fbd_shows_the_counter_reset_source) {
    const std::string svg = draw("VAR\n    pulse : BOOL;\n    reset_btn : BOOL;\n"
                                 "    out : BOOL;\nEND_VAR\n"
                                 "out = CTU(pulse, reset_btn, 5)\n");
    CHECK(svg.find("R: reset_btn") != std::string::npos);
    CHECK(svg.find("reset wired") == std::string::npos);
    CHECK(svg.find("translate(8,8)") != std::string::npos);
}

TEST(fbd_page_uses_fbd_headings) {
    app::LadderSession session(kChart);
    session.set_view(app::ChartView::Fbd);
    const net::Response page = app::ladder_page(session);

    CHECK(page.body.find("<title>Scan Cycle FBD</title>") != std::string::npos);
    CHECK(page.body.find("Function block diagram") != std::string::npos);
    CHECK(page.body.find("<h2>Networks</h2>") != std::string::npos);
    CHECK(page.body.find("<h1>Ladder editor</h1>") == std::string::npos);
}

TEST(fbd_page_switches_the_drawing) {
    app::LadderSession session(kChart);
    const std::string before = session.text();
    net::PortalRouter router = app::make_ladder_router(session);

    net::Request request;
    request.method = net::HttpMethod::Get;
    request.path = "/view";
    request.query = "mode=fbd";
    const net::Response response = router.dispatch(request);

    CHECK(response.status == 303);
    CHECK(session.view() == app::ChartView::Fbd);
    CHECK(session.text() == before);

    const net::Response page = app::ladder_page(session);
    CHECK(page.body.find("<svg class=\"fbd\"") != std::string::npos);
    CHECK(page.body.find("<svg class=\"ladder\"") == std::string::npos);
}

TEST(fbd_page_refuses_an_unknown_view) {
    app::LadderSession session(kChart);
    net::PortalRouter router = app::make_ladder_router(session);

    net::Request request;
    request.method = net::HttpMethod::Get;
    request.path = "/view";
    request.query = "mode=boxes";
    const net::Response response = router.dispatch(request);

    CHECK(response.status == 400);
    CHECK(session.view() == app::ChartView::Ladder);
}

TEST(fbd_router_swaps_a_junction) {
    app::LadderSession session(kChart);
    net::PortalRouter router = app::make_ladder_router(session);

    net::Request request;
    request.method = net::HttpMethod::Get;
    request.path = "/swap";
    request.query = "rung=0&path=";
    const net::Response response = router.dispatch(request);

    CHECK(response.status == 303);
    CHECK(session.text().find("out = a & b") != std::string::npos);
}

TEST(fbd_router_refuses_to_swap_a_contact) {
    app::LadderSession session(
        "VAR\n    a : BOOL;\n    out : BOOL;\nEND_VAR\nout = a\n");
    const std::string before = session.text();
    net::PortalRouter router = app::make_ladder_router(session);

    net::Request request;
    request.method = net::HttpMethod::Get;
    request.path = "/swap";
    request.query = "rung=0&path=";
    const net::Response response = router.dispatch(request);

    CHECK(response.status == 400);
    CHECK(session.text() == before);
}
