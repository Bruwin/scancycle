#pragma once
// The compiled form of a Structured Text program: a flat slot array, function
// block instances and stack bytecode. One instruction budget per scan makes
// the classic "scan overrun" fault real and observable.
#include "plc/ast.hpp"
#include "plc/value.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace sc::plc {

enum class Op : u8 {
    PushConst = 0,
    LoadVar,
    StoreVar,
    LoadFbField,
    StoreFbField,
    Binary,
    Unary,
    Jump,
    JumpIfFalse,
    CallBuiltin,
    CallFb,
    Return,
    Halt,
};

struct Instr {
    Op op = Op::Halt;
    i32 a = 0;
    i32 b = 0;
};

struct VarInfo {
    std::string name;
    ValueType type = ValueType::Void;
    i32 fb_index = -1;  // >= 0 when the variable is a function block instance
};

struct FbDecl {
    std::string type;
    std::vector<std::string> fields;
    i32 state_slots = 0;
};

struct CompiledProgram {
    std::string name;
    std::vector<VarInfo> vars;
    std::vector<FbDecl> fbs;
    std::vector<Instr> code;
    std::vector<Value> constants;

    i32 find_var(std::string_view needle) const;
};

std::string disassemble(const CompiledProgram& program);

// The toolbox vocabulary a compiled program actually uses: function block types
// (TON, CTU, ...), boolean operators (AND, OR, NOT, XOR) and declared value
// types (BOOL, INT, ...). Derived from bytecode, so Structured Text and ladder
// report the same names, and a lesson's reference solution is held to the same
// list as a student's submission.
std::vector<std::string> features_used(const CompiledProgram& program);

}  // namespace sc::plc
