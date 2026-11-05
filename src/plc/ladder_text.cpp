#include "plc/ladder_text.hpp"

#include "plc/source_text.hpp"

#include <cctype>
#include <limits>
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

    // Ten digits keeps the multiplication below in range; no lesson waits longer.
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

// Recognises `TON(` at the start of a rung body, returning the offset of '('.
// Case-insensitive, because ladder text here is not case-sensitive anywhere else.
bool timer_call(std::string_view body, std::size_t& open) {
    std::size_t pos = 0;
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos])) != 0) {
        ++pos;
    }
    const std::size_t begin = pos;
    while (pos < body.size() && is_ident_char(body[pos])) {
        ++pos;
    }
    if (!text_equals_ci(body.substr(begin, pos - begin), "ton")) {
        return false;
    }
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos])) != 0) {
        ++pos;
    }
    if (pos >= body.size() || body[pos] != '(') {
        return false;
    }
    open = pos;
    return true;
}

// `body` is everything right of '='. The condition may hold parentheses of its
// own, so the argument split happens at the first comma directly inside TON's.
bool parse_timer_arguments(std::string_view body, std::size_t open, std::size_t body_offset,
                           int line, Diagnostics& diagnostics, NetPtr& condition,
                           i64& preset_ms) {
    std::size_t depth = 0;
    std::size_t close = std::string_view::npos;
    std::size_t comma = std::string_view::npos;
    for (std::size_t i = open; i < body.size(); ++i) {
        if (body[i] == '(') {
            ++depth;
        } else if (body[i] == ')') {
            --depth;
            if (depth == 0) {
                close = i;
                break;
            }
        } else if (body[i] == ',' && depth == 1 && comma == std::string_view::npos) {
            comma = i;
        }
    }

    const auto column_of = [&](std::size_t offset) {
        return static_cast<int>(body_offset + offset + 1);
    };

    if (close == std::string_view::npos) {
        diagnostics.push_back({line, column_of(open), "missing ')' after the TON arguments"});
        return false;
    }
    if (!trim_text(body.substr(close + 1)).empty()) {
        diagnostics.push_back(
            {line, column_of(close + 1), "a TON call ends the rung; remove what follows ')'"});
        return false;
    }
    if (comma == std::string_view::npos) {
        diagnostics.push_back(
            {line, column_of(open), "TON needs two arguments: TON(condition, 1s)"});
        return false;
    }

    const std::string_view in_text = body.substr(open + 1, comma - open - 1);
    if (trim_text(in_text).empty()) {
        diagnostics.push_back({line, column_of(comma), "TON needs a condition before the comma"});
        return false;
    }

    const std::string_view preset_text = trim_text(body.substr(comma + 1, close - comma - 1));
    if (!parse_duration(preset_text, preset_ms)) {
        diagnostics.push_back({line, column_of(comma + 1),
                               "'" + std::string(preset_text) +
                                   "' is not a duration; write 500ms, 1s or 2m"});
        return false;
    }
    if (preset_ms <= 0) {
        diagnostics.push_back(
            {line, column_of(comma + 1), "a TON preset must be longer than zero"});
        return false;
    }

    // Keep the condition's own leading spaces so a diagnostic points at the tag,
    // not at the comma before it.
    std::size_t lead = 0;
    while (lead < in_text.size() &&
           std::isspace(static_cast<unsigned char>(in_text[lead])) != 0) {
        ++lead;
    }
    ConditionParser parser(in_text.substr(lead), line, body_offset + open + 1 + lead, diagnostics);
    condition = parser.parse();
    return condition != nullptr;
}

}  // namespace

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

        const std::string_view coil = trim_text(line.substr(0, equals));
        if (coil.empty()) {
            result.diagnostics.push_back({line_number, 1, "a rung needs a coil before '='"});
            continue;
        }

        const std::string_view body = line.substr(equals + 1);
        Rung rung;
        std::size_t timer_open = 0;
        if (timer_call(body, timer_open)) {
            TimerSink sink;
            sink.var = std::string(coil);
            if (!parse_timer_arguments(body, timer_open, equals + 1, line_number,
                                       result.diagnostics, rung.condition, sink.preset_ms)) {
                continue;
            }
            rung.timer = std::move(sink);
        } else {
            ConditionParser parser(body, line_number, equals + 1, result.diagnostics);
            NetPtr condition = parser.parse();
            if (!condition) {
                continue;
            }
            rung.condition = std::move(condition);
            rung.coils.push_back(Coil{std::string(coil), false});
        }
        result.source.rungs.push_back(std::move(rung));
    }

    result.ok = result.diagnostics.empty();
    return result;
}

}  // namespace sc::plc
