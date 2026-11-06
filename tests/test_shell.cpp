#include "harness.hpp"

#include "app/shell_page.hpp"
#include "net/portal_server.hpp"

#include <string>
#include <string_view>
#include <utility>

using namespace sc;

namespace {

net::Response get(const net::PortalRouter& router, std::string path) {
    net::Request request;
    request.method = net::HttpMethod::Get;
    request.path = std::move(path);
    return router.dispatch(request);
}

bool contains(const std::string& text, std::string_view needle) {
    return text.find(needle) != std::string::npos;
}

}  // namespace

TEST(shell_intro_has_the_start_menu_and_hides_teacher_by_default) {
    app::ShellOptions options;
    const net::PortalRouter router = app::make_shell_router(options);
    const net::Response response = get(router, "/");

    CHECK(response.status == 200);
    CHECK(response.content_type.find("text/html") == 0);
    CHECK(response.body.rfind("<!doctype html>", 0) == 0);
    CHECK(!contains(response.body, "<script><!doctype"));
    CHECK(contains(response.body, "Scan Cycle"));
    CHECK(contains(response.body, "New session"));
    CHECK(contains(response.body, "Load session"));
    CHECK(contains(response.body, "Free Play"));
    CHECK(contains(response.body, options.free_play_url));
    CHECK(contains(response.body, "Settings"));
    CHECK(contains(response.body, "viewport-fit=cover"));
    CHECK(contains(response.body, "autofocus"));
    CHECK(contains(response.body, "focus-visible"));
    CHECK(contains(response.body, "env(safe-area-inset-left)"));
    CHECK(!contains(response.body, "Teacher portal"));
    CHECK(!contains(response.body, "Teacher mode"));
}

TEST(shell_teacher_mode_reveals_the_portal_entry_and_redirect) {
    app::ShellOptions options;
    options.teacher_mode = true;
    options.portal_url = "http://127.0.0.1:8080/";
    const net::PortalRouter router = app::make_shell_router(options);

    const net::Response intro = get(router, "/");
    CHECK(contains(intro.body, "Teacher portal"));
    CHECK(contains(intro.body, options.portal_url));

    const net::Response redirect = get(router, "/teacher");
    CHECK(redirect.status == 303);
    CHECK(redirect.location == options.portal_url);
}

TEST(shell_free_play_entry_uses_the_configured_url_in_both_modes) {
    app::ShellOptions options;
    options.free_play_url = "http://127.0.0.1:9091/free-play";

    const net::PortalRouter student = app::make_shell_router(options);
    const net::Response student_intro = get(student, "/");
    CHECK(contains(student_intro.body, options.free_play_url));
    CHECK(contains(student_intro.body, "Free Play"));

    options.teacher_mode = true;
    const net::PortalRouter teacher = app::make_shell_router(options);
    const net::Response teacher_intro = get(teacher, "/");
    CHECK(contains(teacher_intro.body, options.free_play_url));
    CHECK(contains(teacher_intro.body, "Free Play"));
}

TEST(shell_teacher_route_is_not_available_in_student_mode) {
    app::ShellOptions options;
    const net::PortalRouter router = app::make_shell_router(options);
    const net::Response response = get(router, "/teacher");

    CHECK(response.status == 404);
    CHECK(contains(response.body, "Teacher access"));
}

TEST(shell_factory_page_has_the_canvas_and_controls) {
    app::ShellOptions options;
    const net::PortalRouter router = app::make_shell_router(options);
    const net::Response response = get(router, "/factory");

    CHECK(response.status == 200);
    CHECK(contains(response.body, "data-factory-canvas"));
    CHECK(contains(response.body, "data-factory-controls"));
    CHECK(contains(response.body, "Drag to orbit"));
    CHECK(contains(response.body, "Scroll to zoom"));
    CHECK(contains(response.body, "Open PLC IDE"));
    CHECK(contains(response.body, "aria-current=\"page\""));
}

TEST(shell_scene_is_built_from_the_procedural_kit_layout) {
    app::ShellOptions options;
    const net::PortalRouter router = app::make_shell_router(options);
    const net::Response response = get(router, "/api/factory");

    CHECK(response.status == 200);
    CHECK(response.content_type == "application/json");
    CHECK(contains(response.body, "\"kits\""));
    CHECK(contains(response.body, "\"press\""));
    CHECK(contains(response.body, "\"conveyor\""));
    CHECK(contains(response.body, "\"pieces\""));
    CHECK(contains(response.body, "\"m\":"));
    CHECK(!contains(response.body, "\"pieces\":[]"));
}

TEST(shell_sessions_and_settings_are_real_local_pages) {
    app::ShellOptions options;
    const net::PortalRouter router = app::make_shell_router(options);

    const net::Response sessions = get(router, "/sessions");
    CHECK(sessions.status == 200);
    CHECK(contains(sessions.body, "Choose a session"));
    CHECK(contains(sessions.body, "/factory?focus=press"));
    CHECK(contains(sessions.body, "Back to menu"));

    const net::Response settings = get(router, "/settings");
    CHECK(settings.status == 200);
    CHECK(contains(settings.body, "settings-form"));
    CHECK(contains(settings.body, "setting-grid"));
    CHECK(contains(settings.body, "setting-motion"));
    CHECK(contains(settings.body, "autofocus"));
    CHECK(contains(settings.body, "Back to menu"));
}

TEST(shell_health_endpoint_reports_the_surface) {
    app::ShellOptions options;
    const net::PortalRouter router = app::make_shell_router(options);
    const net::Response response = get(router, "/api/health");

    CHECK(response.status == 200);
    CHECK(contains(response.body, "\"surface\":\"shell\""));
}
