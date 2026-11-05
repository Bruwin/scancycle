#include "plc/ladder_text.hpp"

#include "plc/source_text.hpp"

#include <cctype>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sc::plc {
namespace {

bool is_ident_start(char c) {
    const auto value = static_cast<unsigned char>(c);
    return std::isalpha(value) != 0 || c == '_';
}

bool is_ident_char(char c) {
    const auto value = static_cast<unsigned char>(c);
    return std::isalnum(value) != 0 || c == '_';
}

NetPtr make_contact(std::string name, bool negated) {
    auto node = std::make_unique<NetNode>();
    node->kind = NetKind::Contact;
    node->var = std::move(name);
    node->negated = negated;
    return node;
}

NetPtr make_junction(NetKind kind, std::vector<NetPtr> operands) {
    auto node = std::make_unique<NetNode>();
    node->kind = kind;
    node->operands = std::move(operands);
    return node;
}

NetPtr make_rail() {
    auto node = std::make_unique<NetNode>();
    node->kind = NetKind::Always;
    return node;
}

// Recursive descent over one rung's condition. It reports at most one diagnostic,
// on the first problem: cascading errors from one typo help nobody.
class ConditionParser {
public:
    ConditionParser(std::string_view text, int line, std::size_t column_base,
                    Diagnostics& diagnostics)
        : text_(text), line_(line), column_base_(column_base), diagnostics_(diagnostics) {}

    NetPtr parse() {
        NetPtr node = parse_or();
        if (!node) {
            return nullptr;
        }
        skip_spaces();
        if (pos_ < text_.size()) {
            fail("unexpected '" + std::string(1, text_[pos_]) + "'");
            return nullptr;
        }
        return node;
    }

private:
    void skip_spaces() {
        while (pos_ < text_.size() && std::isspace(static_cast<unsigned char>(text_[pos_])) != 0) {
            ++pos_;
        }
    }

    void fail(std::string message) {
        diagnostics_.push_back(
            {line_, static_cast<int>(column_base_ + pos_ + 1), std::move(message)});
    }

    bool consume(char wanted) {
        skip_spaces();
        if (pos_ < text_.size() && text_[pos_] == wanted) {
            ++pos_;
            return true;
        }
        return false;
    }

    std::string read_identifier() {
        skip_spaces();
        if (pos_ >= text_.size() || !is_ident_start(text_[pos_])) {
            return std::string();
        }
        const std::size_t begin = pos_;
        while (pos_ < text_.size() && is_ident_char(text_[pos_])) {
            ++pos_;
        }
        return std::string(text_.substr(begin, pos_ - begin));
    }

    NetPtr parse_or() {
        NetPtr first = parse_and();
        if (!first) {
            return nullptr;
        }
        std::vector<NetPtr> operands;
        operands.push_back(std::move(first));
        while (consume('|')) {
            NetPtr next = parse_and();
            if (!next) {
                return nullptr;
            }
            operands.push_back(std::move(next));
        }
        if (operands.size() == 1) {
            return std::move(operands.front());
        }
        return make_junction(NetKind::Parallel, std::move(operands));
    }

    NetPtr parse_and() {
        NetPtr first = parse_factor();
        if (!first) {
            return nullptr;
        }
        std::vector<NetPtr> operands;
        operands.push_back(std::move(first));
        while (consume('&')) {
            NetPtr next = parse_factor();
            if (!next) {
                return nullptr;
            }
            operands.push_back(std::move(next));
        }
        if (operands.size() == 1) {
            return std::move(operands.front());
        }
        return make_junction(NetKind::Series, std::move(operands));
    }

    NetPtr parse_factor() {
        skip_spaces();
        if (pos_ >= text_.size()) {
            fail("expected a tag or '('");
            return nullptr;
        }
        if (text_[pos_] == '!') {
            ++pos_;
            skip_spaces();
            if (pos_ < text_.size() && text_[pos_] == '(') {
                fail("'!' applies to a contact, not a group");
                return nullptr;
            }
            const std::string name = read_identifier();
            if (name.empty()) {
                fail("'!' needs a contact after it");
                return nullptr;
            }
            if (text_equals_ci(name, "true") || text_equals_ci(name, "false")) {
                fail("'!' applies to a contact, not a constant");
                return nullptr;
            }
            return make_contact(name, true);
        }
        if (text_[pos_] == '(') {
            ++pos_;
            NetPtr inner = parse_or();
            if (!inner) {
                return nullptr;
            }
            if (!consume(')')) {
                fail("missing ')'");
                return nullptr;
            }
            return inner;
        }
        const std::string name = read_identifier();
        if (name.empty()) {
            fail(std::string("unexpected '") + text_[pos_] + "'");
            return nullptr;
        }
        if (text_equals_ci(name, "true")) {
            return make_rail();
        }
        if (text_equals_ci(name, "false")) {
            return make_junction(NetKind::Parallel, {});
        }
        return make_contact(name, false);
    }

    std::string_view text_;
    std::size_t pos_ = 0;
    int line_ = 0;
    std::size_t column_base_ = 0;
    Diagnostics& diagnostics_;
};

// `500ms`, `1s`, `2m`, and the same with spaces before the unit. Only whole
// numbers: a preset is a threshold on a millisecond clock, not a measurement.
bool parse_duration(std::string_view text, i64& out) {
    const std::string_view trimmed = trim_text(text);
    std::size_t pos = 0;
    while (pos < trimmed.size() && std::isdigit(static_cast<unsigned char>(trimmed[pos])) != 0) {
        ++pos;
    }
    if (pos == 0) {
        return false;
    }

    // Nine digits keeps the multiplication below in range; no lesson waits longer.
    if (pos > 9) {
        return false;
    }
    i64 amount = 0;
    for (std::size_t i = 0; i < pos; ++i) {
        amount = amount * 10 + (trimmed[i] - '0');
    }

    std::size_t unit_begin = pos;
    while (unit_begin < trimmed.size() &&
           std::isspace(static_cast<unsigned char>(trimmed[unit_begin])) != 0) {
        ++unit_begin;
    }
    const std::string_view unit = trimmed.substr(unit_begin);

    i64 scale = 0;
    if (text_equals_ci(unit, "ms")) {
        scale = 1;
    } else if (text_equals_ci(unit, "s")) {
        scale = 1000;
    } else if (text_equals_ci(unit, "m")) {
        scale = 60000;
    } else {
        return false;
    }
    out = amount * scale;
    return true;
}

// A counter preset is a count, so it is a bare whole number.
bool parse_count(std::string_view text, i64& out) {
    const std::string_view trimmed = trim_text(text);
    if (trimmed.empty() || trimmed.size() > 9) {
        return false;
    }
    i64 value = 0;
    for (const char c : trimmed) {
        if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
            return false;
        }
        value = value * 10 + (c - '0');
    }
    out = value;
    return true;
}

struct SinkCall {
    std::string_view type;  // points at a literal: "ton", "tof", "ctu", "ctd"
    std::size_t open = 0;   // offset of '('
};

// Recognises a block name and its opening parenthesis at the start of a rung
// body. Case-insensitive, because ladder text here is not case-sensitive anywhere
// else. A name that is not one of the four sinks is an ordinary contact.
bool block_call(std::string_view body, SinkCall& out) {
    std::size_t pos = 0;
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos])) != 0) {
        ++pos;
    }
    const std::size_t begin = pos;
    while (pos < body.size() && is_ident_char(body[pos])) {
        ++pos;
    }
    const std::string_view word = body.substr(begin, pos - begin);
    std::string_view type;
    for (const char* candidate : {"ton", "tof", "ctu", "ctd"}) {
        if (text_equals_ci(word, candidate)) {
            type = candidate;
        }
    }
    if (type.empty()) {
        return false;
    }
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos])) != 0) {
        ++pos;
    }
    if (pos >= body.size() || body[pos] != '(') {
        return false;
    }
    out.type = type;
    out.open = pos;
    return true;
}

enum class SinkParse { NotACall, Parsed, Failed };

struct ParsedSink {
    std::string_view type;
    NetPtr condition;
    NetPtr second;  // counter reset/load, may stay null
    i64 preset = 0;
    bool counter = false;
};

// Splits the call's arguments at top-level commas, so a network may bring its own
// parentheses. The preset is always the last argument.
SinkParse parse_sink(std::string_view body, std::size_t body_offset, int line,
                     Diagnostics& diagnostics, ParsedSink& out) {
    SinkCall call;
    if (!block_call(body, call)) {
        return SinkParse::NotACall;
    }

    const auto column_of = [&](std::size_t offset) {
        return static_cast<int>(body_offset + offset + 1);
    };

    std::size_t depth = 0;
    std::size_t close = std::string_view::npos;
    std::vector<std::size_t> commas;
    for (std::size_t i = call.open; i < body.size(); ++i) {
        const char c = body[i];
        if (c == '(') {
            ++depth;
        } else if (c == ')') {
            --depth;
            if (depth == 0) {
                close = i;
                break;
            }
        } else if (c == ',' && depth == 1) {
            commas.push_back(i);
        }
    }

    if (close == std::string_view::npos) {
        diagnostics.push_back(
            {line, column_of(call.open), "missing ')' after the block arguments"});
        return SinkParse::Failed;
    }
    if (!trim_text(body.substr(close + 1)).empty()) {
        diagnostics.push_back(
            {line, column_of(close + 1), "a block call ends the rung; remove what follows ')'"});
        return SinkParse::Failed;
    }

    const bool counter = call.type == "ctu" || call.type == "ctd";
    if (counter && (commas.size() < 1 || commas.size() > 2)) {
        diagnostics.push_back(
            {line, column_of(call.open),
             std::string(call.type) + " needs two or three arguments: " +
                 std::string(call.type) + "(pulse, reset, 5)"});
        return SinkParse::Failed;
    }
    if (!counter && commas.size() != 1) {
        diagnostics.push_back(
            {line, column_of(call.open),
             std::string(call.type) + " needs two arguments: " + std::string(call.type) +
                 "(condition, 1s)"});
        return SinkParse::Failed;
    }

    const auto segment = [&](std::size_t index) -> std::string_view {
        const std::size_t begin = index == 0 ? call.open + 1 : commas[index - 1] + 1;
        const std::size_t end = index < commas.size() ? commas[index] : close;
        return body.substr(begin, end - begin);
    };

    const auto parse_network = [&](std::string_view text, std::size_t offset,
                                   const char* what, NetPtr& target) {
        if (trim_text(text).empty()) {
            diagnostics.push_back(
                {line, column_of(offset), std::string(what) + " needs a condition"});
            return false;
        }
        // Keep leading spaces out of the parse but above the column base, so a
        // diagnostic points at the tag rather than at the comma before it.
        std::size_t lead = 0;
        while (lead < text.size() &&
               std::isspace(static_cast<unsigned char>(text[lead])) != 0) {
            ++lead;
        }
        ConditionParser parser(text.substr(lead), line, body_offset + offset + lead,
                               diagnostics);
        target = parser.parse();
        return target != nullptr;
    };

    if (!parse_network(segment(0), call.open + 1, counter ? "the pulse input" : "the condition",
                       out.condition)) {
        return SinkParse::Failed;
    }
    if (counter && commas.size() == 2) {
        if (!parse_network(segment(1), commas[0] + 1, "the reset input", out.second)) {
            return SinkParse::Failed;
        }
    }

    const std::size_t preset_index = commas.size();
    const std::size_t preset_offset = commas[preset_index - 1] + 1;
    const std::string_view preset_text = segment(preset_index);
    if (counter) {
        if (!parse_count(preset_text, out.preset)) {
            diagnostics.push_back({line, column_of(preset_offset),
                                   "'" + std::string(trim_text(preset_text)) +
                                       "' is not a count; write a whole number, e.g. 5"});
            return SinkParse::Failed;
        }
    } else if (!parse_duration(preset_text, out.preset)) {
        diagnostics.push_back({line, column_of(preset_offset),
                               "'" + std::string(trim_text(preset_text)) +
                                   "' is not a duration; write 500ms, 1s or 2m"});
        return SinkParse::Failed;
    }
    if (out.preset <= 0) {
        diagnostics.push_back({line, column_of(preset_offset),
                               "a " + std::string(call.type) +
                                   " preset must be longer than zero"});
        return SinkParse::Failed;
    }

    out.type = call.type;
    out.counter = counter;
    return SinkParse::Parsed;
}

// The tags left of '='. Two are allowed, because a counter has two outputs: Q
// and CV. Splitting them here keeps the sink parser unaware of the left side.
bool split_tags(std::string_view text, std::vector<std::string>& out,
                std::vector<bool>& negated, int line, Diagnostics& diagnostics) {
    if (text.empty()) {
        diagnostics.push_back({line, 1, "a rung needs a coil before '='"});
        return false;
    }

    std::size_t begin = 0;
    while (true) {
        const std::size_t comma = text.find(',', begin);
        const std::size_t end = comma == std::string_view::npos ? text.size() : comma;
        const std::string_view piece = trim_text(text.substr(begin, end - begin));
        const auto column = static_cast<int>(begin) + 1;

        if (piece.empty()) {
            diagnostics.push_back({line, column, "a rung needs a tag before each comma"});
            return false;
        }
        std::string_view name = piece;
        bool inverted = false;
        if (!name.empty() && name.front() == '!') {
            inverted = true;
            name.remove_prefix(1);
            if (name.empty()) {
                diagnostics.push_back({line, column, "'!' needs a tag name"});
                return false;
            }
        }
        bool identifier = is_ident_start(name.front());
        for (const char c : name) {
            if (!is_ident_char(c)) {
                identifier = false;
            }
        }
        if (!identifier) {
            diagnostics.push_back(
                {line, column, "'" + std::string(piece) + "' is not a tag name"});
            return false;
        }

        out.emplace_back(name);
        negated.push_back(inverted);
        if (comma == std::string_view::npos) {
            break;
        }
        begin = comma + 1;
    }

    if (out.size() > 2) {
        diagnostics.push_back({line, 1, "a rung writes at most two tags"});
        return false;
    }
    if (out.size() == 2 && out[0] == out[1]) {
        diagnostics.push_back({line, 1, "a rung cannot write the same tag twice"});
        return false;
    }
    return true;
}

}  // namespace

namespace {

// A junction nested in the same kind is flattened: the file has no way to say
// "this AND is a child AND", and it would read as noise if it did. A parallel
// branch under a series must keep its parentheses because & binds tighter.
std::string format_condition(const NetNode& node, NetKind parent);

std::string format_operands(const NetNode& node, NetKind kind, const char* separator) {
    std::string text;
    for (std::size_t i = 0; i < node.operands.size(); ++i) {
        if (i != 0) {
            text += separator;
        }
        text += format_condition(*node.operands[i], kind);
    }
    return text;
}

std::string format_condition(const NetNode& node, NetKind parent) {
    switch (node.kind) {
        case NetKind::Contact:
            return (node.negated ? "!" : "") + node.var;
        case NetKind::Always:
            return "TRUE";
        case NetKind::Series: {
            if (node.operands.empty()) {
                return "TRUE";
            }
            std::string text = format_operands(node, NetKind::Series, " & ");
            if (parent == NetKind::Parallel) {
                return "(" + text + ")";
            }
            return text;
        }
        case NetKind::Parallel: {
            if (node.operands.empty()) {
                return "FALSE";
            }
            std::string text = format_operands(node, NetKind::Parallel, " | ");
            if (parent == NetKind::Series) {
                return "(" + text + ")";
            }
            return text;
        }
    }
    return "FALSE";
}

std::string format_root_condition(const NetPtr& condition) {
    if (!condition) {
        return "TRUE";
    }
    return format_condition(*condition, condition->kind);
}

std::string format_duration(i64 milliseconds) {
    if (milliseconds > 0 && milliseconds % 60'000 == 0) {
        return std::to_string(milliseconds / 60'000) + "m";
    }
    if (milliseconds > 0 && milliseconds % 1'000 == 0) {
        return std::to_string(milliseconds / 1'000) + "s";
    }
    return std::to_string(milliseconds) + "ms";
}

std::string format_sink(const Rung& rung) {
    const std::string condition = format_root_condition(rung.condition);
    if (rung.timer) {
        return rung.timer->var + " = " + (rung.timer->off_delay ? "TOF(" : "TON(") +
               condition + ", " + format_duration(rung.timer->preset_ms) + ")\n";
    }
    if (rung.counter) {
        std::string text = rung.counter->var;
        if (!rung.counter->value_var.empty()) {
            text += ", " + rung.counter->value_var;
        }
        text += rung.counter->down ? " = CTD(" : " = CTU(";
        text += condition;
        if (rung.counter->reset) {
            text += ", " + format_root_condition(rung.counter->reset);
        }
        text += ", " + std::to_string(rung.counter->preset) + ")\n";
        return text;
    }

    std::string text;
    for (const Coil& coil : rung.coils) {
        text += (coil.negated ? "!" : "");
        text += coil.var + " = " + condition + "\n";
    }
    return text;
}

}  // namespace

std::string format_ladder_source(const LadderSource& source) {
    std::string text;
    if (!source.vars.empty()) {
        text += "VAR\n";
        for (const TagDecl& var : source.vars) {
            text += "    " + var.name + " : " + value_type_name(var.type) + ";\n";
        }
        text += "END_VAR\n\n";
    }
    for (const Rung& rung : source.rungs) {
        text += format_sink(rung);
    }
    return text;
}

LadderParseResult parse_ladder_source(std::string_view source) {
    LadderParseResult result;

    std::string text;
    if (!strip_comments(source, text)) {
        result.diagnostics.push_back({0, 0, "unterminated (* comment"});
        return result;
    }

    VarBlock block;
    if (!parse_var_block(text, block, result.diagnostics) || !result.diagnostics.empty()) {
        return result;
    }
    result.source.vars = std::move(block.vars);

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

        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            result.diagnostics.push_back(
                {line_number, 1, "a rung is 'coil = condition', and this line has no '='"});
            continue;
        }
        if (line.find('=', equals + 1) != std::string_view::npos) {
            result.diagnostics.push_back(
                {line_number, static_cast<int>(equals) + 2, "a rung has one '='"});
            continue;
        }

        std::vector<std::string> tags;
        std::vector<bool> negated;
        if (!split_tags(trim_text(line.substr(0, equals)), tags, negated, line_number,
                        result.diagnostics)) {
            continue;
        }

        const std::string_view body = line.substr(equals + 1);
        Rung rung;
        ParsedSink sink;
        const SinkParse parsed =
            parse_sink(body, equals + 1, line_number, result.diagnostics, sink);
        if (parsed == SinkParse::Failed) {
            continue;
        }
        if (parsed == SinkParse::Parsed) {
            if (negated[0] || (negated.size() > 1 && negated[1])) {
                result.diagnostics.push_back(
                    {line_number, 1, "a timer or counter output cannot be negated"});
                continue;
            }
            rung.condition = std::move(sink.condition);
            if (sink.counter) {
                CounterSink counter;
                counter.var = tags[0];
                counter.preset = sink.preset;
                counter.down = sink.type == "ctd";
                counter.reset = std::move(sink.second);
                if (tags.size() == 2) {
                    counter.value_var = tags[1];
                }
                rung.counter = std::move(counter);
            } else {
                if (tags.size() == 2) {
                    result.diagnostics.push_back(
                        {line_number, 1, "only a counter publishes a second output"});
                    continue;
                }
                TimerSink timer;
                timer.var = tags[0];
                timer.preset_ms = sink.preset;
                timer.off_delay = sink.type == "tof";
                rung.timer = std::move(timer);
            }
        } else {
            if (tags.size() == 2) {
                result.diagnostics.push_back(
                    {line_number, 1, "only a counter publishes a second output"});
                continue;
            }
            ConditionParser parser(body, line_number, equals + 1, result.diagnostics);
            NetPtr condition = parser.parse();
            if (!condition) {
                continue;
            }
            rung.condition = std::move(condition);
            rung.coils.push_back(Coil{tags[0], negated[0]});
        }
        result.source.rungs.push_back(std::move(rung));
    }

    result.ok = result.diagnostics.empty();
    return result;
}

}  // namespace sc::plc
