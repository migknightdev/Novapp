// main.cpp — the novac command line: lex -> parse -> semantic analysis -> LLVM IR.
// Validates programs, dumps tokens / AST / IR, and builds or runs them with clang
// plus the C runtime (runtime/nova_rt.c).
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include "semantic.h"
#include "codegen.h"

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#define NOVAC_NULL_DEV "NUL"
static const char* const EXE_EXT = ".exe";
#else
#include <sys/wait.h>
#define NOVAC_NULL_DEV "/dev/null"
static const char* const EXE_EXT = "";
#endif

using namespace novac;

#define NOVAC_VERSION "0.1.0-beta"
namespace fs = std::filesystem;

static void print_usage(const char* prog, std::ostream& os = std::cerr) {
    os << "novac - Nova++ native compiler\n\n"
              << "Usage:\n"
              << "  " << prog << " --check  <file.nvpp>   Validate: syntax + semantics (imports, scopes, types)\n"
              << "  " << prog << " --ast    <file.nvpp>   Validate, then print the AST (imports spliced in)\n"
              << "  " << prog << " --tokens <file.nvpp>   Print the raw token stream\n"
              << "  " << prog << " --emit-ir <file.nvpp> [-o out.ll]   Validate, then emit LLVM IR\n"
              << "  " << prog << " build <file.nvpp> [-o out] [--keep-ir]   Compile to an executable (needs clang)\n"
              << "  " << prog << " run   <file.nvpp> [--keep-ir]            Compile, then run it\n"
       << "  " << prog << " --version | --help\n";
}

static bool read_file(const std::string& path, std::string& out) {
    std::ifstream f(path);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// "  12 | source line" plus a caret under the column, when the file/line can be read.
static void print_snippet(const std::string& path, int line, int col) {
    std::ifstream f(path);
    if (!f) return;
    std::string text;
    for (int i = 1; std::getline(f, text); ++i) {
        if (i != line) continue;
        if (!text.empty() && text.back() == '\r') text.pop_back();
        std::string tag = std::to_string(line);
        std::cerr << "  " << tag << " | " << text << "\n";
        std::cerr << "  " << std::string(tag.size(), ' ') << " | " << std::string(col > 1 ? col - 1 : 0, ' ') << "^\n";
        return;
    }
}

static void report(const char* kind, const std::string& path, int line, int col, const std::string& message) {
    std::cerr << "[" << kind << "] " << path << ":" << line << ":" << col << ": " << message << "\n";
    print_snippet(path, line, col);
}

// Runs every stage and reports ALL errors found. Semantic analysis only runs
// when lexing and parsing were clean (a partial tree would cascade errors).
static bool front_end(const std::string& path, const std::string& source,
                      std::unique_ptr<ProgramStmt>& program, SemanticAnalyzer& sema) {
    size_t errors = 0;

    Lexer lexer(source, path);
    auto tokens = lexer.tokenize();
    for (auto& e : lexer.errors()) { report("LEX ERROR", path, e.line, e.col, e.message); ++errors; }

    Parser parser(std::move(tokens));
    program = parser.parse_program();
    for (auto& e : parser.errors()) { report("PARSE ERROR", path, e.line, e.col, e.message); ++errors; }

    if (errors == 0) {
        sema.analyze(*program, fs::path(path).parent_path().string());
        for (auto& e : sema.errors()) {
            report("SEMANTIC ERROR", e.file.empty() ? path : e.file, e.line, e.col, e.message);
            ++errors;
        }
    } else {
        std::cerr << "[note] semantic analysis skipped: fix the syntax errors first.\n";
    }

    if (errors) std::cerr << errors << " error(s).\n";
    return errors == 0;
}

static void print_import_stats(const ProgramStmt& program, const SemanticAnalyzer& sema) {
    if (program.imports.empty()) return;
    std::cout << "[novac] Import: using " << sema.imported_used() << " of " << sema.imported_total()
              << " imported function(s).\n";
}

static int run_tokens(const std::string& path) {
    std::string source;
    if (!read_file(path, source)) { std::cerr << "[ERROR] Could not open: " << path << "\n"; return 1; }
    Lexer lexer(source, path);
    auto tokens = lexer.tokenize();
    for (const auto& t : tokens) {
        std::cout << t.line << ":" << t.col << "  " << token_type_name(t.type);
        if (t.type == TokenType::IDENTIFIER || t.type == TokenType::NUMBER) std::cout << "  (" << t.lexeme << ")";
        else if (t.type == TokenType::STRING) std::cout << "  (\"" << t.str_value << "\")";
        std::cout << "\n";
    }
    for (auto& e : lexer.errors()) report("LEX ERROR", path, e.line, e.col, e.message);
    std::cout << "\n-- " << tokens.size() << " tokens, " << lexer.errors().size() << " lex error(s) --\n";
    return lexer.ok() ? 0 : 1;
}

static int run_check(const std::string& path, bool dump_ast) {
    std::string source;
    if (!read_file(path, source)) { std::cerr << "[ERROR] Could not open: " << path << "\n"; return 1; }

    std::unique_ptr<ProgramStmt> program;
    SemanticAnalyzer sema;
    bool ok = front_end(path, source, program, sema);

    if (dump_ast) {
        std::cout << print_ast(*program);
        std::cout << "\n-- " << (ok ? "OK, no errors" : "completed WITH errors (the tree may be partial)") << " --\n";
    } else if (ok) {
        std::cout << "[OK] " << path << " — no errors.\n";
    } else {
        std::cout << "[FAILED] " << path << "\n";
    }
    if (ok) print_import_stats(*program, sema);
    return ok ? 0 : 1;
}

static int run_emit_ir(const std::string& path, const std::string& out_path) {
    std::string source;
    if (!read_file(path, source)) { std::cerr << "[ERROR] Could not open: " << path << "\n"; return 1; }

    std::unique_ptr<ProgramStmt> program;
    SemanticAnalyzer sema;
    if (!front_end(path, source, program, sema)) return 1;

    CodeGen gen;
    std::string ir;
    if (!gen.generate(*program, sema, ir)) {
        for (auto& e : gen.errors()) report("CODEGEN ERROR", e.file.empty() ? path : e.file, e.line, e.col, e.message);
        std::cerr << gen.errors().size() << " error(s).\n";
        return 1;
    }
    if (out_path.empty()) { std::cout << ir; return 0; }
    std::ofstream out(out_path, std::ios::binary);
    if (!out) { std::cerr << "[ERROR] Could not write: " << out_path << "\n"; return 1; }
    out << ir;
    std::cout << "[OK] wrote " << out_path << "\n";
    return 0;
}

// Only plain path characters are accepted for user-supplied paths: they end up inside a shell command.
static bool is_safe_path(const std::string& p) {
    for (unsigned char c : p) {
        if (std::isalnum(c) || c == '_' || c == '-' || c == '.' || c == '/' || c == '\\' || c == ':') continue;
        return false;
    }
    return true;
}

static fs::path find_runtime(const char* argv0) {
    std::vector<fs::path> candidates;
    std::error_code ec;
    if (const char* env = std::getenv("NOVAC_RUNTIME")) candidates.push_back(env);
    fs::path dir = fs::absolute(argv0, ec).parent_path();
    candidates.push_back(dir / "runtime" / "nova_rt.c");
    candidates.push_back(dir.parent_path() / "runtime" / "nova_rt.c");
    candidates.push_back(fs::current_path(ec) / "runtime" / "nova_rt.c");
    for (const auto& c : candidates)
        if (fs::is_regular_file(c, ec)) return c;
    return {};
}

static bool generate_ir(const std::string& path, std::string& ir) {
    std::string source;
    if (!read_file(path, source)) { std::cerr << "[ERROR] Could not open: " << path << "\n"; return false; }
    std::unique_ptr<ProgramStmt> program;
    SemanticAnalyzer sema;
    if (!front_end(path, source, program, sema)) return false;
    CodeGen gen;
    if (!gen.generate(*program, sema, ir)) {
        for (auto& e : gen.errors()) report("CODEGEN ERROR", e.file.empty() ? path : e.file, e.line, e.col, e.message);
        std::cerr << gen.errors().size() << " error(s).\n";
        return false;
    }
    return true;
}

static int run_build(const std::string& path, const std::string& out, bool keep_ir, bool run_after, const char* argv0) {
    if (!is_safe_path(path) || (!out.empty() && !is_safe_path(out))) {
        std::cerr << "[ERROR] Paths may only contain letters, digits, '.', '_', '-', '/', '\\' and ':' (no spaces).\n";
        return 1;
    }
    std::string ir;
    if (!generate_ir(path, ir)) return 1;

    fs::path exe = out.empty() ? fs::path(path).replace_extension(EXE_EXT) : fs::path(out);
    fs::path ll = fs::path(exe).replace_extension(".ll");
    {
        std::ofstream f(ll, std::ios::binary);
        if (!f) { std::cerr << "[ERROR] Could not write: " << ll.string() << "\n"; return 1; }
        f << ir;
    }
    fs::path rt = find_runtime(argv0);
    if (rt.empty()) {
        std::cerr << "[ERROR] Cannot find runtime/nova_rt.c (looked next to novac, one folder up, and in .\\runtime).\n"
                  << "        Set the NOVAC_RUNTIME environment variable to its full path.\n";
        return 1;
    }
    if (std::system("clang --version > " NOVAC_NULL_DEV " 2>&1") != 0) {
        std::cerr << "[ERROR] clang was not found on PATH. Install it (MSYS2: pacman -S mingw-w64-ucrt-x86_64-clang, or winget install LLVM.LLVM).\n";
        return 1;
    }
    std::string cmd = "clang -Wno-override-module \"" + ll.string() + "\" \"" + rt.string() + "\" -o \"" + exe.string() + "\"";
    if (std::system(cmd.c_str()) != 0) { std::cerr << "[ERROR] clang failed to build the executable.\n"; return 1; }
    if (!keep_ir) { std::error_code ec; fs::remove(ll, ec); }
    std::cout << "[OK] built " << exe.string() << "\n";
    if (!run_after) return 0;

    std::error_code ec;
    int status = std::system(("\"" + fs::absolute(exe, ec).string() + "\"").c_str());
#ifndef _WIN32
    if (WIFEXITED(status)) status = WEXITSTATUS(status);
#endif
    return status;
}

int main(int argc, char** argv) {
    if (argc == 2) {
        std::string a = argv[1];
        if (a == "--version" || a == "-v") { std::cout << "novac " NOVAC_VERSION "\n"; return 0; }
        if (a == "--help" || a == "-h") { print_usage(argv[0], std::cout); return 0; }
    }
    if (argc < 3) { print_usage(argv[0]); return 1; }
    std::string mode = argv[1], path = argv[2];
    if (mode == "build" || mode == "run") {
        std::string out;
        bool keep_ir = false;
        for (int i = 3; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "-o" && i + 1 < argc) out = argv[++i];
            else if (a == "--keep-ir") keep_ir = true;
            else { std::cerr << "[ERROR] Unknown option: " << a << "\n"; return 1; }
        }
        return run_build(path, out, keep_ir, mode == "run", argv[0]);
    }
    if (mode == "--tokens") return run_tokens(path);
    if (mode == "--check") return run_check(path, false);
    if (mode == "--ast") return run_check(path, true);
    if (mode == "--emit-ir") return run_emit_ir(path, (argc >= 5 && std::string(argv[3]) == "-o") ? argv[4] : "");
    std::cerr << "[ERROR] Unknown mode: " << mode << "\n\n";
    print_usage(argv[0]);
    return 1;
}
