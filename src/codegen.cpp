#include "codegen.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace novac {

namespace {

std::string ir_escape(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (c >= 32 && c <= 126 && c != '"' && c != '\\') {
            out += static_cast<char>(c);
        } else {
            out += '\\';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

std::string double_const(double d) {
    uint64_t bits;
    std::memcpy(&bits, &d, sizeof bits);
    char buf[32];
    std::snprintf(buf, sizeof buf, "0x%016llX", static_cast<unsigned long long>(bits));
    return buf;
}

const char* llvm_type(const Type& t) {
    switch (t.kind) {
        case TypeKind::Int: return "i64";
        case TypeKind::Float: return "double";
        case TypeKind::Bool: return "i1";
        case TypeKind::String: case TypeKind::Array: return "ptr";
        default: return "void";
    }
}

const char* unsupported_stmt(StmtKind k) {
    switch (k) {
        default:
            return "this statement";
    }
}

const char* unsupported_expr(ExprKind) { return "this expression"; }

bool is_builtin(const std::string& n) {
    return n == "random" || n == "read_file" || n == "write_file" || n == "to_string";
}

std::string default_return(const Type& t) {
    switch (t.kind) {
        case TypeKind::Int: return "ret i64 0";
        case TypeKind::Float: return "ret double " + double_const(0.0);
        case TypeKind::Bool: return "ret i1 false";
        case TypeKind::String: case TypeKind::Array: return "ret ptr null";
        default: return "ret void";
    }
}

TokenType compound_to_binary(TokenType op) {
    switch (op) {
        case TokenType::PLUS_ASSIGN: return TokenType::PLUS;
        case TokenType::MINUS_ASSIGN: return TokenType::MINUS;
        case TokenType::STAR_ASSIGN: return TokenType::STAR;
        default: return TokenType::SLASH;
    }
}

} // namespace

// ------------------------------ plumbing ------------------------------

void CodeGen::error(const std::string& message, int ln, int col) {
    errors_.push_back(CodegenError{message, ln, col, current_file_});
}

std::string CodeGen::intern_string(const std::string& text) {
    auto it = strings_.find(text);
    if (it != strings_.end()) return it->second;
    std::string name = "@.str." + std::to_string(strings_.size());
    globals_ += name + " = private unnamed_addr constant [" + std::to_string(text.size() + 1) +
                " x i8] c\"" + ir_escape(text) + "\\00\", align 1\n";
    strings_[text] = name;
    return name;
}

std::string CodeGen::tmp() { return "%t" + std::to_string(next_tmp_++); }
void CodeGen::line(const std::string& text) { body_ += "  " + text + "\n"; }
std::string CodeGen::new_label(const std::string& base) { return base + "." + std::to_string(next_label_++); }

void CodeGen::emit_label(const std::string& label) {
    body_ += label + ":\n";
    cur_block_ = label;
}

CodeGen::VarInfo CodeGen::declare(const std::string& name, const Type& type) {
    VarInfo v{"%v." + name + "." + std::to_string(next_slot_++), type};
    allocas_ += "  " + v.slot + " = alloca " + llvm_type(type) + "\n";
    scopes_.back()[name] = v;
    return v;
}

const CodeGen::VarInfo* CodeGen::find_var(const std::string& name) const {
    for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
        auto f = it->find(name);
        if (f != it->end()) return &f->second;
    }
    return nullptr;
}

// ------------------------------ values & conversions ------------------------------

CodeGen::Value CodeGen::to_double(const Value& v) {
    if (v.type.kind != TypeKind::Int) return v;
    std::string t = tmp();
    line(t + " = sitofp i64 " + v.repr + " to double");
    return Value{t, Type(TypeKind::Float), true};
}

CodeGen::Value CodeGen::to_bool(const Value& v, int ln, int col) {
    std::string t;
    switch (v.type.kind) {
        case TypeKind::Bool: return v;
        case TypeKind::Int:
            t = tmp();
            line(t + " = icmp ne i64 " + v.repr + ", 0");
            return Value{t, Type(TypeKind::Bool), true};
        case TypeKind::Float:
            t = tmp();
            line(t + " = fcmp une double " + v.repr + ", " + double_const(0.0));
            return Value{t, Type(TypeKind::Bool), true};
        default:
            error("A value of type " + type_name(v.type) + " cannot be used as a condition here.", ln, col);
            return Value{};
    }
}

CodeGen::Value CodeGen::convert(const Value& v, const Type& target, int ln, int col) {
    if (types_equal(v.type, target)) return v;
    if (target.kind == TypeKind::Float && v.type.kind == TypeKind::Int) return to_double(v);
    error("Cannot convert " + type_name(v.type) + " to " + type_name(target) + ".", ln, col);
    return Value{};
}

CodeGen::Value CodeGen::emit_arith(TokenType op, const Value& l, const Value& r, int ln, int col) {
    if (!l.type.numeric() || !r.type.numeric()) {
        error("Arithmetic needs numbers (found " + type_name(l.type) + " and " + type_name(r.type) + ").", ln, col);
        return Value{};
    }
    bool is_float = l.type.kind == TypeKind::Float || r.type.kind == TypeKind::Float;
    Value a = l, b = r;
    if (is_float) { a = to_double(l); b = to_double(r); }

    const char* inst = nullptr;
    switch (op) {
        case TokenType::PLUS: inst = is_float ? "fadd" : "add"; break;
        case TokenType::MINUS: inst = is_float ? "fsub" : "sub"; break;
        case TokenType::STAR: inst = is_float ? "fmul" : "mul"; break;
        case TokenType::SLASH: inst = is_float ? "fdiv" : "sdiv"; break;
        case TokenType::PERCENT:
            if (is_float) { error("'%' needs integer operands.", ln, col); return Value{}; }
            inst = "srem";
            break;
        default: return Value{};
    }
    std::string t = tmp();
    Type result(is_float ? TypeKind::Float : TypeKind::Int);
    line(t + " = " + inst + " " + llvm_type(result) + " " + a.repr + ", " + b.repr);
    return Value{t, result, true};
}

// ------------------------------ expressions ------------------------------

CodeGen::Value CodeGen::emit_expr(const Expr& e) {
    switch (e.kind) {
        case ExprKind::Number: {
            auto& n = static_cast<const NumberExpr&>(e);
            if (n.is_float) return Value{double_const(n.value), Type(TypeKind::Float), true};
            return Value{std::to_string(static_cast<long long>(n.value)), Type(TypeKind::Int), true};
        }
        case ExprKind::String:
            return Value{intern_string(static_cast<const StringExpr&>(e).value), Type(TypeKind::String), true};
        case ExprKind::Bool:
            return Value{static_cast<const BoolExpr&>(e).value ? "true" : "false", Type(TypeKind::Bool), true};
        case ExprKind::Identifier: {
            auto& id = static_cast<const IdentifierExpr&>(e);
            const VarInfo* v = find_var(id.name);
            if (!v) { error("Internal error: '" + id.name + "' has no storage.", e.line, e.col); return Value{}; }
            std::string t = tmp();
            line(t + " = load " + llvm_type(v->type) + ", ptr " + v->slot);
            return Value{t, v->type, true};
        }
        case ExprKind::Unary: return emit_unary(static_cast<const UnaryExpr&>(e));
        case ExprKind::Binary: return emit_binary(static_cast<const BinaryExpr&>(e));
        case ExprKind::Call: return emit_call(static_cast<const CallExpr&>(e));
        case ExprKind::ArrayLiteral: return emit_array_literal(static_cast<const ArrayLiteralExpr&>(e));
        case ExprKind::Index: return emit_index(static_cast<const IndexExpr&>(e));
        case ExprKind::Property: return emit_property(static_cast<const PropertyExpr&>(e));
        case ExprKind::MethodCall: return emit_method(static_cast<const MethodCallExpr&>(e));
        default:
            error(std::string("The code generator does not support ") + unsupported_expr(e.kind) + ".", e.line, e.col);
            return Value{};
    }
}

CodeGen::Value CodeGen::emit_unary(const UnaryExpr& u) {
    Value v = emit_expr(*u.operand);
    if (!v.valid) return Value{};
    if (u.op == TokenType::NOT) {
        Value b = to_bool(v, u.line, u.col);
        if (!b.valid) return Value{};
        std::string t = tmp();
        line(t + " = xor i1 " + b.repr + ", true");
        return Value{t, Type(TypeKind::Bool), true};
    }
    std::string t = tmp();
    if (v.type.kind == TypeKind::Int) {
        line(t + " = sub i64 0, " + v.repr);
        return Value{t, v.type, true};
    }
    if (v.type.kind == TypeKind::Float) {
        line(t + " = fneg double " + v.repr);
        return Value{t, v.type, true};
    }
    error("Unary '-' needs a number, got " + type_name(v.type) + ".", u.line, u.col);
    return Value{};
}

// && and || short-circuit: the right side only runs when it can change the result
// (so `b != 0 && a / b > 1` never divides by zero).
CodeGen::Value CodeGen::emit_logical(const BinaryExpr& b) {
    bool is_and = b.op == TokenType::AND;
    Value l = emit_expr(*b.left);
    if (!l.valid) return Value{};
    Value lb = to_bool(l, b.line, b.col);
    if (!lb.valid) return Value{};

    std::string lhs_block = cur_block_;
    std::string rhs_label = new_label("logic.rhs");
    std::string end_label = new_label("logic.end");
    if (is_and) line("br i1 " + lb.repr + ", label %" + rhs_label + ", label %" + end_label);
    else line("br i1 " + lb.repr + ", label %" + end_label + ", label %" + rhs_label);

    emit_label(rhs_label);
    Value r = emit_expr(*b.right);
    if (!r.valid) return Value{};
    Value rb = to_bool(r, b.line, b.col);
    if (!rb.valid) return Value{};
    std::string rhs_end = cur_block_;
    line("br label %" + end_label);

    emit_label(end_label);
    std::string t = tmp();
    line(t + " = phi i1 [ " + (is_and ? "false" : "true") + ", %" + lhs_block + " ], [ " + rb.repr + ", %" + rhs_end + " ]");
    return Value{t, Type(TypeKind::Bool), true};
}

CodeGen::Value CodeGen::emit_binary(const BinaryExpr& b) {
    if (b.op == TokenType::AND || b.op == TokenType::OR) return emit_logical(b);

    Value l = emit_expr(*b.left);
    Value r = emit_expr(*b.right);
    if (!l.valid || !r.valid) return Value{};

    if (b.op == TokenType::PLUS && (l.type.kind == TypeKind::String || r.type.kind == TypeKind::String)) {
        Value ls = to_str(l, b.line, b.col), rs = to_str(r, b.line, b.col);
        if (!ls.valid || !rs.valid) return Value{};
        return str_concat(ls, rs);
    }

    switch (b.op) {
        case TokenType::PLUS: case TokenType::MINUS: case TokenType::STAR:
        case TokenType::SLASH: case TokenType::PERCENT:
            return emit_arith(b.op, l, r, b.line, b.col);

        case TokenType::LT: case TokenType::GT: case TokenType::LE: case TokenType::GE:
        case TokenType::EQ: case TokenType::NEQ: {
            bool both_bool = l.type.kind == TypeKind::Bool && r.type.kind == TypeKind::Bool;
            bool both_str = l.type.kind == TypeKind::String && r.type.kind == TypeKind::String;
            if (!both_bool && !both_str && (!l.type.numeric() || !r.type.numeric())) {
                error("Cannot compare " + type_name(l.type) + " with " + type_name(r.type) + ".", b.line, b.col);
                return Value{};
            }
            if (both_str) { // compare through strcmp-style ordering, then against 0
                Value c = call_rt("nova_str_cmp", "i32", "ptr " + l.repr + ", ptr " + r.repr, Type());
                const char* pred = "eq";
                switch (b.op) {
                    case TokenType::LT: pred = "slt"; break;
                    case TokenType::GT: pred = "sgt"; break;
                    case TokenType::LE: pred = "sle"; break;
                    case TokenType::GE: pred = "sge"; break;
                    case TokenType::NEQ: pred = "ne"; break;
                    default: break;
                }
                std::string t = tmp();
                line(t + " = icmp " + pred + " i32 " + c.repr + ", 0");
                return Value{t, Type(TypeKind::Bool), true};
            }
            bool is_float = l.type.kind == TypeKind::Float || r.type.kind == TypeKind::Float;
            Value x = l, y = r;
            if (is_float) { x = to_double(l); y = to_double(r); }
            const char* ipred = "eq";
            const char* fpred = "oeq";
            switch (b.op) {
                case TokenType::LT: ipred = "slt"; fpred = "olt"; break;
                case TokenType::GT: ipred = "sgt"; fpred = "ogt"; break;
                case TokenType::LE: ipred = "sle"; fpred = "ole"; break;
                case TokenType::GE: ipred = "sge"; fpred = "oge"; break;
                case TokenType::NEQ: ipred = "ne"; fpred = "une"; break;
                default: break;
            }
            std::string t = tmp();
            if (is_float) line(t + " = fcmp " + fpred + " double " + x.repr + ", " + y.repr);
            else line(t + " = icmp " + ipred + " " + llvm_type(x.type) + " " + x.repr + ", " + y.repr);
            return Value{t, Type(TypeKind::Bool), true};
        }
        default:
            error("Unsupported operator.", b.line, b.col);
            return Value{};
    }
}

// ------------------------------ pf ------------------------------

// pf prints a `+` chain piece by piece. A chain is "concatenation" exactly when
// semantic analysis typed it as string; a numeric chain like `1 + 2` stays arithmetic.
void CodeGen::flatten_pf(const Expr& e, std::vector<Piece>& out) {
    if (e.kind == ExprKind::String) {
        out.push_back(Piece{static_cast<const StringExpr&>(e).value, nullptr});
        return;
    }
    if (e.kind == ExprKind::Bool) {
        out.push_back(Piece{static_cast<const BoolExpr&>(e).value ? "true" : "false", nullptr});
        return;
    }
    if (e.kind == ExprKind::Number && !static_cast<const NumberExpr&>(e).is_float) {
        out.push_back(Piece{std::to_string(static_cast<long long>(static_cast<const NumberExpr&>(e).value)), nullptr});
        return;
    }
    if (e.kind == ExprKind::Binary) {
        auto& b = static_cast<const BinaryExpr&>(e);
        if (b.op == TokenType::PLUS && b.resolved_type.kind == TypeKind::String) {
            flatten_pf(*b.left, out);
            flatten_pf(*b.right, out);
            return;
        }
    }
    out.push_back(Piece{"", &e});
}

void CodeGen::call_printf(const std::string& fmt, const std::string& arg_with_type) {
    line(tmp() + " = call i32 (ptr, ...) @printf(ptr " + fmt + (arg_with_type.empty() ? "" : ", " + arg_with_type) + ")");
}

void CodeGen::emit_pf(const PfStmt& pf) {
    std::vector<Piece> raw;
    flatten_pf(*pf.value, raw);

    std::vector<Piece> pieces; // adjacent literals folded into one
    for (auto& p : raw) {
        if (!p.expr && !pieces.empty() && !pieces.back().expr) pieces.back().text += p.text;
        else pieces.push_back(p);
    }
    if (pieces.empty() || pieces.back().expr) pieces.push_back(Piece{"\n", nullptr});
    else pieces.back().text += "\n";

    for (auto& p : pieces) {
        if (!p.expr) {
            call_printf(intern_string("%s"), "ptr " + intern_string(p.text));
            continue;
        }
        Value v = emit_expr(*p.expr);
        if (!v.valid) continue;
        switch (v.type.kind) {
            case TypeKind::String: call_printf(intern_string("%s"), "ptr " + v.repr); break;
            case TypeKind::Int: call_printf(intern_string("%lld"), "i64 " + v.repr); break;
            case TypeKind::Float: call_printf(intern_string("%g"), "double " + v.repr); break;
            case TypeKind::Bool: {
                std::string s = tmp();
                line(s + " = select i1 " + v.repr + ", ptr " + intern_string("true") + ", ptr " + intern_string("false"));
                call_printf(intern_string("%s"), "ptr " + s);
                break;
            }
            default:
                error("'pf' cannot print a value of type " + type_name(v.type) + ".", p.expr->line, p.expr->col);
        }
    }
}

// ------------------------------ statements ------------------------------

void CodeGen::emit_store(const VarInfo& var, const Value& v) {
    line(std::string("store ") + llvm_type(var.type) + " " + v.repr + ", ptr " + var.slot);
}

void CodeGen::emit_assign(const AssignStmt& as) {
    Value rhs = emit_expr(*as.value);
    if (!rhs.valid) return;

    if (as.is_declaration) {
        if (rhs.type.unknown() || rhs.type.kind == TypeKind::Void) {
            error("Could not determine the type of '" + as.name + "'.", as.line, as.col);
            return;
        }
        emit_store(declare(as.name, rhs.type), rhs);
        return;
    }

    const VarInfo* var = find_var(as.name);
    if (!var) { error("Internal error: '" + as.name + "' has no storage.", as.line, as.col); return; }

    Value result = rhs;
    if (as.op != TokenType::ASSIGN) {
        std::string t = tmp();
        line(t + " = load " + llvm_type(var->type) + ", ptr " + var->slot);
        Value cur{t, var->type, true};
        if (as.op == TokenType::PLUS_ASSIGN && var->type.kind == TypeKind::String) {
            Value rs = to_str(rhs, as.line, as.col);
            if (!rs.valid) return;
            result = str_concat(cur, rs);
        } else {
            result = emit_arith(compound_to_binary(as.op), cur, rhs, as.line, as.col);
        }
        if (!result.valid) return;
    }
    Value stored = convert(result, var->type, as.line, as.col);
    if (stored.valid) emit_store(*var, stored);
}

void CodeGen::emit_incdec(const IncDecStmt& id) {
    const VarInfo* var = find_var(id.name);
    if (!var) { error("Internal error: '" + id.name + "' has no storage.", id.line, id.col); return; }
    std::string t = tmp();
    line(t + " = load " + llvm_type(var->type) + ", ptr " + var->slot);
    Value cur{t, var->type, true};
    Value one{var->type.kind == TypeKind::Float ? double_const(1.0) : std::string("1"), var->type, true};
    Value result = emit_arith(id.op == TokenType::PLUS_PLUS ? TokenType::PLUS : TokenType::MINUS, cur, one, id.line, id.col);
    if (result.valid) emit_store(*var, result);
}

// ------------------------------ control flow (4c) ------------------------------
// Invariant: every `br` is immediately followed by a new label, so no block ever
// has instructions after its terminator (break/continue open a "dead" block).

void CodeGen::emit_block(const Block& block) {
    scopes_.emplace_back();
    for (const auto& s : block) emit_stmt(*s);
    scopes_.pop_back();
}

CodeGen::Value CodeGen::emit_cond(const Expr& e) {
    Value v = emit_expr(e);
    if (!v.valid) return Value{};
    return to_bool(v, e.line, e.col);
}

void CodeGen::emit_if(const IfStmt& s) {
    std::string end_l = new_label("if.end");
    auto branch = [&](const Expr& cond, const Block& body) {
        Value c = emit_cond(cond);
        std::string then_l = new_label("if.then"), else_l = new_label("if.else");
        if (c.valid) line("br i1 " + c.repr + ", label %" + then_l + ", label %" + else_l);
        else line("br label %" + else_l); // error already recorded; keep the IR well-formed
        emit_label(then_l);
        emit_block(body);
        line("br label %" + end_l);
        emit_label(else_l);
    };
    branch(*s.condition, s.then_body);
    for (const auto& e : s.elifs) branch(*e.condition, e.body);
    if (s.else_body) emit_block(*s.else_body);
    line("br label %" + end_l);
    emit_label(end_l);
}

void CodeGen::emit_while(const WhileStmt& s) {
    std::string cond_l = new_label("while.cond"), body_l = new_label("while.body"), end_l = new_label("while.end");
    line("br label %" + cond_l);
    emit_label(cond_l);
    Value c = emit_cond(*s.condition);
    if (c.valid) line("br i1 " + c.repr + ", label %" + body_l + ", label %" + end_l);
    else line("br label %" + end_l);
    emit_label(body_l);
    loops_.push_back(LoopCtx{end_l, cond_l});
    emit_block(s.body);
    loops_.pop_back();
    line("br label %" + cond_l);
    emit_label(end_l);
}

void CodeGen::emit_for(const ForStmt& s) {
    scopes_.emplace_back(); // loop scope: holds the init variable
    if (s.init) emit_stmt(*s.init);
    std::string cond_l = new_label("for.cond"), body_l = new_label("for.body");
    std::string inc_l = new_label("for.inc"), end_l = new_label("for.end");
    line("br label %" + cond_l);
    emit_label(cond_l);
    if (s.condition) {
        Value c = emit_cond(*s.condition);
        if (c.valid) line("br i1 " + c.repr + ", label %" + body_l + ", label %" + end_l);
        else line("br label %" + end_l);
    } else {
        line("br label %" + body_l);
    }
    emit_label(body_l);
    loops_.push_back(LoopCtx{end_l, inc_l}); // `continue` runs the increment first
    emit_block(s.body);
    loops_.pop_back();
    line("br label %" + inc_l);
    emit_label(inc_l);
    if (s.increment) emit_stmt(*s.increment);
    line("br label %" + cond_l);
    emit_label(end_l);
    scopes_.pop_back();
}

// switch: cases are tested in order; there is NO fallthrough (each case body
// ends by leaving the switch); `default` runs when nothing matched.
void CodeGen::emit_switch(const SwitchStmt& s) {
    Value subj = emit_expr(*s.subject);
    if (!subj.valid) return;
    if (subj.type.kind != TypeKind::Int && subj.type.kind != TypeKind::Bool && subj.type.kind != TypeKind::String) {
        error("'switch' supports int, bool and string subjects, got " + type_name(subj.type) + ".", s.line, s.col);
        return;
    }
    std::string end_l = new_label("sw.end");
    std::vector<std::string> labels;
    int default_idx = -1;
    for (size_t i = 0; i < s.cases.size(); ++i) {
        labels.push_back(new_label(s.cases[i].value ? "sw.case" : "sw.default"));
        if (!s.cases[i].value) default_idx = static_cast<int>(i);
    }

    for (size_t i = 0; i < s.cases.size(); ++i) {
        if (!s.cases[i].value) continue;
        Value cv = emit_expr(*s.cases[i].value);
        if (!cv.valid) return;
        if (cv.type.kind != subj.type.kind) {
            error("Case value of type " + type_name(cv.type) + " does not match a switch on " + type_name(subj.type) + ".",
                  s.cases[i].value->line, s.cases[i].value->col);
            return;
        }
        std::string c = tmp();
        if (subj.type.kind == TypeKind::String) {
            Value cmp = call_rt("nova_str_cmp", "i32", "ptr " + subj.repr + ", ptr " + cv.repr, Type());
            line(c + " = icmp eq i32 " + cmp.repr + ", 0");
        } else {
            line(c + " = icmp eq " + llvm_type(subj.type) + " " + subj.repr + ", " + cv.repr);
        }
        std::string next = new_label("sw.test");
        line("br i1 " + c + ", label %" + labels[i] + ", label %" + next);
        emit_label(next);
    }
    line("br label %" + (default_idx >= 0 ? labels[default_idx] : end_l));

    loops_.push_back(LoopCtx{end_l, ""});
    for (size_t i = 0; i < s.cases.size(); ++i) {
        emit_label(labels[i]);
        emit_block(s.cases[i].body);
        line("br label %" + end_l);
    }
    loops_.pop_back();
    emit_label(end_l);
}

// ------------------------------ strings & arrays (4e) ------------------------------

CodeGen::Value CodeGen::call_rt(const std::string& fn, const std::string& ret_ty, const std::string& args, const Type& result) {
    if (ret_ty == "void") {
        line("call void @" + fn + "(" + args + ")");
        return Value{"", Type(TypeKind::Void), true};
    }
    std::string t = tmp();
    line(t + " = call " + ret_ty + " @" + fn + "(" + args + ")");
    return Value{t, result, true};
}

CodeGen::Value CodeGen::to_str(const Value& v, int ln, int col) {
    switch (v.type.kind) {
        case TypeKind::String: return v;
        case TypeKind::Int: return call_rt("nova_int_to_str", "ptr", "i64 " + v.repr, Type(TypeKind::String));
        case TypeKind::Float: return call_rt("nova_float_to_str", "ptr", "double " + v.repr, Type(TypeKind::String));
        case TypeKind::Bool: {
            std::string z = tmp();
            line(z + " = zext i1 " + v.repr + " to i32");
            return call_rt("nova_bool_to_str", "ptr", "i32 " + z, Type(TypeKind::String));
        }
        default:
            error("A value of type " + type_name(v.type) + " cannot be converted to text.", ln, col);
            return Value{};
    }
}

CodeGen::Value CodeGen::str_concat(const Value& a, const Value& b) {
    return call_rt("nova_str_concat", "ptr", "ptr " + a.repr + ", ptr " + b.repr, Type(TypeKind::String));
}

// Array slots are 8 bytes: ints as-is, floats bit-cast, bools zero-extended, strings as pointers.
std::string CodeGen::to_bits(const Value& v) {
    std::string t;
    switch (v.type.kind) {
        case TypeKind::Int: return v.repr;
        case TypeKind::Float: t = tmp(); line(t + " = bitcast double " + v.repr + " to i64"); return t;
        case TypeKind::Bool: t = tmp(); line(t + " = zext i1 " + v.repr + " to i64"); return t;
        default: t = tmp(); line(t + " = ptrtoint ptr " + v.repr + " to i64"); return t;
    }
}

CodeGen::Value CodeGen::from_bits(const std::string& bits, const Type& elem) {
    std::string t;
    switch (elem.kind) {
        case TypeKind::Int: return Value{bits, elem, true};
        case TypeKind::Float: t = tmp(); line(t + " = bitcast i64 " + bits + " to double"); break;
        case TypeKind::Bool: t = tmp(); line(t + " = trunc i64 " + bits + " to i1"); break;
        default: t = tmp(); line(t + " = inttoptr i64 " + bits + " to ptr"); break;
    }
    return Value{t, elem, true};
}

CodeGen::Value CodeGen::emit_array_literal(const ArrayLiteralExpr& a) {
    Type at = a.resolved_type;
    if (at.kind != TypeKind::Array || at.elem == TypeKind::Unknown) {
        error("Cannot infer the element type of this array (empty array literals are not supported yet).", a.line, a.col);
        return Value{};
    }
    Type elem(at.elem);
    Value arr = call_rt("nova_arr_new", "ptr", "", at);
    for (const auto& el : a.elements) {
        Value v = emit_expr(*el);
        if (!v.valid) return Value{};
        Value c = convert(v, elem, el->line, el->col);
        if (!c.valid) return Value{};
        call_rt("nova_arr_push", "void", "ptr " + arr.repr + ", i64 " + to_bits(c), Type());
    }
    return arr;
}

CodeGen::Value CodeGen::emit_index(const IndexExpr& ix) {
    Value arr = emit_expr(*ix.array);
    Value idx = emit_expr(*ix.index);
    if (!arr.valid || !idx.valid) return Value{};
    if (arr.type.kind != TypeKind::Array || arr.type.elem == TypeKind::Unknown || idx.type.kind != TypeKind::Int) {
        error("Cannot index " + type_name(arr.type) + " with " + type_name(idx.type) + ".", ix.line, ix.col);
        return Value{};
    }
    Value bits = call_rt("nova_arr_get", "i64", "ptr " + arr.repr + ", i64 " + idx.repr + ", i32 " + std::to_string(ix.line), Type(TypeKind::Int));
    return from_bits(bits.repr, Type(arr.type.elem));
}

CodeGen::Value CodeGen::emit_property(const PropertyExpr& p) {
    Value o = emit_expr(*p.object);
    if (!o.valid) return Value{};
    if (p.name == "length" && o.type.kind == TypeKind::Array)
        return call_rt("nova_arr_len", "i64", "ptr " + o.repr, Type(TypeKind::Int));
    if (p.name == "length" && o.type.kind == TypeKind::String)
        return call_rt("nova_str_len", "i64", "ptr " + o.repr, Type(TypeKind::Int));
    error("Unsupported property '." + p.name + "' on " + type_name(o.type) + ".", p.line, p.col);
    return Value{};
}

CodeGen::Value CodeGen::emit_method(const MethodCallExpr& m) {
    Value o = emit_expr(*m.object);
    if (!o.valid) return Value{};
    std::vector<Value> args;
    for (const auto& a : m.args) {
        Value v = emit_expr(*a);
        if (!v.valid) return Value{};
        args.push_back(v);
    }
    bool ok = o.type.kind == TypeKind::String;
    for (auto& a : args) ok = ok && a.type.kind == TypeKind::String;
    if (!ok) {
        error("Method '." + m.method + "()' needs a string receiver and string arguments.", m.line, m.col);
        return Value{};
    }
    std::string recv = "ptr " + o.repr;
    if (m.method == "upper" || m.method == "lower")
        return call_rt("nova_str_" + m.method, "ptr", recv, Type(TypeKind::String));
    if (m.method == "contains" && args.size() == 1) {
        Value r = call_rt("nova_str_contains", "i32", recv + ", ptr " + args[0].repr, Type());
        std::string t = tmp();
        line(t + " = icmp ne i32 " + r.repr + ", 0");
        return Value{t, Type(TypeKind::Bool), true};
    }
    if (m.method == "split" && args.size() == 1)
        return call_rt("nova_str_split", "ptr", recv + ", ptr " + args[0].repr, Type(TypeKind::Array, TypeKind::String));
    error("Unsupported method '." + m.method + "()'.", m.line, m.col);
    return Value{};
}

void CodeGen::emit_array_add(const ArrayAddStmt& s) {
    const VarInfo* var = find_var(s.array_name);
    if (!var || var->type.kind != TypeKind::Array || var->type.elem == TypeKind::Unknown) {
        error("'" + s.array_name + "' is not an array with a known element type.", s.line, s.col);
        return;
    }
    Value v = emit_expr(*s.value);
    if (!v.valid) return;
    Value c = convert(v, Type(var->type.elem), s.line, s.col);
    if (!c.valid) return;
    std::string arr = tmp();
    line(arr + " = load ptr, ptr " + var->slot);
    call_rt("nova_arr_push", "void", "ptr " + arr + ", i64 " + to_bits(c), Type());
}

// `.rm` with an int removes by position (1-based); any other value removes the first equal element.
void CodeGen::emit_array_remove(const ArrayRemoveStmt& s) {
    const VarInfo* var = find_var(s.array_name);
    if (!var || var->type.kind != TypeKind::Array || var->type.elem == TypeKind::Unknown) {
        error("'" + s.array_name + "' is not an array with a known element type.", s.line, s.col);
        return;
    }
    Value v = emit_expr(*s.value_or_index);
    if (!v.valid) return;
    std::string arr = tmp();
    line(arr + " = load ptr, ptr " + var->slot);
    if (v.type.kind == TypeKind::Int) {
        call_rt("nova_arr_remove_at", "void", "ptr " + arr + ", i64 " + v.repr + ", i32 " + std::to_string(s.line), Type());
        return;
    }
    Value c = convert(v, Type(var->type.elem), s.line, s.col);
    if (!c.valid) return;
    int kind = var->type.elem == TypeKind::Float ? 1 : (var->type.elem == TypeKind::String ? 2 : 0);
    call_rt("nova_arr_remove_value", "void", "ptr " + arr + ", i64 " + to_bits(c) + ", i32 " + std::to_string(kind), Type());
}

void CodeGen::emit_foreach(const ForeachStmt& s) {
    Value arr = emit_expr(*s.collection);
    if (!arr.valid) return;
    if (arr.type.kind != TypeKind::Array || arr.type.elem == TypeKind::Unknown) {
        error("'for ... in' needs an array with a known element type, got " + type_name(arr.type) + ".", s.line, s.col);
        return;
    }
    Type elem(arr.type.elem);
    scopes_.emplace_back();
    VarInfo item = declare(s.item_name, elem);
    std::string idx_slot = "%v.idx." + std::to_string(next_slot_++);
    allocas_ += "  " + idx_slot + " = alloca i64\n";
    line("store i64 0, ptr " + idx_slot);

    std::string cond_l = new_label("foreach.cond"), body_l = new_label("foreach.body");
    std::string inc_l = new_label("foreach.inc"), end_l = new_label("foreach.end");
    line("br label %" + cond_l);
    emit_label(cond_l);
    std::string i = tmp();
    line(i + " = load i64, ptr " + idx_slot);
    Value n = call_rt("nova_arr_len", "i64", "ptr " + arr.repr, Type(TypeKind::Int)); // live length, re-read every iteration
    std::string c = tmp();
    line(c + " = icmp slt i64 " + i + ", " + n.repr);
    line("br i1 " + c + ", label %" + body_l + ", label %" + end_l);

    emit_label(body_l);
    std::string pos = tmp();
    line(pos + " = add i64 " + i + ", 1");
    Value bits = call_rt("nova_arr_get", "i64", "ptr " + arr.repr + ", i64 " + pos + ", i32 " + std::to_string(s.line), Type(TypeKind::Int));
    emit_store(item, from_bits(bits.repr, elem));
    loops_.push_back(LoopCtx{end_l, inc_l});
    emit_block(s.body);
    loops_.pop_back();
    line("br label %" + inc_l);

    emit_label(inc_l);
    std::string cur = tmp(), nxt = tmp();
    line(cur + " = load i64, ptr " + idx_slot);
    line(nxt + " = add i64 " + cur + ", 1");
    line("store i64 " + nxt + ", ptr " + idx_slot);
    line("br label %" + cond_l);
    emit_label(end_l);
    scopes_.pop_back();
}

// ------------------------------ input / sleep / exit (4f) ------------------------------

void CodeGen::emit_input(const InputStmt& s) {
    const VarInfo* var = find_var(s.target);
    if (!var) { error("Internal error: '" + s.target + "' has no storage.", s.line, s.col); return; }
    std::string ln = "i32 " + std::to_string(s.line);
    Value v;
    switch (var->type.kind) {
        case TypeKind::Int: v = call_rt("nova_input_int", "i64", ln, Type(TypeKind::Int)); break;
        case TypeKind::Float: v = call_rt("nova_input_float", "double", ln, Type(TypeKind::Float)); break;
        case TypeKind::String: v = call_rt("nova_input_str", "ptr", "", Type(TypeKind::String)); break;
        case TypeKind::Bool: {
            Value r = call_rt("nova_input_bool", "i32", ln, Type());
            std::string t = tmp();
            line(t + " = icmp ne i32 " + r.repr + ", 0");
            v = Value{t, Type(TypeKind::Bool), true};
            break;
        }
        default:
            error("Cannot read input into a variable of type " + type_name(var->type) + ".", s.line, s.col);
            return;
    }
    emit_store(*var, v);
}

void CodeGen::emit_sleep(const SleepStmt& s) {
    Value v = emit_expr(*s.milliseconds);
    if (!v.valid) return;
    std::string ms = v.repr;
    if (v.type.kind == TypeKind::Float) {
        ms = tmp();
        line(ms + " = fptosi double " + v.repr + " to i64");
    } else if (v.type.kind != TypeKind::Int) {
        error("sleep() needs a number of milliseconds.", s.line, s.col);
        return;
    }
    call_rt("nova_sleep", "void", "i64 " + ms, Type());
}

void CodeGen::emit_exit(const ExitStmt& s) {
    std::string code = "0";
    if (s.code) {
        Value v = emit_expr(*s.code);
        if (!v.valid) return;
        if (v.type.kind != TypeKind::Int) { error("'exit' needs an integer code.", s.line, s.col); return; }
        code = tmp();
        line(code + " = trunc i64 " + v.repr + " to i32");
    }
    call_rt("nova_exit", "void", "i32 " + code, Type());
    line("unreachable");
    emit_label(new_label("dead")); // code after `exit` is unreachable but stays well-formed
}

// ------------------------------ functions (4d) ------------------------------

CodeGen::Value CodeGen::emit_call(const CallExpr& c) {
    if (c.callee == "to_string" && c.args.size() == 1) {
        Value v = emit_expr(*c.args[0]);
        return v.valid ? to_str(v, c.line, c.col) : Value{};
    }
    if (c.callee == "random" && c.args.size() == 2) {
        Value a = emit_expr(*c.args[0]);
        Value b = emit_expr(*c.args[1]);
        if (!a.valid || !b.valid) return Value{};
        return call_rt("nova_random", "i64", "i64 " + a.repr + ", i64 " + b.repr, Type(TypeKind::Int));
    }
    if (c.callee == "read_file" && c.args.size() == 1) {
        Value p = emit_expr(*c.args[0]);
        if (!p.valid) return Value{};
        return call_rt("nova_read_file", "ptr", "ptr " + p.repr, Type(TypeKind::String));
    }
    if (c.callee == "write_file" && c.args.size() == 2) {
        Value p = emit_expr(*c.args[0]);
        Value t = emit_expr(*c.args[1]);
        if (!p.valid || !t.valid) return Value{};
        Value r = call_rt("nova_write_file", "i32", "ptr " + p.repr + ", ptr " + t.repr, Type());
        std::string ok = tmp();
        line(ok + " = icmp ne i32 " + r.repr + ", 0");
        return Value{ok, Type(TypeKind::Bool), true};
    }
    if (c.instance.empty()) {
        if (is_builtin(c.callee))
            error("Internal error: the builtin '" + c.callee + "' was not handled.", c.line, c.col);
        else
            error("Internal error: no typed instance for the call to '" + c.callee + "'.", c.line, c.col);
        return Value{};
    }
    const FuncInstance* inst = sema_->find_instance(c.instance);
    if (!inst) { error("Internal error: unknown instance '" + c.instance + "'.", c.line, c.col); return Value{}; }

    std::string args;
    for (size_t i = 0; i < c.args.size(); ++i) {
        Value a = emit_expr(*c.args[i]);
        if (!a.valid) return Value{};
        if (i >= inst->params.size() || !types_equal(a.type, inst->params[i])) {
            error("Internal error: argument " + std::to_string(i + 1) + " of '" + c.instance + "' has the wrong type.", c.line, c.col);
            return Value{};
        }
        args += (i ? ", " : "") + std::string(llvm_type(a.type)) + " " + a.repr;
    }
    if (queued_.insert(inst->key).second) queue_.push_back(inst->key);

    std::string callee = "@\"" + inst->key + "\"(" + args + ")";
    if (inst->ret.kind == TypeKind::Void) {
        line("call void " + callee);
        return Value{"", Type(TypeKind::Void), true};
    }
    std::string t = tmp();
    line(t + " = call " + llvm_type(inst->ret) + " " + callee);
    return Value{t, inst->ret, true};
}

void CodeGen::emit_return(const ReturnStmt& r) {
    if (!in_function_) { error("Internal error: 'return' outside a function.", r.line, r.col); return; }
    if (!r.value) {
        line("ret void");
    } else {
        Value v = emit_expr(*r.value);
        if (!v.valid) return;
        Value c = convert(v, cur_ret_, r.line, r.col);
        if (!c.valid) return;
        line(std::string("ret ") + llvm_type(cur_ret_) + " " + c.repr);
    }
    emit_label(new_label("dead")); // anything after a return is unreachable but must stay well-formed
}

void CodeGen::reset_function_state() {
    allocas_.clear();
    body_.clear();
    scopes_.assign(1, {});
    loops_.clear();
    cur_block_ = "entry";
    next_tmp_ = next_label_ = next_slot_ = 0;
}

std::string CodeGen::assemble(const std::string& header, const std::string& default_ret) {
    return header + " {\nentry:\n" + allocas_ + body_ + "  " + default_ret + "\n}\n\n";
}

void CodeGen::emit_function(const FuncInstance& inst) {
    if (!inst.decl) return;
    reset_function_state();
    in_function_ = true;
    cur_ret_ = inst.ret;
    current_file_ = inst.decl->source_file;

    std::string params;
    for (size_t i = 0; i < inst.decl->params.size(); ++i) {
        std::string reg = "%p." + inst.decl->params[i];
        params += (i ? ", " : "") + std::string(llvm_type(inst.params[i])) + " " + reg;
        emit_store(declare(inst.decl->params[i], inst.params[i]), Value{reg, inst.params[i], true});
    }
    for (const auto& s : inst.decl->body) emit_stmt(*s);

    std::string ret_ty = inst.ret.kind == TypeKind::Void ? "void" : llvm_type(inst.ret);
    functions_ += assemble("define " + ret_ty + " @\"" + inst.key + "\"(" + params + ")", default_return(inst.ret));
    in_function_ = false;
    current_file_.clear();
}

void CodeGen::emit_stmt(const Stmt& stmt) {
    switch (stmt.kind) {
        case StmtKind::Pf: emit_pf(static_cast<const PfStmt&>(stmt)); break;
        case StmtKind::VarDecl: {
            auto& vd = static_cast<const VarDeclStmt&>(stmt);
            Value v = emit_expr(*vd.value);
            if (!v.valid) break;
            if (v.type.unknown() || v.type.kind == TypeKind::Void) {
                error("Could not determine the type of '" + vd.name + "'.", vd.line, vd.col);
                break;
            }
            emit_store(declare(vd.name, v.type), v);
            break;
        }
        case StmtKind::Assign: emit_assign(static_cast<const AssignStmt&>(stmt)); break;
        case StmtKind::IncDec: emit_incdec(static_cast<const IncDecStmt&>(stmt)); break;
        case StmtKind::Return: emit_return(static_cast<const ReturnStmt&>(stmt)); break;
        case StmtKind::ExprStmt: emit_expr(*static_cast<const ExprStmt&>(stmt).expr); break; // result (if any) discarded
        case StmtKind::FuncDecl:
            error("Nested function declarations are not supported; declare functions at the top level.", stmt.line, stmt.col);
            break;
        case StmtKind::Input: emit_input(static_cast<const InputStmt&>(stmt)); break;
        case StmtKind::Sleep: emit_sleep(static_cast<const SleepStmt&>(stmt)); break;
        case StmtKind::Exit: emit_exit(static_cast<const ExitStmt&>(stmt)); break;
        case StmtKind::ArrayAdd: emit_array_add(static_cast<const ArrayAddStmt&>(stmt)); break;
        case StmtKind::ArrayRemove: emit_array_remove(static_cast<const ArrayRemoveStmt&>(stmt)); break;
        case StmtKind::Foreach: emit_foreach(static_cast<const ForeachStmt&>(stmt)); break;
        case StmtKind::If: emit_if(static_cast<const IfStmt&>(stmt));  break;
        case StmtKind::While: emit_while(static_cast<const WhileStmt&>(stmt)); break;
        case StmtKind::For: emit_for(static_cast<const ForStmt&>(stmt)); break;
        case StmtKind::Switch: emit_switch(static_cast<const SwitchStmt&>(stmt)); break;
        case StmtKind::Break: {
            if (loops_.empty()) { error("Internal error: 'break' outside a loop.", stmt.line, stmt.col); break; }
            line("br label %" + loops_.back().break_label);
            emit_label(new_label("dead"));
            break;
        }
        case StmtKind::Continue: {
            const LoopCtx* target = nullptr;
            for (auto it = loops_.rbegin(); it != loops_.rend(); ++it)
                if (!it->continue_label.empty()) { target = &*it; break; }
            if (!target) { error("Internal error: 'continue' outside a loop.", stmt.line, stmt.col); break; }
            line("br label %" + target->continue_label);
            emit_label(new_label("dead"));
            break;
        }
        default:
            error(std::string("The code generator does not support ") + unsupported_stmt(stmt.kind) + ".",
                  stmt.line, stmt.col);
    }
}

bool CodeGen::generate(const ProgramStmt& program, const SemanticAnalyzer& sema, std::string& out_ir) {
    errors_.clear();
    globals_.clear();
    strings_.clear();
    functions_.clear();
    queue_.clear();
    queued_.clear();
    current_file_.clear();
    sema_ = &sema;
    in_function_ = false;

    reset_function_state();
    for (const auto& stmt : program.body) {
        if (stmt->kind == StmtKind::FuncDecl) continue; // hoisted: emitted per typed instance, when called
        emit_stmt(*stmt);
    }
    std::string main_text = assemble("define i32 @main()", "ret i32 0");

    for (size_t i = 0; i < queue_.size(); ++i) { // the worklist grows while we emit
        std::string key = queue_[i];
        if (const FuncInstance* inst = sema_->find_instance(key)) emit_function(*inst);
    }
    if (!errors_.empty()) return false;

    std::string ir;
    ir += "; Generated by novac (Part 4f) — requires LLVM >= 15\n";
    ir += globals_;
    ir += "\ndeclare i32 @printf(ptr, ...)\n";
    ir += "; runtime (runtime/nova_rt.c)\n"
          "declare ptr @nova_str_concat(ptr, ptr)\ndeclare ptr @nova_int_to_str(i64)\n"
          "declare ptr @nova_float_to_str(double)\ndeclare ptr @nova_bool_to_str(i32)\n"
          "declare i32 @nova_str_cmp(ptr, ptr)\ndeclare ptr @nova_str_upper(ptr)\ndeclare ptr @nova_str_lower(ptr)\n"
          "declare i32 @nova_str_contains(ptr, ptr)\ndeclare ptr @nova_str_split(ptr, ptr)\ndeclare i64 @nova_str_len(ptr)\n"
          "declare ptr @nova_arr_new()\ndeclare void @nova_arr_push(ptr, i64)\ndeclare i64 @nova_arr_get(ptr, i64, i32)\n"
          "declare void @nova_arr_remove_at(ptr, i64, i32)\ndeclare void @nova_arr_remove_value(ptr, i64, i32)\n"
          "declare i64 @nova_arr_len(ptr)\n"
          "declare void @nova_sleep(i64)\ndeclare void @nova_exit(i32)\ndeclare i64 @nova_random(i64, i64)\n"
          "declare ptr @nova_read_file(ptr)\ndeclare i32 @nova_write_file(ptr, ptr)\n"
          "declare ptr @nova_input_str()\ndeclare i64 @nova_input_int(i32)\ndeclare double @nova_input_float(i32)\n"
          "declare i32 @nova_input_bool(i32)\n\n";
    ir += main_text;
    ir += functions_;
    out_ir = std::move(ir);
    return true;
}

} // namespace novac
