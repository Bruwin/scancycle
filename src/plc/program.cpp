#include "plc/program.hpp"

#include <cstdio>

namespace sc::plc {

i32 CompiledProgram::find_var(std::string_view needle) const {
    for (std::size_t i = 0; i < vars.size(); ++i) {
        if (std::string_view(vars[i].name) == needle) {
            return static_cast<i32>(i);
        }
    }
    return -1;
}

namespace {

const char* op_name(Op op) {
    switch (op) {
        case Op::PushConst:
            return "push";
        case Op::LoadVar:
            return "load";
        case Op::StoreVar:
            return "store";
        case Op::LoadFbField:
            return "load.fb";
        case Op::StoreFbField:
            return "store.fb";
        case Op::Binary:
            return "binary";
        case Op::Unary:
            return "unary";
        case Op::Jump:
            return "jump";
        case Op::JumpIfFalse:
            return "jump.if.false";
        case Op::CallBuiltin:
            return "call.builtin";
        case Op::CallFb:
            return "call.fb";
        case Op::Return:
            return "return";
        case Op::Halt:
            return "halt";
    }
    return "?";
}

}  // namespace

std::string disassemble(const CompiledProgram& program) {
    std::string out;
    char line[128];
    for (std::size_t i = 0; i < program.code.size(); ++i) {
        const Instr& instr = program.code[i];
        std::snprintf(line, sizeof(line), "%4zu  %-14s %d %d\n", i, op_name(instr.op), instr.a,
                      instr.b);
        out += line;
    }
    return out;
}

}  // namespace sc::plc
