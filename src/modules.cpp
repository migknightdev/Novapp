#include "modules.h"

#include <filesystem>
#include <fstream>
#include <queue>
#include <sstream>
#include "lexer.h"
#include "parser.h"

namespace novac {

namespace fs = std::filesystem;

namespace {

void calls_in_expr(const Expr& e, std::set<std::string>& out);
void calls_in_block(const Block& b, std::set<std::string>& out);

void calls_in_expr(const Expr& e, std::set<std::string>& out) {
    switch (e.kind) {
        case ExprKind::Call: {
            auto& c = static_cast<const CallExpr&>(e);
            out.insert(c.callee);
            for (auto& a : c.args) calls_in_expr(*a, out);
            break;
        }
        case ExprKind::Index: {
            auto& i = static_cast<const IndexExpr&>(e);
            calls_in_expr(*i.array, out);
            calls_in_expr(*i.index, out);
            break;
        }
        case ExprKind::Property: calls_in_expr(*static_cast<const PropertyExpr&>(e).object, out); break;
        case ExprKind::MethodCall: {
            auto& m = static_cast<const MethodCallExpr&>(e);
            calls_in_expr(*m.object, out);
            for (auto& a : m.args) calls_in_expr(*a, out);
            break;
        }
        case ExprKind::Binary: {
            auto& b = static_cast<const BinaryExpr&>(e);
            calls_in_expr(*b.left, out);
            calls_in_expr(*b.right, out);
            break;
        }
        case ExprKind::Unary: calls_in_expr(*static_cast<const UnaryExpr&>(e).operand, out); break;
        case ExprKind::ArrayLiteral:
            for (auto& el : static_cast<const ArrayLiteralExpr&>(e).elements) calls_in_expr(*el, out);
            break;
        default: break;
    }
}

void calls_in_stmt(const Stmt& s, std::set<std::string>& out) {
    auto ex = [&](const ExprPtr& p) { if (p) calls_in_expr(*p, out); };
    switch (s.kind) {
        case StmtKind::FuncDecl: calls_in_block(static_cast<const FuncDeclStmt&>(s).body, out); break;
        case StmtKind::VarDecl: ex(static_cast<const VarDeclStmt&>(s).value); break;
        case StmtKind::Assign: ex(static_cast<const AssignStmt&>(s).value); break;
        case StmtKind::If: {
            auto& i = static_cast<const IfStmt&>(s);
            ex(i.condition);
            calls_in_block(i.then_body, out);
            for (auto& e : i.elifs) { ex(e.condition); calls_in_block(e.body, out); }
            if (i.else_body) calls_in_block(*i.else_body, out);
            break;
        }
        case StmtKind::While: {
            auto& w = static_cast<const WhileStmt&>(s);
            ex(w.condition);
            calls_in_block(w.body, out);
            break;
        }
        case StmtKind::For: {
            auto& f = static_cast<const ForStmt&>(s);
            if (f.init) calls_in_stmt(*f.init, out);
            ex(f.condition);
            if (f.increment) calls_in_stmt(*f.increment, out);
            calls_in_block(f.body, out);
            break;
        }
        case StmtKind::Foreach: {
            auto& f = static_cast<const ForeachStmt&>(s);
            ex(f.collection);
            calls_in_block(f.body, out);
            break;
        }
        case StmtKind::Return: ex(static_cast<const ReturnStmt&>(s).value); break;
        case StmtKind::Pf: ex(static_cast<const PfStmt&>(s).value); break;
        case StmtKind::Sleep: ex(static_cast<const SleepStmt&>(s).milliseconds); break;
        case StmtKind::Exit: ex(static_cast<const ExitStmt&>(s).code); break;
        case StmtKind::Switch: {
            auto& sw = static_cast<const SwitchStmt&>(s);
            ex(sw.subject);
            for (auto& c : sw.cases) { ex(c.value); calls_in_block(c.body, out); }
            break;
        }
        case StmtKind::ArrayAdd: ex(static_cast<const ArrayAddStmt&>(s).value); break;
        case StmtKind::ArrayRemove: ex(static_cast<const ArrayRemoveStmt&>(s).value_or_index); break;
        case StmtKind::ExprStmt: ex(static_cast<const ExprStmt&>(s).expr); break;
        default: break;
    }
}

void calls_in_block(const Block& b, std::set<std::string>& out) {
    for (auto& s : b) if (s) calls_in_stmt(*s, out);
}

} // namespace

void ModuleLoader::add_error(const std::string& message, const std::string& file, int line, int col) {
    errors_.push_back(ModuleError{message, file, line, col});
}

void ModuleLoader::load(const std::vector<StmtPtr>& imports, const std::string& base_dir) {
    load_imports(imports, base_dir, "");
}

void ModuleLoader::load_imports(const std::vector<StmtPtr>& imports, const std::string& base_dir, const std::string& importer) {
    for (const auto& st : imports) {
        const auto& im = static_cast<const ImportStmt&>(*st);

        // A relative `From` dir is relative to the importing file's directory.
        fs::path base(base_dir);
        fs::path dir = base;
        if (im.from_dir && !im.from_dir->empty()) {
            fs::path d(*im.from_dir);
            dir = d.is_absolute() ? d : base / d;
        }
        fs::path full = dir / im.file;

        std::error_code ec;
        if (!fs::is_regular_file(full, ec)) {
            add_error("Cannot find imported file '" + im.file + "' (looked in '" +
                      (dir.empty() ? std::string(".") : dir.string()) + "').", importer, im.line, im.col);
            failed_ = true;
            continue;
        }
        fs::path key = fs::weakly_canonical(full, ec);
        if (ec) key = full.lexically_normal();
        if (!visited_.insert(key.string()).second) continue;

        std::ifstream f(full);
        std::stringstream ss;
        ss << f.rdbuf();
        const std::string file_name = full.string();

        Lexer lexer(ss.str(), file_name);
        auto tokens = lexer.tokenize();
        for (auto& e : lexer.errors()) add_error("Syntax error: " + e.message, file_name, e.line, e.col);
        Parser parser(std::move(tokens));
        parser.set_require_nova_include(false);
        auto prog = parser.parse_program();
        for (auto& e : parser.errors()) add_error("Syntax error: " + e.message, file_name, e.line, e.col);
        if (!lexer.ok() || !parser.ok()) failed_ = true;

        ProgramStmt* p = prog.get();
        programs_.push_back(std::move(prog));

        for (auto& s : p->body) {
            if (s->kind != StmtKind::FuncDecl) {
                add_error("Library files may only contain function declarations.", file_name, s->line, s->col);
                continue;
            }
            auto* fn = static_cast<FuncDeclStmt*>(s.get());
            fn->source_file = file_name;
            auto it = pool_.find(fn->name);
            if (it != pool_.end()) {
                add_error("Function '" + fn->name + "' is defined in both '" + it->second->source_file +
                          "' and '" + file_name + "'.", file_name, fn->line, fn->col);
                continue;
            }
            pool_[fn->name] = fn;
        }

        load_imports(p->imports, full.parent_path().string(), file_name);
    }
}

std::vector<StmtPtr> ModuleLoader::take_reachable(const ProgramStmt& program) {
    std::set<std::string> needed;
    std::queue<std::string> work;
    auto mark = [&](const std::set<std::string>& names) {
        for (const auto& n : names)
            if (pool_.count(n) && needed.insert(n).second) work.push(n);
    };

    std::set<std::string> seeds;
    calls_in_block(program.body, seeds); // includes the main file's own functions' bodies
    mark(seeds);
    while (!work.empty()) {
        std::string name = work.front();
        work.pop();
        std::set<std::string> inner;
        calls_in_block(pool_[name]->body, inner);
        mark(inner);
    }

    std::vector<StmtPtr> taken;
    for (auto& p : programs_) {
        for (auto& s : p->body) {
            if (!s || s->kind != StmtKind::FuncDecl) continue;
            auto* fn = static_cast<FuncDeclStmt*>(s.get());
            auto it = pool_.find(fn->name);
            if (it != pool_.end() && it->second == fn && needed.count(fn->name)) taken.push_back(std::move(s));
        }
    }
    return taken;
}

} // namespace novac
