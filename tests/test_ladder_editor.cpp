#include "harness.hpp"

#include "app/ladder_page.hpp"
#include "app/ladder_session.hpp"
#include "net/http.hpp"
#include "net/portal_server.hpp"
#include "plc/ladder_text.hpp"
#include "ui/ladder_svg.hpp"

#include <string>
#include <string_view>

using namespace sc;

namespace {

const char* kChart = R"ld(VAR
    a : BOOL;
    b : BOOL;
    out : BOOL;
END_VAR
out = (a | b)
)ld";

const Application::Watch* find_watch(const Application::WatchSnapshot& snapshot,
                                     std::string_view name) {
    for (const Application::Watch& tag : snapshot.tags) {
        if (tag.name == name) {
            return &tag;
        }
    }
    return nullptr;
}

}  // namespace

TEST(ladder_svg_draws_contacts_and_branch_links) {
    plc::LadderParseResult parsed = plc::parse_ladder_source(kChart);
    CHECK(parsed.ok);
    if (!parsed.ok || parsed.source.rungs.empty()) {
        return;
    }

    const std::string svg = ui::ladder_rung_svg(parsed.source.rungs[0], {0, true});
    CHECK(svg.find("<svg class=\"ladder\"") != std::string::npos);
    CHECK(svg.find(">a</text>") != std::string::npos);
    CHECK(svg.find("&amp;path=0\"") != std::string::npos);
    CHECK(svg.find("&amp;path=1\"") != std::string::npos);
    CHECK(svg.find("href=\"/toggle?rung=0") != std::string::npos);
}

TEST(a_negated_contact_draws_a_slash) {
    plc::LadderParseResult parsed = plc::parse_ladder_source(
        "VAR\n    a : BOOL;\n    out : BOOL;\nEND_VAR\nout = !a\n");
    CHECK(parsed.ok);
    if (!parsed.ok || parsed.source.rungs.empty()) {
        return;
    }
    const std::string svg = ui::ladder_rung_svg(parsed.source.rungs[0], {0, false});
    CHECK(svg.find("class=\"slash\"") != std::string::npos);
    CHECK(svg.find("href=") == std::string::npos);
}

TEST(ladder_session_compiles_and_steps) {
    app::LadderSession session("VAR\n    out : BOOL;\nEND_VAR\nout = TRUE\n");
    CHECK(session.parsed());
    CHECK(session.compiled());
    CHECK(session.step());

    const Application::WatchSnapshot snapshot = session.snapshot();
    CHECK(snapshot.scan == 1);
    const Application::Watch* out = find_watch(snapshot, "out");
    CHECK(out != nullptr);
    if (out != nullptr) {
        CHECK(out->present);
        CHECK(out->value.as_bool());
    }
}

TEST(ladder_session_toggles_a_contact) {
    app::LadderSession session(
        "VAR\n    a : BOOL;\n    out : BOOL;\nEND_VAR\nout = a\n");
    CHECK(session.compiled());

    CHECK(session.edit(0, [](plc::Rung& rung) {
        return plc::toggle_negated(rung, {});
    }));
    CHECK(session.text().find("out = !a") != std::string::npos);
    CHECK(session.step());

    const Application::WatchSnapshot snapshot = session.snapshot();
    const Application::Watch* out = find_watch(snapshot, "out");
    CHECK(out != nullptr);
    if (out != nullptr) {
        CHECK(out->value.as_bool());
    }
}

TEST(a_refused_edit_leaves_the_text_alone) {
    app::LadderSession session(kChart);
    const std::string before = session.text();

    CHECK(!session.edit(0, [](plc::Rung& rung) {
        return plc::toggle_negated(rung, {7});
    }));
    CHECK(session.text() == before);
    CHECK(!session.error().empty());
}

TEST(ladder_router_toggles_a_contact) {
    app::LadderSession session(kChart);
    net::PortalRouter router = app::make_ladder_router(session);

    net::Request request;
    request.method = net::HttpMethod::Get;
    request.path = "/toggle";
    request.query = "rung=0&path=0";
    const net::Response response = router.dispatch(request);

    CHECK(response.status == 303);
    CHECK(session.text().find("out = !a | b") != std::string::npos);
}

TEST(ladder_router_adds_a_series_contact) {
    app::LadderSession session(kChart);
    net::PortalRouter router = app::make_ladder_router(session);

    net::Request request;
    request.method = net::HttpMethod::Post;
    request.path = "/add";
    request.body = "rung=0&path=&tag=b&kind=series";
    const net::Response response = router.dispatch(request);

    CHECK(response.status == 303);
    CHECK(session.text().find("out = (a | b) & b") != std::string::npos);
}

TEST(ladder_router_refuses_a_bad_path) {
    app::LadderSession session(kChart);
    const std::string before = session.text();
    net::PortalRouter router = app::make_ladder_router(session);

    net::Request request;
    request.method = net::HttpMethod::Get;
    request.path = "/toggle";
    request.query = "rung=0&path=9.9";
    const net::Response response = router.dispatch(request);

    CHECK(response.status == 400);
    CHECK(session.text() == before);
}

TEST(ladder_page_shows_the_diagram_and_controls) {
    app::LadderSession session(kChart);
    const net::Response page = app::ladder_page(session);

    CHECK(page.status == 200);
    CHECK(page.body.find("<svg class=\"ladder\"") != std::string::npos);
    CHECK(page.body.find("Step one scan") != std::string::npos);
    CHECK(page.body.find("Add a rung") != std::string::npos);
}

TEST(ladder_page_escapes_a_broken_file) {
    app::LadderSession session("(* <script>alert(1)</script>\n");
    const net::Response page = app::ladder_page(session);

    CHECK(page.status == 200);
    CHECK(page.body.find("&lt;script") != std::string::npos);
    CHECK(page.body.find("<script") == std::string::npos);
}
