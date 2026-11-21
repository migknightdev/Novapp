#include "semantic.h"
#include "modules.h"

namespace novac {

namespace {

struct Builtin { const char* name; std::vector<TypeKind> params; Type ret; };

const Builtin* find_builtin(const std::string& name) {
    static const std::vector<Builtin> table = {
        {"random",     {TypeKind::Int, TypeKind::Int},       Type(TypeKind::Int)},
        {"read_file",  {TypeKind::String},                   Type(TypeKind::String)},
        {"write_file", {TypeKind::String, TypeKind::String}, Type(TypeKind::Bool)},
        {"to_string",  {TypeKind::Unknown},                  Type(TypeKind::String)},
    };
    for (const auto& b : table) if (name == b.name) return &b;
    return nullptr;
}

bool block_returns_value(const Block& b);

bool stmt_returns_value(const Stmt& s) {
    switch (s.kind) {
        case StmtKind::Return: return static_cast<const ReturnStmt&>(s).value != nullptr;
        case StmtKind::If: {
            auto& i = static_cast<const IfStmt&>(s);
            if (block_returns_value(i.then_body)) return true;
            for (auto& e : i.elifs) if (block_returns_value(e.body)) return true;
            return i.else_body && block_returns_value(*i.else_body);
        }
        case StmtKind::While: return block_returns_value(static_cast<const WhileStmt&>(s).body);
        case StmtKind::For: return block_returns_value(static_cast<const ForStmt&>(s).body);
        case StmtKind::Foreach: return block_returns_value(static_cast<const ForeachStmt&>(s).body);
        case StmtKind::Switch:
            for (auto& c : static_cast<const SwitchStmt&>(s).cases) if (block_returns_value(c.body)) return true;
            return false;
        default: return false; // nested FuncDecl intentionally not searched
    }
}

bool block_returns_value(const Block& b) {
    for (auto& s : b) if (stmt_returns_value(*s)) return true;
    return false;
}

} // namespace

void SemanticAnalyzer::add_error(const std::string& message, int line, int col) {
    // One error per source position: a generic body and its typed instances can hit the same problem.
    if (!seen_errors_.insert(current_file_ + ":" + std::to_string(line) + ":" + std::to_string(col)).second) return;
    errors_.push_back(SemaError{message, line, col, current_file_});
}

// ------------------------------ 3a ------------------------------

void SemanticAnalyzer::register_functions(const ProgramStmt& program) {
    for (const auto& stmt : program.body) {
        if (stmt->kind != StmtKind::FuncDecl) continue;
        const auto& fn = static_cast<const FuncDeclStmt&>(*stmt);
        current_file_ = fn.source_file;

        if (fn.name == "main") {
            add_error("'main' is a reserved function name — the compiler always generates its own entry point.",
                      fn.line, fn.col);
            continue;
        }

        Symbol sym;
        sym.name = fn.name;
        sym.kind = SymbolKind::Function;
        sym.param_count = static_cast<int>(fn.params.size());
        sym.decl_line = fn.line;
        sym.decl_col = fn.col;

        if (!globals_.declare(sym)) {
            const Symbol* prev = globals_.resolve_local(fn.name);
            add_error("Function '" + fn.name + "' is already declared (first declared at line " +
                      std::to_string(prev->decl_line) + ").", fn.line, fn.col);
        } else {
            returns_value_[fn.name] = block_returns_value(fn.body);
            funcs_[fn.name] = &fn;
        }
    }
    current_file_.clear();
}

// ------------------------------ 3b helpers ------------------------------

void SemanticAnalyzer::declare_var(Scope& scope, const std::string& name, Mutability mut, int line, int col, Type type) {
    Symbol sym;
    sym.name = name;
    sym.kind = SymbolKind::Variable;
    sym.mutability = mut;
    sym.type = type;
    sym.decl_line = line;
    sym.decl_col = col;
    if (!scope.declare(sym)) {
        const Symbol* prev = scope.resolve_local(name);
        add_error("'" + name + "' is already declared in this scope (line " +
                  std::to_string(prev->decl_line) + ").", line, col);
    }
}

void SemanticAnalyzer::check_not_const(const Symbol* sym, const std::string& name, int line, int col) {
    if (sym && sym->mutability == Mutability::Const) {
        add_error("Cannot modify '" + name + "': declared as const (line " +
                  std::to_string(sym->decl_line) + ").", line, col);
    }
}

const Symbol* SemanticAnalyzer::require_var(Scope& scope, const std::string& name, const char* what, int line, int col) {
    const Symbol* sym = scope.resolve(name);
    if (!sym) add_error("'" + name + "' is used as " + what + " but was never declared.", line, col);
    return sym;
}

// ------------------------------ 3c: expressions ------------------------------

Type SemanticAnalyzer::check_value(const Expr& e, Scope& scope) {
    Type t = check_expr(e, scope);
    if (t.kind == TypeKind::Void) {
        add_error("This call produces no value (the function never returns one).", e.line, e.col);
        return Type();
    }
    return t;
}

void SemanticAnalyzer::check_cond(const Expr& e, Scope& scope, const char* what) {
    Type t = check_value(e, scope);
    if (t.kind == TypeKind::String || t.kind == TypeKind::Array)
        add_error(std::string(what) + " must be a bool or a number, got " + type_name(t) + ".", e.line, e.col);
}

Type SemanticAnalyzer::check_expr(const Expr& e, Scope& scope) {
    Type t = check_expr_impl(e, scope);
    e.resolved_type = t; // annotate the node for codegen
    return t;
}

Type SemanticAnalyzer::check_expr_impl(const Expr& e, Scope& scope) {
    switch (e.kind) {
        case ExprKind::Number:
            return Type(static_cast<const NumberExpr&>(e).is_float ? TypeKind::Float : TypeKind::Int);
        case ExprKind::String: return Type(TypeKind::String);
        case ExprKind::Bool: return Type(TypeKind::Bool);

        case ExprKind::Identifier: {
            auto& id = static_cast<const IdentifierExpr&>(e);
            const Symbol* sym = scope.resolve(id.name);
            if (!sym) { add_error("'" + id.name + "' is not declared.", e.line, e.col); return Type(); }
            return sym->type;
        }

        case ExprKind::ArrayLiteral: {
            auto& arr = static_cast<const ArrayLiteralExpr&>(e);
            TypeKind elem = TypeKind::Unknown;
            for (auto& el : arr.elements) {
                Type t = check_value(*el, scope);
                if (t.unknown()) continue;
                if (t.kind == TypeKind::Array) {
                    add_error("Arrays of arrays are not supported.", el->line, el->col);
                    continue;
                }
                if (elem == TypeKind::Unknown) elem = t.kind;
                else if (elem != t.kind) {
                    if ((elem == TypeKind::Int || elem == TypeKind::Float) && t.numeric()) elem = TypeKind::Float;
                    else add_error(std::string("Array elements must all have the same type (found ") +
                                   kind_name(elem) + " and " + kind_name(t.kind) + ").", el->line, el->col);
                }
            }
            return Type(TypeKind::Array, elem);
        }

        case ExprKind::Index: {
            auto& ix = static_cast<const IndexExpr&>(e);
            Type at = check_value(*ix.array, scope);
            Type it = check_value(*ix.index, scope);
            if (!at.unknown() && at.kind != TypeKind::Array)
                add_error("Cannot index a value of type " + type_name(at) + ".", e.line, e.col);
            if (!it.unknown() && it.kind != TypeKind::Int)
                add_error("Array index must be an integer, got " + type_name(it) + ".", e.line, e.col);
            if (ix.index->kind == ExprKind::Number && static_cast<const NumberExpr&>(*ix.index).value < 1)
                add_error("Array indexes start at 1.", e.line, e.col);
            return at.kind == TypeKind::Array ? Type(at.elem) : Type();
        }

        case ExprKind::Property: {
            auto& pr = static_cast<const PropertyExpr&>(e);
            Type ot = check_value(*pr.object, scope);
            if (pr.name == "length") {
                if (!ot.unknown() && ot.kind != TypeKind::Array && ot.kind != TypeKind::String)
                    add_error(".length is only available on arrays and strings, got " + type_name(ot) + ".", e.line, e.col);
                return Type(TypeKind::Int);
            }
            add_error("Unknown property '." + pr.name + "'.", e.line, e.col);
            return Type();
        }

        case ExprKind::MethodCall: {
            auto& mc = static_cast<const MethodCallExpr&>(e);
            Type ot = check_value(*mc.object, scope);
            std::vector<Type> args;
            for (auto& a : mc.args) args.push_back(check_value(*a, scope));

            int arity = -1;
            Type ret;
            if (mc.method == "upper" || mc.method == "lower") { arity = 0; ret = Type(TypeKind::String); }
            else if (mc.method == "contains") { arity = 1; ret = Type(TypeKind::Bool); }
            else if (mc.method == "split") { arity = 1; ret = Type(TypeKind::Array, TypeKind::String); }
            else { add_error("Unknown method '." + mc.method + "()'.", e.line, e.col); return Type(); }

            if (!ot.unknown() && ot.kind != TypeKind::String)
                add_error("Method '." + mc.method + "()' is only available on strings, got " + type_name(ot) + ".", e.line, e.col);
            if (static_cast<int>(args.size()) != arity) {
                add_error("Method '." + mc.method + "()' expects " + std::to_string(arity) + " argument(s) but got " +
                          std::to_string(args.size()) + ".", e.line, e.col);
            } else {
                for (auto& a : args)
                    if (!a.unknown() && a.kind != TypeKind::String)
                        add_error("Argument of '." + mc.method + "()' must be a string, got " + type_name(a) + ".", e.line, e.col);
            }
            return ret;
        }

        case ExprKind::Unary: {
            auto& u = static_cast<const UnaryExpr&>(e);
            Type t = check_value(*u.operand, scope);
            if (u.op == TokenType::NOT) {
                if (t.kind == TypeKind::String || t.kind == TypeKind::Array)
                    add_error("Operator '!' cannot be applied to " + type_name(t) + ".", e.line, e.col);
                return Type(TypeKind::Bool);
            }
            if (!t.unknown() && !t.numeric())
                add_error("Unary '-' requires a number, got " + type_name(t) + ".", e.line, e.col);
            return t.numeric() ? t : Type();
        }

        case ExprKind::Binary: {
            auto& b = static_cast<const BinaryExpr&>(e);
            Type l = check_value(*b.left, scope);
            Type r = check_value(*b.right, scope);
            const std::string op = token_type_name(b.op);
            auto bad = [&]() {
                add_error("Operator '" + op + "' cannot be applied to " + type_name(l) + " and " + type_name(r) + ".",
                          b.line, b.col);
            };

            if (b.op == TokenType::PLUS && (l.kind == TypeKind::String || r.kind == TypeKind::String)) {
                if (l.kind == TypeKind::Array || r.kind == TypeKind::Array) bad();
                return Type(TypeKind::String); // string + anything printable concatenates
            }

            switch (b.op) {
                case TokenType::PLUS: case TokenType::MINUS: case TokenType::STAR:
                case TokenType::SLASH: case TokenType::PERCENT:
                    if ((!l.unknown() && !l.numeric()) || (!r.unknown() && !r.numeric())) { bad(); return Type(); }
                    if (b.op == TokenType::PERCENT && (l.kind == TypeKind::Float || r.kind == TypeKind::Float)) {
                        add_error("Operator '%' requires integer operands.", b.line, b.col);
                        return Type(TypeKind::Int);
                    }
                    if (l.unknown() || r.unknown()) return Type();
                    return Type(l.kind == TypeKind::Float || r.kind == TypeKind::Float ? TypeKind::Float : TypeKind::Int);

                case TokenType::LT: case TokenType::GT: case TokenType::LE: case TokenType::GE:
                    if (!l.unknown() && !r.unknown() &&
                        !((l.numeric() && r.numeric()) || (l.kind == TypeKind::String && r.kind == TypeKind::String)))
                        bad();
                    return Type(TypeKind::Bool);

                case TokenType::EQ: case TokenType::NEQ:
                    if (!l.unknown() && !r.unknown() && !assignable(l, r) && !assignable(r, l))
                        add_error("Cannot compare " + type_name(l) + " with " + type_name(r) + ".", b.line, b.col);
                    return Type(TypeKind::Bool);

                case TokenType::AND: case TokenType::OR:
                    if (l.kind == TypeKind::String || l.kind == TypeKind::Array ||
                        r.kind == TypeKind::String || r.kind == TypeKind::Array) bad();
                    return Type(TypeKind::Bool);

                default: return Type();
            }
        }

        case ExprKind::Call: {
            auto& c = static_cast<const CallExpr&>(e);
            std::vector<Type> args;
            for (auto& a : c.args) args.push_back(check_value(*a, scope));

            if (const Builtin* bi = find_builtin(c.callee)) {
                if (args.size() != bi->params.size()) {
                    add_error("Function '" + c.callee + "' expects " + std::to_string(bi->params.size()) +
                              " argument(s) but got " + std::to_string(args.size()) + ".", e.line, e.col);
                } else {
                    for (size_t i = 0; i < args.size(); ++i) {
                        TypeKind p = bi->params[i];
                        if (p != TypeKind::Unknown && !args[i].unknown() && !assignable(Type(p), args[i]))
                            add_error("Argument " + std::to_string(i + 1) + " of '" + c.callee + "' must be " +
                                      kind_name(p) + ", got " + type_name(args[i]) + ".", e.line, e.col);
                    }
                }
                return bi->ret;
            }

            const Symbol* fn = globals_.resolve_local(c.callee);
            if (!fn) {
                if (!imports_incomplete_) add_error("Unknown function '" + c.callee + "'.", e.line, e.col);
                return Type(); // may come from an import; resolved in 3d
            }
            if (static_cast<int>(args.size()) != fn->param_count) {
                add_error("Function '" + c.callee + "' expects " + std::to_string(fn->param_count) +
                          " argument(s) but got " + std::to_string(args.size()) + ".", e.line, e.col);
            } else {
                // Known argument types: type-check the body for exactly these types (an instance).
                bool all_known = true;
                for (auto& a : args) if (a.unknown() || a.kind == TypeKind::Void) all_known = false;
                if (all_known) {
                    if (const FuncInstance* inst = instantiate(c.callee, args, e.line, e.col)) {
                        c.instance = inst->key;
                        return inst->ret; // Void, the inferred type, or Unknown while a recursive call is pending
                    }
                    return Type();
                }
            }
            auto it = returns_value_.find(c.callee);
            return (it != returns_value_.end() && !it->second) ? Type(TypeKind::Void) : Type();
        }
    }
    return Type();
}

// ------------------------------ statements (3b scoping + 3c types) ------------------------------

void SemanticAnalyzer::walk_block(Block& block, Scope& scope) {
    for (auto& stmt : block) walk_stmt(*stmt, scope);
}

void SemanticAnalyzer::walk_stmt(Stmt& stmt, Scope& scope) {
    switch (stmt.kind) {
        case StmtKind::FuncDecl: {
            auto& fn = static_cast<FuncDeclStmt&>(stmt);
            FuncCtx saved_ctx = ctx_;
            std::string saved_file = current_file_;
            if (!fn.source_file.empty()) current_file_ = fn.source_file;
            int saved_loop = loop_depth_, saved_switch = switch_depth_;
            ctx_ = FuncCtx{};
            ctx_.in_func = true;
            ctx_.name = fn.name;
            loop_depth_ = switch_depth_ = 0;

            Scope fscope(nullptr); // functions can't see the caller's/main's variables
            for (const auto& param : fn.params) declare_var(fscope, param, Mutability::Plain, fn.line, fn.col);
            walk_block(fn.body, fscope);

            ctx_ = saved_ctx;
            current_file_ = saved_file;
            loop_depth_ = saved_loop;
            switch_depth_ = saved_switch;
            break;
        }
        case StmtKind::VarDecl: {
            auto& vd = static_cast<VarDeclStmt&>(stmt);
            Type t = check_value(*vd.value, scope); // value first: `let x = x + 1` must not see the new x
            declare_var(scope, vd.name, vd.mutability, vd.line, vd.col, t);
            break;
        }
        case StmtKind::Assign: {
            auto& as = static_cast<AssignStmt&>(stmt);
            Type vt = check_value(*as.value, scope);
            const Symbol* sym = scope.resolve(as.name);
            if (as.op == TokenType::ASSIGN) {
                if (!sym) {
                    as.is_declaration = true;
                    declare_var(scope, as.name, Mutability::Plain, as.line, as.col, vt);
                } else {
                    check_not_const(sym, as.name, as.line, as.col);
                    if (!assignable(sym->type, vt))
                        add_error("Cannot assign " + type_name(vt) + " to '" + as.name + "' of type " +
                                  type_name(sym->type) + ".", as.line, as.col);
                }
            } else {
                sym = require_var(scope, as.name, token_type_name(as.op), as.line, as.col);
                check_not_const(sym, as.name, as.line, as.col);
                if (sym) {
                    const Type& tt = sym->type;
                    const std::string op = token_type_name(as.op);
                    bool string_concat = as.op == TokenType::PLUS_ASSIGN && tt.kind == TypeKind::String;
                    if (string_concat) {
                        if (vt.kind == TypeKind::Array)
                            add_error("Operator '+=' cannot be applied to string and array.", as.line, as.col);
                    } else if ((!tt.unknown() && !tt.numeric()) || (!vt.unknown() && !vt.numeric())) {
                        add_error("Operator '" + op + "' cannot be applied to " + type_name(tt) + " and " +
                                  type_name(vt) + ".", as.line, as.col);
                    } else if (!assignable(tt, vt)) {
                        add_error("Cannot assign " + type_name(vt) + " to '" + as.name + "' of type " +
                                  type_name(tt) + ".", as.line, as.col);
                    }
                }
            }
            break;
        }
        case StmtKind::IncDec: {
            auto& id = static_cast<IncDecStmt&>(stmt);
            const Symbol* sym = require_var(scope, id.name, token_type_name(id.op), id.line, id.col);
            check_not_const(sym, id.name, id.line, id.col);
            if (sym && !sym->type.unknown() && !sym->type.numeric())
                add_error(std::string("'") + token_type_name(id.op) + "' requires a numeric variable, got " +
                          type_name(sym->type) + ".", id.line, id.col);
            break;
        }
        case StmtKind::If: {
            auto& iff = static_cast<IfStmt&>(stmt);
            check_cond(*iff.condition, scope, "'if' condition");
            { Scope s(&scope); walk_block(iff.then_body, s); }
            for (auto& el : iff.elifs) {
                check_cond(*el.condition, scope, "'elif' condition");
                Scope s(&scope);
                walk_block(el.body, s);
            }
            if (iff.else_body) { Scope s(&scope); walk_block(*iff.else_body, s); }
            break;
        }
        case StmtKind::While: {
            auto& w = static_cast<WhileStmt&>(stmt);
            check_cond(*w.condition, scope, "'while' condition");
            Scope s(&scope);
            loop_depth_++;
            walk_block(w.body, s);
            loop_depth_--;
            break;
        }
        case StmtKind::For: {
            auto& f = static_cast<ForStmt&>(stmt);
            Scope loop(&scope);
            if (f.init) walk_stmt(*f.init, loop);
            if (f.condition) check_cond(*f.condition, loop, "'for' condition");
            if (f.increment) walk_stmt(*f.increment, loop);
            Scope body(&loop);
            loop_depth_++;
            walk_block(f.body, body);
            loop_depth_--;
            break;
        }
        case StmtKind::Foreach: {
            auto& fe = static_cast<ForeachStmt&>(stmt);
            Type ct = check_value(*fe.collection, scope);
            if (!ct.unknown() && ct.kind != TypeKind::Array)
                add_error("'for ... in' needs an array, got " + type_name(ct) + ".", fe.line, fe.col);
            Scope loop(&scope);
            declare_var(loop, fe.item_name, Mutability::Plain, fe.line, fe.col,
                        ct.kind == TypeKind::Array ? Type(ct.elem) : Type());
            Scope body(&loop);
            loop_depth_++;
            walk_block(fe.body, body);
            loop_depth_--;
            break;
        }
        case StmtKind::Switch: {
            auto& sw = static_cast<SwitchStmt&>(stmt);
            Type st = check_value(*sw.subject, scope);
            switch_depth_++;
            for (auto& c : sw.cases) {
                if (c.value) {
                    Type ct = check_value(*c.value, scope);
                    if (!st.unknown() && !ct.unknown() && !assignable(st, ct) && !assignable(ct, st))
                        add_error("Case value of type " + type_name(ct) + " can't match a switch on " +
                                  type_name(st) + ".", c.value->line, c.value->col);
                }
                Scope s(&scope);
                walk_block(c.body, s);
            }
            switch_depth_--;
            break;
        }
        case StmtKind::Input: {
            auto& in = static_cast<InputStmt&>(stmt);
            const Symbol* sym = require_var(scope, in.target, "'input' target", in.line, in.col);
            check_not_const(sym, in.target, in.line, in.col);
            break;
        }
        case StmtKind::ArrayAdd: {
            auto& a = static_cast<ArrayAddStmt&>(stmt);
            const Symbol* sym = require_var(scope, a.array_name, "an array", a.line, a.col);
            Type vt = check_value(*a.value, scope);
            if (sym) {
                if (!sym->type.unknown() && sym->type.kind != TypeKind::Array)
                    add_error("'" + a.array_name + "' is not an array.", a.line, a.col);
                else if (sym->type.kind == TypeKind::Array && !assignable(Type(sym->type.elem), vt))
                    add_error("Cannot add " + type_name(vt) + " to an array of " + kind_name(sym->type.elem) + ".",
                              a.line, a.col);
            }
            break;
        }
        case StmtKind::ArrayRemove: {
            auto& a = static_cast<ArrayRemoveStmt&>(stmt);
            const Symbol* sym = require_var(scope, a.array_name, "an array", a.line, a.col);
            Type vt = check_value(*a.value_or_index, scope);
            if (sym) {
                if (!sym->type.unknown() && sym->type.kind != TypeKind::Array)
                    add_error("'" + a.array_name + "' is not an array.", a.line, a.col);
                else if (sym->type.kind == TypeKind::Array && !vt.unknown() && vt.kind != TypeKind::Int &&
                         !assignable(Type(sym->type.elem), vt))
                    add_error("Cannot remove " + type_name(vt) + " from an array of " + kind_name(sym->type.elem) +
                              " (use a value of that type, or an integer index).", a.line, a.col);
            }
            break;
        }
        case StmtKind::Return: {
            auto& r = static_cast<ReturnStmt&>(stmt);
            if (!ctx_.in_func) add_error("'return' outside of a function.", r.line, r.col);
            Type t;
            if (r.value) t = check_value(*r.value, scope);
            if (ctx_.in_func) {
                if (r.value) {
                    if (ctx_.has_bare_ret)
                        add_error("Function '" + ctx_.name + "' mixes 'return <value>' and a bare 'return'.", r.line, r.col);
                    ctx_.has_value_ret = true;
                    if (!t.unknown()) {
                        if (ctx_.ret.unknown()) ctx_.ret = t;
                        else if (!types_equal(ctx_.ret, t))
                            add_error("Function '" + ctx_.name + "' returns inconsistent types (" +
                                      type_name(ctx_.ret) + " and " + type_name(t) + ").", r.line, r.col);
                    }
                } else {
                    if (ctx_.has_value_ret)
                        add_error("Function '" + ctx_.name + "' mixes 'return <value>' and a bare 'return'.", r.line, r.col);
                    ctx_.has_bare_ret = true;
                }
            }
            break;
        }
        case StmtKind::Pf:
            check_value(*static_cast<PfStmt&>(stmt).value, scope);
            break;
        case StmtKind::Sleep: {
            auto& sl = static_cast<SleepStmt&>(stmt);
            Type t = check_value(*sl.milliseconds, scope);
            if (!t.unknown() && !t.numeric())
                add_error("sleep() needs a number of milliseconds, got " + type_name(t) + ".", sl.line, sl.col);
            break;
        }
        case StmtKind::Exit: {
            auto& ex = static_cast<ExitStmt&>(stmt);
            if (ex.code) {
                Type t = check_value(*ex.code, scope);
                if (!t.unknown() && t.kind != TypeKind::Int)
                    add_error("'exit' needs an integer code, got " + type_name(t) + ".", ex.line, ex.col);
            }
            break;
        }
        case StmtKind::Break:
            if (loop_depth_ == 0 && switch_depth_ == 0)
                add_error("'break' outside of a loop or switch.", stmt.line, stmt.col);
            break;
        case StmtKind::Continue:
            if (loop_depth_ == 0) add_error("'continue' outside of a loop.", stmt.line, stmt.col);
            break;
        case StmtKind::ExprStmt:
            check_expr(*static_cast<ExprStmt&>(stmt).expr, scope); // void calls are fine as statements
            break;
        default:
            break; // Import / Block / Program
    }
}

// ------------------------------ 4d: typed instances ------------------------------

const FuncInstance* SemanticAnalyzer::find_instance(const std::string& key) const {
    auto it = instances_.find(key);
    return it != instances_.end() && it->second.done ? &it->second : nullptr;
}

Type SemanticAnalyzer::analyze_instance(FuncDeclStmt& fn, const std::vector<Type>& args) {
    FuncCtx saved_ctx = ctx_;
    int saved_loop = loop_depth_, saved_switch = switch_depth_;
    std::string saved_file = current_file_;
    ctx_ = FuncCtx{};
    ctx_.in_func = true;
    ctx_.name = fn.name;
    loop_depth_ = switch_depth_ = 0;
    current_file_ = fn.source_file;

    Scope fscope(nullptr);
    for (size_t i = 0; i < fn.params.size(); ++i)
        declare_var(fscope, fn.params[i], Mutability::Plain, fn.line, fn.col, args[i]);
    walk_block(fn.body, fscope);
    Type ret = ctx_.has_value_ret ? ctx_.ret : Type(TypeKind::Void);

    ctx_ = saved_ctx;
    loop_depth_ = saved_loop;
    switch_depth_ = saved_switch;
    current_file_ = saved_file;
    return ret;
}

// Self-recursion: pass 1 runs with the return type still unknown (the recursive
// call is "unknown"); if the function called itself, pass 2 re-checks the body
// with the return type found in pass 1. Mutual recursion is not supported yet.
const FuncInstance* SemanticAnalyzer::instantiate(const std::string& name, const std::vector<Type>& args, int line, int col) {
    auto fit = funcs_.find(name);
    if (fit == funcs_.end()) return nullptr;

    std::string key = name + "(";
    for (size_t i = 0; i < args.size(); ++i) key += (i ? "," : "") + type_name(args[i]);
    key += ")";

    auto it = instances_.find(key);
    if (it != instances_.end()) {
        if (it->second.done) return &it->second;
        if (stack_.back() != key) {
            add_error("Mutual recursion is not supported yet ('" + name + "' is called while it is still being analyzed).", line, col);
            return nullptr;
        }
        it->second.recursed = true;
        return &it->second; // `ret` holds the pending return type
    }
    if (stack_.size() > 64) {
        add_error("Too many nested function instantiations while analyzing '" + key + "'.", line, col);
        return nullptr;
    }

    FuncInstance& inst = instances_[key];
    inst.key = key;
    inst.params = args;
    stack_.push_back(key);

    inst.decl = clone_func(*fit->second);
    Type ret = analyze_instance(*inst.decl, args);
    if (inst.recursed) {
        if (ret.unknown()) {
            add_error("Cannot infer the return type of '" + key + "': every return depends on the recursive call.", line, col);
        } else {
            inst.ret = ret; // recursive call sites now see the real type
            inst.decl = clone_func(*fit->second);
            Type again = analyze_instance(*inst.decl, args);
            if (!again.unknown()) ret = again;
        }
    }
    inst.ret = ret;
    inst.done = true;
    stack_.pop_back();
    return &inst;
}

void SemanticAnalyzer::analyze(ProgramStmt& program, const std::string& base_dir) {
    if (!program.imports.empty()) {
        ModuleLoader loader;
        loader.load(program.imports, base_dir);
        for (const auto& e : loader.errors()) errors_.push_back(SemaError{e.message, e.line, e.col, e.file});
        imports_incomplete_ = loader.had_failures();
        imported_total_ = loader.pool_size();
        // Tree-shaking: only the library functions this program (transitively) calls are compiled in.
        auto taken = loader.take_reachable(program);
        imported_used_ = taken.size();
        for (auto& s : taken) program.body.push_back(std::move(s));
    }
    register_functions(program);
    Scope main_scope(nullptr);
    walk_block(program.body, main_scope);
}

} // namespace novac
