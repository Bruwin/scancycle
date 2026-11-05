#pragma once
// Structured Text is case-insensitive, so every identifier and keyword leaves
// the lexer lowercased. Nothing downstream has to remember that.
#include "core/types.hpp"

#include <string>

namespace sc::plc {

enum class TokenKind : u8 {
    End = 0,
    Identifier,
    IntLiteral,
    RealLiteral,
    TimeLiteral,
    StringLiteral,
    Keyword,
    Operator,
};

struct Token {
    TokenKind kind = TokenKind::End;
    std::string text;
    i64 ival = 0;
    f64 fval = 0.0;
    int line = 1;
    int column = 1;
};

}  // namespace sc::plc
