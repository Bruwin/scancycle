#include "harness.hpp"

#include "app/application.hpp"
#include "app/watch_text.hpp"
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

TEST(snapshot_carries_the_scan_count_and_the_clock) {
    Application app(make_config());
    CHECK(app.initialize());
    if (!app.plc()) {
        return;
    }

    CHECK(app.step_scan());
    CHECK(app.step_scan());

    const Application::WatchSnapshot shot = app.snapshot({"motor"});
    CHECK(shot.scan == 2);
    CHECK(shot.now_ms == 20);
    CHECK(!shot.faulted);
    CHECK(shot.tags.size() == 1);
    CHECK(shot.forces.empty());
}

TEST(a_snapshot_does_not_move_what_it_observes) {
    Application app(make_config());
    CHECK(app.initialize());
    if (!app.plc()) {
        return;
    }
    CHECK(app.step_scan());

    const Application::WatchSnapshot first = app.snapshot({"motor"});
    const Application::WatchSnapshot second = app.snapshot({"motor"});

    CHECK(first.scan == second.scan);
    CHECK(first.now_ms == second.now_ms);
    CHECK(first.tags.size() == second.tags.size());
    CHECK(first.tags[0].value.as_bool() == second.tags[0].value.as_bool());
    CHECK(app.scans() == first.scan);
}

TEST(watch_text_writes_one_tab_separated_line_per_tag) {
    Application app(make_config());
    CHECK(app.initialize());
    if (!app.plc()) {
        return;
    }

    const std::string text = watch_to_text(app.snapshot({"motor", "no_such_var"}));
    CHECK(text == "scan=0\tnow_ms=0\tfaulted=0\n"
                  "tag\tmotor\tBOOL\tFALSE\t1\t0\n"
                  "tag\tno_such_var\t-\t-\t0\t0\n");
}

TEST(watch_text_reports_a_force_after_the_tags) {
    Application app(make_config());
    CHECK(app.initialize());
    if (!app.plc()) {
        return;
    }
    CHECK(app.set_force("motor", true));

    const std::string text = watch_to_text(app.snapshot({"motor"}));
    CHECK(text == "scan=0\tnow_ms=0\tfaulted=0\n"
                  "tag\tmotor\tBOOL\tFALSE\t1\t1\n"
                  "force\tmotor\tTRUE\n");
}

namespace {

const char* kGatedProgram = R"st(PROGRAM Gate
VAR
    a : BOOL;
    b : BOOL;
END_VAR
    a := a AND b;
END_PROGRAM
)st";

}  // namespace

TEST(application_enforces_the_toolbox_it_is_given) {
    AppConfig withheld = make_config();
    withheld.program_source = kGatedProgram;
    withheld.allowed_instructions = {"BOOL"};

    Application refused(std::move(withheld));
    CHECK(!refused.initialize());
    CHECK(refused.error().find("AND") != std::string::npos);
    CHECK(refused.error().find("does not grant") != std::string::npos);

    AppConfig granted = make_config();
    granted.program_source = kGatedProgram;
    granted.allowed_instructions = {"BOOL", "AND"};

    Application allowed(std::move(granted));
    CHECK(allowed.initialize());
}

TEST(application_checks_the_toolbox_before_the_plant) {
    // The program names a device the config never declares, so a toolbox failure
    // must be reported first; otherwise a student sees a wiring complaint about a
    // program that was never going to be accepted.
    AppConfig config = make_config();
    config.program_source = kGatedProgram;
    config.allowed_instructions = {};

    Application open(std::move(config));
    CHECK(open.initialize());
}
