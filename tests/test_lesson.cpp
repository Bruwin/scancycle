#include "harness.hpp"

#include "app/lesson_runner.hpp"
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
    CHECK(built.size() == 3);

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
}
