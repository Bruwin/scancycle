#include "plc/source_text.hpp"

#include <cctype>
#include <string>
#include <string_view>
#include <utility>

namespace sc::plc {
namespace {

bool split_declaration(std::string_view line, std::string& name, std::string& type) {
    const std::size_t colon = line.find(':');
    if (colon == std::string_view::npos) {
        return false;
    }
    name = std::string(trim_text(line.substr(0, colon)));
    std::string_view tail = trim_text(line.substr(colon + 1));
    if (!tail.empty() && tail.back() == ';') {
        tail.remove_suffix(1);
    }
    type = std::string(trim_text(tail));
    return !name.empty() && !type.empty();
}

}  // namespace

std::string_view trim_text(std::string_view text) {
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

bool text_equals_ci(std::string_view a, std::string_view b) {
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

bool parse_var_block(const std::string& text, VarBlock& block, Diagnostics& diagnostics) {
    std::size_t pos = 0;
    int line_number = 0;
    bool inside = false;

    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos) {
            end = text.size();
        }
        ++line_number;
        const std::string_view line = trim_text(std::string_view(text).substr(pos, end - pos));
        pos = end + 1;

        if (line.empty()) {
            continue;
        }
        if (!inside) {
            if (!text_equals_ci(line, "var")) {
                return true;
            }
            inside = true;
            block.present = true;
            continue;
        }
        if (text_equals_ci(line, "end_var")) {
            block.begin = pos;
            block.next_line = line_number + 1;
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
        block.vars.push_back(std::move(declaration));
    }

    if (inside) {
        diagnostics.push_back({line_number, 1, "the VAR block has no END_VAR"});
        return false;
    }
    return true;
}

}  // namespace sc::plc
