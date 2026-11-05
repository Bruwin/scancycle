#include "plc/netlist.hpp"

#include <string>
#include <utility>

namespace sc::plc {
namespace {

i32 add_const(CompiledProgram& program, Value value) {
    program.constants.push_back(std::move(value));
    return static_cast<i32>(program.constants.size() - 1);
}

// Runs before any code is emitted, so the emitter can assume every name resolves.
bool validate(const NetNode& node, const CompiledProgram& program, std::string& error) {
    if (node.kind == NetKind::Contact) {
        if (program.find_var(node.var) < 0) {
            error = "unknown tag '" + node.var + "'";
            return false;
        }
        return true;
    }
    for (const NetPtr& operand : node.operands) {
        if (!operand) {
            error = "a junction has an empty operand";
            return false;
        }
        if (!validate(*operand, program, error)) {
            return false;
        }
    }
    return true;
}

void emit(CompiledProgram& program, const NetNode& node) {
    switch (node.kind) {
        case NetKind::Contact:
            program.code.push_back({Op::LoadVar, program.find_var(node.var), 0});
            if (node.negated) {
                program.code.push_back({Op::Unary, static_cast<i32>(UnOp::Not), 0});
            }
            return;

        case NetKind::Series:
            // A junction with no operands is the neutral element: an empty series
            // conducts, an empty parallel does not.
            if (node.operands.empty()) {
                program.code.push_back(
                    {Op::PushConst, add_const(program, Value::boolean(true)), 0});
                return;
            }
            emit(program, *node.operands.front());
            for (std::size_t i = 1; i < node.operands.size(); ++i) {
                emit(program, *node.operands[i]);
                program.code.push_back({Op::Binary, static_cast<i32>(BinOp::And), 0});
            }
            return;

        case NetKind::Parallel:
            if (node.operands.empty()) {
                program.code.push_back(
                    {Op::PushConst, add_const(program, Value::boolean(false)), 0});
                return;
            }
            emit(program, *node.operands.front());
            for (std::size_t i = 1; i < node.operands.size(); ++i) {
                emit(program, *node.operands[i]);
                program.code.push_back({Op::Binary, static_cast<i32>(BinOp::Or), 0});
            }
            return;
    }
}

}  // namespace

NetlistResult compile_netlist(std::string_view name, std::span<const Rung> rungs,
                              std::span<const TagDecl> vars) {
    NetlistResult result;
    result.program.name = std::string(name);

    if (!declare_tags(vars, result.program, result.diagnostics)) {
        return result;
    }

    for (std::size_t i = 0; i < rungs.size(); ++i) {
        const std::string where = "rung " + std::to_string(i + 1) + ": ";
        const Rung& rung = rungs[i];
        if (rung.coils.empty()) {
            result.diagnostics.push_back({0, 0, where + "a rung needs at least one coil"});
            continue;
        }
        if (rung.condition) {
            std::string error;
            if (!validate(*rung.condition, result.program, error)) {
                result.diagnostics.push_back({0, 0, where + error});
            }
        }
        for (const Coil& coil : rung.coils) {
            if (result.program.find_var(coil.var) < 0) {
                result.diagnostics.push_back(
                    {0, 0, where + "coil '" + coil.var + "' is not a declared variable"});
            }
        }
    }

    if (!result.diagnostics.empty()) {
        return result;
    }

    for (const Rung& rung : rungs) {
        for (const Coil& coil : rung.coils) {
            if (rung.condition) {
                emit(result.program, *rung.condition);
            } else {
                result.program.code.push_back(
                    {Op::PushConst, add_const(result.program, Value::boolean(true)), 0});
            }
            if (coil.negated) {
                result.program.code.push_back({Op::Unary, static_cast<i32>(UnOp::Not), 0});
            }
            result.program.code.push_back({Op::StoreVar, result.program.find_var(coil.var), 0});
        }
    }
    result.program.code.push_back({Op::Halt, 0, 0});
    result.ok = true;
    return result;
}

}  // namespace sc::plc
