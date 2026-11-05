#include "plc/il.hpp"

#include <cctype>
#include <string>
#include <string_view>

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

}  // namespace

IlResult compile_il(std::string_view name, std::string_view source,
                    std::span<const TagDecl> vars) {
    IlResult result;
    result.program.name = std::string(name);
    if (!declare_tags(vars, result.program, result.diagnostics)) {
        return result;
    }

    int line_number = 0;
    int depth = 0;  // values sitting on the accumulator
    std::size_t pos = 0;

    while (pos < source.size()) {
        std::size_t end = source.find('\n', pos);
        if (end == std::string_view::npos) {
            end = source.size();
        }
        ++line_number;

        std::string_view line = source.substr(pos, end - pos);
        pos = end + 1;

        const std::size_t comment = line.find("//");
        if (comment != std::string_view::npos) {
            line = line.substr(0, comment);
        }
        line = trim(line);
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
