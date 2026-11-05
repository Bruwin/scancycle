#pragma once
#include "plc/diagnostic.hpp"
#include "plc/token.hpp"

#include <string_view>
#include <vector>

namespace sc::plc {

class Lexer {
public:
    explicit Lexer(std::string_view source) : source_(source) {}

    std::vector<Token> tokenize();
    const Diagnostics& diagnostics() const { return diagnostics_; }

private:
    char peek(std::size_t offset = 0) const;
    char advance();
    bool match(char expected);

    void skip_trivia();
    bool lex_identifier(std::vector<Token>& out);
    bool lex_number(std::vector<Token>& out);
    bool lex_string(std::vector<Token>& out);
    void lex_operator(std::vector<Token>& out);
    bool parse_duration(i64& out_milliseconds);

    void error(int line, int column, std::string message);

    std::string_view source_;
    std::size_t pos_ = 0;
    int line_ = 1;
    int column_ = 1;
    Diagnostics diagnostics_;
};

bool is_keyword(std::string_view text);

}  // namespace sc::plc
