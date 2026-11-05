#include "plc/sfc.hpp"

#include "plc/ladder_text.hpp"
#include "plc/source_text.hpp"

#include <cctype>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sc::plc {
namespace {

bool is_identifier(std::string_view text) {
    if (text.empty()) {
        return false;
    }
    if (std::isalpha(static_cast<unsigned char>(text.front())) == 0 && text.front() != '_') {
        return false;
    }
    for (const char c : text) {
        if (std::isalnum(static_cast<unsigned char>(c)) == 0 && c != '_') {
            return false;
        }
    }
    return true;
}

// Matches `word` at the start of the line and hands back the rest. A word glued
// to more letters is not the keyword: STEPX is a step, not a STEP.
bool keyword(std::string_view line, std::string_view word, std::string_view& rest) {
    if (line.size() < word.size() || !text_equals_ci(line.substr(0, word.size()), word)) {
        return false;
    }
    if (line.size() == word.size()) {
        rest = {};
        return true;
    }
    if (std::isspace(static_cast<unsigned char>(line[word.size()])) == 0) {
        return false;
    }
    rest = trim_text(line.substr(word.size()));
    return true;
}

NetPtr contact(std::string name, bool negated = false) {
    auto node = std::make_unique<NetNode>();
    node->kind = NetKind::Contact;
    node->var = std::move(name);
    node->negated = negated;
    return node;
}

NetPtr always_node() {
    auto node = std::make_unique<NetNode>();
    node->kind = NetKind::Always;
    return node;
}

// Builds a junction and collapses a single operand, so a two-term expression does
// not carry a junction node the formatter would print with parentheses.
NetPtr junction(NetKind kind, std::vector<NetPtr> operands) {
    if (operands.size() == 1) {
        return std::move(operands.front());
    }
    auto node = std::make_unique<NetNode>();
    node->kind = kind;
    node->operands = std::move(operands);
    return node;
}

}  // namespace

std::string sfc_next_tag(std::string_view step) {
    return "__sfc_" + std::string(step) + "_next";
}

SfcParseResult parse_sfc_source(std::string_view source) {
    SfcParseResult result;

    std::string text;
    if (!strip_comments(source, text)) {
        result.diagnostics.push_back({0, 0, "unterminated (* comment"});
        return result;
    }

    VarBlock block;
    if (!parse_var_block(text, block, result.diagnostics) || !result.diagnostics.empty()) {
        return result;
    }
    result.chart.vars = block.vars;

    std::string initial_name;
    int initial_line = 0;
    bool in_step = false;
    const std::string_view view = text;
    int line_number = block.next_line - 1;
    std::size_t pos = block.begin;

    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos) {
            end = text.size();
        }
        ++line_number;
        const std::string_view line = trim_text(view.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty()) {
            continue;
        }

        std::string_view rest;
        if (text_equals_ci(line, "end_step")) {
            if (!in_step) {
                result.diagnostics.push_back({line_number, 1, "END_STEP without a STEP"});
            }
            in_step = false;
            continue;
        }
        if (keyword(line, "step", rest)) {
            if (in_step) {
                result.diagnostics.push_back({line_number, 1, "a step cannot contain a step"});
                continue;
            }
            if (!is_identifier(rest)) {
                result.diagnostics.push_back(
                    {line_number, 1, "a step needs a name: STEP name"});
                continue;
            }
            bool twice = false;
            for (const SfcStep& step : result.chart.steps) {
                twice = twice || step.name == rest;
            }
            if (twice) {
                result.diagnostics.push_back(
                    {line_number, 1, "step '" + std::string(rest) + "' is declared twice"});
                continue;
            }
            result.chart.steps.push_back(SfcStep{std::string(rest), {}});
            in_step = true;
            continue;
        }
        if (keyword(line, "action", rest)) {
            if (!in_step) {
                result.diagnostics.push_back({line_number, 1, "an action belongs to a step"});
                continue;
            }
            if (!is_identifier(rest)) {
                result.diagnostics.push_back(
                    {line_number, 1, "an action needs a tag: ACTION tag"});
                continue;
            }
            result.chart.steps.back().actions.push_back(SfcAction{std::string(rest)});
            continue;
        }
        if (keyword(line, "initial", rest)) {
            if (!is_identifier(rest)) {
                result.diagnostics.push_back(
                    {line_number, 1, "INITIAL needs a step name"});
                continue;
            }
            if (!initial_name.empty()) {
                result.diagnostics.push_back({line_number, 1, "INITIAL is given twice"});
                continue;
            }
            initial_name = std::string(rest);
            initial_line = line_number;
            continue;
        }
        if (keyword(line, "transition", rest)) {
            const std::size_t arrow = rest.find("->");
            if (arrow == std::string_view::npos) {
                result.diagnostics.push_back(
                    {line_number, 1, "a transition is 'from -> to := condition'"});
                continue;
            }
            const std::string_view from = trim_text(rest.substr(0, arrow));
            const std::string_view tail = trim_text(rest.substr(arrow + 2));
            const std::size_t assign = tail.find(":=");
            const std::string_view to = trim_text(tail.substr(0, assign));
            const std::string_view guard_text =
                assign == std::string_view::npos
                    ? std::string_view()
                    : trim_text(tail.substr(assign + 2));
            if (!is_identifier(from) || !is_identifier(to)) {
                result.diagnostics.push_back(
                    {line_number, 1, "a transition needs two step names"});
                continue;
            }

            SfcTransition transition;
            transition.from = std::string(from);
            transition.to = std::string(to);
            transition.line = line_number;
            if (!guard_text.empty()) {
                transition.guard =
                    parse_net_expression(guard_text, line_number, result.diagnostics);
                if (!transition.guard) {
                    continue;
                }
            }
            result.chart.transitions.push_back(std::move(transition));
            continue;
        }

        result.diagnostics.push_back(
            {line_number, 1, "'" + std::string(line) + "' is not an SFC statement"});
    }

    if (in_step) {
        result.diagnostics.push_back({0, 0, "a step is missing END_STEP"});
    }
    if (result.chart.steps.empty()) {
        result.diagnostics.push_back({0, 0, "an SFC chart needs at least one step"});
    }

    const auto find_step = [&result](std::string_view name) {
        for (std::size_t i = 0; i < result.chart.steps.size(); ++i) {
            if (result.chart.steps[i].name == name) {
                return i;
            }
        }
        return result.chart.steps.size();
    };

    for (const SfcTransition& transition : result.chart.transitions) {
        if (find_step(transition.from) == result.chart.steps.size()) {
            result.diagnostics.push_back({transition.line, 1,
                                          "a transition leaves unknown step '" +
                                              transition.from + "'"});
        }
        if (find_step(transition.to) == result.chart.steps.size()) {
            result.diagnostics.push_back({transition.line, 1,
                                          "a transition enters unknown step '" +
                                              transition.to + "'"});
        }
    }

    if (!initial_name.empty()) {
        const std::size_t index = find_step(initial_name);
        if (index == result.chart.steps.size()) {
            result.diagnostics.push_back(
                {initial_line, 1, "INITIAL names unknown step '" + initial_name + "'"});
        } else {
            result.chart.initial = index;
        }
    }

    result.ok = result.diagnostics.empty();
    return result;
}

std::string format_sfc_source(const SfcChart& chart) {
    std::string text;
    if (!chart.vars.empty()) {
        text += "VAR\n";
        for (const TagDecl& var : chart.vars) {
            text += "    " + var.name + " : " + value_type_name(var.type) + ";\n";
        }
        text += "END_VAR\n\n";
    }
    if (chart.steps.empty() || chart.initial >= chart.steps.size()) {
        return text;
    }

    text += "INITIAL " + chart.steps[chart.initial].name + "\n\n";
    for (const SfcStep& step : chart.steps) {
        text += "STEP " + step.name + "\n";
        for (const SfcAction& action : step.actions) {
            text += "    ACTION " + action.tag + "\n";
        }
        text += "END_STEP\n\n";
    }
    for (const SfcTransition& transition : chart.transitions) {
        text += "TRANSITION " + transition.from + " -> " + transition.to;
        if (transition.guard) {
            text += " := " + format_net_expression(*transition.guard);
        }
        text += "\n";
    }
    return text;
}

std::vector<Rung> lower_sfc(const SfcChart& chart, std::vector<TagDecl>& vars,
                            Diagnostics& diagnostics) {
    std::vector<Rung> rungs;
    vars = chart.vars;

    if (chart.steps.empty()) {
        diagnostics.push_back({0, 0, "an SFC chart needs at least one step"});
        return rungs;
    }
    if (chart.initial >= chart.steps.size()) {
        diagnostics.push_back({0, 0, "the initial step is not in the chart"});
        return rungs;
    }

    // Step tags are chart structure, so the chart does not declare them; a chart
    // that does declare one would have two slots under one name.
    for (const SfcStep& step : chart.steps) {
        for (const TagDecl& var : vars) {
            if (var.name == step.name) {
                diagnostics.push_back(
                    {0, 0, "step '" + step.name + "' is also a declared tag"});
                return rungs;
            }
        }
        vars.push_back(TagDecl{step.name, ValueType::Bool});
        vars.push_back(TagDecl{sfc_next_tag(step.name), ValueType::Bool});
    }

    for (std::size_t i = 0; i < chart.steps.size(); ++i) {
        const SfcStep& step = chart.steps[i];
        std::vector<NetPtr> incoming;
        std::vector<NetPtr> leaving;
        for (const SfcTransition& transition : chart.transitions) {
            if (transition.from == step.name) {
                leaving.push_back(transition.guard ? clone_net(*transition.guard)
                                                   : always_node());
            }
            if (transition.to == step.name) {
                std::vector<NetPtr> terms;
                terms.push_back(contact(transition.from));
                if (transition.guard) {
                    terms.push_back(clone_net(*transition.guard));
                }
                incoming.push_back(junction(NetKind::Series, std::move(terms)));
            }
        }

        // A step holds until one of its transitions passes, and forever when it
        // has none: that is how a chart ends.
        std::vector<NetPtr> hold;
        hold.push_back(contact(step.name));
        if (!leaving.empty()) {
            const NetPtr passes = junction(NetKind::Parallel, std::move(leaving));
            hold.push_back(negate_net(*passes));
        }

        std::vector<NetPtr> next_terms;
        next_terms.push_back(junction(NetKind::Series, std::move(hold)));

        // Built before `incoming` is moved into the term list: the resting term
        // needs copies of the transitions and a moved-from pointer is null.
        if (i == chart.initial && chart.steps.size() > 1) {
            std::vector<NetPtr> others;
            for (std::size_t j = 0; j < chart.steps.size(); ++j) {
                if (j != i) {
                    others.push_back(contact(chart.steps[j].name));
                }
            }
            // The initial step is the resting state, entered only when the chart is
            // unoccupied: no other step is active, no transition into it is passing,
            // and it is not already active. All three terms matter. Without the last
            // one this would double as a hold and keep the initial step on for the
            // scan in which its transition fires, so the chart would briefly have
            // two active steps instead of one token.
            std::vector<NetPtr> resting;
            resting.push_back(contact(step.name, true));
            resting.push_back(negate_net(*junction(NetKind::Parallel, std::move(others))));
            if (!incoming.empty()) {
                std::vector<NetPtr> copies;
                for (const NetPtr& term : incoming) {
                    copies.push_back(clone_net(*term));
                }
                resting.push_back(
                    negate_net(*junction(NetKind::Parallel, std::move(copies))));
            }
            next_terms.push_back(junction(NetKind::Series, std::move(resting)));
        }

        for (NetPtr& term : incoming) {
            next_terms.push_back(std::move(term));
        }

        Rung rung;
        rung.condition = junction(NetKind::Parallel, std::move(next_terms));
        rung.coils.push_back(Coil{sfc_next_tag(step.name), false});
        rungs.push_back(std::move(rung));
    }

    // All next-state rungs before any state rung: every step is computed from the
    // state the chart had at the start of the scan, so one scan moves one step.
    for (const SfcStep& step : chart.steps) {
        Rung rung;
        rung.condition = contact(sfc_next_tag(step.name));
        rung.coils.push_back(Coil{step.name, false});
        rungs.push_back(std::move(rung));
    }

    // One rung per action tag, driven by every step that names it. Two branches
    // that share an action would otherwise fight, because a second coil in the
    // same scan is last-writer-wins.
    std::vector<std::string> action_tags;
    for (const SfcStep& step : chart.steps) {
        for (const SfcAction& action : step.actions) {
            bool known = false;
            for (const std::string& tag : action_tags) {
                known = known || tag == action.tag;
            }
            if (!known) {
                action_tags.push_back(action.tag);
            }
        }
    }
    for (const std::string& tag : action_tags) {
        std::vector<NetPtr> owners;
        for (const SfcStep& step : chart.steps) {
            for (const SfcAction& action : step.actions) {
                if (action.tag == tag) {
                    owners.push_back(contact(step.name));
                    break;
                }
            }
        }
        Rung rung;
        rung.condition = junction(NetKind::Parallel, std::move(owners));
        rung.coils.push_back(Coil{tag, false});
        rungs.push_back(std::move(rung));
    }

    return rungs;
}

SfcResult compile_sfc(std::string_view name, const SfcChart& chart) {
    SfcResult result;
    std::vector<TagDecl> vars;
    Diagnostics diagnostics;
    std::vector<Rung> rungs = lower_sfc(chart, vars, diagnostics);
    if (!diagnostics.empty()) {
        result.diagnostics = std::move(diagnostics);
        return result;
    }

    NetlistResult built = compile_netlist(name, rungs, vars);
    result.ok = built.ok;
    result.program = std::move(built.program);
    result.diagnostics = std::move(built.diagnostics);
    return result;
}

}  // namespace sc::plc
