#include "plc/parser.hpp"

#include "plc/lexer.hpp"

#include <initializer_list>
#include <string_view>

namespace sc::plc {
namespace {

bool text_is(const Token& token, std::string_view text) {
    return std::string_view(token.text) == text;
}

int binary_precedence(const Token& token) {
    if (token.kind == TokenKind::Keyword) {
        if (text_is(token, "or")) return 1;
        if (text_is(token, "xor")) return 2;
        if (text_is(token, "and")) return 3;
        if (text_is(token, "mod")) return 7;
        return 0;
    }
    if (token.kind != TokenKind::Operator) {
        return 0;
    }
    if (text_is(token, "=") || text_is(token, "<>")) return 4;
    if (text_is(token, "<") || text_is(token, "<=") || text_is(token, ">") ||
        text_is(token, ">="))
        return 5;
    if (text_is(token, "+") || text_is(token, "-")) return 6;
    if (text_is(token, "*") || text_is(token, "/")) return 7;
    if (text_is(token, "**")) return 8;
    return 0;
}

BinOp to_bin_op(const Token& token) {
    if (text_is(token, "+")) return BinOp::Add;
    if (text_is(token, "-")) return BinOp::Sub;
    if (text_is(token, "*")) return BinOp::Mul;
    if (text_is(token, "/")) return BinOp::Div;
    if (text_is(token, "mod")) return BinOp::Mod;
    if (text_is(token, "**")) return BinOp::Pow;
    if (text_is(token, "and")) return BinOp::And;
    if (text_is(token, "or")) return BinOp::Or;
    if (text_is(token, "xor")) return BinOp::Xor;
    if (text_is(token, "=")) return BinOp::Eq;
    if (text_is(token, "<>")) return BinOp::Ne;
    if (text_is(token, "<")) return BinOp::Lt;
    if (text_is(token, "<=")) return BinOp::Le;
    if (text_is(token, ">")) return BinOp::Gt;
    return BinOp::Ge;
}

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    std::unique_ptr<ProgramDecl> parse_program_decl() {
        if (!expect_keyword("program", "expected PROGRAM")) {
            return nullptr;
        }
        if (!check(TokenKind::Identifier)) {
            error(peek(), "expected a program name");
            return nullptr;
        }

        auto program = std::make_unique<ProgramDecl>();
        program->name = advance().text;

        if (!parse_var_blocks(*program)) {
            return nullptr;
        }
        program->body = parse_statement_list({"end_program"});
        if (!expect_keyword("end_program", "expected END_PROGRAM")) {
            return nullptr;
        }
        return program;
    }

    const Diagnostics& diagnostics() const { return diagnostics_; }

private:
    const Token& peek(std::size_t offset = 0) const {
        const std::size_t at = index_ + offset;
        return at < tokens_.size() ? tokens_[at] : tokens_.back();
    }

    const Token& advance() {
        const Token& token = peek();
        if (index_ + 1 < tokens_.size()) {
            ++index_;
        }
        return token;
    }

    bool check(TokenKind kind) const { return peek().kind == kind; }

    bool check_keyword(std::string_view text) const {
        return peek().kind == TokenKind::Keyword && text_is(peek(), text);
    }

    bool check_operator(std::string_view text) const {
        return peek().kind == TokenKind::Operator && text_is(peek(), text);
    }

    bool match_keyword(std::string_view text) {
        if (!check_keyword(text)) {
            return false;
        }
        advance();
        return true;
    }

    bool match_operator(std::string_view text) {
        if (!check_operator(text)) {
            return false;
        }
        advance();
        return true;
    }

    bool expect_keyword(std::string_view text, const char* message) {
        if (match_keyword(text)) {
            return true;
        }
        error(peek(), message);
        return false;
    }

    bool expect_operator(std::string_view text, const char* message) {
        if (match_operator(text)) {
            return true;
        }
        error(peek(), message);
        return false;
    }

    void error(const Token& token, std::string message) {
        diagnostics_.push_back(Diagnostic{token.line, token.column, std::move(message)});
    }

    bool parse_var_blocks(ProgramDecl& program) {
        for (;;) {
            if (!check_keyword("var") && !check_keyword("var_input") &&
                !check_keyword("var_output") && !check_keyword("var_in_out") &&
                !check_keyword("var_temp")) {
                return true;
            }
            advance();

            while (!check_keyword("end_var")) {
                if (check(TokenKind::End)) {
                    error(peek(), "expected END_VAR");
                    return false;
                }
                if (match_operator(";")) {
                    continue;
                }
                if (!check(TokenKind::Identifier)) {
                    error(peek(), "expected a variable name");
                    return false;
                }

                VarDecl decl;
                decl.line = peek().line;
                decl.name = advance().text;

                if (!expect_operator(":", "expected ':' after the variable name")) {
                    return false;
                }
                if (!check(TokenKind::Identifier)) {
                    error(peek(), "expected a type name");
                    return false;
                }
                decl.type_name = advance().text;

                if (!expect_operator(";", "expected ';' after the declaration")) {
                    return false;
                }
                program.vars.push_back(std::move(decl));
            }
            advance();  // END_VAR
        }
    }

    std::vector<StmtPtr> parse_statement_list(std::initializer_list<std::string_view> terminators) {
        std::vector<StmtPtr> statements;
        for (;;) {
            const Token& token = peek();
            if (token.kind == TokenKind::End) {
                return statements;
            }
            if (token.kind == TokenKind::Keyword) {
                bool stop = false;
                for (std::string_view terminator : terminators) {
                    if (text_is(token, terminator)) {
                        stop = true;
                        break;
                    }
                }
                if (stop) {
                    return statements;
                }
            }
            auto statement = parse_statement();
            if (!statement) {
                return statements;
            }
            statements.push_back(std::move(statement));
        }
    }

    StmtPtr parse_statement() {
        if (check_keyword("if")) return parse_if();
        if (check_keyword("while")) return parse_while();
        if (check_keyword("for")) return parse_for();

        if (check_keyword("return")) {
            auto statement = std::make_unique<Stmt>();
            statement->kind = Stmt::Kind::Return;
            statement->line = advance().line;
            expect_operator(";", "expected ';' after RETURN");
            return statement;
        }

        if (match_operator(";")) {
            auto statement = std::make_unique<Stmt>();
            statement->kind = Stmt::Kind::Empty;
            return statement;
        }

        return parse_simple_statement();
    }

    StmtPtr parse_simple_statement() {
        const int line = peek().line;
        auto expression = parse_expression(1);
        if (!expression) {
            return nullptr;
        }

        if (match_operator(":=")) {
            auto statement = std::make_unique<Stmt>();
            statement->kind = Stmt::Kind::Assign;
            statement->line = line;
            statement->lhs = std::move(expression);
            statement->rhs = parse_expression(1);
            if (!statement->rhs) {
                return nullptr;
            }
            if (!expect_operator(";", "expected ';' after the assignment")) {
                return nullptr;
            }
            return statement;
        }

        if (expression->kind == ExprKind::Call) {
            auto statement = std::make_unique<Stmt>();
            statement->kind = Stmt::Kind::Call;
            statement->line = line;
            statement->expr = std::move(expression);
            if (!expect_operator(";", "expected ';' after the call")) {
                return nullptr;
            }
            return statement;
        }

        error(peek(), "expected ':=' or a function block call");
        return nullptr;
    }

    StmtPtr parse_if() {
        auto statement = std::make_unique<Stmt>();
        statement->kind = Stmt::Kind::If;
        statement->line = advance().line;

        for (;;) {
            auto condition = parse_expression(1);
            if (!condition) {
                return nullptr;
            }
            if (!expect_keyword("then", "expected THEN")) {
                return nullptr;
            }
            auto body = parse_statement_list({"elsif", "else", "end_if"});
            statement->branches.emplace_back(std::move(condition), std::move(body));

            if (match_keyword("elsif")) {
                continue;
            }
            break;
        }

        if (match_keyword("else")) {
            statement->else_body = parse_statement_list({"end_if"});
        }
        if (!expect_keyword("end_if", "expected END_IF")) {
            return nullptr;
        }
        return statement;
    }

    StmtPtr parse_while() {
        auto statement = std::make_unique<Stmt>();
        statement->kind = Stmt::Kind::While;
        statement->line = advance().line;
        statement->expr = parse_expression(1);
        if (!statement->expr) {
            return nullptr;
        }
        if (!expect_keyword("do", "expected DO")) {
            return nullptr;
        }
        statement->body = parse_statement_list({"end_while"});
        if (!expect_keyword("end_while", "expected END_WHILE")) {
            return nullptr;
        }
        return statement;
    }

    StmtPtr parse_for() {
        auto statement = std::make_unique<Stmt>();
        statement->kind = Stmt::Kind::For;
        statement->line = advance().line;

        if (!check(TokenKind::Identifier)) {
            error(peek(), "expected a loop variable");
            return nullptr;
        }
        auto loop_var = std::make_unique<Expr>();
        loop_var->kind = ExprKind::Var;
        loop_var->name = advance().text;
        loop_var->line = statement->line;
        statement->lhs = std::move(loop_var);

        if (!expect_operator(":=", "expected ':=' after the loop variable")) {
            return nullptr;
        }
        statement->rhs = parse_expression(1);
        if (!statement->rhs) {
            return nullptr;
        }
        if (!expect_keyword("to", "expected TO")) {
            return nullptr;
        }
        statement->loop_to = parse_expression(1);
        if (!statement->loop_to) {
            return nullptr;
        }
        if (match_keyword("by")) {
            statement->loop_by = parse_expression(1);
            if (!statement->loop_by) {
                return nullptr;
            }
        }
        if (!expect_keyword("do", "expected DO")) {
            return nullptr;
        }
        statement->body = parse_statement_list({"end_for"});
        if (!expect_keyword("end_for", "expected END_FOR")) {
            return nullptr;
        }
        return statement;
    }

    ExprPtr parse_expression(int minimum_precedence) {
        auto lhs = parse_unary();
        if (!lhs) {
            return nullptr;
        }

        for (;;) {
            const int precedence = binary_precedence(peek());
            if (precedence == 0 || precedence < minimum_precedence) {
                break;
            }
            const Token op = advance();
            const bool right_associative = text_is(op, "**");
            auto rhs = parse_expression(right_associative ? precedence : precedence + 1);
            if (!rhs) {
                return nullptr;
            }

            auto node = std::make_unique<Expr>();
            node->kind = ExprKind::Binary;
            node->line = op.line;
            node->bin_op = to_bin_op(op);
            node->a = std::move(lhs);
            node->b = std::move(rhs);
            lhs = std::move(node);
        }

        return lhs;
    }

    ExprPtr parse_unary() {
        if (check_operator("-")) {
            const int line = advance().line;
            auto operand = parse_unary();
            if (!operand) {
                return nullptr;
            }
            auto node = std::make_unique<Expr>();
            node->kind = ExprKind::Unary;
            node->line = line;
            node->un_op = UnOp::Neg;
            node->a = std::move(operand);
            return node;
        }
        if (check_keyword("not")) {
            const int line = advance().line;
            auto operand = parse_unary();
            if (!operand) {
                return nullptr;
            }
            auto node = std::make_unique<Expr>();
            node->kind = ExprKind::Unary;
            node->line = line;
            node->un_op = UnOp::Not;
            node->a = std::move(operand);
            return node;
        }
        return parse_postfix();
    }

    ExprPtr parse_postfix() {
        auto expression = parse_primary();
        if (!expression) {
            return nullptr;
        }

        for (;;) {
            if (match_operator(".")) {
                if (!check(TokenKind::Identifier)) {
                    error(peek(), "expected a member name after '.'");
                    return nullptr;
                }
                auto member = std::make_unique<Expr>();
                member->kind = ExprKind::Member;
                member->line = peek().line;
                member->name = advance().text;
                member->a = std::move(expression);
                expression = std::move(member);
                continue;
            }

            if (match_operator("(")) {
                if (expression->kind != ExprKind::Var) {
                    error(peek(), "only a function block instance or a function can be called");
                    return nullptr;
                }
                auto call = std::make_unique<Expr>();
                call->kind = ExprKind::Call;
                call->line = expression->line;
                call->name = expression->name;
                if (!parse_arguments(*call)) {
                    return nullptr;
                }
                expression = std::move(call);
                continue;
            }

            break;
        }

        return expression;
    }

    bool parse_arguments(Expr& call) {
        if (!check_operator(")")) {
            for (;;) {
                std::string name;
                if (check(TokenKind::Identifier) && peek(1).kind == TokenKind::Operator &&
                    text_is(peek(1), ":=")) {
                    name = peek().text;
                    advance();
                    advance();
                }

                auto argument = parse_expression(1);
                if (!argument) {
                    return false;
                }
                call.args.push_back(std::move(argument));
                call.arg_names.push_back(std::move(name));

                if (match_operator(",")) {
                    continue;
                }
                break;
            }
        }
        return expect_operator(")", "expected ')'");
    }

    ExprPtr parse_primary() {
        const Token& token = peek();

        if (token.kind == TokenKind::IntLiteral) {
            auto node = std::make_unique<Expr>();
            node->kind = ExprKind::IntLit;
            node->line = token.line;
            node->ival = advance().ival;
            return node;
        }
        if (token.kind == TokenKind::RealLiteral) {
            auto node = std::make_unique<Expr>();
            node->kind = ExprKind::RealLit;
            node->line = token.line;
            node->fval = advance().fval;
            return node;
        }
        if (token.kind == TokenKind::TimeLiteral) {
            auto node = std::make_unique<Expr>();
            node->kind = ExprKind::TimeLit;
            node->line = token.line;
            node->ival = advance().ival;
            return node;
        }
        if (token.kind == TokenKind::Keyword && (text_is(token, "true") || text_is(token, "false"))) {
            auto node = std::make_unique<Expr>();
            node->kind = ExprKind::BoolLit;
            node->line = token.line;
            node->ival = text_is(advance(), "true") ? 1 : 0;
            return node;
        }
        if (token.kind == TokenKind::Identifier) {
            auto node = std::make_unique<Expr>();
            node->kind = ExprKind::Var;
            node->line = token.line;
            node->name = advance().text;
            return node;
        }
        if (match_operator("(")) {
            auto inner = parse_expression(1);
            if (!inner) {
                return nullptr;
            }
            if (!expect_operator(")", "expected ')'")) {
                return nullptr;
            }
            return inner;
        }

        error(token, "expected an expression");
        return nullptr;
    }

    std::vector<Token> tokens_;
    std::size_t index_ = 0;
    Diagnostics diagnostics_;
};

}  // namespace

ParseResult parse_program(std::string_view source) {
    Lexer lexer(source);
    std::vector<Token> tokens = lexer.tokenize();

    Parser parser(std::move(tokens));
    auto program = parser.parse_program_decl();

    ParseResult result;
    result.program = std::move(program);
    result.diagnostics = lexer.diagnostics();
    for (const Diagnostic& diagnostic : parser.diagnostics()) {
        result.diagnostics.push_back(diagnostic);
    }
    return result;
}

}  // namespace sc::plc
