#include "harness.hpp"

#include "lesson/grader.hpp"
#include "lesson/lesson.hpp"
#include "net/portal_server.hpp"
#include "sim/plant.hpp"

using namespace sc;

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
    CHECK(router.route_count() == 2);

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
