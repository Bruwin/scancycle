#pragma once
#include "plc/program.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace sc::plc {

// Executes one program's scan cycle against a retained variable image.
// scan() is the only place simulation time enters, and it is passed in, never
// read from a clock, so replays are exact.
class Vm {
public:
    explicit Vm(const CompiledProgram& program);

    void reset();
    bool scan(i64 now_ms);

    bool faulted() const { return faulted_; }
    const std::string& fault_message() const { return fault_message_; }
    i64 instructions_last_scan() const { return instruction_count_; }

    Value get(std::string_view name) const;
    bool set(std::string_view name, Value value);

    const CompiledProgram& program() const { return *program_; }

private:
    bool fail(std::string message);
    void push(Value value);
    Value pop();

    const CompiledProgram* program_;
    std::vector<Value> vars_;
    std::vector<std::vector<Value>> fb_slots_;
    std::vector<Value> stack_;

    bool faulted_ = false;
    std::string fault_message_;
    i64 instruction_count_ = 0;
};

}  // namespace sc::plc
