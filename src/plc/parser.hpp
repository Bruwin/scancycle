#pragma once
#include "plc/ast.hpp"
#include "plc/diagnostic.hpp"

#include <memory>
#include <string_view>

namespace sc::plc {

struct ParseResult {
    std::unique_ptr<ProgramDecl> program;
    Diagnostics diagnostics;

    bool ok() const { return program != nullptr && diagnostics.empty(); }
};

ParseResult parse_program(std::string_view source);

}  // namespace sc::plc
