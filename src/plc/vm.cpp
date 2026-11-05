#include "plc/vm.hpp"

#include "plc/function_blocks.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace sc::plc {
namespace {

Value zero_of(ValueType type) {
    Value value;
    value.type = type;
    switch (type) {
        case ValueType::Bool:
            value.b = false;
            break;
        case ValueType::Real:
        case ValueType::Lreal:
            value.f = 0.0;
            break;
        default:
            value.i = 0;
            break;
    }
    return value;
}

Value apply_binary(BinOp op, const Value& a, const Value& b, std::string& error) {
    const bool real = is_real_type(a.type) || is_real_type(b.type);

    switch (op) {
        case BinOp::Add:
            return real ? Value::real(a.as_real() + b.as_real())
                        : Value::integer(a.as_int() + b.as_int());
        case BinOp::Sub:
            return real ? Value::real(a.as_real() - b.as_real())
                        : Value::integer(a.as_int() - b.as_int());
        case BinOp::Mul:
            return real ? Value::real(a.as_real() * b.as_real())
                        : Value::integer(a.as_int() * b.as_int());
        case BinOp::Div: {
            if (real) {
                const f64 divisor = b.as_real();
                if (divisor == 0.0) {
                    error = "division by zero";
                    return Value();
                }
                return Value::real(a.as_real() / divisor);
            }
            const i64 divisor = b.as_int();
            if (divisor == 0) {
                error = "division by zero";
                return Value();
            }
            return Value::integer(a.as_int() / divisor);
        }
        case BinOp::Mod: {
            const i64 divisor = b.as_int();
            if (divisor == 0) {
                error = "modulo by zero";
                return Value();
            }
            return Value::integer(a.as_int() % divisor);
        }
        case BinOp::Pow:
            return Value::real(std::pow(a.as_real(), b.as_real()));
        case BinOp::And:
            if (a.type == ValueType::Bool || b.type == ValueType::Bool) {
                return Value::boolean(a.as_bool() && b.as_bool());
            }
            return Value::integer(a.as_int() & b.as_int());
        case BinOp::Or:
            if (a.type == ValueType::Bool || b.type == ValueType::Bool) {
                return Value::boolean(a.as_bool() || b.as_bool());
            }
            return Value::integer(a.as_int() | b.as_int());
        case BinOp::Xor:
            if (a.type == ValueType::Bool || b.type == ValueType::Bool) {
                return Value::boolean(a.as_bool() != b.as_bool());
            }
            return Value::integer(a.as_int() ^ b.as_int());
        case BinOp::Eq:
            return Value::boolean(real ? a.as_real() == b.as_real() : a.as_int() == b.as_int());
        case BinOp::Ne:
            return Value::boolean(real ? a.as_real() != b.as_real() : a.as_int() != b.as_int());
        case BinOp::Lt:
            return Value::boolean(real ? a.as_real() < b.as_real() : a.as_int() < b.as_int());
        case BinOp::Le:
            return Value::boolean(real ? a.as_real() <= b.as_real() : a.as_int() <= b.as_int());
        case BinOp::Gt:
            return Value::boolean(real ? a.as_real() > b.as_real() : a.as_int() > b.as_int());
        case BinOp::Ge:
            return Value::boolean(real ? a.as_real() >= b.as_real() : a.as_int() >= b.as_int());
    }
    error = "unsupported operator";
    return Value();
}

Value apply_unary(UnOp op, const Value& value) {
    switch (op) {
        case UnOp::Neg:
            if (is_real_type(value.type)) {
                return Value::real(-value.as_real());
            }
            return Value::integer(-value.as_int());
        case UnOp::Not:
            if (value.type == ValueType::Bool) {
                return Value::boolean(!value.as_bool());
            }
            return Value::integer(~value.as_int());
    }
    return Value();
}

bool check_arity(const std::vector<Value>& args, std::size_t expected, const char* name,
                 std::string& error) {
    if (args.size() == expected) {
        return true;
    }
    error = std::string(name) + ": expected " + std::to_string(expected) + " argument(s), got " +
            std::to_string(args.size());
    return false;
}

Value call_builtin(Builtin id, const std::vector<Value>& args, std::string& error) {
    switch (id) {
        case Builtin::Abs:
            if (!check_arity(args, 1, "ABS", error)) return Value();
            if (is_real_type(args[0].type)) return Value::real(std::fabs(args[0].as_real()));
            return Value::integer(std::llabs(args[0].as_int()));

        case Builtin::Min: {
            if (args.empty()) {
                error = "MIN: expected at least one argument";
                return Value();
            }
            bool real = false;
            for (const Value& value : args) real = real || is_real_type(value.type);
            if (real) {
                f64 result = args[0].as_real();
                for (const Value& value : args) result = std::min(result, value.as_real());
                return Value::real(result);
            }
            i64 result = args[0].as_int();
            for (const Value& value : args) result = std::min(result, value.as_int());
            return Value::integer(result);
        }

        case Builtin::Max: {
            if (args.empty()) {
                error = "MAX: expected at least one argument";
                return Value();
            }
            bool real = false;
            for (const Value& value : args) real = real || is_real_type(value.type);
            if (real) {
                f64 result = args[0].as_real();
                for (const Value& value : args) result = std::max(result, value.as_real());
                return Value::real(result);
            }
            i64 result = args[0].as_int();
            for (const Value& value : args) result = std::max(result, value.as_int());
            return Value::integer(result);
        }

        case Builtin::Limit:
            if (!check_arity(args, 3, "LIMIT", error)) return Value();
            if (args[1].as_real() < args[0].as_real()) return args[0];
            if (args[1].as_real() > args[2].as_real()) return args[2];
            return args[1];

        case Builtin::Sel:
            if (!check_arity(args, 3, "SEL", error)) return Value();
            return args[0].as_bool() ? args[2] : args[1];

        case Builtin::Mux: {
            if (args.empty()) {
                error = "MUX: expected a selector and at least one value";
                return Value();
            }
            const i64 selector = args[0].as_int();
            if (selector < 0 || selector >= static_cast<i64>(args.size()) - 1) {
                error = "MUX: selector out of range";
                return Value();
            }
            return args[static_cast<std::size_t>(selector) + 1];
        }

        case Builtin::Sqrt:
            if (!check_arity(args, 1, "SQRT", error)) return Value();
            if (args[0].as_real() < 0.0) {
                error = "SQRT: negative argument";
                return Value();
            }
            return Value::real(std::sqrt(args[0].as_real()));

        case Builtin::Expt:
            if (!check_arity(args, 2, "EXPT", error)) return Value();
            return Value::real(std::pow(args[0].as_real(), args[1].as_real()));

        case Builtin::Trunc:
            if (!check_arity(args, 1, "TRUNC", error)) return Value();
            return Value::integer(static_cast<i64>(args[0].as_real()));

        case Builtin::Round:
            if (!check_arity(args, 1, "ROUND", error)) return Value();
            return Value::integer(static_cast<i64>(std::llround(args[0].as_real())));

        case Builtin::Shl:
            if (!check_arity(args, 2, "SHL", error)) return Value();
            return Value::integer(static_cast<i64>(static_cast<u64>(args[0].as_int())
                                                   << (args[1].as_int() & 63)));

        case Builtin::Shr:
            if (!check_arity(args, 2, "SHR", error)) return Value();
            return Value::integer(static_cast<i64>(static_cast<u64>(args[0].as_int())
                                                   >> (args[1].as_int() & 63)));

        case Builtin::Rol: {
            if (!check_arity(args, 2, "ROL", error)) return Value();
            const u64 value = static_cast<u64>(args[0].as_int());
            const i64 shift = args[1].as_int() & 63;
            return Value::integer(static_cast<i64>((value << shift) | (value >> (64 - shift))));
        }

        case Builtin::Ror: {
            if (!check_arity(args, 2, "ROR", error)) return Value();
            const u64 value = static_cast<u64>(args[0].as_int());
            const i64 shift = args[1].as_int() & 63;
            return Value::integer(static_cast<i64>((value >> shift) | (value << (64 - shift))));
        }

        case Builtin::None:
            error = "unknown builtin function";
            return Value();
    }
    error = "unknown builtin function";
    return Value();
}

}  // namespace

Vm::Vm(const CompiledProgram& program) : program_(&program) { reset(); }

void Vm::reset() {
    vars_.assign(program_->vars.size(), Value());
    for (std::size_t i = 0; i < program_->vars.size(); ++i) {
        vars_[i] = zero_of(program_->vars[i].type);
    }

    fb_slots_.clear();
    fb_slots_.reserve(program_->fbs.size());
    for (const FbDecl& fb : program_->fbs) {
        const std::size_t total = fb.fields.size() + static_cast<std::size_t>(fb.state_slots);
        std::vector<Value> slots(total, Value());
        for (std::size_t i = fb.fields.size(); i < slots.size(); ++i) {
            slots[i] = zero_of(ValueType::Int);
        }
        fb_slots_.push_back(std::move(slots));
    }

    stack_.clear();
    faulted_ = false;
    fault_message_.clear();
    instruction_count_ = 0;
}

bool Vm::fail(std::string message) {
    faulted_ = true;
    fault_message_ = std::move(message);
    return false;
}

void Vm::push(Value value) { stack_.push_back(value); }

Value Vm::pop() {
    if (stack_.empty()) {
        faulted_ = true;
        fault_message_ = "stack underflow";
        return Value();
    }
    const Value value = stack_.back();
    stack_.pop_back();
    return value;
}

Value Vm::get(std::string_view name) const {
    const i32 index = program_->find_var(name);
    if (index < 0) {
        return Value();
    }
    return vars_[static_cast<std::size_t>(index)];
}

bool Vm::set(std::string_view name, Value value) {
    const i32 index = program_->find_var(name);
    if (index < 0) {
        return false;
    }
    const VarInfo& info = program_->vars[static_cast<std::size_t>(index)];
    if (info.fb_index >= 0) {
        return false;
    }
    vars_[static_cast<std::size_t>(index)] = coerce(value, info.type);
    return true;
}

bool Vm::scan(i64 now_ms) {
    if (faulted_) {
        return false;
    }

    stack_.clear();
    instruction_count_ = 0;

    constexpr i64 kInstructionBudget = 200000;
    i32 pc = 0;
    const std::size_t code_size = program_->code.size();

    while (!faulted_ && pc >= 0 && static_cast<std::size_t>(pc) < code_size) {
        if (++instruction_count_ > kInstructionBudget) {
            return fail("scan watchdog: instruction budget exceeded");
        }

        const Instr instr = program_->code[static_cast<std::size_t>(pc++)];

        switch (instr.op) {
            case Op::PushConst:
                push(program_->constants[static_cast<std::size_t>(instr.a)]);
                break;

            case Op::LoadVar:
                push(vars_[static_cast<std::size_t>(instr.a)]);
                break;

            case Op::StoreVar: {
                const VarInfo& info = program_->vars[static_cast<std::size_t>(instr.a)];
                vars_[static_cast<std::size_t>(instr.a)] = coerce(pop(), info.type);
                break;
            }

            case Op::LoadFbField:
                push(fb_slots_[static_cast<std::size_t>(instr.a)][static_cast<std::size_t>(instr.b)]);
                break;

            case Op::StoreFbField:
                fb_slots_[static_cast<std::size_t>(instr.a)][static_cast<std::size_t>(instr.b)] =
                    pop();
                break;

            case Op::Binary: {
                const Value rhs = pop();
                const Value lhs = pop();
                std::string error;
                const Value result = apply_binary(static_cast<BinOp>(instr.a), lhs, rhs, error);
                if (!error.empty()) {
                    return fail(std::move(error));
                }
                push(result);
                break;
            }

            case Op::Unary: {
                const Value operand = pop();
                push(apply_unary(static_cast<UnOp>(instr.a), operand));
                break;
            }

            case Op::Jump:
                pc = instr.a;
                break;

            case Op::JumpIfFalse:
                if (!pop().as_bool()) {
                    pc = instr.a;
                }
                break;

            case Op::CallBuiltin: {
                const i32 argument_count = instr.b;
                std::vector<Value> arguments(static_cast<std::size_t>(argument_count));
                for (i32 i = argument_count - 1; i >= 0; --i) {
                    arguments[static_cast<std::size_t>(i)] = pop();
                }
                std::string error;
                const Value result = call_builtin(static_cast<Builtin>(instr.a), arguments, error);
                if (!error.empty()) {
                    return fail(std::move(error));
                }
                push(result);
                break;
            }

            case Op::CallFb:
                update_function_block(program_->fbs[static_cast<std::size_t>(instr.a)],
                                      fb_slots_[static_cast<std::size_t>(instr.a)], now_ms);
                break;

            case Op::Return:
            case Op::Halt:
                return !faulted_;
        }
    }

    return !faulted_;
}

}  // namespace sc::plc
