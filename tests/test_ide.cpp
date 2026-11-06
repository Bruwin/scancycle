#include "harness.hpp"

#include "app/ide_page.hpp"
#include "app/ide_session.hpp"
#include "net/http.hpp"

#include <string>
#include <string_view>

using namespace sc;

namespace {

const char* kIdeProgram = R"st(PROGRAM Ide
VAR
    run_light : BOOL;
    count     : INT;
END_VAR
    run_light := TRUE;
    count := count + 1;
END_PROGRAM
)st";

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

TEST(ide_session_compiles_steps_and_watches) {
    app::IdeSession session(kIdeProgram);
    CHECK(session.compiled());
    CHECK(session.diagnostics().empty());
    CHECK(session.snapshot().scan == 0);

    CHECK(session.step());
    const Application::WatchSnapshot snapshot = session.snapshot();
    CHECK(snapshot.scan == 1);
    CHECK(!snapshot.faulted);

    const Application::Watch* light = find_watch(snapshot, "run_light");
    const Application::Watch* count = find_watch(snapshot, "count");
    CHECK(light != nullptr);
    CHECK(count != nullptr);
    if (light != nullptr) {
        CHECK(light->present);
        CHECK(light->value.as_bool());
    }
    if (count != nullptr) {
        CHECK(count->present);
        CHECK(count->value.as_int() == 1);
    }
}

TEST(ide_session_reports_a_parse_failure) {
    app::IdeSession session("PROGRAM Broken\nVAR\n    x : BOOL;\n");
    CHECK(!session.compiled());
    CHECK(!session.diagnostics().empty());
    CHECK(!session.step());
    CHECK(session.error().find("compile a program") != std::string::npos);
}

TEST(ide_session_forces_and_clears_a_bound_output) {
    app::IdeSession session(kIdeProgram);
    CHECK(session.compiled());
    CHECK(session.set_force("run_light", false));
    CHECK(session.snapshot().forces.size() == 1);

    CHECK(session.step());
    const Application::WatchSnapshot forced = session.snapshot();
    const Application::Watch* light = find_watch(forced, "run_light");
    CHECK(light != nullptr);
    if (light != nullptr) {
        CHECK(light->value.as_bool());
        CHECK(light->forced);
    }

    CHECK(session.clear_force("run_light"));
    CHECK(session.snapshot().forces.empty());
    CHECK(!session.clear_force("run_light"));
}

TEST(ide_session_marks_only_bound_io_tags_forceable) {
    app::IdeSession session(kIdeProgram);
    const Application::WatchSnapshot snapshot = session.snapshot();
    const Application::Watch* light = find_watch(snapshot, "run_light");
    const Application::Watch* count = find_watch(snapshot, "count");
    CHECK(light != nullptr);
    CHECK(count != nullptr);
    if (light != nullptr) {
        CHECK(light->forceable);
    }
    if (count != nullptr) {
        CHECK(!count->forceable);
    }
}

TEST(ide_page_only_offers_forces_for_bound_io_tags) {
    app::IdeSession session(kIdeProgram);
    const net::Response response = app::ide_page(session);
    CHECK(response.body.find("name=\"name\" value=\"run_light\"") != std::string::npos);
    CHECK(response.body.find("name=\"name\" value=\"count\"") == std::string::npos);
    CHECK(response.body.find("not an I/O tag") != std::string::npos);
}

TEST(ide_page_shows_the_forced_value) {
    app::IdeSession session(kIdeProgram);
    CHECK(session.step());
    CHECK(session.set_force("run_light", false));

    const net::Response response = app::ide_page(session);
    const std::string row =
        "<code>run_light</code></td><td>BOOL</td><td>TRUE</td><td>FALSE</td>";
    CHECK(response.body.find(row) != std::string::npos);
}

TEST(ide_page_escapes_the_editor_buffer) {
    app::IdeSession session(std::string("(* <script>alert(1)</script> *)\n") + kIdeProgram);
    CHECK(session.compiled());

    const net::Response response = app::ide_page(session);
    CHECK(response.status == 200);
    CHECK(response.body.find("&lt;script") != std::string::npos);
    CHECK(response.body.find("<script") == std::string::npos);
}

TEST(ide_router_steps_a_scan_and_redirects) {
    app::IdeSession session(kIdeProgram);
    net::PortalRouter router = app::make_ide_router(session);

    net::Request page_request;
    page_request.method = net::HttpMethod::Get;
    page_request.path = "/";
    const net::Response page = router.dispatch(page_request);
    CHECK(page.status == 200);
    CHECK(page.body.find("Scan Cycle IDE") != std::string::npos);

    net::Request step_request;
    step_request.method = net::HttpMethod::Post;
    step_request.path = "/step";
    const net::Response step = router.dispatch(step_request);
    CHECK(step.status == 303);
    CHECK(step.location == "/");
    CHECK(session.snapshot().scan == 1);

    const std::string wire = net::format_http_response(step);
    CHECK(wire.rfind("HTTP/1.1 303 See Other\r\n", 0) == 0);
    CHECK(wire.find("Location: /\r\n") != std::string::npos);
}

TEST(ide_router_can_replace_the_program_with_a_broken_one) {
    app::IdeSession session(kIdeProgram);
    net::PortalRouter router = app::make_ide_router(session);

    net::Request apply;
    apply.method = net::HttpMethod::Post;
    apply.path = "/apply";
    apply.body = "source=broken";
    const net::Response response = router.dispatch(apply);
    CHECK(response.status == 303);
    CHECK(response.location == "/");
    CHECK(!session.compiled());
    CHECK(!session.diagnostics().empty());
}

TEST(ide_page_shows_compile_diagnostics) {
    app::IdeSession session("PROGRAM Broken\nVAR\n    x : BOOL;\n");
    const net::Response page = app::ide_page(session);
    CHECK(page.body.find("Diagnostics") != std::string::npos);
    CHECK(page.body.find("not compiled") != std::string::npos);
}

TEST(ide_page_has_no_free_play_banner_by_default) {
    app::IdeSession session(kIdeProgram);
    const net::Response page = app::ide_page(session);

    CHECK(page.body.find("Scan Cycle IDE") != std::string::npos);
    CHECK(page.body.find("Free Play") == std::string::npos);
    CHECK(page.body.find("data-free-play") == std::string::npos);
}

TEST(ide_free_play_page_shows_banner_and_reset) {
    app::IdeSession session(kIdeProgram);
    app::IdePageOptions options;
    options.free_play = true;
    options.starter_source = kIdeProgram;

    const net::Response page = app::ide_page(session, options);

    CHECK(page.status == 200);
    CHECK(page.body.find("Scan Cycle Free Play") != std::string::npos);
    CHECK(page.body.find("data-free-play") != std::string::npos);
    CHECK(page.body.find("no grades") != std::string::npos);
    CHECK(page.body.find("not a security sandbox") != std::string::npos);
    CHECK(page.body.find("Reset to starter") != std::string::npos);
    CHECK(page.body.find("data-free-play-reset") != std::string::npos);
    CHECK(page.body.find("viewport-fit=cover") != std::string::npos);
    CHECK(page.body.find("env(safe-area-inset-left)") != std::string::npos);
    CHECK(page.body.find("focus-visible") != std::string::npos);
    CHECK(page.body.find("for=\"program-source\"") != std::string::npos);
    CHECK(page.body.find("id=\"program-source\"") != std::string::npos);
    CHECK(page.body.find("table-scroll") != std::string::npos);
}

TEST(ide_free_play_reset_restores_the_starter_and_clears_forces) {
    app::IdeSession session(kIdeProgram);
    app::IdePageOptions options;
    options.free_play = true;
    options.starter_source = kIdeProgram;
    net::PortalRouter router = app::make_ide_router(session, options);

    net::Request step;
    step.method = net::HttpMethod::Post;
    step.path = "/step";
    CHECK(router.dispatch(step).status == 303);
    CHECK(session.snapshot().scan == 1);
    CHECK(session.set_force("run_light", false));

    net::Request reset;
    reset.method = net::HttpMethod::Post;
    reset.path = "/reset";
    const net::Response response = router.dispatch(reset);

    CHECK(response.status == 303);
    CHECK(response.location == "/");
    CHECK(session.snapshot().scan == 0);
    CHECK(session.snapshot().forces.empty());
    CHECK(session.source() == kIdeProgram);
    CHECK(session.compiled());
}

TEST(ide_free_play_router_has_no_grade_route) {
    app::IdeSession session(kIdeProgram);
    app::IdePageOptions options;
    options.free_play = true;
    options.starter_source = kIdeProgram;
    const net::PortalRouter router = app::make_ide_router(session, options);

    net::Request grade;
    grade.method = net::HttpMethod::Get;
    grade.path = "/grade";
    CHECK(router.dispatch(grade).status == 404);
}
