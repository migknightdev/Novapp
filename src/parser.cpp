// parser.cpp — Part 2b: expression parsing (precedence climbing via one
// recursive-descent method per precedence level).
#include "parser.h"

namespace novac {

Parser::Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

// ------------------------------ token helpers ------------------------------

const Token& Parser::peek(int offset) const {
    size_t idx = pos_ + static_cast<size_t>(offset);
    if (idx >= tokens_.size()) return tokens_.back(); // EOF sentinel
    return tokens_[idx];
}

const Token& Parser::previous() const {
    return tokens_[pos_ > 0 ? pos_ - 1 : 0];
}

const Token& Parser::advance() {
    if (!at_end()) pos_++;
    return previous();
}

bool Parser::check(TokenType type) const {
    return peek().type == type;
}

bool Parser::match(TokenType type) {
    if (!check(type)) return false;
    advance();
    return true;
}

const Token& Parser::expect(TokenType type, const std::string& message) {
    if (check(type)) return advance();
    add_error_here(message);
    return peek(); // don't consume; caller keeps going, error is recorded
}

bool Parser::at_end() const {
    return peek().type == TokenType::END_OF_FILE;
}

void Parser::add_error(const std::string& message, int line, int col) {
    errors_.push_back(ParseError{message, line, col});
}

void Parser::add_error_here(const std::string& message) {
    add_error(message, peek().line, peek().col);
}

// -------------------------- expression precedence ---------------------------
// Lowest to highest: || , && , ==/!= , </>/<=/>= , +/- , * // % , unary , postfix

ExprPtr Parser::parse_expression() {
    return parse_or();
}

ExprPtr Parser::parse_or() {
    ExprPtr left = parse_and();
    while (check(TokenType::OR)) {
        int l = peek().line, c = peek().col;
        TokenType op = advance().type;
        ExprPtr right = parse_and();
        auto node = std::make_unique<BinaryExpr>();
        node->line = l; node->col = c;
        node->left = std::move(left);
        node->op = op;
        node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

ExprPtr Parser::parse_and() {
    ExprPtr left = parse_equality();
    while (check(TokenType::AND)) {
        int l = peek().line, c = peek().col;
        TokenType op = advance().type;
        ExprPtr right = parse_equality();
        auto node = std::make_unique<BinaryExpr>();
        node->line = l; node->col = c;
        node->left = std::move(left);
        node->op = op;
        node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

ExprPtr Parser::parse_equality() {
    ExprPtr left = parse_relational();
    while (check(TokenType::EQ) || check(TokenType::NEQ)) {
        int l = peek().line, c = peek().col;
        TokenType op = advance().type;
        ExprPtr right = parse_relational();
        auto node = std::make_unique<BinaryExpr>();
        node->line = l; node->col = c;
        node->left = std::move(left);
        node->op = op;
        node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

ExprPtr Parser::parse_relational() {
    ExprPtr left = parse_additive();
    while (check(TokenType::LT) || check(TokenType::GT) ||
           check(TokenType::LE) || check(TokenType::GE)) {
        int l = peek().line, c = peek().col;
        TokenType op = advance().type;
        ExprPtr right = parse_additive();
        auto node = std::make_unique<BinaryExpr>();
        node->line = l; node->col = c;
        node->left = std::move(left);
        node->op = op;
        node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

ExprPtr Parser::parse_additive() {
    ExprPtr left = parse_multiplicative();
    while (check(TokenType::PLUS) || check(TokenType::MINUS)) {
        int l = peek().line, c = peek().col;
        TokenType op = advance().type;
        ExprPtr right = parse_multiplicative();
        auto node = std::make_unique<BinaryExpr>();
        node->line = l; node->col = c;
        node->left = std::move(left);
        node->op = op;
        node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

ExprPtr Parser::parse_multiplicative() {
    ExprPtr left = parse_unary();
    while (check(TokenType::STAR) || check(TokenType::SLASH) || check(TokenType::PERCENT)) {
        int l = peek().line, c = peek().col;
        TokenType op = advance().type;
        ExprPtr right = parse_unary();
        auto node = std::make_unique<BinaryExpr>();
        node->line = l; node->col = c;
        node->left = std::move(left);
        node->op = op;
        node->right = std::move(right);
        left = std::move(node);
    }
    return left;
}

ExprPtr Parser::parse_unary() {
    if (check(TokenType::NOT) || check(TokenType::MINUS)) {
        int l = peek().line, c = peek().col;
        TokenType op = advance().type;
        auto node = std::make_unique<UnaryExpr>();
        node->line = l; node->col = c;
        node->op = op;
        node->operand = parse_unary(); // right-associative chain, e.g. !!x or --x-ish
        return node;
    }
    return parse_postfix();
}

ExprPtr Parser::parse_postfix() {
    ExprPtr expr = parse_primary();

    while (check(TokenType::DOT)) {
        int l = peek().line, c = peek().col;
        advance(); // consume '.'

        if (check(TokenType::NUMBER)) {
            // arr.N  (literal 1-based index; kept as-authored, 0-based
            // conversion is a later-stage concern, not the parser's job)
            const Token& num = advance();
            auto idx = std::make_unique<NumberExpr>(num.num_value, num.is_float);
            idx->line = num.line; idx->col = num.col;

            auto node = std::make_unique<IndexExpr>();
            node->line = l; node->col = c;
            node->array = std::move(expr);
            node->index = std::move(idx);
            expr = std::move(node);
            continue;
        }

        if (check(TokenType::LPAREN)) {
            // arr.(expr)  -- index by variable/expression
            advance(); // consume '('
            ExprPtr index = parse_expression();
            expect(TokenType::RPAREN, "Expected ')' to close 'arr.(expr)' index.");

            auto node = std::make_unique<IndexExpr>();
            node->line = l; node->col = c;
            node->array = std::move(expr);
            node->index = std::move(index);
            expr = std::move(node);
            continue;
        }

        if (check(TokenType::IDENTIFIER)) {
            std::string name = advance().lexeme;

            if (check(TokenType::LPAREN)) {
                // obj.method(args)
                auto args = parse_call_args();
                auto node = std::make_unique<MethodCallExpr>();
                node->line = l; node->col = c;
                node->object = std::move(expr);
                node->method = std::move(name);
                node->args = std::move(args);
                expr = std::move(node);
            } else {
                // obj.property  (e.g. arr.length)
                auto node = std::make_unique<PropertyExpr>();
                node->line = l; node->col = c;
                node->object = std::move(expr);
                node->name = std::move(name);
                expr = std::move(node);
            }
            continue;
        }

        add_error_here("Expected an index (number), '(expr)', or a property/method name after '.'.");
        break;
    }

    return expr;
}

std::vector<ExprPtr> Parser::parse_call_args() {
    std::vector<ExprPtr> args;
    expect(TokenType::LPAREN, "Expected '(' to start argument list.");
    if (!check(TokenType::RPAREN)) {
        args.push_back(parse_expression());
        while (match(TokenType::COMMA)) {
            args.push_back(parse_expression());
        }
    }
    expect(TokenType::RPAREN, "Expected ')' to close argument list.");
    return args;
}

ExprPtr Parser::parse_primary() {
    const Token& t = peek();

    if (check(TokenType::NUMBER)) {
        advance();
        auto node = std::make_unique<NumberExpr>(t.num_value, t.is_float);
        node->line = t.line; node->col = t.col;
        return node;
    }

    if (check(TokenType::STRING)) {
        advance();
        auto node = std::make_unique<StringExpr>(t.str_value);
        node->line = t.line; node->col = t.col;
        return node;
    }

    if (check(TokenType::TRUE_LIT) || check(TokenType::FALSE_LIT)) {
        advance();
        auto node = std::make_unique<BoolExpr>(t.type == TokenType::TRUE_LIT);
        node->line = t.line; node->col = t.col;
        return node;
    }

    if (check(TokenType::IDENTIFIER)) {
        advance();
        if (check(TokenType::LPAREN)) {
            // free function call: somar(2, 3)
            auto args = parse_call_args();
            auto node = std::make_unique<CallExpr>();
            node->line = t.line; node->col = t.col;
            node->callee = t.lexeme;
            node->args = std::move(args);
            return node;
        }
        auto node = std::make_unique<IdentifierExpr>(t.lexeme);
        node->line = t.line; node->col = t.col;
        return node;
    }

    if (check(TokenType::LPAREN)) {
        advance();
        ExprPtr inner = parse_expression();
        expect(TokenType::RPAREN, "Expected ')' to close '('.");
        return inner; // grouping needs no dedicated AST node
    }

    if (check(TokenType::LBRACKET)) {
        advance();
        auto node = std::make_unique<ArrayLiteralExpr>();
        node->line = t.line; node->col = t.col;
        if (!check(TokenType::RBRACKET)) {
            node->elements.push_back(parse_expression());
            while (match(TokenType::SEMICOLON)) {
                node->elements.push_back(parse_expression());
            }
        }
        expect(TokenType::RBRACKET, "Expected ']' to close array literal.");
        return node;
    }

    add_error_here("Expected an expression (number, string, identifier, '(', or '[').");
    advance(); // consume the offending token so we don't loop forever
    auto placeholder = std::make_unique<NumberExpr>(0, false);
    placeholder->line = t.line; placeholder->col = t.col;
    return placeholder;
}

// -------------------------------- Part 2c: statements --------------------------------

bool Parser::is_compound_assign_op(TokenType t) const {
    return t == TokenType::PLUS_ASSIGN || t == TokenType::MINUS_ASSIGN ||
           t == TokenType::STAR_ASSIGN || t == TokenType::SLASH_ASSIGN;
}

bool Parser::is_incdec_op(TokenType t) const {
    return t == TokenType::PLUS_PLUS || t == TokenType::MINUS_MINUS;
}

void Parser::consume_optional_semicolon() {
    match(TokenType::SEMICOLON); // no-op if absent; Nova++ statements don't require ';'
}

StmtPtr Parser::parse_var_decl(Mutability mut) {
    const Token& kw = peek();
    advance(); // consume 'let' or 'const'

    std::string name;
    if (check(TokenType::IDENTIFIER)) {
        name = advance().lexeme;
    } else {
        add_error_here("Expected a variable name after 'let'/'const'.");
    }

    expect(TokenType::ASSIGN, "Expected '=' after variable name in a let/const declaration.");
    ExprPtr value = parse_expression();

    auto node = std::make_unique<VarDeclStmt>();
    node->line = kw.line; node->col = kw.col;
    node->name = std::move(name);
    node->value = std::move(value);
    node->mutability = mut;
    consume_optional_semicolon();
    return node;
}

StmtPtr Parser::parse_prefix_incdec() {
    const Token& op_tok = peek();
    TokenType op = advance().type; // consume ++ or --

    std::string name;
    if (check(TokenType::IDENTIFIER)) {
        name = advance().lexeme;
    } else {
        add_error_here("Expected a variable name after prefix '++'/'--'.");
    }

    auto node = std::make_unique<IncDecStmt>();
    node->line = op_tok.line; node->col = op_tok.col;
    node->name = std::move(name);
    node->op = op;
    node->prefix = true;
    consume_optional_semicolon();
    return node;
}

// Dispatches on what follows an IDENTIFIER: assignment ('=' or compound),
// postfix '++'/'--', or (falling through) a general expression statement
// such as a bare function call: calcnum(numero1, numero2).
StmtPtr Parser::parse_assign_or_incdec_or_expr() {
    const Token& first = peek();

    // Assignment / compound-assignment: NAME (= | += | -= | *= | /=) expr
    if (first.type == TokenType::IDENTIFIER &&
        (peek(1).type == TokenType::ASSIGN || is_compound_assign_op(peek(1).type))) {
        std::string name = advance().lexeme; // NAME
        TokenType op = advance().type;       // = or compound op
        ExprPtr value = parse_expression();

        auto node = std::make_unique<AssignStmt>();
        node->line = first.line; node->col = first.col;
        node->name = std::move(name);
        node->op = op;
        node->value = std::move(value);
        consume_optional_semicolon();
        return node;
    }

    // Postfix increment/decrement: NAME (++ | --)
    if (first.type == TokenType::IDENTIFIER && is_incdec_op(peek(1).type)) {
        std::string name = advance().lexeme; // NAME
        TokenType op = advance().type;       // ++ or --

        auto node = std::make_unique<IncDecStmt>();
        node->line = first.line; node->col = first.col;
        node->name = std::move(name);
        node->op = op;
        node->prefix = false;
        consume_optional_semicolon();
        return node;
    }

    // Fallback: a general expression used as a statement (function calls,
    // method calls, or anything else parse_expression() already handles).
    ExprPtr expr = parse_expression();
    auto node = std::make_unique<ExprStmt>();
    node->line = first.line; node->col = first.col;
    node->expr = std::move(expr);
    consume_optional_semicolon();
    return node;
}

StmtPtr Parser::parse_return() {
    const Token& kw = peek();
    advance(); // consume 'return'

    auto node = std::make_unique<ReturnStmt>();
    node->line = kw.line; node->col = kw.col;

    // Bare `return` (no value) if the statement ends right here.
    if (!check(TokenType::NEWLINE) && !check(TokenType::SEMICOLON) &&
        !check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
        node->value = parse_expression();
    }
    consume_optional_semicolon();
    return node;
}

StmtPtr Parser::parse_pf() {
    const Token& kw = peek();
    advance(); // consume 'pf'

    auto node = std::make_unique<PfStmt>();
    node->line = kw.line; node->col = kw.col;
    node->value = parse_expression();
    consume_optional_semicolon();
    return node;
}

StmtPtr Parser::parse_statement() {
    if (check(TokenType::KW_LET)) return parse_var_decl(Mutability::Let);
    if (check(TokenType::KW_CONST)) return parse_var_decl(Mutability::Const);
    if (check(TokenType::KW_RETURN)) return parse_return();
    if (check(TokenType::KW_PF)) return parse_pf();
    if (check(TokenType::KW_IF)) return parse_if();
    if (check(TokenType::KW_WHILE)) return parse_while();
    if (check(TokenType::KW_FOR)) return parse_for();
    if (check(TokenType::KW_FUNC)) return parse_func_decl();
    if (check(TokenType::KW_INPUT)) return parse_input();
    if (check(TokenType::KW_SLEEP)) return parse_sleep();
    if (check(TokenType::KW_EXIT)) return parse_exit();
    if (check(TokenType::KW_BREAK)) return parse_break();
    if (check(TokenType::KW_CONTINUE)) return parse_continue();
    if (check(TokenType::KW_SWITCH)) return parse_switch();
    if (check(TokenType::PLUS_PLUS) || check(TokenType::MINUS_MINUS)) return parse_prefix_incdec();

    if (check(TokenType::IDENTIFIER) && peek(1).type == TokenType::DOT &&
        peek(2).type == TokenType::IDENTIFIER &&
        (peek(2).lexeme == "add" || peek(2).lexeme == "rm") &&
        peek(3).type != TokenType::LPAREN) {
        return parse_array_add_or_remove();
    }

    if (check(TokenType::IDENTIFIER)) return parse_assign_or_incdec_or_expr();

    ExprPtr expr = parse_expression();
    auto node = std::make_unique<ExprStmt>();
    node->line = peek().line; node->col = peek().col;
    node->expr = std::move(expr);
    consume_optional_semicolon();
    return node;
}

// -------------------------------- Part 2d: control flow --------------------------------

void Parser::skip_newlines() {
    while (match(TokenType::NEWLINE)) {}
}

Block Parser::parse_block() {
    Block block;
    expect(TokenType::LBRACE, "Expected '{' to start a block.");
    skip_newlines();
    while (!check(TokenType::RBRACE) && !at_end()) {
        size_t before = pos_;
        block.push_back(parse_statement());
        if (pos_ == before) {
            // Safety net: parse_statement() made no progress (some token this
            // stage doesn't recognize yet). Skip it so we don't loop forever,
            // and surface it as a real error rather than hanging.
            add_error_here("Unexpected token; skipping to recover.");
            advance();
        }
        skip_newlines();
    }
    expect(TokenType::RBRACE, "Expected '}' to close a block.");
    return block;
}

StmtPtr Parser::parse_if() {
    const Token& kw = peek();
    advance(); // consume 'if'

    expect(TokenType::LPAREN, "Expected '(' after 'if'.");
    ExprPtr cond = parse_expression();
    expect(TokenType::RPAREN, "Expected ')' to close 'if' condition.");
    Block then_body = parse_block();

    auto node = std::make_unique<IfStmt>();
    node->line = kw.line; node->col = kw.col;
    node->condition = std::move(cond);
    node->then_body = std::move(then_body);

    // 'elif' can repeat; both "}<newline>elif" and "} elif" (same line, the
    // compact style used throughout Library.md) are accepted, since we just
    // skip any NEWLINEs and look at what comes next either way.
    for (;;) {
        skip_newlines();
        if (!check(TokenType::KW_ELIF)) break;
        advance();
        expect(TokenType::LPAREN, "Expected '(' after 'elif'.");
        ExprPtr econd = parse_expression();
        expect(TokenType::RPAREN, "Expected ')' to close 'elif' condition.");
        Block ebody = parse_block();
        node->elifs.push_back(ElifClause{std::move(econd), std::move(ebody)});
    }

    skip_newlines();
    if (check(TokenType::KW_ELSE)) {
        advance();
        node->else_body = parse_block();
    }

    return node;
}

StmtPtr Parser::parse_while() {
    const Token& kw = peek();
    advance(); // consume 'while'

    expect(TokenType::LPAREN, "Expected '(' after 'while'.");
    ExprPtr cond = parse_expression();
    expect(TokenType::RPAREN, "Expected ')' to close 'while' condition.");
    Block body = parse_block();

    auto node = std::make_unique<WhileStmt>();
    node->line = kw.line; node->col = kw.col;
    node->condition = std::move(cond);
    node->body = std::move(body);
    return node;
}

StmtPtr Parser::parse_for() {
    const Token& kw = peek();
    advance(); // consume 'for'
    expect(TokenType::LPAREN, "Expected '(' after 'for'.");

    // Disambiguate 'for (item in collection)' from the classic C-style form:
    // only the foreach form has KW_IN as the SECOND token inside the parens.
    if (check(TokenType::IDENTIFIER) && peek(1).type == TokenType::KW_IN) {
        std::string item_name = advance().lexeme; // item
        advance(); // 'in'
        ExprPtr collection = parse_expression();
        expect(TokenType::RPAREN, "Expected ')' to close 'for (item in collection)'.");
        Block body = parse_block();

        auto node = std::make_unique<ForeachStmt>();
        node->line = kw.line; node->col = kw.col;
        node->item_name = std::move(item_name);
        node->collection = std::move(collection);
        node->body = std::move(body);
        return node;
    }

    // Classic: for (init; condition; increment) { body }
    // Each clause is optional (e.g. 'for (;;)'); init/increment reuse the
    // same statement parser as top-level assignment/inc-dec statements,
    // which already consumes its own trailing ';' as a no-op when the next
    // token is ')' instead, so it composes cleanly with the explicit
    // semicolon/RPAREN checks below.
    StmtPtr init = nullptr;
    if (check(TokenType::SEMICOLON)) {
        advance(); // empty init clause
    } else {
        init = parse_assign_or_incdec_or_expr();
    }

    ExprPtr condition = nullptr;
    if (!check(TokenType::SEMICOLON)) {
        condition = parse_expression();
    }
    expect(TokenType::SEMICOLON, "Expected ';' after 'for' condition.");

    StmtPtr increment = nullptr;
    if (!check(TokenType::RPAREN)) {
        increment = parse_assign_or_incdec_or_expr();
    }
    expect(TokenType::RPAREN, "Expected ')' to close 'for(...)' header.");

    Block body = parse_block();

    auto node = std::make_unique<ForStmt>();
    node->line = kw.line; node->col = kw.col;
    node->init = std::move(init);
    node->condition = std::move(condition);
    node->increment = std::move(increment);
    node->body = std::move(body);
    return node;
}

std::string Parser::parse_dotted_name() {
    std::string result;
    if (check(TokenType::IDENTIFIER)) {
        result = advance().lexeme;
    } else {
        add_error_here("Expected a filename.");
        return result;
    }
    while (check(TokenType::DOT) && peek(1).type == TokenType::IDENTIFIER) {
        advance();
        result += ".";
        result += advance().lexeme;
    }
    return result;
}

StmtPtr Parser::parse_func_decl() {
    const Token& kw = peek();
    advance();
    std::string name;
    if (check(TokenType::IDENTIFIER)) name = advance().lexeme;
    else add_error_here("Expected a function name after 'func'.");

    expect(TokenType::LPAREN, "Expected '(' after function name.");
    std::vector<std::string> params;
    if (!check(TokenType::RPAREN)) {
        if (check(TokenType::IDENTIFIER)) params.push_back(advance().lexeme);
        else add_error_here("Expected a parameter name.");
        while (match(TokenType::COMMA)) {
            if (check(TokenType::IDENTIFIER)) params.push_back(advance().lexeme);
            else add_error_here("Expected a parameter name after ','.");
        }
    }
    expect(TokenType::RPAREN, "Expected ')' to close parameter list.");
    skip_newlines();
    Block body = parse_block();

    auto node = std::make_unique<FuncDeclStmt>();
    node->line = kw.line; node->col = kw.col;
    node->name = std::move(name);
    node->params = std::move(params);
    node->body = std::move(body);
    return node;
}

StmtPtr Parser::parse_import() {
    const Token& kw = peek();
    advance();
    std::string filename = parse_dotted_name();

    auto node = std::make_unique<ImportStmt>();
    node->line = kw.line; node->col = kw.col;
    node->file = std::move(filename);

    if (check(TokenType::KW_FROM)) {
        advance();
        if (check(TokenType::STRING)) node->from_dir = advance().str_value;
        else add_error_here("Expected a quoted path after 'From'.");
    }
    consume_optional_semicolon();
    return node;
}

StmtPtr Parser::parse_input() {
    const Token& kw = peek();
    advance();
    std::string target;
    if (check(TokenType::IDENTIFIER)) target = advance().lexeme;
    else add_error_here("Expected a target variable name after 'input'.");

    auto node = std::make_unique<InputStmt>();
    node->line = kw.line; node->col = kw.col;
    node->target = std::move(target);
    consume_optional_semicolon();
    return node;
}

StmtPtr Parser::parse_sleep() {
    const Token& kw = peek();
    advance();
    expect(TokenType::LPAREN, "Expected '(' after 'sleep'.");
    ExprPtr ms = parse_expression();
    expect(TokenType::RPAREN, "Expected ')' to close 'sleep(...)'.");

    auto node = std::make_unique<SleepStmt>();
    node->line = kw.line; node->col = kw.col;
    node->milliseconds = std::move(ms);
    consume_optional_semicolon();
    return node;
}

StmtPtr Parser::parse_exit() {
    const Token& kw = peek();
    advance();
    auto node = std::make_unique<ExitStmt>();
    node->line = kw.line; node->col = kw.col;
    if (!check(TokenType::NEWLINE) && !check(TokenType::SEMICOLON) &&
        !check(TokenType::RBRACE) && !check(TokenType::END_OF_FILE)) {
        node->code = parse_expression();
    }
    consume_optional_semicolon();
    return node;
}

StmtPtr Parser::parse_break() {
    const Token& kw = peek();
    advance();
    auto node = std::make_unique<BreakStmt>();
    node->line = kw.line; node->col = kw.col;
    consume_optional_semicolon();
    return node;
}

StmtPtr Parser::parse_continue() {
    const Token& kw = peek();
    advance();
    auto node = std::make_unique<ContinueStmt>();
    node->line = kw.line; node->col = kw.col;
    consume_optional_semicolon();
    return node;
}

StmtPtr Parser::parse_switch() {
    const Token& kw = peek();
    advance();
    expect(TokenType::LPAREN, "Expected '(' after 'switch'.");
    ExprPtr subject = parse_expression();
    expect(TokenType::RPAREN, "Expected ')' to close 'switch' subject.");
    skip_newlines();
    expect(TokenType::LBRACE, "Expected '{' to start 'switch' body.");
    skip_newlines();

    auto node = std::make_unique<SwitchStmt>();
    node->line = kw.line; node->col = kw.col;
    node->subject = std::move(subject);

    while (check(TokenType::KW_CASE) || check(TokenType::KW_DEFAULT)) {
        CaseClause clause;
        if (check(TokenType::KW_CASE)) { advance(); clause.value = parse_expression(); }
        else { advance(); clause.value = nullptr; }
        expect(TokenType::COLON, "Expected ':' after 'case'/'default' label.");
        skip_newlines();
        while (!check(TokenType::KW_CASE) && !check(TokenType::KW_DEFAULT) &&
               !check(TokenType::RBRACE) && !at_end()) {
            size_t before = pos_;
            clause.body.push_back(parse_statement());
            if (pos_ == before) { add_error_here("Unexpected token in 'switch' case body; skipping."); advance(); }
            skip_newlines();
        }
        node->cases.push_back(std::move(clause));
    }
    expect(TokenType::RBRACE, "Expected '}' to close 'switch' body.");
    return node;
}

StmtPtr Parser::parse_array_add_or_remove() {
    const Token& first = peek();
    std::string array_name = advance().lexeme;
    advance(); // '.'
    std::string method = advance().lexeme;
    ExprPtr value = parse_expression();

    if (method == "add") {
        auto node = std::make_unique<ArrayAddStmt>();
        node->line = first.line; node->col = first.col;
        node->array_name = std::move(array_name);
        node->value = std::move(value);
        consume_optional_semicolon();
        return node;
    }
    auto node = std::make_unique<ArrayRemoveStmt>();
    node->line = first.line; node->col = first.col;
    node->array_name = std::move(array_name);
    node->value_or_index = std::move(value);
    consume_optional_semicolon();
    return node;
}

std::unique_ptr<ProgramStmt> Parser::parse_program() {
    auto prog = std::make_unique<ProgramStmt>();

    while (!at_end()) {
        skip_newlines();
        if (at_end()) break;

        if (check(TokenType::HASH)) {
            advance();
            if (check(TokenType::IDENTIFIER) && peek().lexeme == "include") {
                advance();
                expect(TokenType::LT, "Expected '<' after '#include'.");
                if (check(TokenType::IDENTIFIER) && peek().lexeme == "nova") {
                    advance();
                    prog->has_nova_include = true;
                } else {
                    add_error_here("Expected 'nova' inside '#include <...>'.");
                }
                expect(TokenType::GT, "Expected '>' to close '#include <nova>'.");
            } else {
                add_error_here("Unknown '#' directive (only '#include <nova>' is supported).");
            }
            consume_optional_semicolon();
            continue;
        }

        if (check(TokenType::KW_USING)) {
            advance();
            if (check(TokenType::KW_NOMAIN)) { advance(); prog->no_main = true; }
            else add_error_here("Expected 'noMain' after 'using'.");
            consume_optional_semicolon();
            continue;
        }

        if (check(TokenType::KW_IMPORT)) {
            prog->imports.push_back(parse_import());
            continue;
        }

        size_t before = pos_;
        prog->body.push_back(parse_statement());
        if (pos_ == before) {
            add_error_here("Unexpected token at top level; skipping to recover.");
            advance();
        }
    }

    if (!prog->has_nova_include && require_include_) {
        add_error("Missing mandatory '#include <nova>' directive (required at the top of every Nova++ file).", 1, 1);
    }
    return prog;
}

} // namespace novac
