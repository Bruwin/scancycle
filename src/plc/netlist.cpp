#include "plc/netlist.hpp"

#include "plc/function_blocks.hpp"

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

void emit_condition(CompiledProgram& program, const NetPtr& condition) {
    if (condition) {
        emit(program, *condition);
    } else {
        program.code.push_back({Op::PushConst, add_const(program, Value::boolean(true)), 0});
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
        if (rung.timer && !rung.coils.empty()) {
            result.diagnostics.push_back(
                {0, 0, where + "a rung ends in one sink: coils or a timer, not both"});
            continue;
        }
        if (!rung.timer && rung.coils.empty()) {
            result.diagnostics.push_back({0, 0, where + "a rung needs a coil or a timer"});
            continue;
        }
        if (rung.condition) {
            std::string error;
            if (!validate(*rung.condition, result.program, error)) {
                result.diagnostics.push_back({0, 0, where + error});
            }
        }
        if (rung.timer) {
            const i32 slot = result.program.find_var(rung.timer->var);
            if (slot < 0) {
                result.diagnostics.push_back(
                    {0, 0,
                     where + "timer output '" + rung.timer->var + "' is not a declared variable"});
            } else if (result.program.vars[static_cast<std::size_t>(slot)].type !=
                       ValueType::Bool) {
                result.diagnostics.push_back(
                    {0, 0, where + "timer output '" + rung.timer->var + "' must be BOOL"});
            }
            if (rung.timer->preset_ms <= 0) {
                result.diagnostics.push_back(
                    {0, 0, where + "a timer preset must be longer than zero"});
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

    // Every timer rung owns one TON instance. The instance name starts with '#' so
    // it can never collide with a tag: no source language here spells that.
    i32 timers = 0;
    for (const Rung& rung : rungs) {
        if (rung.timer) {
            const FbDecl* ton = find_builtin_fb("ton");
            if (ton == nullptr) {
                result.diagnostics.push_back({0, 0, "the TON block is not available"});
                return result;
            }
            const i32 fb_index = static_cast<i32>(result.program.fbs.size());
            result.program.fbs.push_back(*ton);

            VarInfo instance;
            instance.name = "#ton" + std::to_string(++timers);
            instance.fb_index = fb_index;
            result.program.vars.push_back(std::move(instance));

            emit_condition(result.program, rung.condition);
            result.program.code.push_back({Op::StoreFbField, fb_index, 0});
            result.program.code.push_back(
                {Op::PushConst, add_const(result.program, Value::time(rung.timer->preset_ms)),
                 0});
            result.program.code.push_back({Op::StoreFbField, fb_index, 1});
            result.program.code.push_back({Op::CallFb, fb_index, 0});
            result.program.code.push_back({Op::LoadFbField, fb_index, 2});
            result.program.code.push_back(
                {Op::StoreVar, result.program.find_var(rung.timer->var), 0});
            continue;
        }
        for (const Coil& coil : rung.coils) {
            emit_condition(result.program, rung.condition);
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
