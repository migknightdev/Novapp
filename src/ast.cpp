// ast.cpp — AST helpers: the debug printer behind `novac --ast` (not used by any
// compiler stage) and the deep-clone functions used for generic function instances.
#include "ast.h"

#include <sstream>

namespace novac {

static std::string pad(int indent) { return std::string(static_cast<size_t>(indent) * 2, ' '); }

std::string print_ast(const Expr& node, int indent) {
    std::ostringstream out;
    std::string p = pad(indent);

    switch (node.kind) {
        case ExprKind::Number: {
            const auto& n = static_cast<const NumberExpr&>(node);
            out << p << "Number(" << n.value << (n.is_float ? "f" : "i") << ")\n";
            break;
        }
        case ExprKind::String: {
            const auto& s = static_cast<const StringExpr&>(node);
            out << p << "String(\"" << s.value << "\")\n";
            break;
        }
        case ExprKind::Bool: {
            const auto& b = static_cast<const BoolExpr&>(node);
            out << p << "Bool(" << (b.value ? "true" : "false") << ")\n";
            break;
        }
        case ExprKind::Identifier: {
            const auto& id = static_cast<const IdentifierExpr&>(node);
            out << p << "Identifier(" << id.name << ")\n";
            break;
        }
        case ExprKind::ArrayLiteral: {
            const auto& arr = static_cast<const ArrayLiteralExpr&>(node);
            out << p << "ArrayLiteral[\n";
            for (auto& el : arr.elements) out << print_ast(*el, indent + 1);
            out << p << "]\n";
            break;
        }
        case ExprKind::Index: {
            const auto& idx = static_cast<const IndexExpr&>(node);
            out << p << "Index(\n";
            out << p << "  array:\n" << print_ast(*idx.array, indent + 2);
            out << p << "  index:\n" << print_ast(*idx.index, indent + 2);
            out << p << ")\n";
            break;
        }
        case ExprKind::Property: {
            const auto& pr = static_cast<const PropertyExpr&>(node);
            out << p << "Property(." << pr.name << ")\n";
            out << print_ast(*pr.object, indent + 1);
            break;
        }
        case ExprKind::MethodCall: {
            const auto& mc = static_cast<const MethodCallExpr&>(node);
            out << p << "MethodCall(." << mc.method << ")\n";
            out << p << "  object:\n" << print_ast(*mc.object, indent + 2);
            if (!mc.args.empty()) {
                out << p << "  args:\n";
                for (auto& a : mc.args) out << print_ast(*a, indent + 2);
            }
            break;
        }
        case ExprKind::Binary: {
            const auto& b = static_cast<const BinaryExpr&>(node);
            out << p << "Binary(" << token_type_name(b.op) << ")\n";
            out << print_ast(*b.left, indent + 1);
            out << print_ast(*b.right, indent + 1);
            break;
        }
        case ExprKind::Unary: {
            const auto& u = static_cast<const UnaryExpr&>(node);
            out << p << "Unary(" << token_type_name(u.op) << ")\n";
            out << print_ast(*u.operand, indent + 1);
            break;
        }
        case ExprKind::Call: {
            const auto& c = static_cast<const CallExpr&>(node);
            out << p << "Call(" << c.callee << ")\n";
            for (auto& a : c.args) out << print_ast(*a, indent + 1);
            break;
        }
    }
    return out.str();
}

static std::string print_block(const Block& block, int indent) {
    std::ostringstream out;
    for (auto& s : block) out << print_ast(*s, indent);
    return out.str();
}

std::string print_ast(const Stmt& node, int indent) {
    std::ostringstream out;
    std::string p = pad(indent);

    switch (node.kind) {
        case StmtKind::Block:
            // Block nodes aren't currently materialized standalone (bodies are
            // plain Block = vector<StmtPtr> embedded in parent nodes); reserved
            // for later if a first-class Block statement is ever needed.
            out << p << "Block(unused)\n";
            break;
        case StmtKind::Import: {
            const auto& im = static_cast<const ImportStmt&>(node);
            out << p << "Import(" << im.file;
            if (im.from_dir) out << ", from=\"" << *im.from_dir << "\"";
            out << ")\n";
            break;
        }
        case StmtKind::FuncDecl: {
            const auto& fn = static_cast<const FuncDeclStmt&>(node);
            out << p << "FuncDecl(" << fn.name << "(";
            for (size_t i = 0; i < fn.params.size(); ++i) {
                out << fn.params[i];
                if (i + 1 < fn.params.size()) out << ", ";
            }
            out << "))\n";
            out << print_block(fn.body, indent + 1);
            break;
        }
        case StmtKind::VarDecl: {
            const auto& vd = static_cast<const VarDeclStmt&>(node);
            const char* mut = vd.mutability == Mutability::Let ? "let" :
                              vd.mutability == Mutability::Const ? "const" : "plain";
            out << p << "VarDecl(" << vd.name << ", " << mut << ")\n";
            out << print_ast(*vd.value, indent + 1);
            break;
        }
        case StmtKind::Assign: {
            const auto& as = static_cast<const AssignStmt&>(node);
            out << p << "Assign(" << as.name << " " << token_type_name(as.op) << (as.is_declaration ? ", decl" : "") << ")\n";
            out << print_ast(*as.value, indent + 1);
            break;
        }
        case StmtKind::IncDec: {
            const auto& id = static_cast<const IncDecStmt&>(node);
            out << p << "IncDec(" << id.name << " " << token_type_name(id.op)
                << (id.prefix ? " prefix" : " postfix") << ")\n";
            break;
        }
        case StmtKind::If: {
            const auto& iff = static_cast<const IfStmt&>(node);
            out << p << "If(\n";
            out << p << "  cond:\n" << print_ast(*iff.condition, indent + 2);
            out << p << "  then:\n" << print_block(iff.then_body, indent + 2);
            for (auto& e : iff.elifs) {
                out << p << "  elif:\n";
                out << p << "    cond:\n" << print_ast(*e.condition, indent + 3);
                out << p << "    then:\n" << print_block(e.body, indent + 3);
            }
            if (iff.else_body) {
                out << p << "  else:\n" << print_block(*iff.else_body, indent + 2);
            }
            out << p << ")\n";
            break;
        }
        case StmtKind::While: {
            const auto& w = static_cast<const WhileStmt&>(node);
            out << p << "While(\n";
            out << p << "  cond:\n" << print_ast(*w.condition, indent + 2);
            out << p << "  body:\n" << print_block(w.body, indent + 2);
            out << p << ")\n";
            break;
        }
        case StmtKind::For: {
            const auto& f = static_cast<const ForStmt&>(node);
            out << p << "For(\n";
            if (f.init) out << p << "  init:\n" << print_ast(*f.init, indent + 2);
            if (f.condition) out << p << "  cond:\n" << print_ast(*f.condition, indent + 2);
            if (f.increment) out << p << "  incr:\n" << print_ast(*f.increment, indent + 2);
            out << p << "  body:\n" << print_block(f.body, indent + 2);
            out << p << ")\n";
            break;
        }
        case StmtKind::Foreach: {
            const auto& fe = static_cast<const ForeachStmt&>(node);
            out << p << "Foreach(" << fe.item_name << " in\n";
            out << print_ast(*fe.collection, indent + 1);
            out << p << "  body:\n" << print_block(fe.body, indent + 2);
            out << p << ")\n";
            break;
        }
        case StmtKind::Return: {
            const auto& r = static_cast<const ReturnStmt&>(node);
            out << p << "Return\n";
            if (r.value) out << print_ast(*r.value, indent + 1);
            break;
        }
        case StmtKind::Pf: {
            const auto& pf = static_cast<const PfStmt&>(node);
            out << p << "Pf\n";
            out << print_ast(*pf.value, indent + 1);
            break;
        }
        case StmtKind::Input: {
            const auto& in = static_cast<const InputStmt&>(node);
            out << p << "Input(" << in.target << ")\n";
            break;
        }
        case StmtKind::Sleep: {
            const auto& sl = static_cast<const SleepStmt&>(node);
            out << p << "Sleep\n";
            out << print_ast(*sl.milliseconds, indent + 1);
            break;
        }
        case StmtKind::Exit: {
            const auto& ex = static_cast<const ExitStmt&>(node);
            out << p << "Exit\n";
            if (ex.code) out << print_ast(*ex.code, indent + 1);
            break;
        }
        case StmtKind::Break:
            out << p << "Break\n";
            break;
        case StmtKind::Continue:
            out << p << "Continue\n";
            break;
        case StmtKind::Switch: {
            const auto& sw = static_cast<const SwitchStmt&>(node);
            out << p << "Switch(\n";
            out << p << "  subject:\n" << print_ast(*sw.subject, indent + 2);
            for (auto& c : sw.cases) {
                if (c.value) {
                    out << p << "  case:\n" << print_ast(*c.value, indent + 2);
                } else {
                    out << p << "  default:\n";
                }
                out << print_block(c.body, indent + 2);
            }
            out << p << ")\n";
            break;
        }
        case StmtKind::ArrayAdd: {
            const auto& aa = static_cast<const ArrayAddStmt&>(node);
            out << p << "ArrayAdd(" << aa.array_name << ")\n";
            out << print_ast(*aa.value, indent + 1);
            break;
        }
        case StmtKind::ArrayRemove: {
            const auto& ar = static_cast<const ArrayRemoveStmt&>(node);
            out << p << "ArrayRemove(" << ar.array_name << ")\n";
            out << print_ast(*ar.value_or_index, indent + 1);
            break;
        }
        case StmtKind::ExprStmt: {
            const auto& es = static_cast<const ExprStmt&>(node);
            out << p << "ExprStmt\n";
            out << print_ast(*es.expr, indent + 1);
            break;
        }
        case StmtKind::Program: {
            const auto& prog = static_cast<const ProgramStmt&>(node);
            out << p << "Program(nova_include=" << (prog.has_nova_include ? "true" : "false")
                << ", no_main=" << (prog.no_main ? "true" : "false") << ")\n";
            for (auto& im : prog.imports) out << print_ast(*im, indent + 1);
            for (auto& st : prog.body) out << print_ast(*st, indent + 1);
            break;
        }
    }
    return out.str();
}

// ------------------------------ deep clone ------------------------------

namespace {
ExprPtr cl(const ExprPtr& p) { return p ? clone_expr(*p) : nullptr; }
template <class T> void at(T& dst, const Expr& src) { dst.line = src.line; dst.col = src.col; }
template <class T> void at(T& dst, const Stmt& src) { dst.line = src.line; dst.col = src.col; }
std::vector<ExprPtr> cl_all(const std::vector<ExprPtr>& v) {
    std::vector<ExprPtr> out;
    for (auto& e : v) out.push_back(clone_expr(*e));
    return out;
}
} // namespace

ExprPtr clone_expr(const Expr& e) {
    switch (e.kind) {
        case ExprKind::Number: {
            auto& n = static_cast<const NumberExpr&>(e);
            auto c = std::make_unique<NumberExpr>(n.value, n.is_float); at(*c, e); return c;
        }
        case ExprKind::String: { auto c = std::make_unique<StringExpr>(static_cast<const StringExpr&>(e).value); at(*c, e); return c; }
        case ExprKind::Bool: { auto c = std::make_unique<BoolExpr>(static_cast<const BoolExpr&>(e).value); at(*c, e); return c; }
        case ExprKind::Identifier: { auto c = std::make_unique<IdentifierExpr>(static_cast<const IdentifierExpr&>(e).name); at(*c, e); return c; }
        case ExprKind::ArrayLiteral: {
            auto c = std::make_unique<ArrayLiteralExpr>(); at(*c, e);
            c->elements = cl_all(static_cast<const ArrayLiteralExpr&>(e).elements); return c;
        }
        case ExprKind::Index: {
            auto& x = static_cast<const IndexExpr&>(e);
            auto c = std::make_unique<IndexExpr>(); at(*c, e);
            c->array = cl(x.array); c->index = cl(x.index); return c;
        }
        case ExprKind::Property: {
            auto& x = static_cast<const PropertyExpr&>(e);
            auto c = std::make_unique<PropertyExpr>(); at(*c, e);
            c->object = cl(x.object); c->name = x.name; return c;
        }
        case ExprKind::MethodCall: {
            auto& x = static_cast<const MethodCallExpr&>(e);
            auto c = std::make_unique<MethodCallExpr>(); at(*c, e);
            c->object = cl(x.object); c->method = x.method; c->args = cl_all(x.args); return c;
        }
        case ExprKind::Binary: {
            auto& x = static_cast<const BinaryExpr&>(e);
            auto c = std::make_unique<BinaryExpr>(); at(*c, e);
            c->left = cl(x.left); c->op = x.op; c->right = cl(x.right); return c;
        }
        case ExprKind::Unary: {
            auto& x = static_cast<const UnaryExpr&>(e);
            auto c = std::make_unique<UnaryExpr>(); at(*c, e);
            c->op = x.op; c->operand = cl(x.operand); return c;
        }
        case ExprKind::Call: {
            auto& x = static_cast<const CallExpr&>(e);
            auto c = std::make_unique<CallExpr>(); at(*c, e);
            c->callee = x.callee; c->args = cl_all(x.args); return c;
        }
    }
    return nullptr;
}

Block clone_block(const Block& b) {
    Block out;
    for (auto& s : b) out.push_back(clone_stmt(*s));
    return out;
}

std::unique_ptr<FuncDeclStmt> clone_func(const FuncDeclStmt& f) {
    auto c = std::make_unique<FuncDeclStmt>(); at(*c, static_cast<const Stmt&>(f));
    c->name = f.name; c->params = f.params; c->body = clone_block(f.body); c->source_file = f.source_file;
    return c;
}

StmtPtr clone_stmt(const Stmt& s) {
    switch (s.kind) {
        case StmtKind::Import: {
            auto& x = static_cast<const ImportStmt&>(s);
            auto c = std::make_unique<ImportStmt>(); at(*c, s); c->file = x.file; c->from_dir = x.from_dir; return c;
        }
        case StmtKind::FuncDecl: return clone_func(static_cast<const FuncDeclStmt&>(s));
        case StmtKind::VarDecl: {
            auto& x = static_cast<const VarDeclStmt&>(s);
            auto c = std::make_unique<VarDeclStmt>(); at(*c, s);
            c->name = x.name; c->value = cl(x.value); c->mutability = x.mutability; return c;
        }
        case StmtKind::Assign: {
            auto& x = static_cast<const AssignStmt&>(s);
            auto c = std::make_unique<AssignStmt>(); at(*c, s);
            c->name = x.name; c->op = x.op; c->value = cl(x.value); return c;
        }
        case StmtKind::IncDec: {
            auto& x = static_cast<const IncDecStmt&>(s);
            auto c = std::make_unique<IncDecStmt>(); at(*c, s);
            c->name = x.name; c->op = x.op; c->prefix = x.prefix; return c;
        }
        case StmtKind::If: {
            auto& x = static_cast<const IfStmt&>(s);
            auto c = std::make_unique<IfStmt>(); at(*c, s);
            c->condition = cl(x.condition); c->then_body = clone_block(x.then_body);
            for (auto& el : x.elifs) c->elifs.push_back(ElifClause{cl(el.condition), clone_block(el.body)});
            if (x.else_body) c->else_body = clone_block(*x.else_body);
            return c;
        }
        case StmtKind::While: {
            auto& x = static_cast<const WhileStmt&>(s);
            auto c = std::make_unique<WhileStmt>(); at(*c, s);
            c->condition = cl(x.condition); c->body = clone_block(x.body); return c;
        }
        case StmtKind::For: {
            auto& x = static_cast<const ForStmt&>(s);
            auto c = std::make_unique<ForStmt>(); at(*c, s);
            if (x.init) c->init = clone_stmt(*x.init);
            c->condition = cl(x.condition);
            if (x.increment) c->increment = clone_stmt(*x.increment);
            c->body = clone_block(x.body); return c;
        }
        case StmtKind::Foreach: {
            auto& x = static_cast<const ForeachStmt&>(s);
            auto c = std::make_unique<ForeachStmt>(); at(*c, s);
            c->item_name = x.item_name; c->collection = cl(x.collection); c->body = clone_block(x.body); return c;
        }
        case StmtKind::Return: {
            auto c = std::make_unique<ReturnStmt>(); at(*c, s); c->value = cl(static_cast<const ReturnStmt&>(s).value); return c;
        }
        case StmtKind::Pf: {
            auto c = std::make_unique<PfStmt>(); at(*c, s); c->value = cl(static_cast<const PfStmt&>(s).value); return c;
        }
        case StmtKind::Input: {
            auto c = std::make_unique<InputStmt>(); at(*c, s); c->target = static_cast<const InputStmt&>(s).target; return c;
        }
        case StmtKind::Sleep: {
            auto c = std::make_unique<SleepStmt>(); at(*c, s); c->milliseconds = cl(static_cast<const SleepStmt&>(s).milliseconds); return c;
        }
        case StmtKind::Exit: {
            auto c = std::make_unique<ExitStmt>(); at(*c, s); c->code = cl(static_cast<const ExitStmt&>(s).code); return c;
        }
        case StmtKind::Break: { auto c = std::make_unique<BreakStmt>(); at(*c, s); return c; }
        case StmtKind::Continue: { auto c = std::make_unique<ContinueStmt>(); at(*c, s); return c; }
        case StmtKind::Switch: {
            auto& x = static_cast<const SwitchStmt&>(s);
            auto c = std::make_unique<SwitchStmt>(); at(*c, s);
            c->subject = cl(x.subject);
            for (auto& cs : x.cases) c->cases.push_back(CaseClause{cl(cs.value), clone_block(cs.body)});
            return c;
        }
        case StmtKind::ArrayAdd: {
            auto& x = static_cast<const ArrayAddStmt&>(s);
            auto c = std::make_unique<ArrayAddStmt>(); at(*c, s); c->array_name = x.array_name; c->value = cl(x.value); return c;
        }
        case StmtKind::ArrayRemove: {
            auto& x = static_cast<const ArrayRemoveStmt&>(s);
            auto c = std::make_unique<ArrayRemoveStmt>(); at(*c, s); c->array_name = x.array_name; c->value_or_index = cl(x.value_or_index); return c;
        }
        case StmtKind::ExprStmt: {
            auto c = std::make_unique<ExprStmt>(); at(*c, s); c->expr = cl(static_cast<const ExprStmt&>(s).expr); return c;
        }
        case StmtKind::Block: case StmtKind::Program: break; // never cloned
    }
    return nullptr;
}

} // namespace novac
