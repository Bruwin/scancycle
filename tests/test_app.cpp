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
