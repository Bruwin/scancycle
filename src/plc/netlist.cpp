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

// An unwired operand still has to put something on the stack: a rail contact is
// TRUE, an unused block input is FALSE.
void emit_operand(CompiledProgram& program, const NetPtr& node, bool when_absent) {
    if (node) {
        emit(program, *node);
    } else {
        program.code.push_back(
            {Op::PushConst, add_const(program, Value::boolean(when_absent)), 0});
    }
}

const char* sink_type(const Rung& rung) {
    if (rung.timer) {
        return rung.timer->off_delay ? "tof" : "ton";
    }
    return rung.counter->down ? "ctd" : "ctu";
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
        const int sinks = (rung.timer ? 1 : 0) + (rung.counter ? 1 : 0) +
                          (rung.coils.empty() ? 0 : 1);
        if (sinks > 1) {
            result.diagnostics.push_back(
                {0, 0, where + "a rung has one sink: coils, a timer or a counter"});
            continue;
        }
        if (sinks == 0) {
            result.diagnostics.push_back(
                {0, 0, where + "a rung needs a coil, a timer or a counter"});
            continue;
        }
        if (rung.condition) {
            std::string error;
            if (!validate(*rung.condition, result.program, error)) {
                result.diagnostics.push_back({0, 0, where + error});
            }
        }
        if (rung.counter && rung.counter->reset) {
            std::string error;
            if (!validate(*rung.counter->reset, result.program, error)) {
                result.diagnostics.push_back({0, 0, where + "reset input: " + error});
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
        if (rung.counter) {
            const i32 slot = result.program.find_var(rung.counter->var);
            if (slot < 0) {
                result.diagnostics.push_back(
                    {0, 0, where + "counter output '" + rung.counter->var +
                              "' is not a declared variable"});
            } else if (result.program.vars[static_cast<std::size_t>(slot)].type !=
                       ValueType::Bool) {
                result.diagnostics.push_back(
                    {0, 0, where + "counter output '" + rung.counter->var + "' must be BOOL"});
            }
            if (rung.counter->preset <= 0) {
                result.diagnostics.push_back(
                    {0, 0, where + "a counter preset must be longer than zero"});
            }
            if (!rung.counter->value_var.empty()) {
                const i32 value_slot = result.program.find_var(rung.counter->value_var);
                if (value_slot < 0) {
                    result.diagnostics.push_back(
                        {0, 0, where + "counter value '" + rung.counter->value_var +
                                  "' is not a declared variable"});
                } else if (!is_integer_type(
                               result.program.vars[static_cast<std::size_t>(value_slot)]
                                   .type)) {
                    result.diagnostics.push_back(
                        {0, 0, where + "counter value '" + rung.counter->value_var +
                                  "' must be an integer type"});
                }
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

    // Every block rung owns one instance. The instance name starts with '#' so it
    // can never collide with a tag: no source language here spells that.
    i32 blocks = 0;
    for (const Rung& rung : rungs) {
        if (rung.timer || rung.counter) {
            const FbDecl* decl = find_builtin_fb(sink_type(rung));
            if (decl == nullptr) {
                result.diagnostics.push_back({0, 0, "a block sink is not available"});
                return result;
            }
            const i32 fb_index = static_cast<i32>(result.program.fbs.size());
            result.program.fbs.push_back(*decl);

            VarInfo instance;
            instance.name = "#fb" + std::to_string(++blocks);
            instance.fb_index = fb_index;
            result.program.vars.push_back(std::move(instance));

            // Input 0 is the rung's condition in every block sink.
            emit_operand(result.program, rung.condition, true);
            result.program.code.push_back({Op::StoreFbField, fb_index, 0});

            const i32 output_field = rung.timer ? 2 : 3;
            if (rung.timer) {
                result.program.code.push_back(
                    {Op::PushConst,
                     add_const(result.program, Value::time(rung.timer->preset_ms)), 0});
                result.program.code.push_back({Op::StoreFbField, fb_index, 1});
            } else {
                emit_operand(result.program, rung.counter->reset, false);
                result.program.code.push_back({Op::StoreFbField, fb_index, 1});
                result.program.code.push_back(
                    {Op::PushConst,
                     add_const(result.program, Value::integer(rung.counter->preset)), 0});
                result.program.code.push_back({Op::StoreFbField, fb_index, 2});
            }

            result.program.code.push_back({Op::CallFb, fb_index, 0});
            result.program.code.push_back({Op::LoadFbField, fb_index, output_field});
            const std::string& target = rung.timer ? rung.timer->var : rung.counter->var;
            result.program.code.push_back(
                {Op::StoreVar, result.program.find_var(target), 0});
            if (rung.counter && !rung.counter->value_var.empty()) {
                result.program.code.push_back({Op::LoadFbField, fb_index, 4});
                result.program.code.push_back(
                    {Op::StoreVar, result.program.find_var(rung.counter->value_var), 0});
            }
            continue;
        }
        for (const Coil& coil : rung.coils) {
            emit_operand(result.program, rung.condition, true);
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
