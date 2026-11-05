#include "harness.hpp"

#include "plc/compiler.hpp"
#include "plc/lexer.hpp"
#include "plc/parser.hpp"
#include "plc/vm.hpp"

#include <string>

namespace {

const char* kConveyor = R"st(PROGRAM ConveyorInterlock
VAR
    start_btn   : BOOL;
    stop_btn    : BOOL;
    jam_sensor  : BOOL;
    motor_run   : BOOL;
    run_latch   : BOOL;
    delay_timer : TON;
END_VAR
    run_latch := (run_latch OR start_btn) AND NOT stop_btn AND NOT jam_sensor;
    delay_timer(IN := run_latch, PT := T#2s);
    motor_run := delay_timer.Q;
END_PROGRAM
)st";

const char* kTimer = R"st(PROGRAM Delay
VAR
    enable : BOOL;
    timer  : TON;
    out    : BOOL;
END_VAR
    timer(IN := enable, PT := T#100ms);
    out := timer.Q;
END_PROGRAM
)st";

std::string run_trace(const char* source, int scans, int dt_ms) {
    std::string trace;
    sc::plc::ParseResult parsed = sc::plc::parse_program(source);
    if (!parsed.program) {
        return "PARSE_ERROR";
    }
    sc::plc::CompileResult compiled = sc::plc::compile(*parsed.program);
    if (!compiled.ok) {
        return "COMPILE_ERROR";
    }
    sc::plc::Vm vm(compiled.program);
    vm.set("start_btn", sc::plc::Value::boolean(true));
    for (int i = 0; i < scans; ++i) {
        if (!vm.scan(static_cast<sc::i64>(i) * dt_ms)) {
            return "FAULT";
        }
        trace += vm.get("run_latch").to_string();
        trace += '/';
        trace += vm.get("motor_run").to_string();
        trace += ';';
    }
    return trace;
}

}  // namespace

TEST(lexer_lowercases_and_reads_time_literals) {
    sc::plc::Lexer lexer("Motor_RUN := T#1m30s;");
    const std::vector<sc::plc::Token> tokens = lexer.tokenize();

    CHECK(tokens.size() >= 5);
    CHECK(tokens[0].kind == sc::plc::TokenKind::Identifier);
    CHECK(tokens[0].text == "motor_run");
    CHECK(tokens[2].kind == sc::plc::TokenKind::TimeLiteral);
    CHECK(tokens[2].ival == 90000);
    CHECK(lexer.diagnostics().empty());
}

TEST(reference_program_parses_and_compiles_cleanly) {
    sc::plc::ParseResult parsed = sc::plc::parse_program(kConveyor);
    CHECK(parsed.diagnostics.empty());
    CHECK(parsed.program != nullptr);
    if (!parsed.program) {
        return;
    }

    sc::plc::CompileResult compiled = sc::plc::compile(*parsed.program);
    CHECK(compiled.diagnostics.empty());
    CHECK(compiled.ok);
    CHECK(compiled.program.vars.size() == 6);
    CHECK(compiled.program.fbs.size() == 1);
    CHECK(!compiled.program.code.empty());
}

TEST(seal_in_latch_sets_and_resets) {
    sc::plc::ParseResult parsed = sc::plc::parse_program(kConveyor);
    if (!parsed.program) {
        CHECK(false);
        return;
    }
    sc::plc::CompileResult compiled = sc::plc::compile(*parsed.program);
    if (!compiled.ok) {
        CHECK(false);
        return;
    }
    sc::plc::Vm vm(compiled.program);

    CHECK(vm.set("start_btn", sc::plc::Value::boolean(true)));
    CHECK(vm.scan(0));
    CHECK(vm.get("run_latch").as_bool());

    CHECK(vm.set("start_btn", sc::plc::Value::boolean(false)));
    CHECK(vm.scan(10));
    CHECK(vm.get("run_latch").as_bool());

    CHECK(vm.set("stop_btn", sc::plc::Value::boolean(true)));
    CHECK(vm.scan(20));
    CHECK(!vm.get("run_latch").as_bool());
}

TEST(ton_holds_the_output_until_the_preset_elapses) {
    sc::plc::ParseResult parsed = sc::plc::parse_program(kTimer);
    if (!parsed.program) {
        CHECK(false);
        return;
    }
    sc::plc::CompileResult compiled = sc::plc::compile(*parsed.program);
    if (!compiled.ok) {
        CHECK(false);
        return;
    }
    sc::plc::Vm vm(compiled.program);
    CHECK(vm.set("enable", sc::plc::Value::boolean(true)));

    bool came_on_early = false;
    bool on_after_preset = false;

    for (int scan = 0; scan <= 10; ++scan) {
        CHECK(vm.scan(static_cast<sc::i64>(scan) * 10));
        if (scan < 10 && vm.get("out").as_bool()) {
            came_on_early = true;
        }
        if (scan == 10) {
            on_after_preset = vm.get("out").as_bool();
        }
    }

    CHECK(!came_on_early);
    CHECK(on_after_preset);
}

TEST(ctu_counts_rising_edges_only) {
    const char* source = R"st(PROGRAM Counting
VAR
    pulse   : BOOL;
    counter : CTU;
    done    : BOOL;
END_VAR
    counter(CU := pulse, PV := 3);
    done := counter.Q;
END_PROGRAM
)st";

    sc::plc::ParseResult parsed = sc::plc::parse_program(source);
    if (!parsed.program) {
        CHECK(false);
        return;
    }
    sc::plc::CompileResult compiled = sc::plc::compile(*parsed.program);
    if (!compiled.ok) {
        CHECK(false);
        return;
    }
    sc::plc::Vm vm(compiled.program);

    const bool pulses[] = {true, false, true, false, true, true, true};
    for (int scan = 0; scan < 7; ++scan) {
        CHECK(vm.set("pulse", sc::plc::Value::boolean(pulses[scan])));
        CHECK(vm.scan(static_cast<sc::i64>(scan) * 10));
    }

    // Three rising edges, and the held-high scans afterwards must not count.
    CHECK(vm.get("done").as_bool());
}

TEST(ctu_publishes_its_count_through_cv) {
    const char* source = R"st(PROGRAM Counting
VAR
    pulse   : BOOL;
    counter : CTU;
    parts   : INT;
END_VAR
    counter(CU := pulse, PV := 3);
    parts := counter.CV;
END_PROGRAM
)st";

    sc::plc::ParseResult parsed = sc::plc::parse_program(source);
    if (!parsed.program) {
        CHECK(false);
        return;
    }
    sc::plc::CompileResult compiled = sc::plc::compile(*parsed.program);
    if (!compiled.ok) {
        CHECK(false);
        return;
    }
    sc::plc::Vm vm(compiled.program);

    const bool pulses[] = {true, false, true, false, true};
    for (int scan = 0; scan < 5; ++scan) {
        CHECK(vm.set("pulse", sc::plc::Value::boolean(pulses[scan])));
        CHECK(vm.scan(static_cast<sc::i64>(scan) * 10));
    }

    // CV is the count itself, not the preset flag.
    CHECK(vm.get("parts").as_int() == 3);
}

TEST(int_declaration_wraps_at_sixteen_bits) {
    const char* source = R"st(PROGRAM Wrapping
VAR
    i : INT;
END_VAR
    i := 40000;
END_PROGRAM
)st";

    sc::plc::ParseResult parsed = sc::plc::parse_program(source);
    if (!parsed.program) {
        CHECK(false);
        return;
    }
    sc::plc::CompileResult compiled = sc::plc::compile(*parsed.program);
    if (!compiled.ok) {
        CHECK(false);
        return;
    }
    sc::plc::Vm vm(compiled.program);
    CHECK(vm.scan(0));
    CHECK(vm.get("i").as_int() == -25536);
}

TEST(division_by_zero_faults_the_task) {
    const char* source = R"st(PROGRAM Divide
VAR
    z : INT;
    r : INT;
END_VAR
    z := 0;
    r := 10 / z;
END_PROGRAM
)st";

    sc::plc::ParseResult parsed = sc::plc::parse_program(source);
    if (!parsed.program) {
        CHECK(false);
        return;
    }
    sc::plc::CompileResult compiled = sc::plc::compile(*parsed.program);
    if (!compiled.ok) {
        CHECK(false);
        return;
    }
    sc::plc::Vm vm(compiled.program);

    CHECK(!vm.scan(0));
    CHECK(vm.faulted());
    CHECK(!vm.fault_message().empty());
}

TEST(for_loops_and_standard_functions_work) {
    const char* source = R"st(PROGRAM Loop
VAR
    i       : INT;
    total   : INT;
    clamped : INT;
END_VAR
    total := 0;
    FOR i := 1 TO 5 DO
        total := total + i;
    END_FOR;
    clamped := LIMIT(0, 42, 10);
END_PROGRAM
)st";

    sc::plc::ParseResult parsed = sc::plc::parse_program(source);
    if (!parsed.program) {
        CHECK(false);
        return;
    }
    sc::plc::CompileResult compiled = sc::plc::compile(*parsed.program);
    if (!compiled.ok) {
        CHECK(false);
        return;
    }
    sc::plc::Vm vm(compiled.program);
    CHECK(vm.scan(0));
    CHECK(vm.get("total").as_int() == 15);
    CHECK(vm.get("i").as_int() == 6);
    CHECK(vm.get("clamped").as_int() == 10);
}

TEST(assigning_a_timer_output_is_rejected) {
    const char* source = R"st(PROGRAM Bad
VAR
    timer : TON;
END_VAR
    timer.Q := TRUE;
END_PROGRAM
)st";

    sc::plc::ParseResult parsed = sc::plc::parse_program(source);
    CHECK(parsed.program != nullptr);
    if (!parsed.program) {
        return;
    }
    sc::plc::CompileResult compiled = sc::plc::compile(*parsed.program);
    CHECK(!compiled.ok);
    CHECK(!compiled.diagnostics.empty());
}

TEST(replays_are_bit_identical) {
    const std::string first = run_trace(kConveyor, 60, 10);
    const std::string second = run_trace(kConveyor, 60, 10);

    CHECK(first != "PARSE_ERROR");
    CHECK(first != "COMPILE_ERROR");
    CHECK(first != "FAULT");
    CHECK(!first.empty());
    CHECK(first == second);
}

TEST(compiled_program_reports_the_features_it_uses) {
    sc::plc::ParseResult parsed = sc::plc::parse_program(kConveyor);
    CHECK(parsed.program != nullptr);
    if (!parsed.program) {
        return;
    }
    const sc::plc::CompileResult compiled = sc::plc::compile(*parsed.program);
    CHECK(compiled.ok);

    const std::vector<std::string> features = sc::plc::features_used(compiled.program);
    const auto has = [&features](const char* name) {
        for (const std::string& feature : features) {
            if (feature == name) {
                return true;
            }
        }
        return false;
    };

    CHECK(has("TON"));
    CHECK(has("AND"));
    CHECK(has("OR"));
    CHECK(has("NOT"));
    CHECK(has("BOOL"));
    CHECK(!has("CTU"));
    CHECK(features.size() == 5);  // one entry per feature, not per use
}
