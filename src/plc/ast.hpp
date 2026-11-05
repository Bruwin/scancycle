#pragma once
#include "core/types.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace sc::plc {

enum class ExprKind : u8 { IntLit, RealLit, TimeLit, BoolLit, Var, Member, Unary, Binary, Call };

enum class BinOp : u8 { Add, Sub, Mul, Div, Mod, Pow, And, Or, Xor, Eq, Ne, Lt, Le, Gt, Ge };

enum class UnOp : u8 { Neg, Not };

struct Expr;
using ExprPtr = std::unique_ptr<Expr>;

struct Expr {
    ExprKind kind = ExprKind::IntLit;
    int line = 0;

    std::string name;                    // Var, Member, Call target
    std::vector<ExprPtr> args;           // Call arguments
    std::vector<std::string> arg_names;  // parallel to args; empty = positional
    ExprPtr a;                           // Unary operand, Binary lhs, Member base
    ExprPtr b;                           // Binary rhs

    i64 ival = 0;  // IntLit, TimeLit (milliseconds), BoolLit
    f64 fval = 0.0;
    BinOp bin_op = BinOp::Add;
    UnOp un_op = UnOp::Neg;
};

struct Stmt;
using StmtPtr = std::unique_ptr<Stmt>;

struct Stmt {
    enum class Kind : u8 { Assign, Call, If, While, For, Return, Empty };

    Kind kind = Kind::Empty;
    int line = 0;

    ExprPtr lhs;   // Assign target
    ExprPtr rhs;   // Assign source
    ExprPtr expr;  // Call expression or WHILE condition
    ExprPtr loop_to;
    ExprPtr loop_by;

    std::vector<StmtPtr> body;
    std::vector<std::pair<ExprPtr, std::vector<StmtPtr>>> branches;  // IF / ELSIF arms
    std::vector<StmtPtr> else_body;
};

struct VarDecl {
    std::string name;
    std::string type_name;
    int line = 0;
};

struct ProgramDecl {
    std::string name;
    std::vector<VarDecl> vars;
    std::vector<StmtPtr> body;
};

}  // namespace sc::plc
