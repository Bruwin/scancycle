#include "harness.hpp"

#include "plc/compiler.hpp"
#include "plc/netlist.hpp"
#include "plc/parser.hpp"
#include "plc/vm.hpp"

#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

using namespace sc::plc;

NetPtr contact(std::string var, bool negated = false) {
    auto node = std::make_unique<NetNode>();
    node->kind = NetKind::Contact;
    node->var = std::move(var);
    node->negated = negated;
    return node;
}

// A braced operand list would have to copy the unique_ptrs, so operands are
// collected explicitly.
std::vector<NetPtr> list(NetPtr first, NetPtr second) {
    std::vector<NetPtr> out;
    out.push_back(std::move(first));
    out.push_back(std::move(second));
    return out;
}

NetPtr junction(NetKind kind, std::vector<NetPtr> operands) {
    auto node = std::make_unique<NetNode>();
    node->kind = kind;
    node->operands = std::move(operands);
    return node;
}

NetPtr series(NetPtr first, NetPtr second) {
    return junction(NetKind::Series, list(std::move(first), std::move(second)));
}

NetPtr parallel(NetPtr first, NetPtr second) {
    return junction(NetKind::Parallel, list(std::move(first), std::move(second)));
}

std::vector<TagDecl> bool_tags(std::initializer_list<const char*> names) {
    std::vector<TagDecl> out;
    for (const char* name : names) {
        out.push_back(TagDecl{name, ValueType::Bool});
    }
    return out;
}

Rung rung_of(NetPtr condition, std::string coil) {
    Rung rung;
    rung.condition = std::move(condition);
    rung.coils.push_back(Coil{std::move(coil), false});
    return rung;
}

// Runs every combination of the named inputs and returns the coil's column of a
// truth table, so a rung can be checked against the expression it claims to be.
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

}  // namespace

TEST(a_contact_energises_its_coil) {
    const std::vector<TagDecl> declared = bool_tags({"start", "motor"});
    std::vector<Rung> rungs;
    rungs.push_back(rung_of(contact("start"), "motor"));

    const NetlistResult built = compile_netlist("Drive", rungs, declared);
    CHECK(built.ok);
    if (!built.ok) {
        return;
    }

    Vm vm(built.program);
    CHECK(vm.scan(0));
    CHECK(!vm.get("motor").as_bool());

    CHECK(vm.set("start", Value::boolean(true)));
    CHECK(vm.scan(10));
    CHECK(vm.get("motor").as_bool());
}

TEST(contacts_in_series_are_an_and) {
    const std::vector<TagDecl> declared = bool_tags({"a", "b", "out"});
    std::vector<Rung> rungs;
    rungs.push_back(rung_of(series(contact("a"), contact("b")), "out"));

    const NetlistResult built = compile_netlist("And", rungs, declared);
    CHECK(built.ok);
    if (!built.ok) {
        return;
    }

    Vm vm(built.program);
    CHECK(truth_table(vm, {"a", "b"}, "out") == "0001");
}

TEST(contacts_in_parallel_are_an_or) {
    const std::vector<TagDecl> declared = bool_tags({"a", "b", "out"});
    std::vector<Rung> rungs;
    rungs.push_back(rung_of(parallel(contact("a"), contact("b")), "out"));

    const NetlistResult built = compile_netlist("Or", rungs, declared);
    CHECK(built.ok);
    if (!built.ok) {
        return;
    }

    Vm vm(built.program);
    CHECK(truth_table(vm, {"a", "b"}, "out") == "0111");
}

TEST(a_normally_closed_contact_inverts) {
    const std::vector<TagDecl> declared = bool_tags({"a", "out"});
    std::vector<Rung> rungs;
    rungs.push_back(rung_of(contact("a", true), "out"));

    const NetlistResult built = compile_netlist("Not", rungs, declared);
    CHECK(built.ok);
    if (!built.ok) {
        return;
    }

    Vm vm(built.program);
    CHECK(truth_table(vm, {"a"}, "out") == "10");
}

TEST(a_negated_coil_writes_the_inverse) {
    const std::vector<TagDecl> declared = bool_tags({"a", "out"});
    std::vector<Rung> rungs;
    Rung rung = rung_of(contact("a"), "out");
    rung.coils[0].negated = true;
    rungs.push_back(std::move(rung));

    const NetlistResult built = compile_netlist("Negated", rungs, declared);
    CHECK(built.ok);
    if (!built.ok) {
        return;
    }

    Vm vm(built.program);
    // A negated coil energises when the rung does not conduct, which is the
    // opposite of the normally-open coil above.
    CHECK(truth_table(vm, {"a"}, "out") == "10");
}

TEST(an_unconditional_rung_is_always_energised) {
    const std::vector<TagDecl> declared = bool_tags({"out"});
    std::vector<Rung> rungs;
    rungs.push_back(rung_of(nullptr, "out"));

    const NetlistResult built = compile_netlist("Rail", rungs, declared);
    CHECK(built.ok);
    if (!built.ok) {
        return;
    }

    Vm vm(built.program);
    CHECK(vm.scan(0));
    CHECK(vm.get("out").as_bool());
}

TEST(two_coils_on_one_rung_both_follow_the_condition) {
    const std::vector<TagDecl> declared = bool_tags({"a", "one", "two"});
    std::vector<Rung> rungs;
    Rung rung;
    rung.condition = contact("a");
    rung.coils.push_back(Coil{"one", false});
    rung.coils.push_back(Coil{"two", true});
    rungs.push_back(std::move(rung));

    const NetlistResult built = compile_netlist("Pair", rungs, declared);
    CHECK(built.ok);
    if (!built.ok) {
        return;
    }

    Vm vm(built.program);
    CHECK(truth_table(vm, {"a"}, "one") == "01");
    CHECK(truth_table(vm, {"a"}, "two") == "10");
}

TEST(a_rung_without_a_coil_is_rejected) {
    const std::vector<TagDecl> declared = bool_tags({"a"});
    std::vector<Rung> rungs;
    rungs.push_back(Rung{});

    const NetlistResult built = compile_netlist("Empty", rungs, declared);
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("coil") != std::string::npos);
}

TEST(an_undeclared_contact_is_a_diagnostic) {
    const std::vector<TagDecl> declared = bool_tags({"a", "out"});
    std::vector<Rung> rungs;
    rungs.push_back(rung_of(contact("typo"), "out"));

    const NetlistResult built = compile_netlist("Typo", rungs, declared);
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("typo") != std::string::npos);
    CHECK(built.diagnostics[0].message.find("rung 1") != std::string::npos);
}

TEST(an_undeclared_coil_is_a_diagnostic) {
    const std::vector<TagDecl> declared = bool_tags({"a"});
    std::vector<Rung> rungs;
    rungs.push_back(rung_of(contact("a"), "missing"));

    const NetlistResult built = compile_netlist("Missing", rungs, declared);
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("missing") != std::string::npos);
}

TEST(a_tag_declared_twice_is_rejected) {
    const std::vector<TagDecl> declared = bool_tags({"a", "a"});
    std::vector<Rung> rungs;
    rungs.push_back(rung_of(nullptr, "a"));

    const NetlistResult built = compile_netlist("Twice", rungs, declared);
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("twice") != std::string::npos);
}

TEST(ladder_and_structured_text_produce_the_same_column) {
    // d := (a OR b) AND NOT c, drawn as a ladder and written as ST.
    const std::vector<TagDecl> declared = bool_tags({"a", "b", "c", "d"});
    std::vector<Rung> rungs;
    rungs.push_back(rung_of(series(parallel(contact("a"), contact("b")), contact("c", true)), "d"));

    const NetlistResult ladder = compile_netlist("Ladder", rungs, declared);
    CHECK(ladder.ok);
    if (!ladder.ok) {
        return;
    }

    const char* source = R"st(PROGRAM Equivalent
VAR
    a : BOOL;
    b : BOOL;
    c : BOOL;
    d : BOOL;
END_VAR
    d := (a OR b) AND NOT c;
END_PROGRAM
)st";
    ParseResult parsed = parse_program(source);
    CHECK(parsed.program != nullptr);
    if (!parsed.program) {
        return;
    }
    CompileResult compiled = compile(*parsed.program);
    CHECK(compiled.ok);
    if (!compiled.ok) {
        return;
    }

    Vm ladder_vm(ladder.program);
    Vm st_vm(compiled.program);
    const std::string ladder_column = truth_table(ladder_vm, {"a", "b", "c"}, "d");
    const std::string st_column = truth_table(st_vm, {"a", "b", "c"}, "d");

    CHECK(ladder_column == st_column);
    CHECK(ladder_column == "01110000");
}

namespace {

Rung timer_rung(NetPtr condition, std::string out, sc::i64 preset_ms) {
    Rung rung;
    rung.condition = std::move(condition);
    rung.timer = TimerSink{std::move(out), preset_ms};
    return rung;
}

}  // namespace

TEST(a_timer_rung_holds_its_output_until_the_preset_elapses) {
    const std::vector<TagDecl> declared = bool_tags({"run", "ready"});
    std::vector<Rung> rungs;
    rungs.push_back(timer_rung(contact("run"), "ready", 1000));

    const NetlistResult built = compile_netlist("Delay", rungs, declared);
    CHECK(built.ok);
    if (!built.ok) {
        return;
    }

    Vm vm(built.program);
    CHECK(vm.set("run", Value::boolean(true)));
    CHECK(vm.scan(0));
    CHECK(!vm.get("ready").as_bool());
    CHECK(vm.scan(500));
    CHECK(!vm.get("ready").as_bool());
    CHECK(vm.scan(999));
    CHECK(!vm.get("ready").as_bool());
    CHECK(vm.scan(1000));
    CHECK(vm.get("ready").as_bool());
}

TEST(a_timer_restarts_when_its_condition_drops) {
    const std::vector<TagDecl> declared = bool_tags({"run", "ready"});
    std::vector<Rung> rungs;
    rungs.push_back(timer_rung(contact("run"), "ready", 1000));

    const NetlistResult built = compile_netlist("Restart", rungs, declared);
    CHECK(built.ok);
    if (!built.ok) {
        return;
    }

    Vm vm(built.program);
    CHECK(vm.set("run", Value::boolean(true)));
    CHECK(vm.scan(0));
    CHECK(vm.scan(800));
    CHECK(!vm.get("ready").as_bool());

    CHECK(vm.set("run", Value::boolean(false)));
    CHECK(vm.scan(900));
    CHECK(!vm.get("ready").as_bool());

    // A fresh start at 1000 means the second second is the one that counts.
    CHECK(vm.set("run", Value::boolean(true)));
    CHECK(vm.scan(1000));
    CHECK(!vm.get("ready").as_bool());
    CHECK(vm.scan(1500));
    CHECK(!vm.get("ready").as_bool());
    CHECK(vm.scan(2000));
    CHECK(vm.get("ready").as_bool());
}

TEST(a_timer_and_coils_on_one_rung_are_refused) {
    const std::vector<TagDecl> declared = bool_tags({"a", "ready", "out"});
    std::vector<Rung> rungs;
    Rung rung = timer_rung(contact("a"), "ready", 1000);
    rung.coils.push_back(Coil{"out", false});
    rungs.push_back(std::move(rung));

    const NetlistResult built = compile_netlist("Mixed", rungs, declared);
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("one sink") != std::string::npos);
}

TEST(a_timer_output_must_be_a_declared_tag) {
    const std::vector<TagDecl> declared = bool_tags({"run"});
    std::vector<Rung> rungs;
    rungs.push_back(timer_rung(contact("run"), "ready", 1000));

    const NetlistResult built = compile_netlist("Undeclared", rungs, declared);
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("ready") != std::string::npos);
}

TEST(a_timer_output_must_be_bool) {
    std::vector<TagDecl> declared;
    declared.push_back(TagDecl{"run", ValueType::Bool});
    declared.push_back(TagDecl{"count", ValueType::Int});
    std::vector<Rung> rungs;
    rungs.push_back(timer_rung(contact("run"), "count", 1000));

    const NetlistResult built = compile_netlist("WrongType", rungs, declared);
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("must be BOOL") != std::string::npos);
}

TEST(a_timer_preset_must_be_longer_than_zero) {
    const std::vector<TagDecl> declared = bool_tags({"run", "ready"});
    std::vector<Rung> rungs;
    rungs.push_back(timer_rung(contact("run"), "ready", 0));

    const NetlistResult built = compile_netlist("Zero", rungs, declared);
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("longer than zero") != std::string::npos);
}

namespace {

Rung counter_rung(NetPtr condition, NetPtr reset, std::string out, sc::i64 preset,
                  bool down = false) {
    Rung rung;
    rung.condition = std::move(condition);
    CounterSink sink;
    sink.var = std::move(out);
    sink.preset = preset;
    sink.down = down;
    sink.reset = std::move(reset);
    rung.counter = std::move(sink);
    return rung;
}

Rung off_delay_rung(NetPtr condition, std::string out, sc::i64 preset_ms) {
    Rung rung;
    rung.condition = std::move(condition);
    rung.timer = TimerSink{std::move(out), preset_ms, true};
    return rung;
}

}  // namespace

TEST(an_off_delay_timer_holds_its_output_after_the_condition_opens) {
    const std::vector<TagDecl> declared = bool_tags({"run", "ready"});
    std::vector<Rung> rungs;
    rungs.push_back(off_delay_rung(contact("run"), "ready", 1000));

    const NetlistResult built = compile_netlist("RunOn", rungs, declared);
    CHECK(built.ok);
    if (!built.ok) {
        return;
    }

    Vm vm(built.program);
    CHECK(vm.set("run", Value::boolean(true)));
    CHECK(vm.scan(0));
    CHECK(vm.get("ready").as_bool());

    CHECK(vm.set("run", Value::boolean(false)));
    CHECK(vm.scan(100));
    CHECK(vm.get("ready").as_bool());
    CHECK(vm.scan(1099));
    CHECK(vm.get("ready").as_bool());
    CHECK(vm.scan(1100));
    CHECK(!vm.get("ready").as_bool());
}

TEST(an_up_counter_sets_its_output_at_the_preset) {
    const std::vector<TagDecl> declared = bool_tags({"pulse", "done"});
    std::vector<Rung> rungs;
    rungs.push_back(counter_rung(contact("pulse"), nullptr, "done", 2));

    const NetlistResult built = compile_netlist("Count", rungs, declared);
    CHECK(built.ok);
    if (!built.ok) {
        return;
    }

    Vm vm(built.program);
    CHECK(vm.set("pulse", Value::boolean(true)));
    CHECK(vm.scan(10));
    CHECK(!vm.get("done").as_bool());

    // Holding the input high is not a second edge.
    CHECK(vm.scan(20));
    CHECK(!vm.get("done").as_bool());

    CHECK(vm.set("pulse", Value::boolean(false)));
    CHECK(vm.scan(30));
    CHECK(vm.set("pulse", Value::boolean(true)));
    CHECK(vm.scan(40));
    CHECK(vm.get("done").as_bool());
}

TEST(an_up_counter_reset_input_clears_the_count) {
    const std::vector<TagDecl> declared = bool_tags({"pulse", "reset", "done"});
    std::vector<Rung> rungs;
    rungs.push_back(counter_rung(contact("pulse"), contact("reset"), "done", 2));

    const NetlistResult built = compile_netlist("ResetCount", rungs, declared);
    CHECK(built.ok);
    if (!built.ok) {
        return;
    }

    Vm vm(built.program);
    CHECK(vm.set("pulse", Value::boolean(true)));
    CHECK(vm.scan(10));
    CHECK(vm.set("pulse", Value::boolean(false)));
    CHECK(vm.scan(20));
    CHECK(vm.set("pulse", Value::boolean(true)));
    CHECK(vm.scan(30));
    CHECK(vm.get("done").as_bool());

    CHECK(vm.set("reset", Value::boolean(true)));
    CHECK(vm.scan(40));
    CHECK(!vm.get("done").as_bool());

    CHECK(vm.set("reset", Value::boolean(false)));
    CHECK(vm.set("pulse", Value::boolean(false)));
    CHECK(vm.scan(50));
    CHECK(vm.set("pulse", Value::boolean(true)));
    CHECK(vm.scan(60));
    CHECK(!vm.get("done").as_bool());
}

TEST(a_down_counter_counts_down_from_its_preset) {
    const std::vector<TagDecl> declared = bool_tags({"pulse", "load", "empty"});
    std::vector<Rung> rungs;
    rungs.push_back(counter_rung(contact("pulse"), contact("load"), "empty", 2, true));

    const NetlistResult built = compile_netlist("CountDown", rungs, declared);
    CHECK(built.ok);
    if (!built.ok) {
        return;
    }

    Vm vm(built.program);
    CHECK(vm.set("load", Value::boolean(true)));
    CHECK(vm.scan(0));
    CHECK(!vm.get("empty").as_bool());

    CHECK(vm.set("load", Value::boolean(false)));
    CHECK(vm.set("pulse", Value::boolean(true)));
    CHECK(vm.scan(10));
    CHECK(!vm.get("empty").as_bool());

    CHECK(vm.set("pulse", Value::boolean(false)));
    CHECK(vm.scan(20));
    CHECK(vm.set("pulse", Value::boolean(true)));
    CHECK(vm.scan(30));
    CHECK(vm.get("empty").as_bool());
}

TEST(a_counter_output_must_be_bool) {
    std::vector<TagDecl> declared;
    declared.push_back(TagDecl{"pulse", ValueType::Bool});
    declared.push_back(TagDecl{"count", ValueType::Int});
    std::vector<Rung> rungs;
    rungs.push_back(counter_rung(contact("pulse"), nullptr, "count", 2));

    const NetlistResult built = compile_netlist("WrongCountType", rungs, declared);
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("must be BOOL") != std::string::npos);
}

TEST(a_counter_preset_must_be_longer_than_zero) {
    const std::vector<TagDecl> declared = bool_tags({"pulse", "done"});
    std::vector<Rung> rungs;
    rungs.push_back(counter_rung(contact("pulse"), nullptr, "done", 0));

    const NetlistResult built = compile_netlist("ZeroCount", rungs, declared);
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("longer than zero") != std::string::npos);
}

TEST(a_timer_and_counter_on_one_rung_are_refused) {
    const std::vector<TagDecl> declared = bool_tags({"a", "ready", "counted"});
    std::vector<Rung> rungs;
    Rung rung;
    rung.condition = contact("a");
    rung.timer = TimerSink{"ready", 1000};
    rung.counter = CounterSink{"counted", 3, false, nullptr};
    rungs.push_back(std::move(rung));

    const NetlistResult built = compile_netlist("TwoSinks", rungs, declared);
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("one sink") != std::string::npos);
}

TEST(an_undeclared_reset_contact_is_a_diagnostic) {
    const std::vector<TagDecl> declared = bool_tags({"pulse", "done"});
    std::vector<Rung> rungs;
    rungs.push_back(counter_rung(contact("pulse"), contact("nope"), "done", 3));

    const NetlistResult built = compile_netlist("BadReset", rungs, declared);
    CHECK(!built.ok);
    CHECK(built.diagnostics.size() == 1);
    CHECK(built.diagnostics[0].message.find("nope") != std::string::npos);
    CHECK(built.diagnostics[0].message.find("reset input") != std::string::npos);
}
