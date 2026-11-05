#include "plc/compiler.hpp"

#include "plc/function_blocks.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sc::plc {
namespace {

bool has_negative_step(const Expr* step) {
    if (step == nullptr) {
        return false;
    }
    if (step->kind == ExprKind::Unary && step->un_op == UnOp::Neg) {
        return true;
    }
    if (step->kind == ExprKind::IntLit) {
        return step->ival < 0;
    }
    return false;
}

class Compiler {
public:
    CompileResult run(const ProgramDecl& declaration) {
        program_.name = declaration.name;

        for (const VarDecl& var : declaration.vars) {
            declare(var);
        }
        for (const StmtPtr& statement : declaration.body) {
            compile_statement(*statement);
        }
        emit(Op::Halt);

        CompileResult result;
        result.ok = ok_ && diagnostics_.empty();
        result.program = std::move(program_);
        result.diagnostics = std::move(diagnostics_);
        return result;
    }

private:
    void error(int line, std::string message) {
        ok_ = false;
        diagnostics_.push_back(Diagnostic{line, 0, std::move(message)});
    }

    i32 emit(Op op, i32 a = 0, i32 b = 0) {
        program_.code.push_back(Instr{op, a, b});
        return static_cast<i32>(program_.code.size()) - 1;
    }

    void patch(i32 at, i32 target) { program_.code[static_cast<std::size_t>(at)].a = target; }

    i32 here() const { return static_cast<i32>(program_.code.size()); }

    i32 find_var(std::string_view name) const { return program_.find_var(name); }

    void push_const(Value value) {
        program_.constants.push_back(value);
        emit(Op::PushConst, static_cast<i32>(program_.constants.size()) - 1);
    }

    void declare(const VarDecl& declaration) {
        for (const VarInfo& existing : program_.vars) {
            if (existing.name == declaration.name) {
                error(declaration.line, "duplicate declaration of '" + declaration.name + "'");
                return;
            }
        }

        VarInfo info;
        info.name = declaration.name;

        ValueType type = ValueType::Void;
        if (parse_value_type(declaration.type_name, type)) {
            info.type = type;
        } else if (const FbDecl* fb = find_builtin_fb(declaration.type_name)) {
            info.fb_index = static_cast<i32>(program_.fbs.size());
            program_.fbs.push_back(*fb);
        } else {
            error(declaration.line, "unknown type '" + declaration.type_name + "'");
            return;
        }

        program_.vars.push_back(std::move(info));
    }

    bool resolve_member(const Expr& member, i32& fb_index, i32& field_index) {
        if (!member.a || member.a->kind != ExprKind::Var) {
            error(member.line, "member access is only supported on function block instances");
            return false;
        }
        const i32 slot = find_var(member.a->name);
        if (slot < 0) {
            error(member.line, "undeclared variable '" + member.a->name + "'");
            return false;
        }
        const VarInfo& var = program_.vars[static_cast<std::size_t>(slot)];
        if (var.fb_index < 0) {
            error(member.line, "'" + var.name + "' is not a function block instance");
            return false;
        }

        const FbDecl& fb = program_.fbs[static_cast<std::size_t>(var.fb_index)];
        for (std::size_t i = 0; i < fb.fields.size(); ++i) {
            if (fb.fields[i] == member.name) {
                fb_index = var.fb_index;
                field_index = static_cast<i32>(i);
                return true;
            }
        }
        error(member.line, "'" + fb.type + "' has no field '" + member.name + "'");
        return false;
    }

    i32 field_index_of(const FbDecl& fb, std::string_view name) const {
        for (std::size_t i = 0; i < fb.fields.size(); ++i) {
            if (fb.fields[i] == name) {
                return static_cast<i32>(i);
            }
        }
        return -1;
    }

    void compile_expression(const Expr& expression) {
        switch (expression.kind) {
            case ExprKind::IntLit:
                push_const(Value::integer(expression.ival));
                return;
            case ExprKind::RealLit:
                push_const(Value::real(expression.fval));
                return;
            case ExprKind::TimeLit:
                push_const(Value::time(expression.ival));
                return;
            case ExprKind::BoolLit:
                push_const(Value::boolean(expression.ival != 0));
                return;
            case ExprKind::Var: {
                const i32 slot = find_var(expression.name);
                if (slot < 0) {
                    error(expression.line, "undeclared variable '" + expression.name + "'");
                    return;
                }
                if (program_.vars[static_cast<std::size_t>(slot)].fb_index >= 0) {
                    error(expression.line, "'" + expression.name +
                                                "' is a function block instance; read one of its "
                                                "outputs instead");
                    return;
                }
                emit(Op::LoadVar, slot);
                return;
            }
            case ExprKind::Member: {
                i32 fb_index = -1;
                i32 field_index = -1;
                if (!resolve_member(expression, fb_index, field_index)) {
                    return;
                }
                emit(Op::LoadFbField, fb_index, field_index);
                return;
            }
            case ExprKind::Unary: {
                compile_expression(*expression.a);
                emit(Op::Unary, static_cast<i32>(expression.un_op));
                return;
            }
            case ExprKind::Binary: {
                compile_expression(*expression.a);
                compile_expression(*expression.b);
                emit(Op::Binary, static_cast<i32>(expression.bin_op));
                return;
            }
            case ExprKind::Call: {
                const i32 slot = find_var(expression.name);
                if (slot >= 0 && program_.vars[static_cast<std::size_t>(slot)].fb_index >= 0) {
                    error(expression.line,
                          "'" + expression.name +
                              "' is a function block instance; call it as its own statement");
                    return;
                }
                const Builtin builtin = find_builtin_function(expression.name);
                if (builtin == Builtin::None) {
                    error(expression.line, "unknown function '" + expression.name + "'");
                    return;
                }
                for (const ExprPtr& argument : expression.args) {
                    compile_expression(*argument);
                }
                emit(Op::CallBuiltin, static_cast<i32>(builtin),
                     static_cast<i32>(expression.args.size()));
                return;
            }
        }
    }

    void compile_assignment(const Stmt& statement) {
        if (statement.lhs->kind == ExprKind::Member) {
            i32 fb_index = -1;
            i32 field_index = -1;
            if (!resolve_member(*statement.lhs, fb_index, field_index)) {
                return;
            }
            const FbDecl& fb = program_.fbs[static_cast<std::size_t>(fb_index)];
            const std::string& field = fb.fields[static_cast<std::size_t>(field_index)];
            if (is_fb_output_field(fb.type, field)) {
                error(statement.line,
                      "'" + field + "' is an output of a " + fb.type + " and cannot be assigned");
                return;
            }
            compile_expression(*statement.rhs);
            emit(Op::StoreFbField, fb_index, field_index);
            return;
        }

        if (statement.lhs->kind != ExprKind::Var) {
            error(statement.line, "the left side of ':=' must be a variable");
            return;
        }
        const i32 slot = find_var(statement.lhs->name);
        if (slot < 0) {
            error(statement.line, "undeclared variable '" + statement.lhs->name + "'");
            return;
        }
        if (program_.vars[static_cast<std::size_t>(slot)].fb_index >= 0) {
            error(statement.line,
                  "cannot assign to the function block instance '" + statement.lhs->name + "'");
            return;
        }
        compile_expression(*statement.rhs);
        emit(Op::StoreVar, slot);
    }

    void compile_fb_call(const Expr& call) {
        const i32 slot = find_var(call.name);
        if (slot < 0 || program_.vars[static_cast<std::size_t>(slot)].fb_index < 0) {
            error(call.line, "'" + call.name + "' is not a function block instance");
            return;
        }
        const i32 fb_index = program_.vars[static_cast<std::size_t>(slot)].fb_index;
        const FbDecl& fb = program_.fbs[static_cast<std::size_t>(fb_index)];

        for (std::size_t i = 0; i < call.args.size(); ++i) {
            i32 field_index = -1;
            if (i < call.arg_names.size() && !call.arg_names[i].empty()) {
                field_index = field_index_of(fb, call.arg_names[i]);
                if (field_index < 0) {
                    error(call.line,
                          "'" + fb.type + "' has no field '" + call.arg_names[i] + "'");
                    return;
                }
            } else {
                field_index = static_cast<i32>(i);
                if (field_index >= static_cast<i32>(fb.fields.size())) {
                    error(call.line, "too many arguments in the call to '" + call.name + "'");
                    return;
                }
            }

            const std::string& field = fb.fields[static_cast<std::size_t>(field_index)];
            if (is_fb_output_field(fb.type, field)) {
                error(call.line,
                      "cannot write the output '" + field + "' of a " + fb.type + " instance");
                return;
            }

            compile_expression(*call.args[i]);
            emit(Op::StoreFbField, fb_index, field_index);
        }

        emit(Op::CallFb, fb_index);
    }

    void compile_statement(const Stmt& statement) {
        switch (statement.kind) {
            case Stmt::Kind::Empty:
                return;
            case Stmt::Kind::Assign:
                compile_assignment(statement);
                return;
            case Stmt::Kind::Call:
                compile_fb_call(*statement.expr);
                return;
            case Stmt::Kind::Return:
                emit(Op::Return);
                return;
            case Stmt::Kind::If: {
                std::vector<i32> end_jumps;
                for (const auto& branch : statement.branches) {
                    compile_expression(*branch.first);
                    const i32 jump_false = emit(Op::JumpIfFalse);
                    for (const StmtPtr& child : branch.second) {
                        compile_statement(*child);
                    }
                    end_jumps.push_back(emit(Op::Jump));
                    patch(jump_false, here());
                }
                for (const StmtPtr& child : statement.else_body) {
                    compile_statement(*child);
                }
                for (i32 jump : end_jumps) {
                    patch(jump, here());
                }
                return;
            }
            case Stmt::Kind::While: {
                const i32 start = here();
                compile_expression(*statement.expr);
                const i32 jump_false = emit(Op::JumpIfFalse);
                for (const StmtPtr& child : statement.body) {
                    compile_statement(*child);
                }
                emit(Op::Jump, start);
                patch(jump_false, here());
                return;
            }
            case Stmt::Kind::For: {
                if (statement.lhs->kind != ExprKind::Var) {
                    error(statement.line, "the loop variable must be a variable");
                    return;
                }
                const i32 slot = find_var(statement.lhs->name);
                if (slot < 0) {
                    error(statement.line, "undeclared variable '" + statement.lhs->name + "'");
                    return;
                }
                if (program_.vars[static_cast<std::size_t>(slot)].fb_index >= 0) {
                    error(statement.line, "the loop variable must be numeric");
                    return;
                }

                compile_expression(*statement.rhs);
                emit(Op::StoreVar, slot);

                const i32 start = here();
                emit(Op::LoadVar, slot);
                compile_expression(*statement.loop_to);
                emit(Op::Binary, static_cast<i32>(has_negative_step(statement.loop_by.get())
                                                      ? BinOp::Ge
                                                      : BinOp::Le));
                const i32 jump_false = emit(Op::JumpIfFalse);

                for (const StmtPtr& child : statement.body) {
                    compile_statement(*child);
                }

                emit(Op::LoadVar, slot);
                if (statement.loop_by) {
                    compile_expression(*statement.loop_by);
                } else {
                    push_const(Value::integer(1));
                }
                emit(Op::Binary, static_cast<i32>(BinOp::Add));
                emit(Op::StoreVar, slot);
                emit(Op::Jump, start);
                patch(jump_false, here());
                return;
            }
        }
    }

    CompiledProgram program_;
    Diagnostics diagnostics_;
    bool ok_ = true;
};

}  // namespace

CompileResult compile(const ProgramDecl& declaration) {
    Compiler compiler;
    return compiler.run(declaration);
}

}  // namespace sc::plc
