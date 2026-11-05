#include "harness.hpp"

#include "app/application.hpp"
#include "app/grade_log.hpp"
#include "app/watch_text.hpp"
#include "io/commissioning.hpp"
#include "io/df1_driver.hpp"
#include "io/df1_slave.hpp"
#include "lesson/lesson.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

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

const char* kDf1Program = R"st(PROGRAM Bench
VAR
    start_btn : BOOL;
    run       : BOOL;
END_VAR
    run := start_btn;
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

TEST(application_runs_its_scan_loop_through_an_injected_driver) {
    io::Df1Slave peer;
    peer.add_word_file(7, io::kPcccFileTypeInteger, 2);
    CHECK(peer.set_word(7, 0, 1));

    io::CommissioningPolicy policy;
    policy.allow_address("run");
    policy.set_min_write_interval_ms(0);
    CHECK(policy.begin_session(0, 60'000));

    AppConfig config;
    config.program_source = kDf1Program;
    config.driver_factory = [&peer, &policy](plc::Vm& vm) {
        io::Df1Config df1;
        df1.station = 1;
        df1.destination = 0;
        df1.checksum = io::Df1Checksum::Bcc;
        df1.tags = {
            io::Df1Tag{"start_btn", 7, 0, 0, io::kPcccFileTypeInteger,
                       io::Df1Direction::Input},
            io::Df1Tag{"run", 7, 1, 0, io::kPcccFileTypeInteger,
                       io::Df1Direction::Output},
        };
        df1.commissioning = &policy;
        return std::make_unique<io::Df1Driver>(df1, vm, peer);
    };

    Application app(std::move(config));
    CHECK(app.initialize());
    if (!app.plc()) {
        return;
    }

    CHECK(app.step_scan());
    CHECK(app.plc()->get("run").as_bool());
    CHECK(peer.bit(7, 1, 0));

    // A live controller has no force table; refusing is better than pretending.
    CHECK(!app.set_force("run", true));
    CHECK(app.error().find("simulated") != std::string::npos);
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
    CHECK(refused.toolbox_refused());
    CHECK(refused.refused_feature() == "AND");

    AppConfig granted = make_config();
    granted.program_source = kGatedProgram;
    granted.allowed_instructions = {"BOOL", "AND"};

    Application allowed(std::move(granted));
    CHECK(allowed.initialize());
    CHECK(!allowed.toolbox_refused());
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

TEST(grade_log_round_trips_a_record_with_awkward_text) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "scancycle-grade-log-round-trip.tsv";
    std::filesystem::remove(path);

    app::GradeLog log(path.string());
    std::string error;
    CHECK(log.entries(error).empty());
    CHECK(error.empty());

    app::GradeRecord first;
    first.at = 1700000000;
    first.lesson_id = "press-batch";
    first.passed = true;
    first.passed_count = 4;
    first.total = 4;
    first.program = "PROGRAM X\nVAR\n    a : BOOL;\nEND_VAR\n\ta := TRUE;\nEND_PROGRAM\n";

    CHECK(log.append(first, error));
    CHECK(error.empty());

    app::GradeRecord second;
    second.at = 1700000060;
    second.lesson_id = "conveyor-interlock";
    second.passed = false;
    second.passed_count = 1;
    second.total = 3;

    CHECK(log.append(second, error));

    const std::vector<app::GradeRecord> entries = log.entries(error);
    CHECK(error.empty());
    CHECK(entries.size() == 2);
    CHECK(entries[0].at == 1700000000);
    CHECK(entries[0].lesson_id == "press-batch");
    CHECK(entries[0].passed);
    CHECK(entries[0].passed_count == 4);
    CHECK(entries[0].total == 4);
    CHECK(entries[0].program == first.program);
    CHECK(entries[1].lesson_id == "conveyor-interlock");
    CHECK(!entries[1].passed);
    CHECK(entries[1].program.empty());

    std::filesystem::remove(path);
}

TEST(grade_log_skips_a_line_it_cannot_read) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "scancycle-grade-log-malformed.tsv";
    {
        std::ofstream file(path, std::ios::trunc);
        file << "1700000000\tpress-batch\t1\t4\t4\t\n";
        file << "not a record\n";
        file << "1700000001\tpress-cycle\tx\t4\t4\t\n";  // passed is not 0 or 1
    }

    app::GradeLog log(path.string());
    std::string error;
    const std::vector<app::GradeRecord> entries = log.entries(error);
    CHECK(entries.size() == 1);
    CHECK(entries[0].lesson_id == "press-batch");

    std::filesystem::remove(path);
}

TEST(grade_log_keeps_the_per_check_report) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "scancycle-grade-log-checks.tsv";
    std::filesystem::remove(path);
    app::GradeLog log(path.string());
    std::string error;

    app::GradeRecord record;
    record.at = 1700000002;
    record.lesson_id = "press-batch";
    record.passed = false;
    record.passed_count = 1;
    record.total = 3;
    record.program = "PROGRAM X\nEND_PROGRAM\n";
    record.checks.push_back(app::GradeCheck{true, "Output on", "motor: output on"});
    record.checks.push_back(app::GradeCheck{false, "Box, counted; once", "saw 1\n"});
    record.checks.push_back(app::GradeCheck{false, "Latch holds", "output off"});
    CHECK(log.append(record, error));
    CHECK(error.empty());

    // A description holding a comma, a semicolon and a newline must not turn one
    // attempt into several lines.
    std::size_t lines = 0;
    {
        std::ifstream file(path);
        std::string line;
        while (std::getline(file, line)) {
            ++lines;
        }
    }
    CHECK(lines == 1);

    const std::vector<app::GradeRecord> entries = log.entries(error);
    CHECK(entries.size() == 1);
    CHECK(entries[0].checks.size() == 3);
    CHECK(entries[0].checks[0].passed);
    CHECK(entries[0].checks[0].description == "Output on");
    CHECK(entries[0].checks[0].detail == "motor: output on");
    CHECK(!entries[0].checks[1].passed);
    CHECK(entries[0].checks[1].description == "Box, counted; once");
    CHECK(entries[0].checks[1].detail == "saw 1\n");
    CHECK(entries[0].checks[2].detail == "output off");

    std::filesystem::remove(path);
}

TEST(grade_log_reads_a_line_from_before_checks_were_stored) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "scancycle-grade-log-legacy.tsv";
    {
        std::ofstream file(path, std::ios::trunc);
        file << "1700000000\tpress-batch\t1\t4\t4\t\n";
    }

    app::GradeLog log(path.string());
    std::string error;
    const std::vector<app::GradeRecord> entries = log.entries(error);
    CHECK(entries.size() == 1);
    CHECK(entries[0].passed);
    CHECK(entries[0].total == 4);
    CHECK(entries[0].checks.empty());

    std::filesystem::remove(path);
}

TEST(grade_log_keeps_the_student_name) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "scancycle-grade-log-student.tsv";
    std::filesystem::remove(path);
    app::GradeLog log(path.string());
    std::string error;

    app::GradeRecord record;
    record.at = 1700000003;
    record.lesson_id = "press-batch";
    record.passed = true;
    record.passed_count = 1;
    record.total = 1;
    record.student = "Ada, 2B\nLovelace";
    CHECK(log.append(record, error));

    // The name rides in the last field, so a name holding a comma or a newline
    // cannot split one attempt across lines.
    std::size_t lines = 0;
    {
        std::ifstream file(path);
        std::string line;
        while (std::getline(file, line)) {
            ++lines;
        }
    }
    CHECK(lines == 1);

    const std::vector<app::GradeRecord> entries = log.entries(error);
    CHECK(entries.size() == 1);
    CHECK(entries[0].student == "Ada, 2B\nLovelace");
    CHECK(entries[0].checks.empty());

    std::filesystem::remove(path);
}

TEST(grade_log_reads_a_row_written_before_students_were_stored) {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "scancycle-grade-log-no-student.tsv";
    {
        std::ofstream file(path, std::ios::trunc);
        file << "1700000000\tpress-batch\t1\t1\t1\t\t1,Output%20on,motor%20on\n";
    }

    app::GradeLog log(path.string());
    std::string error;
    const std::vector<app::GradeRecord> entries = log.entries(error);
    CHECK(entries.size() == 1);
    CHECK(entries[0].student.empty());
    CHECK(entries[0].checks.size() == 1);

    std::filesystem::remove(path);
}
