#pragma once
#include <string>
#include <vector>

namespace sc::plc {

struct Diagnostic {
    int line = 0;
    int column = 0;
    std::string message;
};

using Diagnostics = std::vector<Diagnostic>;

}  // namespace sc::plc
