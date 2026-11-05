#include "harness.hpp"

#include "app/lesson_runner.hpp"
#include "lesson/grader.hpp"
#include "app/lesson_api.hpp"
#include "lesson/lesson.hpp"
#include "net/http.hpp"
#include "net/http_server.hpp"
#include "net/portal_server.hpp"
#include "sim/plant.hpp"

#include <cctype>
#include <thread>

using namespace sc;

namespace {

std::string form_encode(std::string_view text) {
    std::string out;
    for (const char character : text) {
        const auto byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte) != 0 || character == '_' || character == '-' || character == '.') {
            out += character;
        } else if (character == ' ') {
            out += '+';
        } else {
            const char* hex = "0123456789ABCDEF";
            out += '%';
            out += hex[(byte >> 4) & 0xF];
            out += hex[byte & 0xF];
        }
    }
    return out;
}

}  // namespace

TEST(lesson_grants_only_its_instruction_set) {
    const lesson::Lesson built_in = lesson::make_conveyor_interlock_lesson();

    CHECK(lesson::instruction_allowed(built_in, "TON"));
    CHECK(lesson::instruction_allowed(built_in, "AND"));
    CHECK(!lesson::instruction_allowed(built_in, "CTU"));

    lesson::Lesson unrestricted;
    CHECK(lesson::instruction_allowed(unrestricted, "anything"));
}

TEST(grader_passes_a_correct_plant) {
    const lesson::Lesson built_in = lesson::make_conveyor_interlock_lesson();

    sim::Plant plant;
    const u32 motor = plant.add(sim::DeviceKind::Motor, "motor");
    plant.set_output(motor, true);

    const lesson::GradeReport report = lesson::evaluate(built_in, plant, true);

    CHECK(report.lesson_id == "conveyor-interlock");
    CHECK(report.checks.size() == built_in.rubric.size());
    CHECK(report.passed());
    CHECK(report.passed_count == report.checks.size());
}

TEST(grader_fails_a_wrong_plant) {
    const lesson::Lesson built_in = lesson::make_conveyor_interlock_lesson();

    sim::Plant plant;
    plant.add(sim::DeviceKind::Motor, "motor");  // never commanded on

    const lesson::GradeReport report = lesson::evaluate(built_in, plant, false);

    CHECK(!report.passed());
    CHECK(report.passed_count < report.checks.size());
    for (const lesson::CheckResult& check : report.checks) {
        CHECK(!check.passed);
        CHECK(!check.detail.empty());
    }
}

TEST(grader_reports_a_missing_device_instead_of_crashing) {
    lesson::Lesson spot_check;
    spot_check.id = "spot-check";
    spot_check.rubric.push_back(
        lesson::RubricCheck{"on", "Motor on", lesson::CheckKind::DeviceOutputTrue, 9, 0.0});

    sim::Plant empty;
    const lesson::GradeReport report = lesson::evaluate(spot_check, empty, true);

    CHECK(!report.passed());
    CHECK(report.checks.size() == 1);
    CHECK(report.checks.front().detail.find("device") != std::string::npos);
}

TEST(portal_router_serves_the_default_surface) {
    std::vector<lesson::Lesson> lessons{lesson::make_conveyor_interlock_lesson()};
    const net::PortalRouter router = net::make_default_router(lessons);
    CHECK(router.route_count() == 4);  // two JSON routes, the catalog, one lesson page

    net::Request request;
    request.method = net::HttpMethod::Get;

    request.path = "/api/health";
    const net::Response health = router.dispatch(request);
    CHECK(health.status == 200);
    CHECK(health.body.find("ok") != std::string::npos);

    request.path = "/api/lessons";
    const net::Response catalog = router.dispatch(request);
    CHECK(catalog.status == 200);
    CHECK(catalog.body.find("conveyor-interlock") != std::string::npos);

    request.path = "/";
    const net::Response page = router.dispatch(request);
    CHECK(page.status == 200);
    CHECK(page.content_type.find("text/html") != std::string::npos);
    CHECK(page.body.find("conveyor-interlock") != std::string::npos);
    CHECK(page.body.find("action=\"/grade\"") != std::string::npos);

    request.path = "/api/missing";
    const net::Response missing = router.dispatch(request);
    CHECK(missing.status == 404);

    // Method matters: a POST to a GET route is not a match.
    request.path = "/api/health";
    request.method = net::HttpMethod::Post;
    CHECK(router.dispatch(request).status == 404);
}

TEST(portal_router_escapes_json) {
    CHECK(net::json_escape("a\"b") == "a\\\"b");
    CHECK(net::json_escape("a\\b") == "a\\\\b");
}

TEST(plant_finds_devices_by_name) {
    const sim::Plant slice = sim::make_vertical_slice_plant();
    CHECK(slice.find("press") == 2);
    CHECK(slice.find("run_light") == 4);
    CHECK(slice.find("does-not-exist") == sim::kNoDevice);
}

TEST(press_lesson_owns_its_cell) {
    const lesson::Lesson press = lesson::make_press_cycle_lesson();

    CHECK(press.starting_plant.count() == 5);
    const u32 machine = press.starting_plant.find("press");
    CHECK(machine != sim::kNoDevice);
    CHECK(press.starting_plant.device(machine).kind == sim::DeviceKind::Machine);
    CHECK(press.rubric.size() == 4);
    CHECK(!press.reference_solution.empty());

    // The rubric must address this lesson's own devices, not a stale index.
    CHECK(press.rubric.front().device == machine);
}

TEST(press_lesson_reference_solution_passes_its_own_rubric) {
    const lesson::Lesson press = lesson::make_press_cycle_lesson();
    const LessonRunResult run = run_lesson(press);

    CHECK(run.ok);
    CHECK(run.error.empty());
    CHECK(!run.faulted);
    CHECK(run.scans > 0);
    CHECK(run.boxes_processed >= 3);
    CHECK(run.report.passed());
}

TEST(press_lesson_fails_a_program_that_does_nothing) {
    lesson::Lesson idle = lesson::make_press_cycle_lesson();
    idle.reference_solution =
        "PROGRAM Idle\nVAR\n    unused : BOOL;\nEND_VAR\n    unused := FALSE;\nEND_PROGRAM\n";

    const LessonRunResult run = run_lesson(idle);

    CHECK(run.ok);  // the run completed; the student simply did not pass
    CHECK(run.boxes_processed == 0);
    CHECK(!run.report.passed());
}

TEST(lesson_runner_rejects_a_lesson_without_a_program) {
    lesson::Lesson empty;
    empty.id = "empty";
    empty.starting_plant = sim::make_vertical_slice_plant();

    const LessonRunResult run = run_lesson(empty);

    CHECK(!run.ok);
    CHECK(!run.error.empty());
}

TEST(conveyor_lesson_reference_solution_passes_its_own_rubric) {
    const lesson::Lesson conveyor = lesson::make_conveyor_interlock_lesson();
    const LessonRunResult run = run_lesson(conveyor);

    CHECK(run.ok);
    CHECK(!run.faulted);
    CHECK(run.report.passed());
}

TEST(lesson_runner_rejects_a_preset_the_program_does_not_declare) {
    lesson::Lesson broken = lesson::make_conveyor_interlock_lesson();
    broken.preset_inputs.push_back({"no_such_button", true});

    const LessonRunResult run = run_lesson(broken);

    CHECK(!run.ok);
    CHECK(run.error.find("no_such_button") != std::string::npos);
}

TEST(batch_lesson_reference_solution_passes_its_own_rubric) {
    const lesson::Lesson batch = lesson::make_batch_cycle_lesson();

    CHECK(batch.starting_plant.count() == 5);
    CHECK(lesson::instruction_allowed(batch, "CTU"));
    CHECK(lesson::instruction_allowed(batch, "TON"));
    CHECK(batch.reference_solution.find("CTU") != std::string::npos);

    const LessonRunResult run = run_lesson(batch);

    CHECK(run.ok);
    CHECK(run.error.empty());
    CHECK(!run.faulted);
    CHECK(run.boxes_processed >= 3);
    CHECK(run.report.passed());
}

TEST(builtin_lessons_all_reach_the_portal_catalog) {
    const std::vector<lesson::Lesson> built = lesson::make_builtin_lessons();
    CHECK(built.size() == 4);

    std::vector<std::string> ids;
    for (const lesson::Lesson& one : built) {
        CHECK(!one.id.empty());
        CHECK(!one.reference_solution.empty());
        CHECK(one.starting_plant.count() > 0);
        for (const std::string& seen : ids) {
            CHECK(seen != one.id);
        }
        ids.push_back(one.id);
    }

    const net::PortalRouter router = net::make_default_router(built);

    net::Request request;
    request.method = net::HttpMethod::Get;
    request.path = "/api/lessons";
    const net::Response catalog = router.dispatch(request);

    CHECK(catalog.status == 200);
    CHECK(catalog.body.find("conveyor-interlock") != std::string::npos);
    CHECK(catalog.body.find("press-cycle") != std::string::npos);
    CHECK(catalog.body.find("press-batch") != std::string::npos);
    CHECK(catalog.body.find("press-batch-ladder") != std::string::npos);
}

TEST(portal_http_parser_reads_a_get) {
    const net::ParsedRequest parsed = net::parse_http_request(
        "GET /api/lessons?cache=1 HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n");

    CHECK(parsed.ok);
    CHECK(parsed.request.method == net::HttpMethod::Get);
    CHECK(parsed.request.path == "/api/lessons");
}

TEST(portal_http_parser_rejects_junk) {
    CHECK(!net::parse_http_request("nonsense\r\n\r\n").ok);
    CHECK(!net::parse_http_request("GET relative HTTP/1.1\r\n\r\n").ok);
    CHECK(!net::parse_http_request("BREW /pot HTTP/1.1\r\n\r\n").ok);
}

TEST(portal_http_formats_a_response) {
    net::Response response;
    response.body = "{\"ok\":true}";

    const std::string text = net::format_http_response(response);

    CHECK(text.rfind("HTTP/1.1 200 OK\r\n", 0) == 0);
    CHECK(text.find("Content-Type: application/json") != std::string::npos);
    CHECK(text.find("Content-Length: 11") != std::string::npos);
    CHECK(text.find("Connection: close") != std::string::npos);
    CHECK(text.find("{\"ok\":true}") != std::string::npos);
}

TEST(portal_transport_answers_the_catalog_over_a_socket) {
    const std::vector<lesson::Lesson> lessons = lesson::make_builtin_lessons();
    const net::PortalRouter router = net::make_default_router(lessons);

    net::HttpServer server(router);
    CHECK(server.listen(0));
    if (server.port() == 0) {
        return;
    }

    bool served = false;
    std::thread worker([&server, &served] { served = server.serve_one(); });

    std::string error;
    const std::string raw = net::http_round_trip(
        server.port(), "GET /api/lessons HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n", error);
    worker.join();

    CHECK(error.empty());
    CHECK(served);
    CHECK(raw.find("HTTP/1.1 200 OK") != std::string::npos);
    CHECK(raw.find("Content-Type: application/json") != std::string::npos);
    CHECK(raw.find("\"id\":\"conveyor-interlock\"") != std::string::npos);
}

TEST(ladder_lesson_reference_solution_passes_its_own_rubric) {
    const lesson::Lesson ladder = lesson::make_ladder_batch_lesson();

    CHECK(ladder.reference_language == lesson::ReferenceLanguage::Ladder);
    CHECK(ladder.starting_plant.count() == 5);
    CHECK(ladder.reference_solution.find("TON(") != std::string::npos);
    CHECK(ladder.reference_solution.find("CTU(") != std::string::npos);

    const LessonRunResult run = run_lesson(ladder);

    CHECK(run.ok);
    CHECK(run.error.empty());
    CHECK(!run.faulted);
    CHECK(run.boxes_processed >= 3);
    CHECK(run.report.passed());
}

TEST(portal_form_field_decodes_url_encoding) {
    CHECK(net::form_field("lesson=press-batch&program=a+b%2Bc", "lesson") == "press-batch");
    CHECK(net::form_field("lesson=press-batch&program=a+b%2Bc", "program") == "a b+c");
    CHECK(net::form_field("other=1", "lesson").empty());
    CHECK(net::form_field("lesson=", "lesson").empty());
}

TEST(portal_serves_a_catalog_page) {
    const std::vector<lesson::Lesson> lessons = lesson::make_builtin_lessons();
    const net::PortalRouter router = net::make_default_router(lessons);

    net::Request request;
    request.method = net::HttpMethod::Get;
    request.path = "/";
    const net::Response page = router.dispatch(request);

    CHECK(page.status == 200);
    CHECK(page.content_type.rfind("text/html", 0) == 0);
    CHECK(page.body.find("<form") != std::string::npos);
    CHECK(page.body.find("press-batch-ladder") != std::string::npos);
}

TEST(grade_route_runs_the_reference_solution) {
    const std::vector<lesson::Lesson> lessons = lesson::make_builtin_lessons();

    net::Request request;
    request.method = net::HttpMethod::Post;
    request.path = "/api/grade";
    request.body = "lesson=press-batch";

    const net::Response response = app::grade_lesson(lessons, request);

    CHECK(response.status == 200);
    CHECK(response.body.find("\"lesson\":\"press-batch\"") != std::string::npos);
    CHECK(response.body.find("\"passed\":true") != std::string::npos);
    CHECK(response.body.find("\"checks\":[") != std::string::npos);
}

TEST(grade_route_rejects_an_unknown_lesson) {
    const std::vector<lesson::Lesson> lessons = lesson::make_builtin_lessons();

    net::Request request;
    request.method = net::HttpMethod::Post;
    request.path = "/api/grade";
    request.body = "lesson=no-such-lesson";

    const net::Response response = app::grade_lesson(lessons, request);

    CHECK(response.status == 404);
    CHECK(response.body.find("unknown lesson") != std::string::npos);
}

TEST(grade_route_grades_a_submitted_program) {
    const std::vector<lesson::Lesson> lessons = lesson::make_builtin_lessons();
    const std::string idle =
        "PROGRAM Idle\nVAR\n    unused : BOOL;\nEND_VAR\n    unused := FALSE;\nEND_PROGRAM\n";

    net::Request request;
    request.method = net::HttpMethod::Post;
    request.path = "/api/grade";
    request.body = "lesson=press-cycle&program=" + form_encode(idle);

    const net::Response response = app::grade_lesson(lessons, request);

    CHECK(response.status == 200);
    CHECK(response.body.find("\"lesson\":\"press-cycle\"") != std::string::npos);
    CHECK(response.body.find("\"passed\":false") != std::string::npos);
}

TEST(lesson_page_explains_the_rubric_before_submitting) {
    const std::vector<lesson::Lesson> lessons = lesson::make_builtin_lessons();
    const net::PortalRouter router = net::make_default_router(lessons);

    net::Request request;
    request.method = net::HttpMethod::Get;
    request.path = "/lessons/press-batch";
    const net::Response page = router.dispatch(request);

    CHECK(page.status == 200);
    CHECK(page.content_type.rfind("text/html", 0) == 0);
    CHECK(page.body.find("count a batch") != std::string::npos);
    CHECK(page.body.find("Delay the line start by a second") != std::string::npos);
    CHECK(page.body.find("Press completes three parts") != std::string::npos);
    CHECK(page.body.find("press: at least 3 parts processed") != std::string::npos);
    CHECK(page.body.find("<textarea name=\"program\"") != std::string::npos);
    CHECK(page.body.find("<option value=\"press-batch\" selected>") != std::string::npos);
}

TEST(lesson_page_is_absent_for_an_unknown_id) {
    const std::vector<lesson::Lesson> lessons = lesson::make_builtin_lessons();
    const net::PortalRouter router = net::make_default_router(lessons);

    net::Request request;
    request.method = net::HttpMethod::Get;
    request.path = "/lessons/no-such-lesson";

    CHECK(router.dispatch(request).status == 404);
}

TEST(portal_escapes_page_text) {
    CHECK(net::html_escape("<a & b>") == "&lt;a &amp; b&gt;");
    CHECK(net::html_escape("\"quoted\"") == "&quot;quoted&quot;");
}

TEST(grade_page_renders_the_rubric) {
    const std::vector<lesson::Lesson> lessons = lesson::make_builtin_lessons();

    net::Request request;
    request.method = net::HttpMethod::Post;
    request.path = "/grade";
    request.body = "lesson=press-batch";

    const net::Response response = app::grade_lesson_page(lessons, request);

    CHECK(response.status == 200);
    CHECK(response.content_type.rfind("text/html", 0) == 0);
    CHECK(response.body.find("all 4 checks passed") != std::string::npos);
    CHECK(response.body.find("Press completes three parts") != std::string::npos);
    CHECK(response.body.find("<td class=\"pass\">pass") != std::string::npos);
    CHECK(response.body.find("href=\"/\"") != std::string::npos);
}

TEST(grade_page_explains_an_unknown_lesson) {
    const std::vector<lesson::Lesson> lessons = lesson::make_builtin_lessons();

    net::Request request;
    request.method = net::HttpMethod::Post;
    request.path = "/grade";
    request.body = "lesson=no-such-lesson";

    const net::Response response = app::grade_lesson_page(lessons, request);

    CHECK(response.status == 404);
    CHECK(response.content_type.rfind("text/html", 0) == 0);
    CHECK(response.body.find("unknown lesson") != std::string::npos);
    CHECK(response.body.find("Back to the catalog") != std::string::npos);
}

TEST(grade_page_keeps_the_submitted_program) {
    const std::vector<lesson::Lesson> lessons = lesson::make_builtin_lessons();
    const std::string idle =
        "PROGRAM Idle\nVAR\n    unused : BOOL;\nEND_VAR\n    unused := FALSE;\nEND_PROGRAM\n";

    net::Request request;
    request.method = net::HttpMethod::Post;
    request.path = "/grade";
    request.body = "lesson=press-cycle&program=" + form_encode(idle);

    const net::Response response = app::grade_lesson_page(lessons, request);

    CHECK(response.status == 200);
    CHECK(response.body.find("unused := FALSE;") != std::string::npos);
    CHECK(response.body.find("<textarea name=\"program\"") != std::string::npos);
    CHECK(response.body.find("<option value=\"press-cycle\" selected>") != std::string::npos);
}

TEST(grade_page_keeps_a_program_that_failed_to_compile) {
    const std::vector<lesson::Lesson> lessons = lesson::make_builtin_lessons();
    const std::string broken =
        "PROGRAM Broken\nVAR\n    x : BOOL;\nEND_VAR\n    x := ;\nEND_PROGRAM\n";

    net::Request request;
    request.method = net::HttpMethod::Post;
    request.path = "/grade";
    request.body = "lesson=conveyor-interlock&program=" + form_encode(broken);

    const net::Response response = app::grade_lesson_page(lessons, request);

    CHECK(response.status == 500);
    CHECK(response.body.find("Could not grade") != std::string::npos);
    CHECK(response.body.find("x := ;") != std::string::npos);
    CHECK(response.body.find("Your program is still here") != std::string::npos);
}

TEST(portal_detects_a_body_that_did_not_arrive_whole) {
    CHECK(net::request_body_complete("GET / HTTP/1.1\r\n\r\n"));
    CHECK(net::request_body_complete(
        "POST /grade HTTP/1.1\r\nContent-Length: 5\r\n\r\nabcde"));
    CHECK(!net::request_body_complete(
        "POST /grade HTTP/1.1\r\nContent-Length: 5\r\n\r\nabc"));
    CHECK(!net::request_body_complete("POST /grade HTTP/1.1\r\nContent-Len"));
}

TEST(portal_transport_grades_over_a_socket) {
    const std::vector<lesson::Lesson> lessons = lesson::make_builtin_lessons();
    net::PortalRouter router = net::make_default_router(lessons);
    router.route(net::HttpMethod::Post, "/api/grade",
                 [&lessons](const net::Request& request) {
                     return app::grade_lesson(lessons, request);
                 });

    net::HttpServer server(router);
    CHECK(server.listen(0));
    if (server.port() == 0) {
        return;
    }

    const std::string body = "lesson=press-batch";
    const std::string raw = "POST /api/grade HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: " +
                            std::to_string(body.size()) + "\r\n\r\n" + body;

    bool served = false;
    std::thread worker([&server, &served] { served = server.serve_one(); });

    std::string error;
    const std::string answer = net::http_round_trip(server.port(), raw, error);
    worker.join();

    CHECK(error.empty());
    CHECK(served);
    CHECK(answer.find("HTTP/1.1 200 OK") != std::string::npos);
    CHECK(answer.find("\"lesson\":\"press-batch\"") != std::string::npos);
}
