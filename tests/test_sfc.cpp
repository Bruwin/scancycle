#include "harness.hpp"

#include "plc/sfc.hpp"
#include "plc/vm.hpp"

#include <string>
#include <utility>
#include <vector>

using namespace sc;

namespace {

const char* kChart = R"sfc(VAR
    motor : BOOL;
    start : BOOL;
    done : BOOL;
END_VAR

INITIAL idle

STEP idle
END_STEP

STEP fill
    ACTION motor
END_STEP

TRANSITION idle -> fill := start
TRANSITION fill -> idle := done
)sfc";

bool tag(plc::Vm& vm, const char* name) { return vm.get(name).as_bool(); }

}  // namespace

TEST(sfc_parse_reads_steps_actions_and_transitions) {
    const plc::SfcParseResult parsed = plc::parse_sfc_source(kChart);
    CHECK(parsed.ok);
    CHECK(parsed.chart.steps.size() == 2);
    CHECK(parsed.chart.transitions.size() == 2);
    CHECK(parsed.chart.vars.size() == 3);
    CHECK(parsed.chart.initial == 0);
    if (parsed.chart.steps.size() == 2) {
        CHECK(parsed.chart.steps[1].name == "fill");
        CHECK(parsed.chart.steps[1].actions.size() == 1);
    }
}

TEST(sfc_source_round_trips_through_the_formatter) {
    const plc::SfcParseResult first = plc::parse_sfc_source(kChart);
    CHECK(first.ok);
    if (!first.ok) {
        return;
    }
    const std::string text = plc::format_sfc_source(first.chart);
    const plc::SfcParseResult second = plc::parse_sfc_source(text);
    CHECK(second.ok);
    CHECK(second.chart.steps.size() == first.chart.steps.size());
    CHECK(second.chart.transitions.size() == first.chart.transitions.size());
    CHECK(plc::format_sfc_source(second.chart) == text);
}

TEST(sfc_starts_in_its_initial_step) {
    const plc::SfcParseResult parsed = plc::parse_sfc_source(kChart);
    CHECK(parsed.ok);
    if (!parsed.ok) {
        return;
    }
    const plc::SfcResult built = plc::compile_sfc("Chart", parsed.chart);
    CHECK(built.ok);
    if (!built.ok) {
        return;
    }

    plc::Vm vm(built.program);
    vm.scan(0);
    CHECK(tag(vm, "idle"));
    CHECK(!tag(vm, "fill"));
    CHECK(!tag(vm, "motor"));
}

TEST(sfc_fires_a_transition_and_runs_the_action) {
    const plc::SfcParseResult parsed = plc::parse_sfc_source(kChart);
    CHECK(parsed.ok);
    if (!parsed.ok) {
        return;
    }
    const plc::SfcResult built = plc::compile_sfc("Chart", parsed.chart);
    CHECK(built.ok);
    if (!built.ok) {
        return;
    }

    plc::Vm vm(built.program);
    vm.scan(0);
    vm.set("start", plc::Value::boolean(true));
    vm.scan(10);
    CHECK(!tag(vm, "idle"));
    CHECK(tag(vm, "fill"));
    CHECK(tag(vm, "motor"));

    vm.set("start", plc::Value::boolean(false));
    vm.set("done", plc::Value::boolean(true));
    vm.scan(20);
    CHECK(tag(vm, "idle"));
    CHECK(!tag(vm, "fill"));
    CHECK(!tag(vm, "motor"));
}

TEST(sfc_walks_one_step_per_scan) {
    const char* source = R"sfc(VAR
    spare : BOOL;
END_VAR

INITIAL idle

STEP idle
END_STEP

STEP two
END_STEP

STEP three
END_STEP

TRANSITION idle -> two
TRANSITION two -> three
)sfc";
    const plc::SfcParseResult parsed = plc::parse_sfc_source(source);
    CHECK(parsed.ok);
    if (!parsed.ok) {
        return;
    }
    const plc::SfcResult built = plc::compile_sfc("Walk", parsed.chart);
    CHECK(built.ok);
    if (!built.ok) {
        return;
    }

    plc::Vm vm(built.program);
    vm.scan(0);
    CHECK(tag(vm, "idle"));
    CHECK(!tag(vm, "two"));
    CHECK(!tag(vm, "three"));

    // Both transitions pass at once; the chart still moves one step, because the
    // next-state rungs all read the state from the start of the scan.
    vm.scan(10);
    CHECK(tag(vm, "two"));
    CHECK(!tag(vm, "three"));
    CHECK(!tag(vm, "idle"));

    vm.scan(20);
    CHECK(tag(vm, "three"));
    CHECK(!tag(vm, "two"));
    CHECK(!tag(vm, "idle"));
}

TEST(sfc_reports_a_transition_to_an_unknown_step) {
    const plc::SfcParseResult parsed = plc::parse_sfc_source(
        "STEP idle\nEND_STEP\nTRANSITION idle -> nowhere := a\n");
    CHECK(!parsed.ok);
    if (!parsed.diagnostics.empty()) {
        CHECK(parsed.diagnostics.front().message.find("nowhere") != std::string::npos);
    }
}

TEST(sfc_reports_a_step_declared_twice) {
    const plc::SfcParseResult parsed =
        plc::parse_sfc_source("STEP idle\nEND_STEP\nSTEP idle\nEND_STEP\n");
    CHECK(!parsed.ok);
}

TEST(sfc_refuses_a_step_that_shadows_a_tag) {
    const plc::SfcParseResult parsed = plc::parse_sfc_source(
        "VAR\n    idle : BOOL;\nEND_VAR\nSTEP idle\nEND_STEP\n");
    CHECK(parsed.ok);
    if (!parsed.ok) {
        return;
    }

    plc::Diagnostics diagnostics;
    std::vector<plc::TagDecl> vars;
    plc::lower_sfc(parsed.chart, vars, diagnostics);
    CHECK(!diagnostics.empty());
}

TEST(sfc_refuses_a_chart_without_a_step) {
    const plc::SfcParseResult parsed = plc::parse_sfc_source("VAR\n    a : BOOL;\nEND_VAR\n");
    CHECK(!parsed.ok);
}
