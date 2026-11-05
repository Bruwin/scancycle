#include "plc/il.hpp"

#include "plc/source_text.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace sc::plc {

IlResult compile_il(std::string_view name, std::string_view source,
                    std::span<const TagDecl> vars) {
    IlResult result;
    result.program.name = std::string(name);

    std::string text;
    if (!strip_comments(source, text)) {
        result.diagnostics.push_back({0, 0, "unterminated (* comment"});
        return result;
    }

    VarBlock block;
    if (!parse_var_block(text, block, result.diagnostics) || !result.diagnostics.empty()) {
        return result;
    }

    // One file, one source of truth. A file that declares its own tags cannot also
    // be handed a list, because the two would drift and the trace would quietly
    // show whichever one happened to win.
    if (block.present && !vars.empty()) {
        result.diagnostics.push_back(
            {0, 0, "the VAR block and the tag list both declare this program's tags"});
        return result;
    }

    const std::span<const TagDecl> declared =
        block.present ? std::span<const TagDecl>(block.vars) : vars;
    if (!declare_tags(declared, result.program, result.diagnostics)) {
        return result;
    }

    const std::string_view text_view = text;
    int line_number = block.next_line - 1;
    int depth = 0;  // values sitting on the accumulator
    std::size_t pos = block.begin;

    while (pos < text.size()) {
        std::size_t end = text.find('\n', pos);
        if (end == std::string::npos) {
            end = text.size();
        }
        ++line_number;
        const std::string_view line = trim_text(text_view.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty()) {
            continue;
        }

        const std::size_t space = line.find_first_of(" \t");
        const std::string_view op = line.substr(0, space);
        const std::string_view operand =
            space == std::string_view::npos ? std::string_view() : trim_text(line.substr(space));

        const bool load = text_equals_ci(op, "ld") || text_equals_ci(op, "ldn");
        const bool combine = text_equals_ci(op, "and") || text_equals_ci(op, "andn") ||
                             text_equals_ci(op, "or") || text_equals_ci(op, "orn");
        const bool store = text_equals_ci(op, "st") || text_equals_ci(op, "stn");

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
