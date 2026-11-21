// ast.h — Part 2 of the novac project: AST node definitions.
//
// A real tree, built by the parser from the Part 1 token stream. Every node
// carries its own `kind` tag (fast switch-based dispatch, no RTTI needed) and
// source position for error messages. Ownership is via unique_ptr so the
// whole tree is destroyed automatically with the Program root.
#pragma once

#include <string>
#include <vector>
#include <memory>
#include <optional>
#include "lexer.h" // for TokenType (reused for binary/assign/compound operators)
#include "types.h"

namespace novac {

// ============================== Expressions ==============================

enum class ExprKind {
    Number,
    String,
    Bool,
    Identifier,
    ArrayLiteral,
    Index,       // arr.N or arr.(expr) -> Index(array, index_expr)
    Property,    // arr.length -> Property(object, "length")
    MethodCall,  // texto.upper(), texto.contains(x) -> MethodCall(object, name, args)
    Binary,      // left OP right
    Unary,       // OP operand  (! or unary -)
    Call,        // somar(2, 3)
};

struct Expr {
    ExprKind kind;
    int line = 0;
    int col = 0;
    mutable Type resolved_type; // filled in by semantic analysis (3c), used by codegen (4b+)
    virtual ~Expr() = default;
protected:
    explicit Expr(ExprKind k) : kind(k) {}
};
using ExprPtr = std::unique_ptr<Expr>;

struct NumberExpr : Expr {
    double value;
    bool is_float;
    NumberExpr(double v, bool f) : Expr(ExprKind::Number), value(v), is_float(f) {}
};

struct StringExpr : Expr {
    std::string value;
    explicit StringExpr(std::string v) : Expr(ExprKind::String), value(std::move(v)) {}
};

struct BoolExpr : Expr {
    bool value;
    explicit BoolExpr(bool v) : Expr(ExprKind::Bool), value(v) {}
};

struct IdentifierExpr : Expr {
    std::string name;
    explicit IdentifierExpr(std::string n) : Expr(ExprKind::Identifier), name(std::move(n)) {}
};

struct ArrayLiteralExpr : Expr {
    std::vector<ExprPtr> elements;
    ArrayLiteralExpr() : Expr(ExprKind::ArrayLiteral) {}
};

// arr.N (literal index, 1-based in source) or arr.(expr) (any expression index)
struct IndexExpr : Expr {
    ExprPtr array;
    ExprPtr index; // always present; a literal `.N` is folded into a NumberExpr here too
    IndexExpr() : Expr(ExprKind::Index) {}
};

// arr.length  (property access with no call parens)
struct PropertyExpr : Expr {
    ExprPtr object;
    std::string name;
    PropertyExpr() : Expr(ExprKind::Property) {}
};

// texto.upper(), texto.contains(x), texto.split(",")
struct MethodCallExpr : Expr {
    ExprPtr object;
    std::string method;
    std::vector<ExprPtr> args;
    MethodCallExpr() : Expr(ExprKind::MethodCall) {}
};

struct BinaryExpr : Expr {
    ExprPtr left;
    TokenType op;
    ExprPtr right;
    BinaryExpr() : Expr(ExprKind::Binary) {}
};

struct UnaryExpr : Expr {
    TokenType op;
    ExprPtr operand;
    UnaryExpr() : Expr(ExprKind::Unary) {}
};

// somar(2, 3) — a free function call (not a method call on an object)
struct CallExpr : Expr {
    std::string callee;
    std::vector<ExprPtr> args;
    mutable std::string instance; // typed instance this call resolves to, e.g. "add(int,int)" (set by semantic analysis)
    CallExpr() : Expr(ExprKind::Call) {}
};

// ============================== Statements =================================

enum class StmtKind {
    Block,
    Import,
    FuncDecl,
    VarDecl,
    Assign,
    IncDec,
    If,
    While,
    For,
    Foreach,
    Return,
    Pf,
    Input,
    Sleep,
    Exit,
    Break,
    Continue,
    Switch,
    ArrayAdd,
    ArrayRemove,
    ExprStmt,
    Program,
};

struct Stmt {
    StmtKind kind;
    int line = 0;
    int col = 0;
    virtual ~Stmt() = default;
protected:
    explicit Stmt(StmtKind k) : kind(k) {}
};
using StmtPtr = std::unique_ptr<Stmt>;
using Block = std::vector<StmtPtr>;

// import <file> [From <dir>]
struct ImportStmt : Stmt {
    std::string file;
    std::optional<std::string> from_dir;
    ImportStmt() : Stmt(StmtKind::Import) {}
};

struct FuncDeclStmt : Stmt {
    std::string name;
    std::vector<std::string> params;
    Block body;
    std::string source_file; // non-empty when pulled in from an imported library (3d)
    FuncDeclStmt() : Stmt(StmtKind::FuncDecl) {}
};

enum class Mutability { Plain, Let, Const };

// First occurrence of `name = value` (optionally prefixed with let/const) in
// a scope. A later plain `name = value` for the same name is an AssignStmt,
// not another VarDeclStmt — matching vcomp.cpp's existing declared-vars
// tracking, which the *semantic analysis* stage (Part 3) will now enforce
// with a real per-scope symbol table instead of a flat std::set<string>.
struct VarDeclStmt : Stmt {
    std::string name;
    ExprPtr value;
    Mutability mutability = Mutability::Plain;
    VarDeclStmt() : Stmt(StmtKind::VarDecl) {}
};

// name = value | name += value | name -= value | name *= value | name /= value
struct AssignStmt : Stmt {
    std::string name;
    TokenType op; // ASSIGN, PLUS_ASSIGN, MINUS_ASSIGN, STAR_ASSIGN, SLASH_ASSIGN
    ExprPtr value;
    bool is_declaration = false; // set by semantic analysis (3b): first `x = v` in scope
    AssignStmt() : Stmt(StmtKind::Assign) {}
};

// name++ | name-- | ++name | --name
struct IncDecStmt : Stmt {
    std::string name;
    TokenType op; // PLUS_PLUS or MINUS_MINUS
    bool prefix = false;
    IncDecStmt() : Stmt(StmtKind::IncDec) {}
};

struct ElifClause {
    ExprPtr condition;
    Block body;
};

struct IfStmt : Stmt {
    ExprPtr condition;
    Block then_body;
    std::vector<ElifClause> elifs;
    std::optional<Block> else_body;
    IfStmt() : Stmt(StmtKind::If) {}
};

struct WhileStmt : Stmt {
    ExprPtr condition;
    Block body;
    WhileStmt() : Stmt(StmtKind::While) {}
};

// Classic C-style for(init; cond; increment). `init` and `increment` are
// statements (VarDecl/Assign/IncDec) to reuse the same node types, matching
// what the grammar actually allows in each clause.
struct ForStmt : Stmt {
    StmtPtr init;      // may be null (empty init clause)
    ExprPtr condition;  // may be null (empty condition -> infinite loop)
    StmtPtr increment; // may be null (empty increment clause)
    Block body;
    ForStmt() : Stmt(StmtKind::For) {}
};

// for (item in collection) { ... }
struct ForeachStmt : Stmt {
    std::string item_name;
    ExprPtr collection;
    Block body;
    ForeachStmt() : Stmt(StmtKind::Foreach) {}
};

struct ReturnStmt : Stmt {
    ExprPtr value; // may be null (bare `return`)
    ReturnStmt() : Stmt(StmtKind::Return) {}
};

// pf <expr>  — printing semantics (stream vs. concat) are a codegen decision
// (Part 4), not a parse-time one; the parser just captures the expression,
// operator precedence and all.
struct PfStmt : Stmt {
    ExprPtr value;
    PfStmt() : Stmt(StmtKind::Pf) {}
};

struct InputStmt : Stmt {
    std::string target;
    InputStmt() : Stmt(StmtKind::Input) {}
};

struct SleepStmt : Stmt {
    ExprPtr milliseconds;
    SleepStmt() : Stmt(StmtKind::Sleep) {}
};

struct ExitStmt : Stmt {
    ExprPtr code; // may be null
    ExitStmt() : Stmt(StmtKind::Exit) {}
};

struct BreakStmt : Stmt { BreakStmt() : Stmt(StmtKind::Break) {} };
struct ContinueStmt : Stmt { ContinueStmt() : Stmt(StmtKind::Continue) {} };

struct CaseClause {
    ExprPtr value; // null means `default:`
    Block body;
};

struct SwitchStmt : Stmt {
    ExprPtr subject;
    std::vector<CaseClause> cases; // a clause with value == nullptr is `default`
    SwitchStmt() : Stmt(StmtKind::Switch) {}
};

// fruits.add "orange"  — no parens, single raw argument expression
struct ArrayAddStmt : Stmt {
    std::string array_name;
    ExprPtr value;
    ArrayAddStmt() : Stmt(StmtKind::ArrayAdd) {}
};

// fruits.rm "banana" | fruits.rm 2
struct ArrayRemoveStmt : Stmt {
    std::string array_name;
    ExprPtr value_or_index;
    ArrayRemoveStmt() : Stmt(StmtKind::ArrayRemove) {}
};

// A bare expression used as a statement — e.g. a standalone function call:
// calcnum(numero1, numero2)
struct ExprStmt : Stmt {
    ExprPtr expr;
    ExprStmt() : Stmt(StmtKind::ExprStmt) {}
};

struct ProgramStmt : Stmt {
    bool has_nova_include = false;
    bool no_main = false;
    std::vector<StmtPtr> imports;   // ImportStmt nodes, in source order
    std::vector<StmtPtr> body;      // everything else at top level, in source order
    ProgramStmt() : Stmt(StmtKind::Program) {}
};

// ============================== Debug printing ==============================

// Renders the tree as indented text, for tests and manual inspection until
// Part 4 (codegen) exists. Not used by any later compiler stage.
std::string print_ast(const Stmt& node, int indent = 0);
std::string print_ast(const Expr& node, int indent = 0);

// Deep copies (annotations such as resolved_type / is_declaration / instance are NOT copied).
// Used to give each generic function instance its own, separately typed, body.
ExprPtr clone_expr(const Expr& e);
StmtPtr clone_stmt(const Stmt& s);
Block clone_block(const Block& b);
std::unique_ptr<FuncDeclStmt> clone_func(const FuncDeclStmt& f);

} // namespace novac
