#include "plc/lexer.hpp"

#include <cctype>
#include <cstdlib>
#include <string>

namespace sc::plc {
namespace {

const char* const kKeywords[] = {
    "program",     "end_program", "var",        "var_input",  "var_output", "var_in_out",
    "var_temp",    "end_var",     "if",         "then",       "elsif",      "else",
    "end_if",      "while",       "do",         "end_while",  "for",        "to",
    "by",          "end_for",     "return",     "and",        "or",         "xor",
    "not",         "mod",         "true",       "false",      "function",   "end_function",
    "function_block", "end_function_block", "constant", "exit",
};

char to_lower(char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }

bool is_identifier_start(char c) {
    return std::isalpha(static_cast<unsigned char>(c)) != 0 || c == '_';
}

bool is_identifier_part(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
}

}  // namespace

bool is_keyword(std::string_view text) {
    for (const char* keyword : kKeywords) {
        if (text == keyword) {
            return true;
        }
    }
    return false;
}

char Lexer::peek(std::size_t offset) const {
    const std::size_t at = pos_ + offset;
    return at < source_.size() ? source_[at] : '\0';
}

char Lexer::advance() {
    if (pos_ >= source_.size()) {
        return '\0';
    }
    const char c = source_[pos_++];
    if (c == '\n') {
        ++line_;
        column_ = 1;
    } else {
        ++column_;
    }
    return c;
}

bool Lexer::match(char expected) {
    if (peek() != expected) {
        return false;
    }
    advance();
    return true;
}

void Lexer::error(int line, int column, std::string message) {
    diagnostics_.push_back(Diagnostic{line, column, std::move(message)});
}

void Lexer::skip_trivia() {
    for (;;) {
        const char c = peek();
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
            continue;
        }
        if (c == '/' && peek(1) == '/') {
            while (peek() != '\0' && peek() != '\n') {
                advance();
            }
            continue;
        }
        if (c == '(' && peek(1) == '*') {
            const int start_line = line_;
            const int start_column = column_;
            advance();
            advance();
            int depth = 1;
            while (depth > 0 && peek() != '\0') {
                if (peek() == '(' && peek(1) == '*') {
                    advance();
                    advance();
                    ++depth;
                } else if (peek() == '*' && peek(1) == ')') {
                    advance();
                    advance();
                    --depth;
                } else {
                    advance();
                }
            }
            if (depth > 0) {
                error(start_line, start_column, "unterminated block comment");
            }
            continue;
        }
        return;
    }
}

bool Lexer::parse_duration(i64& out_milliseconds) {
    i64 total = 0;
    bool any = false;

    for (;;) {
        if (std::isdigit(static_cast<unsigned char>(peek())) == 0) {
            break;
        }
        i64 whole = 0;
        while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
            whole = whole * 10 + (advance() - '0');
        }
        f64 fraction = 0.0;
        if (peek() == '.') {
            advance();
            f64 scale = 1.0;
            while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                scale *= 10.0;
                fraction += static_cast<f64>(advance() - '0') / scale;
            }
        }

        std::string unit;
        while (std::isalpha(static_cast<unsigned char>(peek())) != 0) {
            unit.push_back(to_lower(advance()));
        }
        if (unit.empty()) {
            return false;
        }

        i64 multiplier = 0;
        if (unit == "ms") {
            multiplier = 1;
        } else if (unit == "s") {
            multiplier = 1000;
        } else if (unit == "m") {
            multiplier = 60000;
        } else if (unit == "h") {
            multiplier = 3600000;
        } else if (unit == "d") {
            multiplier = 86400000;
        } else {
            return false;
        }

        total += whole * multiplier + static_cast<i64>(fraction * static_cast<f64>(multiplier));
        any = true;
    }

    out_milliseconds = total;
    return any;
}

bool Lexer::lex_identifier(std::vector<Token>& out) {
    const int start_line = line_;
    const int start_column = column_;

    std::string text;
    while (is_identifier_part(peek())) {
        text.push_back(to_lower(advance()));
    }

    Token token;
    token.line = start_line;
    token.column = start_column;

    // T#2s / TIME#500ms
    if ((text == "t" || text == "time") && peek() == '#') {
        advance();
        i64 milliseconds = 0;
        if (!parse_duration(milliseconds)) {
            error(start_line, start_column, "malformed time literal");
            return false;
        }
        token.kind = TokenKind::TimeLiteral;
        token.ival = milliseconds;
        token.text = text;
        out.push_back(std::move(token));
        return true;
    }

    token.kind = is_keyword(text) ? TokenKind::Keyword : TokenKind::Identifier;
    token.text = std::move(text);
    out.push_back(std::move(token));
    return true;
}

bool Lexer::lex_number(std::vector<Token>& out) {
    const int start_line = line_;
    const int start_column = column_;

    std::string text;
    while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
        text.push_back(advance());
    }

    bool is_real = false;
    if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek(1))) != 0) {
        is_real = true;
        text.push_back(advance());
        while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
            text.push_back(advance());
        }
    }

    if (peek() == 'e' || peek() == 'E') {
        is_real = true;
        text.push_back(advance());
        if (peek() == '+' || peek() == '-') {
            text.push_back(advance());
        }
        while (std::isdigit(static_cast<unsigned char>(peek())) != 0) {
            text.push_back(advance());
        }
    }

    Token token;
    token.line = start_line;
    token.column = start_column;
    token.text = text;
    if (is_real) {
        token.kind = TokenKind::RealLiteral;
        token.fval = std::strtod(text.c_str(), nullptr);
    } else {
        token.kind = TokenKind::IntLiteral;
        token.ival = std::strtoll(text.c_str(), nullptr, 10);
    }
    out.push_back(std::move(token));
    return true;
}

bool Lexer::lex_string(std::vector<Token>& out) {
    const int start_line = line_;
    const int start_column = column_;
    advance();  // opening quote

    std::string text;
    while (peek() != '\'' && peek() != '\0') {
        text.push_back(advance());
    }
    if (peek() == '\0') {
        error(start_line, start_column, "unterminated string literal");
        return false;
    }
    advance();

    Token token;
    token.kind = TokenKind::StringLiteral;
    token.text = std::move(text);
    token.line = start_line;
    token.column = start_column;
    out.push_back(std::move(token));
    return true;
}

void Lexer::lex_operator(std::vector<Token>& out) {
    const int start_line = line_;
    const int start_column = column_;

    static const char* const kTwoChar[] = {":=", "<>", "<=", ">=", "**"};
    for (const char* op : kTwoChar) {
        if (peek() == op[0] && peek(1) == op[1]) {
            advance();
            advance();
            Token token;
            token.kind = TokenKind::Operator;
            token.text = op;
            token.line = start_line;
            token.column = start_column;
            out.push_back(std::move(token));
            return;
        }
    }

    const char c = advance();
    Token token;
    token.kind = TokenKind::Operator;
    token.text = std::string(1, c);
    token.line = start_line;
    token.column = start_column;
    out.push_back(std::move(token));
}

std::vector<Token> Lexer::tokenize() {
    std::vector<Token> out;

    for (;;) {
        skip_trivia();
        if (pos_ >= source_.size()) {
            break;
        }

        const char c = peek();
        if (is_identifier_start(c)) {
            lex_identifier(out);
        } else if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
            lex_number(out);
        } else if (c == '\'') {
            lex_string(out);
        } else {
            lex_operator(out);
        }
    }

    Token end;
    end.kind = TokenKind::End;
    end.line = line_;
    end.column = column_;
    out.push_back(std::move(end));
    return out;
}

}  // namespace sc::plc
