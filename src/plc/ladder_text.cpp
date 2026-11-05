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

        ConditionParser parser(line.substr(equals + 1), line_number, equals + 1,
                               result.diagnostics);
        NetPtr condition = parser.parse();
        if (!condition) {
            continue;
        }

        Rung rung;
        rung.condition = std::move(condition);
        rung.coils.push_back(Coil{std::string(coil), false});
        result.source.rungs.push_back(std::move(rung));
    }

    result.ok = result.diagnostics.empty();
    return result;
}

}  // namespace sc::plc
