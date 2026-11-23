// codegen.h — Part 4: AST -> textual LLVM IR (requires LLVM >= 15, opaque `ptr`).
// Emitting IR as text keeps novac free of any LLVM library dependency; a
// toolchain (clang) is only needed to turn the .ll into an executable.
// Run semantic analysis first: codegen reads the types it recorded on the AST.
//   4a  pf "text"
//   4b  variables, int/float/bool values, arithmetic, comparisons, && || (short-circuit)
//   4c  if/elif/else, while, for, switch (no fallthrough), break, continue
//   4d  functions: one specialized LLVM function per typed instance made by semantic
//       analysis (parameters are by value), calls, return, recursion
//   4e  strings and arrays through the C runtime in runtime/nova_rt.c
//   4f  input / sleep / exit / random / read_file / write_file
// Code outside the language (e.g. a nested function declaration) is reported as a
// CODEGEN ERROR instead of producing broken IR.
#pragma once

#include <map>
#include <set>
#include <string>
#include <vector>
#include "ast.h"
#include "semantic.h"

namespace novac {

struct CodegenError {
    std::string message;
    int line;
    int col;
    std::string file; // empty = main file; otherwise the imported library
};

class CodeGen {
public:
    // Returns false (see errors()) when the program can't be compiled.
    bool generate(const ProgramStmt& program, const SemanticAnalyzer& sema, std::string& out_ir);
    const std::vector<CodegenError>& errors() const { return errors_; }

private:
    struct Value { std::string repr; Type type; bool valid = false; };
    struct VarInfo { std::string slot; Type type; };
    struct Piece { std::string text; const Expr* expr; }; // expr == nullptr: literal text
    struct LoopCtx { std::string break_label; std::string continue_label; }; // continue_label empty for switch

    std::vector<CodegenError> errors_;
    std::string globals_;   // string constants
    std::string allocas_;   // variable slots (kept in the entry block)
    std::string body_;      // instructions of @main
    std::map<std::string, std::string> strings_;
    std::vector<std::map<std::string, VarInfo>> scopes_;
    std::string cur_block_;
    std::vector<LoopCtx> loops_;
    const SemanticAnalyzer* sema_ = nullptr;
    std::vector<std::string> queue_;   // instances still to emit (worklist)
    std::set<std::string> queued_;
    std::string functions_;            // finished instance functions
    std::string current_file_;
    Type cur_ret_;
    bool in_function_ = false;
    int next_tmp_ = 0, next_label_ = 0, next_slot_ = 0;

    void error(const std::string& message, int line, int col);
    std::string intern_string(const std::string& text);
    std::string tmp();
    void line(const std::string& text);
    std::string new_label(const std::string& base);
    void emit_label(const std::string& label);

    VarInfo declare(const std::string& name, const Type& type);
    const VarInfo* find_var(const std::string& name) const;

    Value emit_expr(const Expr& e);
    Value emit_unary(const UnaryExpr& u);
    Value emit_binary(const BinaryExpr& b);
    Value emit_logical(const BinaryExpr& b);
    Value emit_arith(TokenType op, const Value& l, const Value& r, int ln, int col);
    Value to_double(const Value& v);
    Value to_bool(const Value& v, int ln, int col);
    Value convert(const Value& v, const Type& target, int ln, int col);

    void flatten_pf(const Expr& e, std::vector<Piece>& out);
    void call_printf(const std::string& fmt, const std::string& arg_with_type);
    void emit_stmt(const Stmt& stmt);
    void emit_pf(const PfStmt& pf);
    void emit_store(const VarInfo& var, const Value& v);
    void emit_assign(const AssignStmt& as);
    void emit_incdec(const IncDecStmt& id);
    void emit_block(const Block& block);
    Value emit_cond(const Expr& e);
    void emit_if(const IfStmt& s);
    void emit_while(const WhileStmt& s);
    void emit_for(const ForStmt& s);
    void emit_switch(const SwitchStmt& s);
    Value emit_call(const CallExpr& c);
    Value call_rt(const std::string& fn, const std::string& ret_ty, const std::string& args, const Type& result);
    Value to_str(const Value& v, int ln, int col);
    Value str_concat(const Value& a, const Value& b);
    std::string to_bits(const Value& v);
    Value from_bits(const std::string& bits, const Type& elem);
    Value emit_array_literal(const ArrayLiteralExpr& a);
    Value emit_index(const IndexExpr& ix);
    Value emit_property(const PropertyExpr& p);
    Value emit_method(const MethodCallExpr& m);
    void emit_array_add(const ArrayAddStmt& s);
    void emit_array_remove(const ArrayRemoveStmt& s);
    void emit_foreach(const ForeachStmt& s);
    void emit_input(const InputStmt& s);
    void emit_sleep(const SleepStmt& s);
    void emit_exit(const ExitStmt& s);
    void emit_return(const ReturnStmt& r);
    void emit_function(const FuncInstance& inst);
    void reset_function_state();
    std::string assemble(const std::string& header, const std::string& default_ret);
};

} // namespace novac
