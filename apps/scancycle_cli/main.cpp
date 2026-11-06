#include "app/application.hpp"
#include "app/df1_tool.hpp"
#include "app/lesson_runner.hpp"
#include "app/watch_text.hpp"
#include "core/deterministic_rng.hpp"
#include "io/df1_profile.hpp"
#include "io/df1_serial_link.hpp"
#include "lesson/lesson.hpp"
#include "plc/compiler.hpp"
#include "plc/il.hpp"
#include "plc/ladder_text.hpp"
#include "plc/parser.hpp"
#include "plc/program.hpp"
#include "plc/sfc.hpp"
#include "plc/vm.hpp"
#include "world/mesh.hpp"
#include "world/voxel_chunk.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <span>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace {

void print_usage() {
    std::printf(
        "scancycle_cli - Scan Cycle tooling (0.1.0)\n"
        "\n"
        "usage:\n"
        "  scancycle_cli run <program.st> [--scans=N] [--dt-ms=N] [--set name=value]\n"
        "  scancycle_cli disasm <program.st>\n"
        "  scancycle_cli il <program.il> [--scans=N] [--dt-ms=N] [--set name=value]\n"
        "  scancycle_cli ladder <program.ld> [--scans=N] [--dt-ms=N] [--set name=value]\n"
        "  scancycle_cli sfc <chart.sfc> [--scans=N] [--dt-ms=N] [--set name=value]\n"
        "  scancycle_cli bench [--iterations=N]\n"
        "  scancycle_cli trace <program.st> [--scans=N] [--force name=value] [--watch=a,b]\n"
        "                        [--snapshot]\n"
        "  scancycle_cli lesson [--id=ID] [--seconds=N]\n"
        "  scancycle_cli df1 --port=DEVICE (--read=ADDR | --write=ADDR=VALUE)\n"
        "                        [--baud=N] [--node=N] [--host=N] [--checksum=crc|bcc]\n"
        "                        [--timeout-ms=N] [--trace]\n"
        "                        (ADDR like N7:0, B3:5 or N7:0/3)\n");
}

bool read_file(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

bool split_assignment(const std::string& text, std::string& name, std::string& value) {
    const std::size_t at = text.find('=');
    if (at == std::string::npos || at == 0) {
        return false;
    }
    name = text.substr(0, at);
    value = text.substr(at + 1);
    return true;
}

sc::plc::Value parse_value(const std::string& text) {
    if (text == "true" || text == "TRUE") {
        return sc::plc::Value::boolean(true);
    }
    if (text == "false" || text == "FALSE") {
        return sc::plc::Value::boolean(false);
    }
    if (text.find('.') != std::string::npos) {
        return sc::plc::Value::real(std::atof(text.c_str()));
    }
    return sc::plc::Value::integer(std::atoll(text.c_str()));
}

void report_diagnostics(const std::string& path, const sc::plc::Diagnostics& diagnostics,
                        int& problems) {
    for (const sc::plc::Diagnostic& diagnostic : diagnostics) {
        if (diagnostic.column > 0) {
            std::fprintf(stderr, "%s:%d:%d: error: %s\n", path.c_str(), diagnostic.line,
                         diagnostic.column, diagnostic.message.c_str());
        } else {
            std::fprintf(stderr, "%s:%d: error: %s\n", path.c_str(), diagnostic.line,
                         diagnostic.message.c_str());
        }
        ++problems;
    }
}

// Runs a compiled program for N scans and prints the timing and the tag image.
// Both `run` (Structured Text) and `il` (Instruction List) end up here: once a
// program is bytecode, the language it was written in cannot matter.
int run_compiled(const sc::plc::CompiledProgram& program, int scans, int dt_ms,
                 const std::vector<std::string>& assignments) {
    sc::plc::Vm vm(program);
    for (const std::string& assignment : assignments) {
        std::string name;
        std::string value;
        if (!split_assignment(assignment, name, value)) {
            std::fprintf(stderr, "bad --set '%s' (expected name=value)\n", assignment.c_str());
            return 2;
        }
        if (!vm.set(name, parse_value(value))) {
            std::fprintf(stderr, "unknown variable '%s'\n", name.c_str());
            return 2;
        }
    }

    sc::i64 best_ns = std::numeric_limits<sc::i64>::max();
    sc::i64 worst_ns = 0;
    sc::i64 total_ns = 0;
    sc::i64 now_ms = 0;

    for (int i = 0; i < scans; ++i) {
        const auto started = std::chrono::steady_clock::now();
        const bool healthy = vm.scan(now_ms);
        const auto finished = std::chrono::steady_clock::now();
        const sc::i64 elapsed =
            std::chrono::duration_cast<std::chrono::nanoseconds>(finished - started).count();

        best_ns = std::min(best_ns, elapsed);
        worst_ns = std::max(worst_ns, elapsed);
        total_ns += elapsed;

        if (!healthy) {
            std::fprintf(stderr, "fault at scan %d (t=%lld ms): %s\n", i,
                         static_cast<long long>(now_ms), vm.fault_message().c_str());
            return 1;
        }
        now_ms += dt_ms;
    }

    const double average_us =
        scans > 0 ? static_cast<double>(total_ns) / static_cast<double>(scans) / 1000.0 : 0.0;

    std::printf("program : %s\n", program.name.c_str());
    std::printf("scans   : %d at %d ms scan time\n", scans, dt_ms);
    std::printf("code    : %zu instructions, %zu constants, %zu function blocks\n",
                program.code.size(), program.constants.size(), program.fbs.size());
    std::printf("scan    : avg %.2f us, best %.2f us, worst %.2f us\n", average_us,
                static_cast<double>(best_ns) / 1000.0, static_cast<double>(worst_ns) / 1000.0);
    std::printf("     (measured on this machine just now; not a target-hardware figure)\n");
    std::printf("\nvariables:\n");
    for (const sc::plc::VarInfo& var : program.vars) {
        if (var.fb_index >= 0) {
            continue;
        }
        std::printf("  %-16s = %s\n", var.name.c_str(), vm.get(var.name).to_string().c_str());
    }
    return 0;
}

int command_run(const std::vector<std::string>& args) {
    if (args.empty()) {
        print_usage();
        return 2;
    }

    const std::string& path = args[0];
    int scans = 100;
    int dt_ms = 10;
    std::vector<std::string> assignments;

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg.rfind("--scans=", 0) == 0) {
            scans = std::atoi(arg.c_str() + 8);
        } else if (arg.rfind("--dt-ms=", 0) == 0) {
            dt_ms = std::atoi(arg.c_str() + 8);
        } else if (arg.rfind("--set=", 0) == 0) {
            assignments.push_back(arg.substr(6));
        } else if (arg == "--set" && i + 1 < args.size()) {
            assignments.push_back(args[++i]);
        } else {
            std::fprintf(stderr, "unknown option '%s'\n", arg.c_str());
            return 2;
        }
    }

    std::string source;
    if (!read_file(path, source)) {
        std::fprintf(stderr, "cannot read '%s'\n", path.c_str());
        return 1;
    }

    sc::plc::ParseResult parsed = sc::plc::parse_program(source);
    int problems = 0;
    report_diagnostics(path, parsed.diagnostics, problems);
    if (!parsed.program) {
        std::fprintf(stderr, "parse failed\n");
        return 1;
    }

    sc::plc::CompileResult compiled = sc::plc::compile(*parsed.program);
    report_diagnostics(path, compiled.diagnostics, problems);
    if (problems != 0 || !compiled.ok) {
        return 1;
    }

    return run_compiled(compiled.program, scans, dt_ms, assignments);
}

int command_disasm(const std::vector<std::string>& args) {
    if (args.empty()) {
        print_usage();
        return 2;
    }
    std::string source;
    if (!read_file(args[0], source)) {
        std::fprintf(stderr, "cannot read '%s'\n", args[0].c_str());
        return 1;
    }
    sc::plc::ParseResult parsed = sc::plc::parse_program(source);
    int problems = 0;
    report_diagnostics(args[0], parsed.diagnostics, problems);
    if (!parsed.program) {
        return 1;
    }
    sc::plc::CompileResult compiled = sc::plc::compile(*parsed.program);
    report_diagnostics(args[0], compiled.diagnostics, problems);
    if (problems != 0 || !compiled.ok) {
        return 1;
    }
    std::printf("%s", sc::plc::disassemble(compiled.program).c_str());
    return 0;
}

int command_bench(const std::vector<std::string>& args) {
    int iterations = 200;
    for (const std::string& arg : args) {
        if (arg.rfind("--iterations=", 0) == 0) {
            iterations = std::atoi(arg.c_str() + 13);
        }
    }
    if (iterations <= 0) {
        iterations = 1;
    }

    sc::VoxelChunk chunk;
    for (int z = 0; z < sc::kChunkSize; ++z) {
        for (int x = 0; x < sc::kChunkSize; ++x) {
            chunk.set(x, 0, z, 1);
        }
    }

    sc::Pcg32 rng(0x5ca11cULL, 7ULL);
    for (int i = 0; i < 32; ++i) {
        const int x = static_cast<int>(rng.bounded(static_cast<sc::u32>(sc::kChunkSize)));
        const int z = static_cast<int>(rng.bounded(static_cast<sc::u32>(sc::kChunkSize)));
        const int height = 1 + static_cast<int>(rng.bounded(8));
        for (int y = 1; y <= height && y < sc::kChunkSize; ++y) {
            chunk.set(x, y, z, 2);
        }
    }

    sc::MeshData mesh = sc::greedy_mesh(chunk);
    const std::size_t quads = mesh.quad_count();
    const std::size_t vertices = mesh.vertices.size();
    const std::size_t indices = mesh.indices.size();

    const auto started = std::chrono::steady_clock::now();
    for (int i = 0; i < iterations; ++i) {
        mesh = sc::greedy_mesh(chunk);
    }
    const auto finished = std::chrono::steady_clock::now();
    const double total_us =
        std::chrono::duration<double, std::micro>(finished - started).count();

    std::printf("chunk    : %d^3 voxels, palette %u entries, %d bytes of indices\n",
                sc::kChunkSize, chunk.palette_size(), sc::kChunkVolume);
    std::printf("mesh     : %zu quads, %zu vertices, %zu indices (%zu bytes/vertex)\n", quads,
                vertices, indices, sizeof(sc::PackedVertex));
    std::printf("meshing  : %.1f us per chunk over %d iterations\n", total_us / iterations,
                iterations);
    std::printf("     (single-threaded CPU measurement on this machine, not a GPU figure)\n");
    return 0;
}

int command_il(const std::vector<std::string>& args) {
    if (args.empty()) {
        print_usage();
        return 2;
    }

    const std::string& path = args[0];
    int scans = 100;
    int dt_ms = 10;
    std::vector<std::string> assignments;

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg.rfind("--scans=", 0) == 0) {
            scans = std::atoi(arg.c_str() + 8);
        } else if (arg.rfind("--dt-ms=", 0) == 0) {
            dt_ms = std::atoi(arg.c_str() + 8);
        } else if (arg.rfind("--set=", 0) == 0) {
            assignments.push_back(arg.substr(6));
        } else if (arg == "--set" && i + 1 < args.size()) {
            assignments.push_back(args[++i]);
        } else {
            std::fprintf(stderr, "unknown option '%s'\n", arg.c_str());
            return 2;
        }
    }

    std::string source;
    if (!read_file(path, source)) {
        std::fprintf(stderr, "cannot read '%s'\n", path.c_str());
        return 1;
    }

    const sc::plc::IlResult compiled = sc::plc::compile_il(path, source, {});
    int problems = 0;
    report_diagnostics(path, compiled.diagnostics, problems);
    if (problems != 0 || !compiled.ok) {
        return 1;
    }

    return run_compiled(compiled.program, scans, dt_ms, assignments);
}

int command_ladder(const std::vector<std::string>& args) {
    if (args.empty()) {
        print_usage();
        return 2;
    }

    const std::string& path = args[0];
    int scans = 100;
    int dt_ms = 10;
    std::vector<std::string> assignments;

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg.rfind("--scans=", 0) == 0) {
            scans = std::atoi(arg.c_str() + 8);
        } else if (arg.rfind("--dt-ms=", 0) == 0) {
            dt_ms = std::atoi(arg.c_str() + 8);
        } else if (arg.rfind("--set=", 0) == 0) {
            assignments.push_back(arg.substr(6));
        } else if (arg == "--set" && i + 1 < args.size()) {
            assignments.push_back(args[++i]);
        } else {
            std::fprintf(stderr, "unknown option '%s'\n", arg.c_str());
            return 2;
        }
    }

    std::string source;
    if (!read_file(path, source)) {
        std::fprintf(stderr, "cannot read '%s'\n", path.c_str());
        return 1;
    }

    const sc::plc::LadderParseResult parsed = sc::plc::parse_ladder_source(source);
    int problems = 0;
    report_diagnostics(path, parsed.diagnostics, problems);
    if (problems != 0 || !parsed.ok) {
        return 1;
    }

    const sc::plc::NetlistResult compiled =
        sc::plc::compile_netlist(path, parsed.source.rungs, parsed.source.vars);
    report_diagnostics(path, compiled.diagnostics, problems);
    if (problems != 0 || !compiled.ok) {
        return 1;
    }

    return run_compiled(compiled.program, scans, dt_ms, assignments);
}

bool parse_bool_text(const std::string& text, bool& out) {
    if (text == "1" || text == "true" || text == "TRUE" || text == "on") {
        out = true;
        return true;
    }
    if (text == "0" || text == "false" || text == "FALSE" || text == "off") {
        out = false;
        return true;
    }
    return false;
}

// Runs a chart headlessly, the same way the ladder command runs a rung file: the
// chart lowers to the same program, so the trace is the one a lesson grades.
int command_sfc(const std::vector<std::string>& args) {
    if (args.empty()) {
        print_usage();
        return 2;
    }

    const std::string& path = args[0];
    int scans = 100;
    int dt_ms = 10;
    std::vector<std::string> assignments;

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg.rfind("--scans=", 0) == 0) {
            scans = std::atoi(arg.c_str() + 8);
        } else if (arg.rfind("--dt-ms=", 0) == 0) {
            dt_ms = std::atoi(arg.c_str() + 8);
        } else if (arg.rfind("--set=", 0) == 0) {
            assignments.push_back(arg.substr(6));
        } else if (arg == "--set" && i + 1 < args.size()) {
            assignments.push_back(args[++i]);
        } else {
            std::fprintf(stderr, "unknown option '%s'\n", arg.c_str());
            return 2;
        }
    }

    std::string source;
    if (!read_file(path, source)) {
        std::fprintf(stderr, "cannot read '%s'\n", path.c_str());
        return 1;
    }

    const sc::plc::SfcParseResult parsed = sc::plc::parse_sfc_source(source);
    int problems = 0;
    report_diagnostics(path, parsed.diagnostics, problems);
    if (problems != 0 || !parsed.ok) {
        return 1;
    }

    const sc::plc::SfcResult compiled = sc::plc::compile_sfc(path, parsed.chart);
    report_diagnostics(path, compiled.diagnostics, problems);
    if (problems != 0 || !compiled.ok) {
        return 1;
    }

    return run_compiled(compiled.program, scans, dt_ms, assignments);
}

// Steps a program against the vertical-slice cell and prints a watch table, which
// is the headless half of the IDE's monitor view.
int command_trace(const std::vector<std::string>& args) {
    if (args.empty()) {
        print_usage();
        return 2;
    }

    const std::string& path = args[0];
    int scans = 5;
    bool snapshot_mode = false;
    std::vector<std::string> force_args;
    std::vector<std::string> watch_names;

    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg.rfind("--scans=", 0) == 0) {
            scans = std::atoi(arg.c_str() + 8);
        } else if (arg.rfind("--force=", 0) == 0) {
            force_args.push_back(arg.substr(8));
        } else if (arg.rfind("--watch=", 0) == 0) {
            const std::string names = arg.substr(8);
            std::size_t start = 0;
            while (start <= names.size()) {
                const std::size_t comma = names.find(',', start);
                const std::size_t length =
                    comma == std::string::npos ? std::string::npos : comma - start;
                const std::string name = names.substr(start, length);
                if (!name.empty()) {
                    watch_names.push_back(name);
                }
                if (comma == std::string::npos) {
                    break;
                }
                start = comma + 1;
            }
        } else if (arg == "--snapshot") {
            snapshot_mode = true;
        } else {
            std::fprintf(stderr, "unknown option '%s'\n", arg.c_str());
            return 2;
        }
    }

    std::string source;
    if (!read_file(path, source)) {
        std::fprintf(stderr, "cannot read '%s'\n", path.c_str());
        return 1;
    }

    sc::AppConfig config;
    config.program_source = source;
    sc::Application app(config, sc::sim::make_vertical_slice_plant());
    if (!app.initialize()) {
        std::fprintf(stderr, "cannot start the cell: %s\n", app.error().c_str());
        return 1;
    }

    for (const std::string& entry : force_args) {
        std::string name;
        std::string value;
        bool forced = false;
        if (!split_assignment(entry, name, value) || !parse_bool_text(value, forced)) {
            std::fprintf(stderr, "bad --force '%s' (expected name=true|false)\n", entry.c_str());
            return 2;
        }
        if (!app.set_force(name, forced)) {
            std::fprintf(stderr, "%s\n", app.error().c_str());
            return 2;
        }
    }

    if (watch_names.empty()) {
        for (const sc::plc::VarInfo& var : app.plc()->program().vars) {
            if (var.fb_index < 0) {
                watch_names.push_back(var.name);
            }
        }
    }

    if (snapshot_mode) {
        // Machine-readable: one block per scan and nothing else on stdout, so a
        // pasted trace can be diffed without stripping a banner first.
        std::fputs(sc::watch_to_text(app.snapshot(watch_names)).c_str(), stdout);
        for (int i = 1; i <= scans; ++i) {
            if (!app.step_scan()) {
                std::fprintf(stderr, "fault at scan %d: %s\n", i, app.error().c_str());
                return 1;
            }
            std::fputs(sc::watch_to_text(app.snapshot(watch_names)).c_str(), stdout);
        }
        return 0;
    }

    std::printf("cell    : %u devices, %lld ms per scan\n", app.plant().count(),
                static_cast<long long>(config.plc_scan_ms));
    for (const std::string& name : app.unbound_devices()) {
        std::printf("unbound : %s (the program never mentions it)\n", name.c_str());
    }
    // The table below shows what the program computed. A forced tag is marked,
    // but its value is the logic's, not the field's - so say plainly what the
    // field is being told, or the mark is a puzzle rather than a warning.
    for (const sc::io::Force& force : app.forces().entries()) {
        std::printf("force   : %s = %s in the field (the program keeps computing its own)\n",
                    force.name.c_str(), force.value ? "TRUE" : "FALSE");
    }

    std::printf("\nscan");
    for (const std::string& name : watch_names) {
        std::printf(" %-12s", name.c_str());
    }
    std::printf("\n");

    const auto print_row = [&](int scan) {
        const std::vector<sc::Application::Watch> row = app.watch(watch_names);
        std::printf("%4d", scan);
        for (const sc::Application::Watch& entry : row) {
            std::string text = entry.present ? entry.value.to_string() : "?";
            if (entry.forced) {
                text += "*";
            }
            std::printf(" %-12s", text.c_str());
        }
        std::printf("\n");
    };

    print_row(0);
    for (int i = 1; i <= scans; ++i) {
        if (!app.step_scan()) {
            std::fprintf(stderr, "fault at scan %d: %s\n", i, app.error().c_str());
            return 1;
        }
        print_row(i);
    }

    if (!app.forces().empty()) {
        std::printf("\n(*) forced\n");
    }
    return 0;
}

int command_lesson(const std::vector<std::string>& args) {
    std::string wanted;
    double seconds = 30.0;

    for (const std::string& arg : args) {
        if (arg.rfind("--id=", 0) == 0) {
            wanted = arg.substr(5);
        } else if (arg.rfind("--seconds=", 0) == 0) {
            seconds = std::atof(arg.c_str() + 10);
        } else {
            std::fprintf(stderr, "unknown option '%s'\n", arg.c_str());
            return 2;
        }
    }

    std::vector<sc::lesson::Lesson> lessons = sc::lesson::make_builtin_lessons();

    int failures = 0;
    int ran = 0;

    for (const sc::lesson::Lesson& lesson : lessons) {
        if (!wanted.empty() && lesson.id != wanted) {
            continue;
        }
        if (lesson.starting_plant.count() == 0) {
            std::printf("%-18s skipped (no cell attached)\n", lesson.id.c_str());
            continue;
        }

        sc::LessonRunConfig config;
        config.seconds = seconds;
        const sc::LessonRunResult run = sc::run_lesson(lesson, config);
        ++ran;

        std::printf("%s - %s\n", lesson.id.c_str(), lesson.title.c_str());
        if (!run.ok) {
            std::printf("  RUN FAILED: %s\n\n", run.error.c_str());
            ++failures;
            continue;
        }

        std::printf("  %.1f s simulated, %lld scans, %u part(s) through the press\n", seconds,
                    static_cast<long long>(run.scans), run.boxes_processed);
        for (const sc::lesson::CheckResult& check : run.report.checks) {
            std::printf("  [%s] %-18s %s\n", check.passed ? "PASS" : "FAIL", check.id.c_str(),
                        check.detail.c_str());
        }
        std::printf("  => %s\n\n",
                    run.report.passed() ? "lesson reference passes" : "LESSON BROKEN");
        if (!run.report.passed()) {
            ++failures;
        }
    }

    if (ran == 0) {
        std::fprintf(stderr, "no runnable lesson matched\n");
        return 2;
    }
    return failures == 0 ? 0 : 1;
}

bool parse_long_option(const std::string& text, long minimum, long maximum, long& out) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    const long parsed = std::strtol(text.c_str(), &end, 10);
    if (end == nullptr || *end != '\0' || parsed < minimum || parsed > maximum) {
        return false;
    }
    out = parsed;
    return true;
}

// One read or write against a real controller, using the MicroLogix 1000
// profile unless the flags say otherwise. This is the bring-up path: no tag map,
// no VM, one N7 or B3 address on the wire.
int command_df1(const std::vector<std::string>& args) {
    std::string port;
    std::string read_text;
    std::string write_text;
    std::string checksum_text = "crc";
    long baud = 19200;
    long timeout_ms = 1000;
    long node = 1;
    long host = 0;
    bool trace = false;

    for (const std::string& arg : args) {
        long parsed = 0;
        if (arg.rfind("--port=", 0) == 0) {
            port = arg.substr(7);
        } else if (arg.rfind("--read=", 0) == 0) {
            read_text = arg.substr(7);
        } else if (arg.rfind("--write=", 0) == 0) {
            write_text = arg.substr(8);
        } else if (arg.rfind("--checksum=", 0) == 0) {
            checksum_text = arg.substr(11);
        } else if (arg.rfind("--baud=", 0) == 0) {
            if (!parse_long_option(arg.substr(7), 300, 1'000'000, parsed)) {
                std::fprintf(stderr, "--baud must be a positive rate\n");
                return 2;
            }
            baud = parsed;
        } else if (arg.rfind("--timeout-ms=", 0) == 0) {
            if (!parse_long_option(arg.substr(13), 1, 60'000, parsed)) {
                std::fprintf(stderr, "--timeout-ms must be 1..60000\n");
                return 2;
            }
            timeout_ms = parsed;
        } else if (arg.rfind("--node=", 0) == 0) {
            if (!parse_long_option(arg.substr(7), 0, 254, parsed)) {
                std::fprintf(stderr, "--node must be 0..254\n");
                return 2;
            }
            node = parsed;
        } else if (arg.rfind("--host=", 0) == 0) {
            if (!parse_long_option(arg.substr(7), 0, 254, parsed)) {
                std::fprintf(stderr, "--host must be 0..254\n");
                return 2;
            }
            host = parsed;
        } else if (arg == "--trace") {
            trace = true;
        } else {
            std::fprintf(stderr, "unknown option '%s'\n", arg.c_str());
            return 2;
        }
    }

    if (port.empty()) {
        std::fprintf(stderr, "df1 needs --port=DEVICE\n");
        return 2;
    }
    if (read_text.empty() == write_text.empty()) {
        std::fprintf(stderr, "df1 needs exactly one of --read or --write\n");
        return 2;
    }

    sc::io::Df1Checksum checksum = sc::io::Df1Checksum::Crc16;
    if (checksum_text == "crc" || checksum_text == "CRC") {
        checksum = sc::io::Df1Checksum::Crc16;
    } else if (checksum_text == "bcc" || checksum_text == "BCC") {
        checksum = sc::io::Df1Checksum::Bcc;
    } else {
        std::fprintf(stderr, "--checksum must be crc or bcc\n");
        return 2;
    }

    const bool writing = !write_text.empty();
    std::string address_text = writing ? write_text : read_text;
    std::string value_text;
    if (writing) {
        const std::size_t equals = address_text.rfind('=');
        if (equals == std::string::npos || equals == 0) {
            std::fprintf(stderr, "bad --write (expected ADDR=VALUE)\n");
            return 2;
        }
        value_text = address_text.substr(equals + 1);
        address_text = address_text.substr(0, equals);
    }

    sc::app::Df1Address address;
    std::string error;
    if (!sc::app::parse_df1_address(address_text, address, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 2;
    }

    sc::u16 word_value = 0;
    bool bit_value = false;
    if (writing) {
        const bool parsed =
            address.has_bit
                ? sc::app::parse_df1_bit_value(value_text, bit_value, error)
                : sc::app::parse_df1_word_value(value_text, word_value, error);
        if (!parsed) {
            std::fprintf(stderr, "%s\n", error.c_str());
            return 2;
        }
    }

    sc::app::Df1ToolConfig config;
    config.host = static_cast<sc::u8>(host);
    config.node = static_cast<sc::u8>(node);
    config.checksum = checksum;
    config.address = address;

    sc::io::Df1SerialConfig serial =
        sc::io::df1_serial_config_for(sc::io::kMicroLogix1000, port);
    serial.baud = static_cast<int>(baud);
    serial.timeout_ms = static_cast<int>(timeout_ms);
    serial.checksum = checksum;
    if (trace) {
        serial.trace = [](bool transmitted, std::span<const sc::u8> bytes) {
            std::string line = transmitted ? "tx" : "rx";
            char hex[8];
            for (const sc::u8 byte : bytes) {
                std::snprintf(hex, sizeof(hex), " %02X", static_cast<unsigned>(byte));
                line += hex;
            }
            std::fprintf(stderr, "%s\n", line.c_str());
        };
    }

    sc::io::Df1SerialLink link(serial);
    sc::app::Df1ToolResult result;
    if (writing) {
        result = address.has_bit
                     ? sc::app::df1_write_bit(config, link, bit_value)
                     : sc::app::df1_write_word(config, link, word_value);
    } else {
        result = address.has_bit ? sc::app::df1_read_bit(config, link)
                                 : sc::app::df1_read_word(config, link);
    }
    if (!result.ok) {
        std::fprintf(stderr, "df1: %s\n", result.error.c_str());
        return 1;
    }

    const std::string label = sc::app::df1_address_text(address);
    if (address.has_bit) {
        std::printf("%s = %d%s\n", label.c_str(), result.bit ? 1 : 0,
                    writing ? " written" : "");
    } else {
        std::printf("%s = %u (0x%04X)%s\n", label.c_str(),
                    static_cast<unsigned>(result.word),
                    static_cast<unsigned>(result.word), writing ? " written" : "");
    }
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) {
        print_usage();
        return 2;
    }

    const std::string command = args[0];
    const std::vector<std::string> rest(args.begin() + 1, args.end());

    if (command == "run") {
        return command_run(rest);
    }
    if (command == "disasm") {
        return command_disasm(rest);
    }
    if (command == "bench") {
        return command_bench(rest);
    }
    if (command == "ladder") {
        return command_ladder(rest);
    }
    if (command == "sfc") {
        return command_sfc(rest);
    }
    if (command == "il") {
        return command_il(rest);
    }
    if (command == "trace") {
        return command_trace(rest);
    }
    if (command == "lesson") {
        return command_lesson(rest);
    }
    if (command == "df1") {
        return command_df1(rest);
    }

    print_usage();
    return 2;
}
