#include "harness.hpp"

#include "app/application.hpp"
#include "lesson/lesson.hpp"

#include <utility>

using namespace sc;

namespace {

const char* kDriveProgram = R"st(PROGRAM Drive
VAR
    start_btn : BOOL;
    motor     : BOOL;
    motor_fb  : BOOL;
END_VAR
    motor := start_btn;
END_PROGRAM
)st";

AppConfig make_config() {
    AppConfig config;
    config.program_source = kDriveProgram;
    config.devices.push_back(DeviceSpec{sim::DeviceKind::Motor, "motor"});
    config.tick_seconds = 0.01;
    config.plc_scan_ms = 10;
    return config;
}

}  // namespace

TEST(application_runs_the_whole_stack) {
    Application app(make_config());
    CHECK(app.initialize());
    if (!app.plc()) {
        return;
    }

    CHECK(app.plc()->set("start_btn", plc::Value::boolean(true)));
    CHECK(app.run_headless(100));

    CHECK(!app.faulted());
    CHECK(app.error().empty());
    CHECK(app.plant().count() == 1);
    CHECK(app.plant().device(0).output);
    CHECK(app.plant().device(0).speed > 0.99);
}

TEST(application_grades_its_own_run) {
    Application app(make_config());
    CHECK(app.initialize());
    if (!app.plc()) {
        return;
    }
    CHECK(app.plc()->set("start_btn", plc::Value::boolean(true)));
    CHECK(app.run_headless(50));

    lesson::Lesson spot_check;
    spot_check.id = "drive-on";
    spot_check.rubric.push_back(
        lesson::RubricCheck{"on", "Motor on", lesson::CheckKind::DeviceOutputTrue, 0, 0.0});
    spot_check.rubric.push_back(
        lesson::RubricCheck{"healthy", "No fault", lesson::CheckKind::ProgramHealthy, 0, 0.0});

    const lesson::GradeReport report = app.grade(spot_check);
    CHECK(report.passed());
}

TEST(application_reports_a_compile_error) {
    AppConfig config = make_config();
    config.program_source = "PROGRAM Broken\nVAR\n  x : BOOL\nEND_PROGRAM\n";

    Application app(std::move(config));
    CHECK(!app.initialize());
    CHECK(!app.error().empty());
}

TEST(application_rejects_a_bad_scan_period) {
    AppConfig config = make_config();
    config.plc_scan_ms = 0;

    Application app(std::move(config));
    CHECK(!app.initialize());
    CHECK(!app.error().empty());
}

TEST(application_refuses_to_run_before_initialising) {
    Application app(make_config());
    CHECK(!app.run_headless(1));
    CHECK(!app.error().empty());
}

TEST(a_forced_output_reaches_the_device_but_not_the_program) {
    Application app(make_config());
    CHECK(app.initialize());
    if (!app.plc()) {
        return;
    }

    // start_btn is off, so the program computes motor := FALSE. The force must
    // reach the field anyway, and must not rewrite what the logic computed.
    CHECK(app.set_force("motor", true));
    CHECK(app.step_scan());

    CHECK(!app.plc()->get("motor").as_bool());
    CHECK(app.plant().device(0).output);
}

TEST(a_forced_input_reaches_the_program_but_not_the_device) {
    Application app(make_config());
    CHECK(app.initialize());
    if (!app.plc()) {
        return;
    }

    CHECK(!app.plant().device(0).input);
    CHECK(app.set_force("motor_fb", true));
    CHECK(app.step_scan());

    CHECK(app.plc()->get("motor_fb").as_bool());
    CHECK(!app.plant().device(0).input);
}

TEST(forcing_an_unbound_name_is_refused) {
    Application app(make_config());
    CHECK(app.initialize());
    if (!app.plc()) {
        return;
    }

    CHECK(!app.set_force("no_such_tag", true));
    CHECK(!app.error().empty());
    CHECK(app.forces().empty());
}

TEST(clearing_a_force_that_is_not_there_is_reported) {
    Application app(make_config());
    CHECK(app.initialize());
    if (!app.plc()) {
        return;
    }

    // "motor" is a valid tag, so the force exists; "motor_fb" is never forced.
    CHECK(app.set_force("motor", true));
    CHECK(app.clear_force("motor"));
    CHECK(!app.clear_force("motor"));
    CHECK(app.forces().empty());
}

TEST(watch_reports_values_and_flags_what_is_forced) {
    Application app(make_config());
    CHECK(app.initialize());
    if (!app.plc()) {
        return;
    }

    CHECK(app.plc()->set("start_btn", plc::Value::boolean(true)));
    CHECK(app.set_force("motor_fb", true));
    CHECK(app.step_scan());

    const std::vector<Application::Watch> watched =
        app.watch({"motor", "motor_fb", "no_such_var"});
    CHECK(watched.size() == 3);

    CHECK(watched[0].present);
    CHECK(watched[0].value.as_bool());
    CHECK(!watched[0].forced);

    CHECK(watched[1].present);
    CHECK(watched[1].forced);

    CHECK(!watched[2].present);
}

TEST(stepping_runs_one_scan_and_leaves_the_plant_still) {
    Application app(make_config());
    CHECK(app.initialize());
    if (!app.plc()) {
        return;
    }

    const f64 distance_before = app.plant().device(0).distance;
    CHECK(app.step_scan());
    CHECK(app.scans() == 1);
    CHECK(app.plant().device(0).distance == distance_before);
}
