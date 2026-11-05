#include "harness.hpp"

#include "plc/compiler.hpp"
#include "plc/ladder_text.hpp"
#include "plc/netlist.hpp"
#include "plc/parser.hpp"
#include "plc/vm.hpp"

#include <string>
#include <utility>
#include <vector>

namespace {

using namespace sc::plc;

const char* kHeader =
    "VAR\n    a : BOOL;\n    b : BOOL;\n    c : BOOL;\n    d : BOOL;\nEND_VAR\n";

struct Built {
    bool ok = false;
    CompiledProgram program;
    Diagnostics diagnostics;
};

Built build(std::string_view source) {
    Built out;
    LadderParseResult parsed = parse_ladder_source(source);
    if (!parsed.ok) {
        out.diagnostics = std::move(parsed.diagnostics);
        return out;
    }
    NetlistResult compiled = compile_netlist("Rungs", parsed.source.rungs, parsed.source.vars);
    out.ok = compiled.ok;
    out.program = std::move(compiled.program);
    out.diagnostics = std::move(compiled.diagnostics);
    return out;
}

std::string truth_table(Vm& vm, const std::vector<std::string>& inputs,
                        const std::string& output) {
    std::string column;
    const std::size_t rows = std::size_t{1} << inputs.size();
    for (std::size_t row = 0; row < rows; ++row) {
        for (std::size_t bit = 0; bit < inputs.size(); ++bit) {
            vm.set(inputs[bit], Value::boolean(((row >> bit) & 1u) != 0));
        }
        vm.scan(static_cast<sc::i64>(row) * 10);
        column += vm.get(output).as_bool() ? '1' : '0';
    }
    return column;
}

std::string column_of(std::string_view body, const std::vector<std::string>& inputs,
                      const std::string& output) {
    const std::string source = std::string(kHeader) + std::string(body);
    Built built = build(source);
    CHECK(built.ok);
    if (!built.ok) {
        return std::string();
    }
    Vm vm(built.program);
    return truth_table(vm, inputs, output);
}

}  // namespace

TEST(a_rung_drives_its_coil_from_a_contact) {
    CHECK(column_of("d = a\n", {"a"}, "d") == "01");
}

TEST(ampersand_is_contacts_in_series) {
    CHECK(column_of("d = a & b\n", {"a", "b"}, "d") == "0001");
}

TEST(bar_is_branches_in_parallel) {
    CHECK(column_of("d = a | b\n", {"a", "b"}, "d") == "0111");
}

TEST(bang_is_a_normally_closed_contact) {
    CHECK(column_of("d = !a\n", {"a"}, "d") == "10");
}

TEST(and_binds_tighter_than_or) {
    // Same as a | (b & c), which is what the precedence says it must be.
    CHECK(column_of("d = a | b & c\n", {"a", "b", "c"}, "d") == "01010111");
}

TEST(parentheses_override_precedence) {
    CHECK(column_of("d = (a | b) & c\n", {"a", "b", "c"}, "d") == "00000111");
}

TEST(a_rung_file_and_structured_text_agree) {
    const std::string source = std::string(kHeader) + "d = (a | b) & !c\n";
    Built built = build(source);
    CHECK(built.ok);
    if (!built.ok) {
        return;
    }

    const char* st_source = R"st(PROGRAM Equivalent
VAR
    a : BOOL;
    b : BOOL;
    c : BOOL;
    d : BOOL;
END_VAR
    d := (a OR b) AND NOT c;
END_PROGRAM
)st";
    ParseResult parsed = parse_program(st_source);
    CHECK(parsed.program != nullptr);
    if (!parsed.program) {
        return;
    }
    CompileResult compiled = compile(*parsed.program);
    CHECK(compiled.ok);
    if (!compiled.ok) {
        return;
    }

    Vm ladder_vm(built.program);
    Vm st_vm(compiled.program);
    const std::string ladder_column = truth_table(ladder_vm, {"a", "b", "c"}, "d");
    CHECK(ladder_column == truth_table(st_vm, {"a", "b", "c"}, "d"));
    CHECK(ladder_column == "01110000");
}

TEST(comments_and_blank_lines_are_skipped) {
    const std::string source = std::string(kHeader) +
                               "\n(* the seal-in *)\n// another comment\nd = a\n";
    Built built = build(source);
    CHECK(built.ok);
    CHECK(built.diagnostics.empty());
}

TEST(a_line_without_an_equals_is_a_diagnostic) {
    const std::string source = std::string(kHeader) + "d = a\nd a\n";
    Built built = build(source);
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].line == 8);
    CHECK(built.diagnostics[0].message.find("no '='") != std::string::npos);
}

TEST(a_second_equals_is_refused) {
    const std::string source = std::string(kHeader) + "d == a\n";
    Built built = build(source);
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("one '='") != std::string::npos);
}

TEST(a_missing_close_paren_is_a_diagnostic) {
    const std::string source = std::string(kHeader) + "d = (a & b\n";
    Built built = build(source);
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("missing ')'") != std::string::npos);
}

TEST(bang_before_a_group_is_refused) {
    const std::string source = std::string(kHeader) + "d = !(a | b)\n";
    Built built = build(source);
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("not a group") != std::string::npos);
}

TEST(an_empty_condition_is_a_diagnostic) {
    const std::string source = std::string(kHeader) + "d =\n";
    Built built = build(source);
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("expected a tag") != std::string::npos);
}

TEST(an_undeclared_contact_fails_in_the_netlist) {
    const std::string source = std::string(kHeader) + "d = zzz\n";
    Built built = build(source);
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("zzz") != std::string::npos);
}

TEST(an_unterminated_comment_is_a_diagnostic) {
    Built built = build("(* never closed\nVAR\n    a : BOOL;\nEND_VAR\nd = a\n");
    CHECK(!built.ok);
    CHECK(!built.diagnostics.empty());
    CHECK(built.diagnostics[0].message.find("unterminated") != std::string::npos);
}

TEST(a_timer_rung_delays_its_coil) {
    const std::string source = std::string(kHeader) + "d = TON(a, 1s)\n";
    Built built = build(source);
    CHECK(built.ok);
    if (!built.ok) {
        return;
    }

    Vm vm(built.program);
    CHECK(vm.set("a", Value::boolean(true)));
    CHECK(vm.scan(0));
    CHECK(!vm.get("d").as_bool());
    CHECK(vm.scan(500));
    CHECK(!vm.get("d").as_bool());
    CHECK(vm.scan(1000));
    CHECK(vm.get("d").as_bool());
}

TEST(a_timer_rung_may_be_lower_case) {
    const std::string source = std::string(kHeader) + "d = ton(a, 1s)\n";
    Built built = build(source);
    CHECK(built.ok);
    if (!built.ok) {
        return;
    }

    Vm vm(built.program);
    CHECK(vm.set("a", Value::boolean(true)));
    CHECK(vm.scan(0));
    CHECK(!vm.get("d").as_bool());
    CHECK(vm.scan(1000));
    CHECK(vm.get("d").as_bool());
}

TEST(a_timer_preset_may_be_written_in_milliseconds) {
    const std::string source = std::string(kHeader) + "d = TON(a, 250ms)\n";
    Built built = build(source);
    CHECK(built.ok);
    if (!built.ok) {
        return;
    }

    Vm vm(built.program);
    CHECK(vm.set("a", Value::boolean(true)));
    CHECK(vm.scan(0));
    CHECK(vm.scan(200));
    CHECK(!vm.get("d").as_bool());
    CHECK(vm.scan(250));
    CHECK(vm.get("d").as_bool());
}

TEST(a_timer_preset_needs_a_unit) {
    Built built = build(std::string(kHeader) + "d = TON(a, 5)\n");
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("not a duration") != std::string::npos);
}

TEST(a_timer_without_a_preset_is_a_diagnostic) {
    Built built = build(std::string(kHeader) + "d = TON(a)\n");
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("two arguments") != std::string::npos);
}

TEST(a_zero_timer_preset_is_refused) {
    Built built = build(std::string(kHeader) + "d = TON(a, 0s)\n");
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("longer than zero") != std::string::npos);
}

TEST(a_timer_rung_ends_at_its_closing_paren) {
    Built built = build(std::string(kHeader) + "d = TON(a, 1s) & b\n");
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("remove what follows") != std::string::npos);
}

TEST(an_off_delay_timer_rung_holds_the_coil) {
    const std::string source = std::string(kHeader) + "d = TOF(a, 1s)\n";
    Built built = build(source);
    CHECK(built.ok);
    if (!built.ok) {
        return;
    }

    Vm vm(built.program);
    CHECK(vm.set("a", Value::boolean(true)));
    CHECK(vm.scan(0));
    CHECK(vm.get("d").as_bool());

    CHECK(vm.set("a", Value::boolean(false)));
    CHECK(vm.scan(100));
    CHECK(vm.get("d").as_bool());
    CHECK(vm.scan(1100));
    CHECK(!vm.get("d").as_bool());
}

TEST(a_counter_rung_counts_rising_edges) {
    const std::string source = std::string(kHeader) + "d = CTU(a, 2)\n";
    Built built = build(source);
    CHECK(built.ok);
    if (!built.ok) {
        return;
    }

    Vm vm(built.program);
    CHECK(vm.scan(0));
    CHECK(!vm.get("d").as_bool());

    CHECK(vm.set("a", Value::boolean(true)));
    CHECK(vm.scan(10));
    CHECK(!vm.get("d").as_bool());
    CHECK(vm.scan(20));
    CHECK(!vm.get("d").as_bool());

    CHECK(vm.set("a", Value::boolean(false)));
    CHECK(vm.scan(30));
    CHECK(vm.set("a", Value::boolean(true)));
    CHECK(vm.scan(40));
    CHECK(vm.get("d").as_bool());
}

TEST(a_counter_rung_takes_a_reset_network) {
    const std::string source = std::string(kHeader) + "d = CTU(a, b, 2)\n";
    Built built = build(source);
    CHECK(built.ok);
    if (!built.ok) {
        return;
    }

    Vm vm(built.program);
    CHECK(vm.set("a", Value::boolean(true)));
    CHECK(vm.scan(10));
    CHECK(vm.set("a", Value::boolean(false)));
    CHECK(vm.scan(20));
    CHECK(vm.set("a", Value::boolean(true)));
    CHECK(vm.scan(30));
    CHECK(vm.get("d").as_bool());

    CHECK(vm.set("b", Value::boolean(true)));
    CHECK(vm.scan(40));
    CHECK(!vm.get("d").as_bool());

    CHECK(vm.set("b", Value::boolean(false)));
    CHECK(vm.set("a", Value::boolean(false)));
    CHECK(vm.scan(50));
    CHECK(vm.set("a", Value::boolean(true)));
    CHECK(vm.scan(60));
    CHECK(!vm.get("d").as_bool());
}

TEST(a_counter_preset_must_be_a_whole_number) {
    Built built = build(std::string(kHeader) + "d = CTU(a, two)\n");
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("not a count") != std::string::npos);
}

TEST(a_counter_without_a_preset_is_a_diagnostic) {
    Built built = build(std::string(kHeader) + "d = CTU(a)\n");
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("two or three arguments") != std::string::npos);
}

TEST(a_counter_reset_needs_a_condition) {
    Built built = build(std::string(kHeader) + "d = CTU(a, , 3)\n");
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("reset input needs a condition") !=
          std::string::npos);
}

TEST(a_counter_rung_publishes_its_count) {
    const std::string header =
        "VAR\n    a : BOOL;\n    d : BOOL;\n    parts : INT;\nEND_VAR\n";
    Built built = build(header + "d, parts = CTU(a, 5)\n");
    CHECK(built.ok);
    if (!built.ok) {
        return;
    }

    Vm vm(built.program);
    CHECK(vm.set("a", Value::boolean(true)));
    CHECK(vm.scan(10));
    CHECK(vm.get("parts").as_int() == 1);
    CHECK(!vm.get("d").as_bool());

    CHECK(vm.set("a", Value::boolean(false)));
    CHECK(vm.scan(20));
    CHECK(vm.set("a", Value::boolean(true)));
    CHECK(vm.scan(30));
    CHECK(vm.get("parts").as_int() == 2);
}

TEST(only_a_counter_has_a_second_output) {
    Built built = build(std::string(kHeader) + "d, a = a\n");
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("second output") != std::string::npos);
}

TEST(a_rung_writes_at_most_two_tags) {
    Built built = build(std::string(kHeader) + "d, a, b = a\n");
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("at most two tags") != std::string::npos);
}

TEST(a_counter_value_tag_must_be_declared_on_a_rung) {
    Built built = build(std::string(kHeader) + "d, missing = CTU(a, 2)\n");
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("missing") != std::string::npos);
}
