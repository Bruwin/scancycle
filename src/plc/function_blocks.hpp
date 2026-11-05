#pragma once
#include "plc/program.hpp"

#include <string_view>
#include <vector>

namespace sc::plc {

enum class Builtin : i32 {
    None = -1,
    Abs = 0,
    Min,
    Max,
    Limit,
    Sel,
    Mux,
    Sqrt,
    Expt,
    Trunc,
    Round,
    Shl,
    Shr,
    Rol,
    Ror,
};

Builtin find_builtin_function(std::string_view name);

const FbDecl* find_builtin_fb(std::string_view type_name);
bool is_fb_output_field(std::string_view type_name, std::string_view field);

// Runs one evaluation of a standard function block, advancing its retained
// state. now_ms is simulation time, never wall-clock time.
void update_function_block(const FbDecl& decl, std::vector<Value>& slots, i64 now_ms);

}  // namespace sc::plc
