#include "harness.hpp"

#include "app/grade_log.hpp"
#include "app/grades_coverage.hpp"
#include "app/grades_csv.hpp"
#include "app/grades_page.hpp"
#include "app/grades_pager.hpp"
#include "app/lesson_runner.hpp"
#include "lesson/grader.hpp"
#include "app/lesson_api.hpp"
#include "lesson/lesson.hpp"
#include "net/http.hpp"
#include "net/http_server.hpp"
#include "net/portal_server.hpp"
#include "sim/plant.hpp"

#include <cctype>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

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
    CHECK(parsed.request.query == "cache=1");
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
    CHECK(page.body.find("<th>Lesson</th>") != std::string::npos);
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

TEST(lesson_page_lists_the_lesson_toolbox) {
    lesson::Lesson restricted;
    restricted.id = "restricted";
    restricted.title = "Restricted";
    restricted.allowed_instructions = {"TON", "CTU", "AND", ":=", "INT"};

    lesson::Lesson open;
    open.id = "open";
    open.title = "Open";

    const std::vector<lesson::Lesson> lessons{restricted, open};
    const net::PortalRouter router = net::make_default_router(lessons);

    net::Request request;
    request.method = net::HttpMethod::Get;
    request.path = "/lessons/restricted";
    const net::Response tool = router.dispatch(request);

    CHECK(tool.status == 200);
    CHECK(tool.body.find("<td>Instruction</td><td><code>TON</code>, <code>CTU</code></td>") !=
          std::string::npos);
    CHECK(tool.body.find("<td>Operator</td><td><code>AND</code>, <code>:=</code></td>") !=
          std::string::npos);
    CHECK(tool.body.find("<td>Data type</td><td><code>INT</code></td>") != std::string::npos);
    CHECK(tool.body.find("Every instruction is available") == std::string::npos);

    request.path = "/lessons/open";
    const net::Response every = router.dispatch(request);
    CHECK(every.status == 200);
    CHECK(every.body.find("Every instruction is available") != std::string::npos);
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

TEST(toolbox_allows_treats_an_empty_list_as_everything) {
    const std::vector<std::string> none;
    CHECK(lesson::toolbox_allows(none, "CTU"));

    const std::vector<std::string> granted{"TON", "AND", "BOOL"};
    CHECK(lesson::toolbox_allows(granted, "TON"));
    CHECK(!lesson::toolbox_allows(granted, "CTU"));
}

TEST(grade_route_refuses_an_instruction_the_lesson_withholds) {
    const std::vector<lesson::Lesson> lessons = lesson::make_builtin_lessons();
    // conveyor-interlock grants TON but not TOF.
    const std::string tof_program =
        "PROGRAM Delay\nVAR\n    enable : BOOL;\n    timer : TOF;\n    out : BOOL;\n"
        "END_VAR\n    timer(IN := enable, PT := T#100ms);\n    out := timer.Q;\n"
        "END_PROGRAM\n";

    net::Request request;
    request.method = net::HttpMethod::Post;
    request.path = "/grade";
    request.body = "lesson=conveyor-interlock&program=" + form_encode(tof_program);

    const net::Response page = app::grade_lesson_page(lessons, request);
    CHECK(page.status == 200);
    CHECK(page.body.find("Uses only the instructions this lesson grants") != std::string::npos);
    CHECK(page.body.find("uses 'TOF'") != std::string::npos);
    CHECK(page.body.find("not run: the program was refused") != std::string::npos);
    CHECK(page.body.find("0 of ") != std::string::npos);
    CHECK(page.body.find("<textarea name=\"program\"") != std::string::npos);

    const net::Response json = app::grade_lesson(lessons, request);
    CHECK(json.status == 200);
    CHECK(json.body.find("\"lesson\":\"conveyor-interlock\"") != std::string::npos);
    CHECK(json.body.find("\"passed\":false") != std::string::npos);
    CHECK(json.body.find("\"id\":\"toolbox\"") != std::string::npos);
}

TEST(refused_report_explains_the_withheld_instruction) {
    const lesson::Lesson conveyor = lesson::make_conveyor_interlock_lesson();
    const lesson::GradeReport report = lesson::refused_report(conveyor, "TOF");

    CHECK(report.lesson_id == "conveyor-interlock");
    CHECK(!report.passed());
    CHECK(report.passed_count == 0);
    CHECK(report.checks.size() == conveyor.rubric.size() + 1);
    CHECK(report.checks.front().id == "toolbox");
    CHECK(!report.checks.front().passed);
    CHECK(report.checks.front().detail.find("TOF") != std::string::npos);
    CHECK(report.checks.front().detail.find("grant") != std::string::npos);
    CHECK(report.checks.back().detail == "not run: the program was refused");
}

TEST(a_graded_attempt_is_written_to_the_log) {
    const std::vector<lesson::Lesson> lessons = lesson::make_builtin_lessons();
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "scancycle-portal-log-test.tsv";
    std::filesystem::remove(path);
    app::GradeLog log(path.string());

    net::Request request;
    request.method = net::HttpMethod::Post;
    request.path = "/grade";
    request.body = "lesson=press-batch";

    const net::Response response = app::grade_lesson_page(lessons, request, &log);
    CHECK(response.status == 200);

    std::string error;
    const std::vector<app::GradeRecord> entries = log.entries(error);
    CHECK(error.empty());
    CHECK(entries.size() == 1);
    CHECK(entries[0].lesson_id == "press-batch");
    CHECK(entries[0].passed);
    CHECK(entries[0].passed_count == entries[0].total);
    CHECK(entries[0].program.empty());

    std::filesystem::remove(path);
}

TEST(grades_page_lists_the_newest_attempt_first) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "scancycle-grades-page-test.tsv";
    std::filesystem::remove(path);
    app::GradeLog log(path.string());

    std::string error;
    app::GradeRecord older;
    older.at = 1700000000;
    older.lesson_id = "press-batch";
    older.passed = true;
    older.passed_count = 4;
    older.total = 4;
    CHECK(log.append(older, error));

    app::GradeRecord newer;
    newer.at = 1700000001;
    newer.lesson_id = "conveyor-interlock";
    newer.passed = false;
    newer.passed_count = 1;
    newer.total = 3;
    newer.program = "PROGRAM X\nEND_PROGRAM\n";
    CHECK(log.append(newer, error));

    const net::Response page = app::grades_page(log);
    CHECK(page.status == 200);
    CHECK(page.content_type.rfind("text/html", 0) == 0);
    CHECK(page.body.find("Graded attempts") != std::string::npos);
    CHECK(page.body.find("2 attempts recorded") != std::string::npos);
    CHECK(page.body.find("conveyor-interlock") < page.body.find("press-batch"));
    CHECK(page.body.find("class=\"fail\">fail") != std::string::npos);
    CHECK(page.body.find("class=\"pass\">pass") != std::string::npos);
    CHECK(page.body.find("bytes") != std::string::npos);

    std::filesystem::remove(path);
}

TEST(grades_page_summarises_each_lesson) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "scancycle-grades-summary-test.tsv";
    std::filesystem::remove(path);
    app::GradeLog log(path.string());
    std::string error;

    const auto append = [&log, &error](std::int64_t at, const std::string& id, bool passed) {
        app::GradeRecord record;
        record.at = at;
        record.lesson_id = id;
        record.passed = passed;
        record.passed_count = passed ? 1 : 0;
        record.total = 1;
        return log.append(record, error);
    };

    CHECK(append(1700000000, "press-batch", true));
    CHECK(append(1700000060, "press-batch", false));
    CHECK(append(1700000120, "conveyor-interlock", false));
    CHECK(append(1700000180, "press-batch", false));

    const net::Response page = app::grades_page(log);
    CHECK(page.status == 200);

    // Three attempts, one pass: a whole percentage, rounded down. The lesson is
    // a filter link, which is why the cell wraps the code element.
    CHECK(page.body.find("<td><a href=\"/grades?lesson=press-batch\"><code>press-batch</code>"
                         "</a></td><td>3</td><td>1</td><td>33%</td>") != std::string::npos);
    CHECK(page.body.find("<td><a href=\"/grades?lesson=conveyor-interlock\">"
                         "<code>conveyor-interlock</code></a></td><td>1</td><td>0</td>"
                         "<td>0%</td>") != std::string::npos);

    // The latest attempt is the newest record even when it failed.
    const std::size_t press =
        page.body.find("<code>press-batch</code></a></td><td>3</td>");
    CHECK(press != std::string::npos);
    CHECK(page.body.find("class=\"fail\">fail", press) != std::string::npos);

    // The summary comes before the attempt list.
    CHECK(page.body.find("By lesson") < page.body.find("Attempts</h2>"));

    std::filesystem::remove(path);
}

TEST(grades_page_lists_the_checks_the_class_misses_most) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "scancycle-grades-missed-test.tsv";
    std::filesystem::remove(path);
    app::GradeLog log(path.string());
    std::string error;

    const auto append = [&log, &error](std::int64_t at, const std::string& id,
                                       std::vector<app::GradeCheck> checks) {
        app::GradeRecord record;
        record.at = at;
        record.lesson_id = id;
        record.checks = std::move(checks);
        record.total = record.checks.size();
        for (const app::GradeCheck& check : record.checks) {
            if (check.passed) {
                ++record.passed_count;
            }
        }
        record.passed = record.passed_count == record.total;
        return log.append(record, error);
    };

    CHECK(append(1700000000, "press-batch",
                 {app::GradeCheck{true, "Output on", "motor: output on"},
                  app::GradeCheck{false, "Latch holds", "output off"}}));
    CHECK(append(1700000060, "press-batch",
                 {app::GradeCheck{true, "Output on", "motor: output on"},
                  app::GradeCheck{false, "Latch holds", "output off"}}));
    CHECK(append(1700000120, "press-batch",
                 {app::GradeCheck{true, "Output on", "motor: output on"},
                  app::GradeCheck{true, "Latch holds", "output held"}}));
    CHECK(append(1700000180, "conveyor-interlock",
                 {app::GradeCheck{false, "Latch holds", "output off"}}));
    // An attempt from before checks were stored must not count as a miss.
    app::GradeRecord legacy;
    legacy.at = 1700000240;
    legacy.lesson_id = "press-batch";
    CHECK(log.append(legacy, error));

    const net::Response page = app::grades_page(log);
    CHECK(page.status == 200);

    // Two misses in three sightings outranks one in one.
    const std::size_t worst =
        page.body.find("<td><code>press-batch</code></td><td>Latch holds</td><td>2</td>"
                       "<td>3</td><td>66%</td>");
    const std::size_t other =
        page.body.find("<td><code>conveyor-interlock</code></td><td>Latch holds</td><td>1</td>"
                       "<td>1</td><td>100%</td>");
    CHECK(worst != std::string::npos);
    CHECK(other != std::string::npos);
    CHECK(worst < other);

    // A check nobody missed is not a problem to report.
    CHECK(page.body.find("<td>Output on</td>") == std::string::npos);
    CHECK(page.body.find("<h2>Missed checks</h2>") != std::string::npos);

    std::filesystem::remove(path);
}

TEST(grades_page_says_when_there_is_nothing_missed_to_report) {
    const std::filesystem::path clean =
        std::filesystem::temp_directory_path() / "scancycle-grades-clean-test.tsv";
    std::filesystem::remove(clean);
    {
        app::GradeLog log(clean.string());
        std::string error;
        app::GradeRecord record;
        record.at = 1700000000;
        record.lesson_id = "press-batch";
        record.passed = true;
        record.passed_count = 1;
        record.total = 1;
        record.checks.push_back(app::GradeCheck{true, "Output on", "motor: output on"});
        CHECK(log.append(record, error));

        const net::Response page = app::grades_page(log);
        CHECK(page.status == 200);
        CHECK(page.body.find("No check has been missed") != std::string::npos);
        CHECK(page.body.find("<h2>Missed checks</h2>") != std::string::npos);
    }
    std::filesystem::remove(clean);

    const std::filesystem::path legacy =
        std::filesystem::temp_directory_path() / "scancycle-grades-legacy-checks.tsv";
    std::filesystem::remove(legacy);
    {
        app::GradeLog log(legacy.string());
        std::string error;
        app::GradeRecord record;
        record.at = 1700000000;
        record.lesson_id = "press-batch";
        record.passed = false;
        record.total = 2;
        CHECK(log.append(record, error));

        const net::Response page = app::grades_page(log);
        CHECK(page.status == 200);
        CHECK(page.body.find("recorded without per-check detail") != std::string::npos);
    }
    std::filesystem::remove(legacy);
}

TEST(grades_page_says_so_when_nothing_was_recorded) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "scancycle-grades-empty-test.tsv";
    std::filesystem::remove(path);
    const app::GradeLog log(path.string());

    const net::Response page = app::grades_page(log);
    CHECK(page.status == 200);
    CHECK(page.body.find("No graded attempts yet.") != std::string::npos);
    CHECK(page.body.find("0 attempts recorded") != std::string::npos);
}

TEST(attempt_page_reopens_one_report_with_its_program) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "scancycle-attempt-page-test.tsv";
    std::filesystem::remove(path);
    app::GradeLog log(path.string());
    std::string error;

    app::GradeRecord record;
    record.at = 1700000000;
    record.lesson_id = "press-batch";
    record.passed = false;
    record.passed_count = 1;
    record.total = 2;
    record.program = "PROGRAM X\nVAR\n  a : BOOL;\nEND_VAR\n  a := a < 1 AND a;\nEND_PROGRAM\n";
    record.checks.push_back(app::GradeCheck{true, "Output on", "motor: output on"});
    record.checks.push_back(app::GradeCheck{false, "Latch holds", "output off"});
    CHECK(log.append(record, error));

    const net::Response page = app::attempt_page(log, "1");
    CHECK(page.status == 200);
    CHECK(page.content_type.rfind("text/html", 0) == 0);
    CHECK(page.body.find("press-batch") != std::string::npos);
    CHECK(page.body.find("1 of 2") != std::string::npos);
    CHECK(page.body.find("Latch holds") != std::string::npos);
    CHECK(page.body.find("output off") != std::string::npos);
    CHECK(page.body.find("class=\"fail\">fail") != std::string::npos);
    CHECK(page.body.find("class=\"pass\">pass") != std::string::npos);
    CHECK(page.body.find("<pre>") != std::string::npos);
    // The program is shown as text, never parsed as markup.
    CHECK(page.body.find("a := a &lt; 1 AND a;") != std::string::npos);
    CHECK(page.body.find("a := a < 1 AND a;") == std::string::npos);
    CHECK(page.body.find("@media print") != std::string::npos);

    std::filesystem::remove(path);
}

TEST(attempt_page_is_missing_for_anything_not_recorded) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "scancycle-attempt-page-missing.tsv";
    std::filesystem::remove(path);
    app::GradeLog log(path.string());

    CHECK(app::attempt_page(log, "1").status == 404);
    CHECK(app::attempt_page(log, "0").status == 404);
    CHECK(app::attempt_page(log, "").status == 404);
    CHECK(app::attempt_page(log, "abc").status == 404);

    std::string error;
    app::GradeRecord graded;
    graded.lesson_id = "press-batch";
    graded.passed = true;
    graded.passed_count = 1;
    graded.total = 1;
    graded.checks.push_back(app::GradeCheck{true, "Output on", "motor: output on"});
    CHECK(log.append(graded, error));

    // No stored checks is what an older portal wrote: the attempt still opens,
    // it just cannot show the rubric.
    app::GradeRecord older;
    older.lesson_id = "press-batch";
    older.passed = true;
    older.passed_count = 1;
    older.total = 1;
    CHECK(log.append(older, error));

    CHECK(app::attempt_page(log, "1").status == 200);
    CHECK(app::attempt_page(log, "2").status == 200);
    CHECK(app::attempt_page(log, "2").body.find("before per-check detail") !=
          std::string::npos);
    CHECK(app::attempt_page(log, "3").status == 404);
    CHECK(app::attempt_page(log, "1.0").status == 404);
    CHECK(app::attempt_page(log, "-1").status == 404);

    std::filesystem::remove(path);
}

TEST(router_prefix_serves_a_family_of_paths) {
    net::PortalRouter router;
    router.route(net::HttpMethod::Get, "/grades", [](const net::Request&) {
        net::Response response;
        response.body = "index";
        return response;
    });
    router.route_prefix(net::HttpMethod::Get, "/grades/", [](const net::Request& request) {
        net::Response response;
        response.body = "attempt " + request.path.substr(8);
        return response;
    });

    net::Request exact;
    exact.path = "/grades";
    CHECK(router.dispatch(exact).body == "index");

    net::Request attempt;
    attempt.path = "/grades/7";
    CHECK(router.dispatch(attempt).body == "attempt 7");

    net::Request wrong_method;
    wrong_method.method = net::HttpMethod::Post;
    wrong_method.path = "/grades/7";
    CHECK(router.dispatch(wrong_method).status == 404);

    net::Request unrelated;
    unrelated.path = "/gradesX";
    CHECK(router.dispatch(unrelated).status == 404);

    CHECK(router.route_count() == 2);
}

TEST(grades_csv_exports_one_row_per_attempt) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "scancycle-grades-csv-test.tsv";
    std::filesystem::remove(path);
    app::GradeLog log(path.string());
    std::string error;

    app::GradeRecord pass;
    pass.at = 1700000000;
    pass.lesson_id = "press-batch";
    pass.student = "Ada, 2B";
    pass.passed = true;
    pass.passed_count = 1;
    pass.total = 1;
    pass.checks.push_back(app::GradeCheck{true, "Output on", "motor: output on"});
    CHECK(log.append(pass, error));

    app::GradeRecord refused;
    refused.at = 1700000060;
    refused.lesson_id = "conveyor-interlock";
    refused.passed_count = 1;
    refused.total = 3;
    refused.checks.push_back(app::GradeCheck{false, "Box, once\nor twice", "saw 1"});
    refused.checks.push_back(app::GradeCheck{false, "Latch \"held\"", "output off"});
    refused.checks.push_back(app::GradeCheck{true, "Output on", "on"});
    CHECK(log.append(refused, error));

    const net::Response csv = app::grades_csv(log);
    CHECK(csv.status == 200);
    CHECK(csv.content_type.rfind("text/csv", 0) == 0);
    CHECK(csv.body.rfind("attempt,when_utc,student,lesson,result,passed,total,missed_checks\n",
                         0) == 0);

    std::size_t lines = 0;
    for (const char character : csv.body) {
        lines += character == '\n' ? 1 : 0;
    }
    CHECK(lines == 3);

    // The named attempt quotes its name; the unnamed one leaves the cell empty.
    CHECK(csv.body.find("1,2023-11-14 22:13,\"Ada, 2B\",press-batch,pass,1,1,\n") !=
          std::string::npos);
    CHECK(csv.body.find("2,2023-11-14 22:14,,conveyor-interlock,fail,1,3,") !=
          std::string::npos);
    // Commas, quotes and line breaks survive inside one quoted cell, and the
    // passed check is left out of the missed list.
    CHECK(csv.body.find("\"Box, once or twice; Latch \"\"held\"\"\"") != std::string::npos);

    std::filesystem::remove(path);
}

TEST(grades_csv_is_only_the_header_when_nothing_was_recorded) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "scancycle-grades-csv-empty-test.tsv";
    std::filesystem::remove(path);
    app::GradeLog log(path.string());

    const net::Response csv = app::grades_csv(log);
    CHECK(csv.status == 200);
    CHECK(csv.body ==
          "attempt,when_utc,student,lesson,result,passed,total,missed_checks\n");

    std::filesystem::remove(path);
}

TEST(grades_page_narrows_to_one_lesson) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "scancycle-grades-filter-test.tsv";
    std::filesystem::remove(path);
    app::GradeLog log(path.string());
    std::string error;

    app::GradeRecord first;
    first.at = 1700000000;
    first.lesson_id = "press-batch";
    first.student = "Ada";
    first.passed = true;
    first.passed_count = 1;
    first.total = 1;
    first.checks.push_back(app::GradeCheck{true, "Output on", "motor: output on"});
    CHECK(log.append(first, error));

    app::GradeRecord second;
    second.at = 1700000060;
    second.lesson_id = "conveyor-interlock";
    second.total = 1;
    second.checks.push_back(app::GradeCheck{false, "Latch holds", "output off"});
    CHECK(log.append(second, error));

    app::GradeRecord third;
    third.at = 1700000120;
    third.lesson_id = "press-batch";
    third.total = 1;
    third.checks.push_back(app::GradeCheck{false, "Latch holds", "output off"});
    CHECK(log.append(third, error));

    const net::Response filtered = app::grades_page(log, "press-batch");
    CHECK(filtered.status == 200);
    CHECK(filtered.body.find("2 attempts for lesson <code>press-batch</code>") !=
          std::string::npos);
    CHECK(filtered.body.find("conveyor-interlock") == std::string::npos);
    CHECK(filtered.body.find("href=\"/grades?lesson=press-batch\"") != std::string::npos);
    CHECK(filtered.body.find("href=\"/grades.csv?lesson=press-batch\"") !=
          std::string::npos);
    CHECK(filtered.body.find("Show the whole log") != std::string::npos);
    // Ordinals are file positions: the second press attempt is still attempt 3,
    // and the report link carries the filter so the way back keeps it.
    CHECK(filtered.body.find("href=\"/grades/3?lesson=press-batch\"") != std::string::npos);
    CHECK(filtered.body.find("href=\"/grades/1?lesson=press-batch\"") != std::string::npos);
    CHECK(filtered.body.find("href=\"/grades/2") == std::string::npos);
    CHECK(filtered.body.find("<td>Ada</td>") != std::string::npos);
    CHECK(app::attempt_page(log, "1").body.find("student <code>Ada</code>") !=
          std::string::npos);

    // Without the field the page still shows everything.
    const net::Response all = app::grades_page(log);
    CHECK(all.body.find("conveyor-interlock") != std::string::npos);
    CHECK(all.body.find("3 attempts recorded") != std::string::npos);

    const net::Response nothing = app::grades_page(log, "no-such-lesson");
    CHECK(nothing.status == 200);
    CHECK(nothing.body.find("No attempts recorded for") != std::string::npos);
    CHECK(nothing.body.find("Show the whole log") != std::string::npos);

    std::filesystem::remove(path);
}

TEST(grades_csv_exports_one_lesson_and_keeps_ordinals) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "scancycle-grades-csv-filter-test.tsv";
    std::filesystem::remove(path);
    app::GradeLog log(path.string());
    std::string error;

    const auto append = [&log, &error](std::int64_t at, const std::string& id, bool passed) {
        app::GradeRecord record;
        record.at = at;
        record.lesson_id = id;
        record.passed = passed;
        record.passed_count = passed ? 1 : 0;
        record.total = 1;
        return log.append(record, error);
    };

    CHECK(append(1700000000, "press-batch", true));
    CHECK(append(1700000060, "conveyor-interlock", false));
    CHECK(append(1700000120, "press-batch", false));

    const net::Response csv = app::grades_csv(log, "press-batch");
    std::vector<std::string> lines;
    std::size_t pos = 0;
    while (pos < csv.body.size()) {
        const std::size_t end = csv.body.find('\n', pos);
        if (end == std::string::npos) {
            break;
        }
        lines.push_back(csv.body.substr(pos, end - pos));
        pos = end + 1;
    }
    CHECK(lines.size() == 3);
    if (lines.size() == 3) {
        CHECK(lines[1].rfind("1,", 0) == 0);
        CHECK(lines[1].find("press-batch") != std::string::npos);
        CHECK(lines[2].rfind("3,", 0) == 0);
        CHECK(lines[2].find("press-batch") != std::string::npos);
    }

    std::filesystem::remove(path);
}

TEST(grades_page_summarises_each_student) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "scancycle-grades-student-summary.tsv";
    std::filesystem::remove(path);
    app::GradeLog log(path.string());
    std::string error;

    const auto append = [&log, &error](std::int64_t at, const std::string& student, bool passed) {
        app::GradeRecord record;
        record.at = at;
        record.lesson_id = "press-batch";
        record.student = student;
        record.passed = passed;
        record.passed_count = passed ? 1 : 0;
        record.total = 1;
        return log.append(record, error);
    };

    CHECK(append(1700000000, "Ada", true));
    CHECK(append(1700000060, "Ada", false));
    CHECK(append(1700000120, "Bob", true));
    CHECK(append(1700000180, "", false));

    const net::Response page = app::grades_page(log);
    CHECK(page.status == 200);
    CHECK(page.body.find("<h2>By student</h2>") != std::string::npos);
    // The name is a filter link, which is why the cell wraps the name.
    CHECK(page.body.find("<td><a href=\"/grades?student=Ada\">Ada</a></td><td>2</td>"
                         "<td>1</td><td>50%</td>") != std::string::npos);
    CHECK(page.body.find("<td><a href=\"/grades?student=Bob\">Bob</a></td><td>1</td>"
                         "<td>1</td><td>100%</td>") != std::string::npos);
    CHECK(page.body.find("<em>(unnamed)</em></td><td>1</td><td>0</td><td>0%</td>") !=
          std::string::npos);

    // Named students are alphabetical and the unnamed attempts come last.
    const std::size_t ada = page.body.find("<a href=\"/grades?student=Ada\">Ada</a>");
    const std::size_t bob = page.body.find("<a href=\"/grades?student=Bob\">Bob</a>");
    CHECK(ada < bob);
    CHECK(bob < page.body.find("<em>(unnamed)</em>"));

    // The tables sit in a stable order above the attempts.
    CHECK(page.body.find("By lesson") < page.body.find("By student"));
    CHECK(page.body.find("By student") < page.body.find("Missed checks"));

    std::filesystem::remove(path);
}

TEST(grades_page_hides_the_student_table_when_no_attempt_was_named) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "scancycle-grades-anonymous-log.tsv";
    std::filesystem::remove(path);
    app::GradeLog log(path.string());
    std::string error;

    app::GradeRecord first;
    first.at = 1700000000;
    first.lesson_id = "press-batch";
    first.passed = true;
    first.passed_count = 1;
    first.total = 1;
    CHECK(log.append(first, error));

    app::GradeRecord second;
    second.at = 1700000060;
    second.lesson_id = "press-batch";
    second.total = 1;
    CHECK(log.append(second, error));

    const net::Response page = app::grades_page(log);
    CHECK(page.status == 200);
    CHECK(page.body.find("By student") == std::string::npos);
    CHECK(page.body.find("By lesson") != std::string::npos);

    std::filesystem::remove(path);
}

TEST(grades_carry_the_student_name_from_the_form) {
    const std::vector<lesson::Lesson> lessons = lesson::make_builtin_lessons();
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "scancycle-student-log-test.tsv";
    std::filesystem::remove(path);
    app::GradeLog log(path.string());

    net::Request request;
    request.method = net::HttpMethod::Post;
    request.body = "lesson=press-batch&student=Ada+Lovelace";

    const net::Response page = app::grade_lesson_page(lessons, request, &log);
    CHECK(page.status == 200);
    CHECK(page.body.find("Submitted by <code>Ada Lovelace</code>") != std::string::npos);
    // The form keeps the name, so grading again does not make the student anonymous.
    CHECK(page.body.find("value=\"Ada Lovelace\"") != std::string::npos);

    std::string error;
    const std::vector<app::GradeRecord> records = log.entries(error);
    CHECK(records.size() == 1);
    if (records.size() == 1) {
        CHECK(records[0].student == "Ada Lovelace");
        CHECK(records[0].lesson_id == "press-batch");
        CHECK(records[0].program.empty());
    }

    std::filesystem::remove(path);
}

TEST(grades_page_narrows_to_one_student) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "scancycle-grades-student-filter.tsv";
    std::filesystem::remove(path);
    app::GradeLog log(path.string());
    std::string error;

    const auto append = [&log, &error](std::int64_t at, const std::string& student, bool passed) {
        app::GradeRecord record;
        record.at = at;
        record.lesson_id = "press-batch";
        record.student = student;
        record.passed = passed;
        record.passed_count = passed ? 1 : 0;
        record.total = 1;
        return log.append(record, error);
    };

    CHECK(append(1700000000, "Ada", true));
    CHECK(append(1700000060, "Bob", true));
    CHECK(append(1700000120, "Ada", false));

    const net::Response filtered = app::grades_page(log, "", "Ada");
    CHECK(filtered.status == 200);
    CHECK(filtered.body.find("2 attempts by <code>Ada</code>") != std::string::npos);
    CHECK(filtered.body.find("Coverage by student and lesson") != std::string::npos);
    CHECK(filtered.body.find("Bob") == std::string::npos);
    CHECK(filtered.body.find("href=\"/grades.csv?student=Ada\"") != std::string::npos);
    CHECK(filtered.body.find("Show the whole log") != std::string::npos);
    // Ordinals survive: the second Ada attempt is still attempt 3, and the
    // report link carries the student filter.
    CHECK(filtered.body.find("href=\"/grades/3?student=Ada\"") != std::string::npos);
    CHECK(filtered.body.find("href=\"/grades/1?student=Ada\"") != std::string::npos);
    CHECK(filtered.body.find("href=\"/grades/2") == std::string::npos);

    // Both filters together narrow to one student's work at one lesson.
    const net::Response both = app::grades_page(log, "press-batch", "Ada");
    CHECK(both.body.find("2 attempts by <code>Ada</code> for lesson <code>press-batch</code>") !=
          std::string::npos);

    const net::Response nothing = app::grades_page(log, "", "Zoe");
    CHECK(nothing.status == 200);
    CHECK(nothing.body.find("No attempts recorded by <code>Zoe</code>") != std::string::npos);

    std::filesystem::remove(path);
}

TEST(grades_csv_exports_one_student_and_keeps_ordinals) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "scancycle-grades-csv-student-filter.tsv";
    std::filesystem::remove(path);
    app::GradeLog log(path.string());
    std::string error;

    const auto append = [&log, &error](std::int64_t at, const std::string& student, bool passed) {
        app::GradeRecord record;
        record.at = at;
        record.lesson_id = "press-batch";
        record.student = student;
        record.passed = passed;
        record.passed_count = passed ? 1 : 0;
        record.total = 1;
        return log.append(record, error);
    };

    CHECK(append(1700000000, "Ada", true));
    CHECK(append(1700000060, "Bob", true));
    CHECK(append(1700000120, "Ada", false));

    const net::Response csv = app::grades_csv(log, "", "Ada");
    std::vector<std::string> lines;
    std::size_t pos = 0;
    while (pos < csv.body.size()) {
        const std::size_t end = csv.body.find('\n', pos);
        if (end == std::string::npos) {
            break;
        }
        lines.push_back(csv.body.substr(pos, end - pos));
        pos = end + 1;
    }
    CHECK(lines.size() == 3);
    if (lines.size() == 3) {
        CHECK(lines[1].rfind("1,", 0) == 0);
        CHECK(lines[1].find("Ada") != std::string::npos);
        CHECK(lines[2].rfind("3,", 0) == 0);
        CHECK(lines[2].find("Ada") != std::string::npos);
        CHECK(csv.body.find("Bob") == std::string::npos);
    }

    std::filesystem::remove(path);
}

TEST(attempt_page_keeps_the_filter_on_its_back_link) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "scancycle-attempt-back-link.tsv";
    std::filesystem::remove(path);
    app::GradeLog log(path.string());
    std::string error;

    app::GradeRecord record;
    record.at = 1700000000;
    record.lesson_id = "press-batch";
    record.student = "Ada";
    record.passed = true;
    record.passed_count = 1;
    record.total = 1;
    CHECK(log.append(record, error));

    const net::Response plain = app::attempt_page(log, "1");
    CHECK(plain.body.find("<a href=\"/grades\">Scan Cycle class log</a>") != std::string::npos);
    // A reference-only attempt has nothing to edit, so it offers no form.
    CHECK(plain.body.find("Edit and grade again") == std::string::npos);

    const net::Response filtered = app::attempt_page(log, "1", "press-batch", "Ada");
    CHECK(filtered.body.find("<a href=\"/grades?lesson=press-batch&student=Ada\">"
                             "Scan Cycle class log</a>") != std::string::npos);
    // The missing-attempt page returns to the same view rather than the whole log.
    CHECK(app::attempt_page(log, "9", "press-batch", "Ada")
              .body.find("<a href=\"/grades?lesson=press-batch&student=Ada\">"
                         "Back to the class log</a>") != std::string::npos);

    // A typed program can be corrected in place, and the form keeps the filter
    // so the next report still belongs to the view the teacher is watching.
    app::GradeRecord typed = record;
    typed.student = "Bob";
    typed.program = "PROGRAM P\nEND_PROGRAM\n";
    CHECK(log.append(typed, error));
    const net::Response editable = app::attempt_page(log, "2", "press-batch", "Bob");
    CHECK(editable.body.find("action=\"/grade?lesson=press-batch&student=Bob\"") !=
          std::string::npos);
    CHECK(editable.body.find("<input type=\"hidden\" name=\"student\" value=\"Bob\">") !=
          std::string::npos);
    CHECK(editable.body.find("PROGRAM P\nEND_PROGRAM") != std::string::npos);

    std::filesystem::remove(path);
}

TEST(grade_again_from_a_filtered_report_returns_to_the_filtered_log) {
    const std::vector<lesson::Lesson> lessons = lesson::make_builtin_lessons();

    net::Request request;
    request.method = net::HttpMethod::Post;
    request.path = "/grade";
    request.query = "lesson=press-batch&student=Ada";
    request.body = "lesson=press-batch&student=Ada";

    const net::Response page = app::grade_lesson_page(lessons, request);
    CHECK(page.status == 200);
    CHECK(page.body.find("<a href=\"/grades?lesson=press-batch&student=Ada\">"
                         "Back to the filtered class log</a>") != std::string::npos);
    // Grading again from the report keeps the filter too.
    CHECK(page.body.find("action=\"/grade?lesson=press-batch&student=Ada\"") !=
          std::string::npos);

    net::Request bare = request;
    bare.query.clear();
    bare.body = "lesson=press-batch";
    const net::Response plain = app::grade_lesson_page(lessons, bare);
    CHECK(plain.body.find("Back to the filtered class log") == std::string::npos);
    CHECK(plain.body.find("action=\"/grade\"") != std::string::npos);
}

TEST(attempt_paging_counts_and_links_keep_the_filter) {
    CHECK(app::attempt_page_count(0) == 1);
    CHECK(app::attempt_page_count(1) == 1);
    CHECK(app::attempt_page_count(app::kAttemptsPerPage) == 1);
    CHECK(app::attempt_page_count(app::kAttemptsPerPage + 1) == 2);
    CHECK(app::attempt_page_count(app::kAttemptsPerPage * 2) == 2);
    CHECK(app::attempt_page_count(app::kAttemptsPerPage * 2 + 1) == 3);

    CHECK(app::log_page_href(1, "", "") == "/grades");
    CHECK(app::log_page_href(2, "", "") == "/grades?page=2");
    CHECK(app::log_page_href(2, "press-batch", "Ada") ==
          "/grades?lesson=press-batch&student=Ada&page=2");
    CHECK(app::log_page_href(1, "", "Ada Lovelace") == "/grades?student=Ada%20Lovelace");

    CHECK(app::attempt_pager(1, 1, "", "").empty());
    CHECK(app::attempt_pager(1, 2, "", "").find("Page 1 of 2") != std::string::npos);
    CHECK(app::attempt_pager(2, 2, "", "").find("href=\"/grades\"") != std::string::npos);

    CHECK(app::parse_attempt_page("") == 1);
    CHECK(app::parse_attempt_page("0") == 1);
    CHECK(app::parse_attempt_page("7") == 7);
    CHECK(app::parse_attempt_page("2x") == 1);
}

TEST(grades_page_browses_a_long_log_a_page_at_a_time) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "scancycle-grades-paging.tsv";
    std::filesystem::remove(path);
    app::GradeLog log(path.string());
    std::string error;

    for (int attempt = 0; attempt < 30; ++attempt) {
        app::GradeRecord record;
        record.at = 1700000000 + attempt * 60;
        record.lesson_id = "press-batch";
        record.student = "Ada";
        record.passed = true;
        record.passed_count = 1;
        record.total = 1;
        CHECK(log.append(record, error));
    }

    // Page one holds the oldest 25 records, shown newest first: 25 down to 1.
    const net::Response first = app::grades_page(log);
    CHECK(first.body.find("Page 1 of 2") != std::string::npos);
    CHECK(first.body.find("href=\"/grades/25\"") != std::string::npos);
    CHECK(first.body.find("href=\"/grades/1\"") != std::string::npos);
    CHECK(first.body.find("href=\"/grades/26\"") == std::string::npos);
    CHECK(first.body.find("href=\"/grades?page=2\"") != std::string::npos);

    const net::Response second = app::grades_page(log, "", "", 2);
    CHECK(second.body.find("Page 2 of 2") != std::string::npos);
    CHECK(second.body.find("href=\"/grades/30\"") != std::string::npos);
    CHECK(second.body.find("href=\"/grades/26\"") != std::string::npos);
    CHECK(second.body.find("href=\"/grades/25\"") == std::string::npos);
    CHECK(second.body.find("href=\"/grades\"") != std::string::npos);
    // The summaries still describe all 30, not the five on this page.
    CHECK(second.body.find("<td><a href=\"/grades?student=Ada\">Ada</a></td><td>30</td>") !=
          std::string::npos);

    // A filter rides on both the page links and the report links.
    const net::Response narrowed = app::grades_page(log, "press-batch", "Ada");
    CHECK(narrowed.body.find("href=\"/grades?lesson=press-batch&student=Ada&page=2\"") !=
          std::string::npos);
    CHECK(narrowed.body.find("href=\"/grades/25?lesson=press-batch&student=Ada\"") !=
          std::string::npos);

    // A page past the end is the last page, not an empty one.
    const net::Response clamped = app::grades_page(log, "", "", 99);
    CHECK(clamped.body.find("Page 2 of 2") != std::string::npos);
    CHECK(clamped.body.find("href=\"/grades/30\"") != std::string::npos);

    // The export is never paged: a term's gradebook must not shrink to one page.
    const net::Response csv = app::grades_csv(log);
    std::size_t lines = 0;
    for (const char character : csv.body) {
        if (character == '\n') {
            ++lines;
        }
    }
    CHECK(lines == 31);

    std::filesystem::remove(path);
}

TEST(student_coverage_crosses_students_with_lessons) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "scancycle-coverage.tsv";
    std::filesystem::remove(path);
    app::GradeLog log(path.string());
    std::string error;

    const auto append = [&log, &error](const std::string& student, const std::string& lesson,
                                       bool passed) {
        app::GradeRecord record;
        record.at = 1700000000;
        record.lesson_id = lesson;
        record.student = student;
        record.passed = passed;
        record.passed_count = passed ? 1 : 0;
        record.total = 1;
        return log.append(record, error);
    };

    CHECK(append("Ada", "press-batch", true));
    CHECK(append("Ada", "press-batch", false));
    CHECK(append("Ada", "conveyor-interlock", true));
    CHECK(append("Bob", "press-batch", true));
    CHECK(append("", "press-batch", true));

    const std::vector<app::GradeRecord> records = log.entries(error);
    std::vector<const app::GradeRecord*> shown;
    for (const app::GradeRecord& record : records) {
        shown.push_back(&record);
    }

    const std::string table = app::student_coverage(shown);
    CHECK(table.find("<h2>Coverage by student and lesson</h2>") != std::string::npos);
    CHECK(table.find("<th><a href=\"/grades?student=Ada\">Ada</a></th>") != std::string::npos);
    CHECK(table.find("<a href=\"/grades?student=Ada&lesson=conveyor-interlock\">1/1</a>") !=
          std::string::npos);
    CHECK(table.find("<a href=\"/grades?student=Ada&lesson=press-batch\">1/2</a>") !=
          std::string::npos);
    CHECK(table.find("<a href=\"/grades?student=Bob&lesson=press-batch\">1/1</a>") !=
          std::string::npos);
    // Bob never tried the conveyor, and the unnamed attempt is not a person.
    CHECK(table.find("<td>-</td>") != std::string::npos);
    CHECK(table.find("unnamed") == std::string::npos);

    app::GradeRecord anonymous;
    anonymous.lesson_id = "press-batch";
    anonymous.passed = true;
    anonymous.passed_count = 1;
    anonymous.total = 1;
    const std::vector<const app::GradeRecord*> no_names{&anonymous};
    CHECK(app::student_coverage(no_names).empty());

    std::filesystem::remove(path);
}
