// parser.h — Part 2b of the novac project: the Parser class.
//
// This drop implements EXPRESSION parsing only (parse_expression() is the
// public entry point used by tests). Statement-level parsing (let/const,
// assignment, if/while/for, func, import, ...) lands in Parts 2c-2e; their
// method declarations are stubbed here (commented) as a map of what's coming
// so the header's shape doesn't change later, just gets filled in.
#pragma once

#include <vector>
#include <string>
#include "lexer.h"
#include "ast.h"

namespace novac {

struct ParseError {
    std::string message;
    int line;
    int col;
};

class Parser {
public:
    explicit Parser(std::vector<Token> tokens);

    // --- Part 2b: expressions ---
    // Parses one full expression starting at the current token, respecting
    // operator precedence (||, &&, ==/!=, </>/<=/>=, +/-, * / %, unary, then
    // postfix .index/.property/.method()/call). Leaves the current token on
    // whatever follows the expression (NEWLINE, ';', ')', ',', etc.) without
    // consuming it.
    ExprPtr parse_expression();

    const std::vector<ParseError>& errors() const { return errors_; }
    bool ok() const { return errors_.empty(); }

    // --- Part 2c: simple statements ---
    // Parses exactly one statement starting at the current token (which must
    // NOT be NEWLINE — skipping blank/separator lines between statements is
    // the block parser's job, added in 2d/2e). Consumes an optional trailing
    // ';' but leaves the following NEWLINE/EOF for the caller. Covers:
    // let/const declarations, plain assignment, compound assignment
    // (+= -= *= /=), ++/-- (prefix and postfix), pf, return, and any other
    // expression used as a statement (e.g. a bare function call).
    StmtPtr parse_statement();

    // --- Part 2d: control flow ---
    // Parses a brace-delimited block: '{' NEWLINEs-and-statements '}'.
    // Blank lines between statements are skipped automatically.
    Block parse_block();
    StmtPtr parse_if();     // if (...) { } [elif (...) { }]* [else { }]
    StmtPtr parse_while();  // while (...) { }
    StmtPtr parse_for();    // classic for(init; cond; incr) OR for (item in collection)

    // --- Part 2e: top-level constructs & remaining statement forms ---
    StmtPtr parse_func_decl();
    StmtPtr parse_import();
    StmtPtr parse_input();
    StmtPtr parse_sleep();
    StmtPtr parse_exit();
    StmtPtr parse_break();
    StmtPtr parse_continue();
    StmtPtr parse_switch();
    StmtPtr parse_array_add_or_remove();
    std::unique_ptr<ProgramStmt> parse_program();
    void set_require_nova_include(bool v) { require_include_ = v; } // false for imported libraries

private:
    std::vector<Token> tokens_;
    size_t pos_ = 0;
    std::vector<ParseError> errors_;
    bool require_include_ = true;

    // --- token stream helpers ---
    const Token& peek(int offset = 0) const;
    const Token& previous() const;
    const Token& advance();
    bool check(TokenType type) const;
    bool match(TokenType type);
    const Token& expect(TokenType type, const std::string& message);
    bool at_end() const;
    void add_error(const std::string& message, int line, int col);
    void add_error_here(const std::string& message);

    // --- expression grammar, one method per precedence level (lowest to
    //     highest): or -> and -> equality -> relational -> additive ->
    //     multiplicative -> unary -> postfix -> primary ---
    ExprPtr parse_or();
    ExprPtr parse_and();
    ExprPtr parse_equality();
    ExprPtr parse_relational();
    ExprPtr parse_additive();
    ExprPtr parse_multiplicative();
    ExprPtr parse_unary();
    ExprPtr parse_postfix();
    ExprPtr parse_primary();
    std::vector<ExprPtr> parse_call_args(); // consumes '(' ... ')'

    // --- Part 2c: simple-statement helpers ---
    bool is_compound_assign_op(TokenType t) const;
    bool is_incdec_op(TokenType t) const;
    StmtPtr parse_var_decl(Mutability mut);      // let/const NAME = expr
    StmtPtr parse_assign_or_incdec_or_expr();    // dispatch on IDENTIFIER lookahead
    StmtPtr parse_prefix_incdec();               // ++NAME / --NAME
    StmtPtr parse_return();
    StmtPtr parse_pf();
    void consume_optional_semicolon();
    void skip_newlines();
    std::string parse_dotted_name();
};

} // namespace novac
