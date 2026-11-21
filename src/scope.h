// scope.h — Part 3a: symbol table infrastructure for semantic analysis.
//
// A Scope is a parent-linked symbol table: declaring a name only affects the
// current scope, but resolving a name walks up through parents (matching how
// a variable declared outside an if/while/for block is still visible and
// reassignable from inside it). Part 3a only populates a single global Scope
// with function symbols; Part 3b will create nested scopes per function body
// and reuse this same class for variables.
#pragma once

#include <string>
#include <unordered_map>
#include "ast.h" // for Mutability
#include "types.h"

namespace novac {

enum class SymbolKind { Variable, Function };

struct Symbol {
    std::string name;
    SymbolKind kind = SymbolKind::Variable;
    Mutability mutability = Mutability::Plain; // meaningful for Variable symbols (Part 3b)
    Type type;                                  // static type of a Variable (3c)
    int param_count = -1;                       // meaningful for Function symbols (arity); -1 = n/a
    int decl_line = 0;
    int decl_col = 0;
};

class Scope {
public:
    explicit Scope(Scope* parent = nullptr) : parent_(parent) {}

    // Declares a new symbol in THIS scope only. Returns false (and leaves
    // the existing symbol untouched) if the name is already declared here.
    bool declare(const Symbol& sym);

    // Looks up a name in this scope, then each parent in turn. Returns
    // nullptr if the name isn't declared anywhere in the chain.
    const Symbol* resolve(const std::string& name) const;

    // Looks up a name ONLY in this scope, with no parent walk. Used to tell
    // "already declared in THIS scope" (a real redeclaration) apart from
    // "declared in an outer scope" (Part 3b's declaration-vs-reassignment
    // rule needs exactly this distinction).
    const Symbol* resolve_local(const std::string& name) const;

    Scope* parent() const { return parent_; }

private:
    Scope* parent_;
    std::unordered_map<std::string, Symbol> symbols_;
};

} // namespace novac
