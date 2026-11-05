#include "plc/il.hpp"

#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sc::plc {
namespace {

std::string_view trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        --end;
    }
    return text.substr(begin, end - begin);
}

bool equals_ci(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        const auto left = static_cast<unsigned char>(a[i]);
        const auto right = static_cast<unsigned char>(b[i]);
        if (std::tolower(left) != std::tolower(right)) {
            return false;
        }
    }
    return true;
}

// Blanks comments in place without changing the length or the newlines, so every
// diagnostic later still quotes the line the author actually wrote.
bool strip_comments(std::string_view source, std::string& out) {
    out.assign(source);
    bool in_block = false;
    for (std::size_t i = 0; i < out.size(); ++i) {
        const char c = out[i];
        if (in_block) {
            if (c == '*' && i + 1 < out.size() && out[i + 1] == ')') {
                out[i] = ' ';
                out[i + 1] = ' ';
                ++i;
                in_block = false;
            } else if (c != '\n') {
                out[i] = ' ';
            }
            continue;
        }
        if (c == '(' && i + 1 < out.size() && out[i + 1] == '*') {
            out[i] = ' ';
            out[i + 1] = ' ';
            ++i;
            in_block = true;
            continue;
        }
        if (c == '/' && i + 1 < out.size() && out[i + 1] == '/') {
            while (i < out.size() && out[i] != '\n') {
                out[i] = ' ';
                ++i;
            }
        }
    }
    return !in_block;
}

struct Header {
    bool present = false;
    std::size_t begin = 0;  // first byte after END_VAR
    int next_line = 1;      // line number of the first instruction
    std::vector<TagDecl> vars;
};

bool split_declaration(std::string_view line, std::string& name, std::string& type) {
    const std::size_t colon = line.find(':');
    if (colon == std::string_view::npos) {
        return false;
    }
    name = std::string(trim(line.substr(0, colon)));
    std::string_view tail = trim(line.substr(colon + 1));
    if (!tail.empty() && tail.back() == ';') {
        tail.remove_suffix(1);
    }
    type = std::string(trim(tail));
    return !name.empty() && !type.empty();
}

// Reads an optional VAR block. A file without one is legal: the caller may own
// the tag list instead, which is what an editor with a tag database does.
bool parse_header(const std::string& text, Header& header, Diagnostics& diagnostics) {
    std::size_t pos = 0;
    int line_number = 0;
    bool inside = false;

    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos) {
            end = text.size();
        }
        ++line_number;
        const std::string_view line = trim(std::string_view(text).substr(pos, end - pos));
        pos = end + 1;

        if (line.empty()) {
            continue;
        }
        if (!inside) {
            if (!equals_ci(line, "var")) {
                return true;
            }
            inside = true;
            header.present = true;
            continue;
        }
        if (equals_ci(line, "end_var")) {
            header.begin = pos;
            header.next_line = line_number + 1;
            return true;
        }

        std::string name;
        std::string type;
        if (!split_declaration(line, name, type)) {
            diagnostics.push_back({line_number, 1, "expected 'name : TYPE;' in the VAR block"});
            continue;
        }
        // IEC keywords are case-insensitive, and parse_value_type speaks lower
        // case, so a file that writes BOOL must not be told BOOL is unknown.
        std::string lowered = type;
        for (char& c : lowered) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        ValueType value_type = ValueType::Bool;
        if (!parse_value_type(lowered, value_type)) {
            diagnostics.push_back({line_number, 1, "unknown type '" + type + "'"});
            continue;
        }
        TagDecl declaration;
        declaration.name = std::move(name);
        declaration.type = value_type;
        header.vars.push_back(std::move(declaration));
    }

    if (inside) {
        diagnostics.push_back({line_number, 1, "the VAR block has no END_VAR"});
        return false;
    }
    return true;
}

}  // namespace

IlResult compile_il(std::string_view name, std::string_view source,
                    std::span<const TagDecl> vars) {
    IlResult result;
    result.program.name = std::string(name);

    std::string text;
    if (!strip_comments(source, text)) {
        result.diagnostics.push_back({0, 0, "unterminated (* comment"});
        return result;
    }

    Header header;
    if (!parse_header(text, header, result.diagnostics) || !result.diagnostics.empty()) {
        return result;
    }

    // One file, one source of truth. A file that declares its own tags cannot also
    // be handed a list, because the two would drift and the trace would quietly
    // show whichever one happened to win.
    if (header.present && !vars.empty()) {
        result.diagnostics.push_back(
            {0, 0, "the VAR block and the tag list both declare this program's tags"});
        return result;
    }

    const std::span<const TagDecl> declared =
        header.present ? std::span<const TagDecl>(header.vars) : vars;
    if (!declare_tags(declared, result.program, result.diagnostics)) {
        return result;
    }

    const std::string_view text_view = text;
    int line_number = header.next_line - 1;
    int depth = 0;  // values sitting on the accumulator
    std::size_t pos = header.begin;

    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos) {
            end = text.size();
        }
        ++line_number;
        const std::string_view line = trim(text_view.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty()) {
            continue;
        }

        const std::size_t space = line.find_first_of(" \t");
        const std::string_view op = line.substr(0, space);
        const std::string_view operand =
            space == std::string_view::npos ? std::string_view() : trim(line.substr(space));

        const bool load = equals_ci(op, "ld") || equals_ci(op, "ldn");
        const bool combine = equals_ci(op, "and") || equals_ci(op, "andn") ||
                             equals_ci(op, "or") || equals_ci(op, "orn");
        const bool store = equals_ci(op, "st") || equals_ci(op, "stn");

        if (!load && !combine && !store) {
            result.diagnostics.push_back(
                {line_number, 1, "unknown instruction '" + std::string(op) + "'"});
            continue;
        }
        if (operand.empty()) {
            result.diagnostics.push_back({line_number, 1, std::string(op) + " needs a tag"});
            continue;
        }
        if (combine && depth == 0) {
            result.diagnostics.push_back(
                {line_number, 1, std::string(op) + " has nothing to combine with"});
            continue;
        }
        if (store && depth == 0) {
            result.diagnostics.push_back(
                {line_number, 1, std::string(op) + " has nothing to store"});
            continue;
        }

        const i32 slot = result.program.find_var(operand);
        if (slot < 0) {
            result.diagnostics.push_back(
                {line_number, 1, "unknown tag '" + std::string(operand) + "'"});
            continue;
        }

        // LD/LDN, AND/ANDN, OR/ORN and ST/STN differ only by a trailing n, so one
        // check covers all four pairs.
        const bool inverted = op.size() > 2 && (op.back() == 'n' || op.back() == 'N');

        // A store must not load its destination first: that would replace the
        // accumulator with the tag's old value and write it straight back, which
        // looks like working logic that never changes anything.
        if (store) {
            if (inverted) {
                result.program.code.push_back({Op::Unary, static_cast<i32>(UnOp::Not), 0});
            }
            result.program.code.push_back({Op::StoreVar, slot, 0});
            depth = 0;
            continue;
        }

        result.program.code.push_back({Op::LoadVar, slot, 0});
        if (inverted) {
            result.program.code.push_back({Op::Unary, static_cast<i32>(UnOp::Not), 0});
        }
        if (combine) {
            const bool is_or = op.front() == 'o' || op.front() == 'O';
            const BinOp bin = is_or ? BinOp::Or : BinOp::And;
            result.program.code.push_back({Op::Binary, static_cast<i32>(bin), 0});
        }
        depth = 1;
    }

    result.program.code.push_back({Op::Halt, 0, 0});
    result.ok = result.diagnostics.empty();
    return result;
}

}  // namespace sc::plc
