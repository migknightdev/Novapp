// test_semantic.cpp — automated checks for Part 3 (3a functions, 3b scoping, 3c types, 3d imports).
// Usage: test_semantic            runs the built-in checks
//        test_semantic file.nvpp  parses + analyzes a file and prints its semantic errors
#include "lexer.h"
#include "parser.h"
#include "semantic.h"
#include <iostream>
#include <filesystem>
#include <fstream>
#include <sstream>

using namespace novac;
int fails = 0;
#define CHECK(c, m) do { if (!(c)) { std::cerr << "FAIL: " << m << "\n"; fails++; } } while(0)

struct Result { std::unique_ptr<ProgramStmt> prog; SemanticAnalyzer sema; bool parse_ok; };

static std::unique_ptr<Result> run(const std::string& body) {
    auto r = std::make_unique<Result>();
    Lexer lex("#include <nova>\n" + body);
    Parser p(lex.tokenize());
    r->prog = p.parse_program();
    r->parse_ok = p.ok();
    r->sema.analyze(*r->prog);
    return r;
}
static bool ok(const std::string& src) { auto r = run(src); return r->parse_ok && r->sema.ok(); }
// Expects a clean PARSE but at least one SEMANTIC error (so a typo in a test can't pass by accident).
static bool semerr(const std::string& src) { auto r = run(src); return r->parse_ok && !r->sema.ok(); }
static const AssignStmt* assign_at(const Result& r, size_t i) { return static_cast<const AssignStmt*>(r.prog->body[i].get()); }

static void test_3a() {
    CHECK(ok("func somar(a, b) { return a + b }\n"), "normal func");
    CHECK(semerr("func main() { pf 1 }\n"), "reserved main");
    CHECK(semerr("func f(a) { pf a }\nfunc f(b) { pf b }\n"), "duplicate func");
    CHECK(ok("func a() { pf 1 }\nfunc b() { pf 2 }\n"), "distinct funcs");
    auto r = run("func somar(a, b) { return a + b }\n");
    const Symbol* s = r->sema.globals().resolve("somar");
    CHECK(s && s->param_count == 2, "arity captured");
}

static void test_3b() {
    { // first plain assignment declares; second reassigns
        auto r = run("x = 1\nx = 2\n");
        CHECK(r->sema.ok(), "x=1;x=2 ok");
        CHECK(assign_at(*r, 0)->is_declaration, "first is declaration");
        CHECK(!assign_at(*r, 1)->is_declaration, "second is reassignment");
    }
    // reassignment from inside nested blocks updates the outer variable
    {
        auto r = run("c = 0\nif (true) {\n  c = 5\n}\nwhile (true) {\n  c = 6\n}\n");
        CHECK(r->sema.ok(), "nested reassignment ok");
        auto* iff = static_cast<IfStmt*>(r->prog->body[1].get());
        CHECK(!static_cast<AssignStmt*>(iff->then_body[0].get())->is_declaration, "c=5 inside if is reassignment");
    }
    { // name first assigned inside a block is a block-local declaration
        auto r = run("if (true) {\n  y = 1\n}\n");
        auto* iff = static_cast<IfStmt*>(r->prog->body[0].get());
        CHECK(static_cast<AssignStmt*>(iff->then_body[0].get())->is_declaration, "y=1 in block is declaration");
    }
    CHECK(semerr("if (true) {\n  y = 1\n}\ny += 1\n"), "block-local y not visible outside");
    // const protection
    CHECK(semerr("const x = 5\nx = 6\n"), "const reassign");
    CHECK(semerr("const x = 5\nx += 1\n"), "const compound assign");
    CHECK(semerr("const x = 5\nx++\n"), "const incdec");
    CHECK(ok("let x = 5\nx = 6\nx += 1\nx++\n"), "let is mutable");
    CHECK(semerr("let x = 1\nlet x = 2\n"), "let redeclared in same scope");
    CHECK(ok("let x = 1\nif (true) {\n  let x = 2\n}\n"), "shadowing in inner scope allowed");
    // compound/incdec/input/array targets must exist
    CHECK(semerr("n += 1\n"), "+= on undeclared");
    CHECK(semerr("n++\n"), "++ on undeclared");
    CHECK(semerr("input nome\n"), "input needs declared var");
    CHECK(ok("nome = \"\"\ninput nome\n"), "input on declared var");
    CHECK(semerr("lista.add \"x\"\n"), "add on undeclared array");
    CHECK(ok("lista = [\"a\"]\nlista.add \"x\"\nlista.rm 1\n"), "array ops on declared");
    // loops
    CHECK(ok("for (i = 0; i < 3; i++) {\n  pf i\n}\n"), "classic for declares counter");
    CHECK(ok("i = 5\nfor (i = 0; i < 3; i++) {\n  pf i\n}\n"), "for reuses existing counter");
    CHECK(ok("lista = [\"a\"]\nfor (item in lista) {\n  pf item\n}\n"), "foreach item in scope");
    CHECK(semerr("lista = [\"a\"]\nfor (item in lista) {\n  pf item\n}\nitem++\n"), "foreach item not visible after loop");
    CHECK(semerr("for (i = 0; i < 3; i++) {\n  pf i\n}\ni++\n"), "for counter not visible after loop");
    // functions: params visible, caller variables not
    CHECK(ok("func f(a) {\n  a = a + 1\n  b = 2\n  b += a\n}\n"), "params + locals");
    CHECK(semerr("x = 1\nfunc f() {\n  x += 1\n}\n"), "function can't see main variables");
    CHECK(ok("x = 1\nfunc f() {\n  x = 2\n}\n"), "x=2 in func is its own local declaration");
    CHECK(semerr("func f(a, a) {\n  pf a\n}\n"), "duplicate param");
    // switch case bodies
    CHECK(ok("n = 1\nswitch (n) {\ncase 1:\n  n = 2\n  break\ndefault:\n  n = 3\n}\n"), "switch reassign outer");
}

static void test_3c() {
    // undeclared names in expressions (value is evaluated BEFORE the new name exists)
    CHECK(semerr("pf x\n"), "undeclared in pf");
    CHECK(semerr("y = x + 1\n"), "undeclared in assignment value");
    CHECK(semerr("x = x + 1\n"), "x = x + 1 with undeclared x");
    CHECK(semerr("let x = x + 1\n"), "let x = x + 1 with undeclared x");
    // assignment typing
    CHECK(semerr("x = 5\nx = \"a\"\n"), "int var assigned string");
    CHECK(semerr("x = 5\nx = 2.5\n"), "int var assigned float");
    CHECK(ok("y = 2.5\ny = 1\n"), "float var accepts int");
    CHECK(ok("s = \"a\"\ns += 1\n"), "string += number concatenates");
    CHECK(semerr("n = 1\nn += \"a\"\n"), "int += string");
    CHECK(semerr("n = 1\nn += 0.5\n"), "int += float narrows");
    CHECK(semerr("s = \"a\"\ns++\n"), "++ on string");
    // operators
    CHECK(ok("pf \"a\" + 1 + \"b\"\n"), "string + int + string");
    CHECK(semerr("x = true + 1\n"), "bool + int");
    CHECK(semerr("pf 5.0 % 2\n"), "% on float");
    CHECK(semerr("pf 1 == \"a\"\n"), "int == string");
    CHECK(ok("pf 1 == 2.0\n"), "int == float");
    CHECK(ok("pf \"a\" < \"b\"\n"), "string < string");
    CHECK(semerr("pf \"a\" < 1\n"), "string < int");
    CHECK(semerr("pf -\"a\"\n"), "unary - on string");
    CHECK(semerr("pf !\"a\"\n"), "! on string");
    // arrays
    CHECK(ok("l = [1; 2; 3]\npf l.(1) + 1\npf l.length\n"), "int array indexing");
    CHECK(semerr("l = [1; 2]\npf l.0\n"), "index 0 (indexes start at 1)");
    CHECK(semerr("l = [1; \"a\"]\n"), "mixed array literal");
    CHECK(semerr("l = [1; 2]\npf l.(\"a\")\n"), "string index");
    CHECK(semerr("n = 5\npf n.(1)\n"), "indexing a non-array");
    CHECK(semerr("l = [\"a\"]\nl.add 1\n"), "add int to string array");
    CHECK(ok("n = [1; 2]\nn.add 3\nn.rm 1\n"), "add/rm on int array");
    CHECK(semerr("x = 1\nx.add 2\n"), "add on non-array");
    CHECK(semerr("n = 5\nfor (i in n) {\n  pf i\n}\n"), "foreach over non-array");
    CHECK(ok("n = [1; 2]\nt = 0\nfor (i in n) {\n  t += i\n}\n"), "foreach item typed from array");
    // string methods / properties
    CHECK(ok("s = \"abc\"\npf s.upper()\npf s.length\n"), "string methods");
    CHECK(semerr("s = \"abc\"\npf s.upper(1)\n"), "upper with an arg");
    CHECK(semerr("n = 5\npf n.upper()\n"), "upper on int");
    CHECK(semerr("s = \"a\"\npf s.foo()\n"), "unknown method");
    CHECK(semerr("s = \"a\"\npf s.size\n"), "unknown property");
    CHECK(ok("s = \"a,b\"\nfor (w in s.split(\",\")) {\n  pf w.upper()\n}\n"), "split + foreach + upper");
    // calls
    CHECK(ok("pf random(1, 10)\n"), "builtin random");
    CHECK(semerr("pf random(1)\n"), "random arity");
    CHECK(semerr("x = read_file(1)\n"), "read_file arg type");
    CHECK(ok("write_file(\"a\", \"b\")\n"), "write_file ok");
    CHECK(semerr("nope(1)\n"), "unknown function");
    CHECK(semerr("func f(a, b) {\n  return a + b\n}\nf(1)\n"), "user func arity");
    CHECK(ok("func f(a, b) {\n  return a + b\n}\nx = f(1, 2)\n"), "user func call");
    CHECK(semerr("func p() {\n  pf 1\n}\nx = p()\n"), "using a non-returning function as a value");
    CHECK(ok("func p() {\n  pf 1\n}\np()\n"), "bare call to non-returning function");
    { auto r = run("import lib_that_does_not_exist.nvpp\nfoo(1)\n");
      CHECK(r->parse_ok && r->sema.errors().size() == 1, "missing import reported once, no cascade"); }
    // return rules
    CHECK(semerr("return 1\n"), "return outside function");
    CHECK(semerr("func f() {\n  if (true) {\n    return 1\n  }\n  return \"a\"\n}\n"), "inconsistent return types");
    CHECK(semerr("func f(a) {\n  if (a == 1) {\n    return\n  }\n  return 2\n}\n"), "mixed bare/value return");
    CHECK(ok("func f(a) {\n  if (a == 1) {\n    return 1\n  }\n  return 2\n}\n"), "consistent returns");
    // conditions, sleep, exit, break/continue
    CHECK(semerr("if (\"a\") {\n  pf 1\n}\n"), "string condition");
    CHECK(ok("if (1) {\n  pf 1\n}\n"), "numeric condition");
    CHECK(semerr("sleep(\"a\")\n"), "sleep with string");
    CHECK(ok("sleep(100)\n"), "sleep with int");
    CHECK(semerr("exit \"a\"\n"), "exit with string");
    CHECK(ok("exit 0\n"), "exit 0");
    CHECK(semerr("break\n"), "break outside loop");
    CHECK(semerr("continue\n"), "continue outside loop");
    CHECK(ok("while (true) {\n  break\n}\n"), "break in loop");
    CHECK(ok("n = 1\nswitch (n) {\ncase 1:\n  break\n}\n"), "break in switch");
    CHECK(semerr("n = 1\nswitch (n) {\ncase \"a\":\n  pf 1\n}\n"), "case type mismatch");
}

namespace fs = std::filesystem;
static void write_text(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text;
}
static std::unique_ptr<Result> run_dir(const fs::path& dir, const std::string& body) {
    auto r = std::make_unique<Result>();
    Lexer lex("#include <nova>\n" + body);
    Parser p(lex.tokenize());
    r->prog = p.parse_program();
    r->parse_ok = p.ok();
    r->sema.analyze(*r->prog, dir.string());
    return r;
}

static void test_3d() {
    fs::path d = fs::temp_directory_path() / "novac_test_3d";
    fs::remove_all(d);
    fs::create_directories(d);
    auto semerr_d = [&](const std::string& src) { auto r = run_dir(d, src); return r->parse_ok && !r->sema.ok(); };
    auto ok_d = [&](const std::string& src) { auto r = run_dir(d, src); return r->parse_ok && r->sema.ok(); };

    write_text(d / "lib.nvpp", "#include <nova>\nfunc calcnum(a, b) {\n  s = a + b\n  pf s\n}\n");
    {
        auto r = run_dir(d, "import lib.nvpp\ncalcnum(1, 2)\n");
        CHECK(r->sema.ok(), "basic import");
        CHECK(r->sema.imported_used() == 1 && r->sema.imported_total() == 1, "1 of 1 used");
        bool spliced = false;
        for (auto& s : r->prog->body)
            if (s->kind == StmtKind::FuncDecl) {
                auto* fn = static_cast<FuncDeclStmt*>(s.get());
                if (fn->name == "calcnum" && !fn->source_file.empty()) spliced = true;
            }
        CHECK(spliced, "imported function spliced into the program with its source_file");
    }
    write_text(d / "bare.nvpp", "func ola() {\n  pf \"oi\"\n}\n");
    CHECK(ok_d("import bare.nvpp\nola()\n"), "library without #include <nova>");

    write_text(d / "chain.nvpp", "func principal(a) {\n  helper(a)\n}\nfunc helper(x) {\n  pf x\n}\nfunc unused(z) {\n  pf nope\n}\n");
    {
        auto r = run_dir(d, "import chain.nvpp\nprincipal(1)\n");
        CHECK(r->sema.ok(), "unused library function is not analyzed (even with an error in it)");
        CHECK(r->sema.imported_used() == 2 && r->sema.imported_total() == 3, "2 of 3 used (call chain followed)");
    }
    {
        auto r = run_dir(d, "import chain.nvpp\npf 1\n");
        CHECK(r->sema.ok() && r->sema.imported_used() == 0, "nothing called -> nothing pulled in");
    }
    {
        auto r = run_dir(d, "import missing.nvpp\nfoo(1)\n");
        CHECK(r->parse_ok && r->sema.errors().size() == 1, "missing file: exactly one error");
    }

    write_text(d / "sub" / "inner.nvpp", "func inner() {\n  pf 1\n}\n");
    CHECK(ok_d("import inner.nvpp From \"sub\"\ninner()\n"), "From relative dir");
    CHECK(ok_d("import inner.nvpp From \"" + (d / "sub").generic_string() + "\"\ninner()\n"), "From absolute dir");
    CHECK(semerr_d("import inner.nvpp\ninner()\n"), "inner.nvpp is not in the base dir");

    write_text(d / "a.nvpp", "import b.nvpp\nfunc fa() {\n  fb()\n}\n");
    write_text(d / "b.nvpp", "func fb() {\n  pf 2\n}\n");
    {
        auto r = run_dir(d, "import a.nvpp\nfa()\n");
        CHECK(r->sema.ok() && r->sema.imported_used() == 2, "nested import: a -> b");
    }
    write_text(d / "c1.nvpp", "import c2.nvpp\nfunc f1() {\n  f2()\n}\n");
    write_text(d / "c2.nvpp", "import c1.nvpp\nfunc f2() {\n  pf 1\n}\n");
    CHECK(ok_d("import c1.nvpp\nf1()\n"), "import cycle terminates");

    write_text(d / "x1.nvpp", "func dup() {\n  pf 1\n}\n");
    write_text(d / "x2.nvpp", "func dup() {\n  pf 2\n}\n");
    CHECK(semerr_d("import x1.nvpp\nimport x2.nvpp\ndup()\n"), "same function in two libraries");
    CHECK(semerr_d("import x1.nvpp\nfunc dup() {\n  pf 3\n}\ndup()\n"), "library function collides with the main file's");
    CHECK(ok_d("import x1.nvpp\nfunc other() {\n  pf 3\n}\ndup()\nother()\n"), "no collision, both used");

    write_text(d / "stmt.nvpp", "x = 1\nfunc g() {\n  pf 1\n}\n");
    CHECK(semerr_d("import stmt.nvpp\ng()\n"), "library with a top-level statement");

    write_text(d / "bad.nvpp", "func broken() {\n  pf undefinedvar\n}\n");
    {
        auto r = run_dir(d, "import bad.nvpp\nbroken()\n");
        CHECK(!r->sema.ok() && r->sema.errors()[0].file.find("bad.nvpp") != std::string::npos,
              "error inside a pulled-in function is attributed to its library file");
    }
    write_text(d / "syn.nvpp", "func oops( {\n");
    {
        auto r = run_dir(d, "import syn.nvpp\noops()\n");
        bool has_file = false;
        for (auto& e : r->sema.errors()) if (e.file.find("syn.nvpp") != std::string::npos) has_file = true;
        CHECK(!r->sema.ok() && has_file, "library syntax error reported with its file");
    }
    CHECK(semerr_d("import lib.nvpp\nnope(1)\n"), "unknown function is a real error once imports resolve");
    CHECK(semerr_d("import lib.nvpp\ncalcnum(1)\n"), "arity checked on imported function");

    fs::remove_all(d);
}

static int analyze_file(const char* path) {
    std::ifstream f(path);
    if (!f) { std::cerr << "Could not open " << path << "\n"; return 1; }
    std::stringstream ss; ss << f.rdbuf();
    Lexer lex(ss.str());
    Parser p(lex.tokenize());
    auto prog = p.parse_program();
    for (auto& e : p.errors()) std::cout << "[PARSE] " << e.line << ":" << e.col << " " << e.message << "\n";
    SemanticAnalyzer sema;
    sema.analyze(*prog);
    for (auto& e : sema.errors()) std::cout << "[SEMA] " << e.line << ":" << e.col << " " << e.message << "\n";
    std::cout << (p.ok() && sema.ok() ? "OK\n" : "ERRORS\n");
    return p.ok() && sema.ok() ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc > 1) return analyze_file(argv[1]);
    test_3a();
    test_3b();
    test_3c();
    test_3d();
    if (fails == 0) { std::cout << "Semantic tests (3a + 3b + 3c + 3d): all passed.\n"; return 0; }
    std::cerr << fails << " check(s) FAILED.\n";
    return 1;
}
