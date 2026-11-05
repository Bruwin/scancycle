#include "plc/program.hpp"

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace sc::plc {
namespace {

// First use wins, so the feature a lesson withholds is reported in the order the
// program reaches for it rather than alphabetically.
void add_once(std::vector<std::string>& names, const std::string& name) {
    for (const std::string& present : names) {
        if (present == name) {
            return;
        }
    }
    names.push_back(name);
}

std::string upper(std::string_view text) {
    std::string out(text);
    for (char& character : out) {
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
    return out;
}

}  // namespace

std::vector<std::string> features_used(const CompiledProgram& program) {
    std::vector<std::string> names;

    // Function block instances are the instructions a lesson grants by name.
    for (const FbDecl& block : program.fbs) {
        add_once(names, upper(block.type));
    }

    // Only the boolean operators count. Comparisons and arithmetic are ordinary
    // expression syntax; a lesson that withheld '+' would make its own rubric
    // unwritable for reasons no student could guess.
    for (const Instr& instruction : program.code) {
        if (instruction.op == Op::Binary) {
            switch (static_cast<BinOp>(instruction.a)) {
                case BinOp::And:
                    add_once(names, "AND");
                    break;
                case BinOp::Or:
                    add_once(names, "OR");
                    break;
                case BinOp::Xor:
                    add_once(names, "XOR");
                    break;
                default:
                    break;
            }
        } else if (instruction.op == Op::Unary && static_cast<UnOp>(instruction.a) == UnOp::Not) {
            add_once(names, "NOT");
        }
    }

    for (const VarInfo& variable : program.vars) {
        if (variable.type != ValueType::Void) {
            add_once(names, upper(value_type_name(variable.type)));
        }
    }
    return names;
}

}  // namespace sc::plc
