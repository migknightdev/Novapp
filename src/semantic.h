// semantic.h — Part 3: semantic analysis.
//   3a function registration / reserved 'main'
//   3b variable scoping (declaration vs. reassignment, const)
//   3c expression typing, undeclared names, call arity, return rules
// 3d (import resolution) extends this analyzer later.
#pragma once

#include <vector>
#include <string>
#include <map>
#include <memory>
#include <set>
#include "ast.h"
#include "scope.h"
#include "types.h"

namespace novac {

struct SemaError {
    std::string message;
    int line;
    int col;
    std::string file; // empty = the main file; otherwise the imported library it came from
};

// One typed copy of a user function for a specific list of argument types
// (parameters are untyped in Nova++, so `add(1, 2)` and `add(1.5, 2)` are two
// instances). `decl` is a clone whose expressions all carry resolved_type.
struct FuncInstance {
    std::string key;                     // "add(int,int)" — also the LLVM symbol name
    std::vector<Type> params;
    Type ret;                            // Void when the function never returns a value
    std::unique_ptr<FuncDeclStmt> decl;
    bool done = false;
    bool recursed = false;               // it calls itself
};

class SemanticAnalyzer {
public:
    // base_dir: directory used to resolve `import` (normally the entry file's directory).
    void analyze(ProgramStmt& program, const std::string& base_dir = "");

    const std::vector<SemaError>& errors() const { return errors_; }
    bool ok() const { return errors_.empty(); }
    const Scope& globals() const { return globals_; }
    size_t imported_used() const { return imported_used_; }   // library functions actually pulled in
    size_t imported_total() const { return imported_total_; } // library functions available
    const FuncInstance* find_instance(const std::string& key) const;
    size_t instance_count() const { return instances_.size(); }

private:
    struct FuncCtx {
        bool in_func = false;
        bool has_value_ret = false;
        bool has_bare_ret = false;
        Type ret;
        std::string name;
    };

    Scope globals_;
    std::vector<SemaError> errors_;
    std::map<std::string, bool> returns_value_; // user func name -> has a `return <value>`
    bool imports_incomplete_ = false;            // an import failed to load: don't cascade 'unknown function'
    std::string current_file_;
    size_t imported_used_ = 0;
    size_t imported_total_ = 0;
    FuncCtx ctx_;
    int loop_depth_ = 0;
    int switch_depth_ = 0;
    std::map<std::string, const FuncDeclStmt*> funcs_;
    std::map<std::string, FuncInstance> instances_;
    std::vector<std::string> stack_;   // instances currently being analyzed
    std::set<std::string> seen_errors_; // one error per source position

    void add_error(const std::string& message, int line, int col);
    void register_functions(const ProgramStmt& program);
    void walk_block(Block& block, Scope& scope);
    void walk_stmt(Stmt& stmt, Scope& scope);
    void declare_var(Scope& scope, const std::string& name, Mutability mut, int line, int col, Type type = Type());
    void check_not_const(const Symbol* sym, const std::string& name, int line, int col);
    const Symbol* require_var(Scope& scope, const std::string& name, const char* what, int line, int col);

    Type check_expr(const Expr& e, Scope& scope);
    Type check_expr_impl(const Expr& e, Scope& scope);   // may return Void (bare call statements)
    Type check_value(const Expr& e, Scope& scope);  // errors if the expression is Void
    void check_cond(const Expr& e, Scope& scope, const char* what);
    const FuncInstance* instantiate(const std::string& name, const std::vector<Type>& args, int line, int col);
    Type analyze_instance(FuncDeclStmt& fn, const std::vector<Type>& args);
};

} // namespace novac
