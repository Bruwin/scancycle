#include "harness.hpp"

#include "app/sfc_page.hpp"
#include "app/sfc_session.hpp"
#include "net/http.hpp"
#include "net/portal_server.hpp"
#include "ui/sfc_svg.hpp"

#include <string>
#include <vector>

using namespace sc;

namespace {

const char* kChart = R"sfc(VAR
    motor : BOOL;
    start : BOOL;
END_VAR

INITIAL idle

STEP idle
END_STEP

STEP fill
    ACTION motor
END_STEP

TRANSITION idle -> fill := start
TRANSITION fill -> idle
)sfc";

const char* kLongGuards = R"sfc(VAR
    part_in_place : BOOL;
    clamped : BOOL;
    at_bottom : BOOL;
    at_top : BOOL;
    motor : BOOL;
END_VAR

INITIAL idle

STEP idle
END_STEP

STEP lower
    ACTION motor
END_STEP

STEP clamp
    ACTION motor
END_STEP

STEP raise
    ACTION motor
END_STEP

STEP home
END_STEP

TRANSITION idle -> lower := part_in_place
TRANSITION lower -> clamp := at_bottom
TRANSITION clamp -> raise := clamped
TRANSITION raise -> home := at_top
)sfc";

}  // namespace

TEST(sfc_session_compiles_and_starts_in_its_initial_step) {
    app::SfcSession session(kChart);
    CHECK(session.parsed());
    CHECK(session.compiled());
    CHECK(session.step());

    const std::vector<bool> active = session.active_steps();
    CHECK(active.size() == 2);
    if (active.size() == 2) {
        CHECK(active[0]);
        CHECK(!active[1]);
    }
    CHECK(session.snapshot().scan == 1);
}

TEST(sfc_drawing_marks_the_active_step) {
    app::SfcSession session(kChart);
    CHECK(session.step());

    const std::string svg = ui::sfc_chart_svg(session.chart(), {session.active_steps()});
    CHECK(svg.find("<svg class=\"sfc\"") != std::string::npos);
    CHECK(svg.find(">idle</text>") != std::string::npos);
    CHECK(svg.find("+ motor") != std::string::npos);
    CHECK(svg.find("class=\"step active\"") != std::string::npos);
    CHECK(svg.find(">start</text>") != std::string::npos);
    CHECK(svg.find("always") != std::string::npos);
}

TEST(sfc_long_guards_widen_the_drawing) {
    app::SfcSession session(kLongGuards);
    CHECK(session.parsed());
    if (!session.parsed()) {
        return;
    }

    const std::string svg = ui::sfc_chart_svg(session.chart(), {});
    CHECK(svg.find("<g transform=\"translate(10,10)\">") != std::string::npos);

    const std::string prefix = "viewBox=\"0 0 ";
    const std::size_t view = svg.find(prefix);
    CHECK(view != std::string::npos);
    if (view != std::string::npos) {
        const std::size_t start = view + prefix.size();
        const std::size_t end = svg.find('"', start);
        const int width = std::stoi(svg.substr(start, end - start));
        CHECK(width > 500);
    }
}

TEST(sfc_page_says_not_started_before_the_first_scan) {
    app::SfcSession session(kChart);
    const net::Response page = app::sfc_page(session);
    CHECK(page.body.find("Not started") != std::string::npos);
    CHECK(page.body.find("initial step activates") != std::string::npos);

    CHECK(session.step());
    const net::Response after = app::sfc_page(session);
    CHECK(after.body.find("Running") != std::string::npos);
}

TEST(sfc_page_shows_the_chart_and_controls) {
    app::SfcSession session(kChart);
    const net::Response page = app::sfc_page(session);

    CHECK(page.status == 200);
    CHECK(page.body.find("<svg class=\"sfc\"") != std::string::npos);
    CHECK(page.body.find("Step one scan") != std::string::npos);
    CHECK(page.body.find("Transitions") != std::string::npos);
    CHECK(page.body.find("Replace the chart") != std::string::npos);
    CHECK(page.body.find("data-zoomable") != std::string::npos);
    CHECK(page.body.find("data-zoom-in") != std::string::npos);
    CHECK(page.body.find("data-zoom-out") != std::string::npos);
    CHECK(page.body.find("data-zoom-reset") != std::string::npos);
    CHECK(page.body.find("data-zoom-level") != std::string::npos);
    CHECK(page.body.find("aria-label=\"Sequential function chart.") != std::string::npos);
}

TEST(sfc_router_steps_the_chart) {
    app::SfcSession session(kChart);
    net::PortalRouter router = app::make_sfc_router(session);

    net::Request request;
    request.method = net::HttpMethod::Post;
    request.path = "/step";
    const net::Response response = router.dispatch(request);

    CHECK(response.status == 303);
    CHECK(session.snapshot().scan == 1);
}

TEST(sfc_page_escapes_a_broken_chart) {
    app::SfcSession session("(* <script>alert(1)</script>\n");
    const net::Response page = app::sfc_page(session);

    CHECK(page.status == 200);
    CHECK(page.body.find("&lt;script&gt;alert(1)&lt;/script&gt;") != std::string::npos);
    CHECK(page.body.find("<script>alert(1)") == std::string::npos);
}
