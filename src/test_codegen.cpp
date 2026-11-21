// test_codegen.cpp — Parts 4a-4c: checks on the emitted IR text (no LLVM needed to run these).
#include "lexer.h"
#include "parser.h"
#include "semantic.h"
#include "codegen.h"
#include <iostream>
#include <sstream>

using namespace novac;
int fails = 0;
#define CHECK(c, m) do { if (!(c)) { std::cerr << "FAIL: " << m << "\n"; fails++; } } while(0)

struct Out { bool ok = false; std::string ir; std::vector<CodegenError> errors; bool front_ok = false; };
static Out gen(const std::string& body) {
    Lexer lex("#include <nova>\n" + body);
    Parser p(lex.tokenize());
    auto prog = p.parse_program();
    SemanticAnalyzer sema;
    sema.analyze(*prog);
    Out o;
    o.front_ok = p.ok() && sema.ok();
    CodeGen cg;
    o.ok = cg.generate(*prog, sema, o.ir);
    o.errors = cg.errors();
    return o;
}
static bool has(const std::string& s, const std::string& n) { return s.find(n) != std::string::npos; }
static size_t count(const std::string& s, const std::string& n) {
    size_t c = 0, pos = 0;
    while ((pos = s.find(n, pos)) != std::string::npos) { ++c; pos += n.size(); }
    return c;
}

static void test_4a() {
    { auto o = gen("");
      CHECK(o.ok && has(o.ir, "define i32 @main()") && has(o.ir, "ret i32 0"), "empty program is a valid main"); }
    { auto o = gen("pf \"Ola\"\n");
      CHECK(o.ok && has(o.ir, "c\"Ola\\0A\\00\""), "text + newline + NUL emitted as a constant");
      CHECK(has(o.ir, "c\"%s\\00\"") && has(o.ir, "@printf(ptr @.str."), "printed through a \"%s\" format"); }
    { auto o = gen("pf \"a\" + \"b\" + \" c\"\n");
      CHECK(o.ok && has(o.ir, "c\"ab c\\0A\\00\""), "literal + chain folds into one string"); }
    { auto o = gen("pf \"100%\"\n");
      CHECK(o.ok && has(o.ir, "c\"100%\\0A\\00\""), "percent is data, never a format string"); }
    { auto o = gen("pf \"q\\\"b\\\\ç\"\n");
      CHECK(o.ok && has(o.ir, "q\\22b\\5C\\C3\\A7"), "quote, backslash and UTF-8 bytes are escaped"); }
    { auto o = gen("pf \"x\"\npf \"x\"\n");
      CHECK(o.ok && count(o.ir, "c\"x\\0A\\00\"") == 1, "identical strings share one constant"); }
}

static void test_4b() {
    { auto o = gen("x = 5\npf x\n");
      CHECK(o.ok && has(o.ir, "alloca i64") && has(o.ir, "store i64 5") && has(o.ir, "load i64") && has(o.ir, "c\"%lld\\00\""),
            "int variable: alloca/store/load, printed with %lld"); }
    { auto o = gen("pf 2 + 3 * 4\n");
      CHECK(o.ok && has(o.ir, "mul i64 3, 4") && has(o.ir, "add i64 2,"), "precedence: mul before add"); }
    { auto o = gen("pf 1 + 2\n");
      CHECK(o.ok && has(o.ir, "add i64 1, 2") && !has(o.ir, "c\"12"), "numeric + inside pf is arithmetic, not digits glued together"); }
    { auto o = gen("pf \"a\" + 1 + \"b\"\n");
      CHECK(o.ok && has(o.ir, "c\"a1b\\0A\\00\""), "literal string/int chain folds at compile time"); }
    { auto o = gen("n = 3\npf \"n=\" + n\n");
      CHECK(o.ok && has(o.ir, "c\"n=\\00\"") && has(o.ir, "c\"\\0A\\00\"") && has(o.ir, "i64 %t"), "mixed pieces: text, value, newline"); }
    { auto o = gen("pf 1.5 + 2\n");
      CHECK(o.ok && has(o.ir, "sitofp i64 2 to double") && has(o.ir, "fadd double") && has(o.ir, "c\"%g\\00\""), "int promoted to double, printed with %g"); }
    { auto o = gen("pf 1 < 2\n");
      CHECK(o.ok && has(o.ir, "icmp slt i64 1, 2") && has(o.ir, "select i1") && has(o.ir, "c\"true\\00\""), "comparison prints true/false"); }
    { auto o = gen("f = 1.5\npf f > 1\n");
      CHECK(o.ok && has(o.ir, "fcmp ogt double"), "float comparison"); }
    { auto o = gen("a = 1\nb = 0\npf b != 0 && a / b > 1\n");
      size_t rhs = o.ir.find("logic.rhs.0:"), div = o.ir.find("sdiv");
      CHECK(o.ok && has(o.ir, "phi i1") && rhs != std::string::npos && div > rhs, "&& short-circuits: sdiv only in the rhs block"); }
    { auto o = gen("x = 1\nx += 2\nx++\nx -= 1\nx *= 3\nx /= 2\nx--\n");
      CHECK(o.ok && count(o.ir, "add i64") == 2 && has(o.ir, "sub i64") && has(o.ir, "mul i64") && has(o.ir, "sdiv i64"),
            "compound assignment and ++/--"); }
    { auto o = gen("f = 1.5\nf += 2\nf++\n");
      CHECK(o.ok && has(o.ir, "fadd double") && has(o.ir, "store double"), "float compound/incdec stay double"); }
    { auto o = gen("s = \"abc\"\npf s\ns = \"x\"\npf s\n");
      CHECK(o.ok && has(o.ir, "alloca ptr") && has(o.ir, "load ptr"), "string variables hold pointers to constants"); }
    { auto o = gen("let ok = 1 < 2 && !(2 < 1)\npf ok\n");
      CHECK(o.ok && has(o.ir, "xor i1") && has(o.ir, "alloca i1"), "bool variable, logical not"); }
    { auto o = gen("pf -5\nx = 2.5\npf -x\n");
      CHECK(o.ok && has(o.ir, "sub i64 0, 5") && has(o.ir, "fneg double"), "unary minus"); }
    { auto o = gen("pf 7 % 2\n");
      CHECK(o.ok && has(o.ir, "srem i64 7, 2"), "modulo"); }
}

// Every `br`/`ret` must be the last instruction of its block (next line is a label or `}`).
static bool well_formed(const std::string& ir) {
    std::istringstream in(ir);
    std::string ln;
    bool prev_terminator = false;
    while (std::getline(in, ln)) {
        bool is_inst = ln.rfind("  ", 0) == 0;
        if (prev_terminator && is_inst) return false;
        prev_terminator = is_inst && (ln.rfind("  br ", 0) == 0 || ln.rfind("  ret ", 0) == 0);
    }
    return true;
}

static void test_4c() {
    { auto o = gen("n = 5\nif (n > 3) {\n  pf \"a\"\n} else {\n  pf \"b\"\n}\n");
      CHECK(o.ok && has(o.ir, "if.then.") && has(o.ir, "if.else.") && has(o.ir, "if.end.") && count(o.ir, "br i1") == 1, "if/else blocks"); }
    { auto o = gen("n = 5\nif (n > 9) {\n  pf 1\n} elif (n > 3) {\n  pf 2\n} elif (n > 1) {\n  pf 3\n} else {\n  pf 4\n}\n");
      CHECK(o.ok && count(o.ir, "br i1") == 3 && well_formed(o.ir), "if/elif/elif/else: one conditional branch per condition"); }
    { auto o = gen("i = 0\nwhile (i < 3) {\n  i++\n}\n");
      CHECK(o.ok && has(o.ir, "while.cond.0:") && has(o.ir, "while.body.1:") && has(o.ir, "while.end.2:") &&
            count(o.ir, "br label %while.cond.0") == 2, "while: entry jump + back edge"); }
    { auto o = gen("for (i = 0; i < 3; i++) {\n  if (i == 1) {\n    continue\n  }\n  pf i\n}\n");
      CHECK(o.ok && has(o.ir, "for.inc.") && has(o.ir, "br label %for.inc.") && well_formed(o.ir), "continue in for jumps to the increment"); }
    { auto o = gen("while (true) {\n  break\n}\n");
      CHECK(o.ok && has(o.ir, "br label %while.end.") && has(o.ir, "dead.") && well_formed(o.ir), "break leaves the loop; code after it lands in a dead block"); }
    { auto o = gen("for (;;) {\n  break\n}\n");
      CHECK(o.ok && well_formed(o.ir), "for(;;) with no condition"); }
    { auto o = gen("d = 2\nswitch (d) {\ncase 1:\n  pf \"a\"\ncase 2:\n  pf \"b\"\ndefault:\n  pf \"c\"\n}\n");
      CHECK(o.ok && count(o.ir, "icmp eq i64") == 2 && has(o.ir, "sw.default.") && has(o.ir, "sw.end.") && well_formed(o.ir),
            "switch: one compare per case, default, no fallthrough (each body ends with br to sw.end)");
      CHECK(count(o.ir, "br label %sw.end.") == 3, "every case body leaves the switch"); }
    { auto o = gen("s = \"a\"\nswitch (s) {\ncase \"a\":\n  pf 1\n}\n");
      CHECK(o.front_ok && !o.ok && has(o.errors[0].message, "4e"), "string switch -> planned for 4e"); }
    { auto o = gen("for (i = 0; i < 2; i++) {\n  for (j = 0; j < 2; j++) {\n    if (j > i) {\n      break\n    }\n    pf i + j\n  }\n}\n");
      CHECK(o.ok && well_formed(o.ir), "nested loops with break"); }
    { auto o = gen("v = 1\nif (true) {\n  let v = 2\n  pf v\n}\npf v\n");
      CHECK(o.ok && count(o.ir, "alloca i64") == 2, "block-local shadowing gets its own slot"); }
    { auto o = gen("while (1 < 2 && 2 < 3) {\n  break\n}\n");
      CHECK(o.ok && has(o.ir, "phi i1") && well_formed(o.ir), "short-circuit condition inside a loop"); }
}

static void test_4d() {
    { auto o = gen("func add(a, b) {\n  return a + b\n}\npf add(1, 2)\npf add(1.5, 2)\n");
      CHECK(o.ok && has(o.ir, "define i64 @\"add(int,int)\"(i64 %p.a, i64 %p.b)") &&
            has(o.ir, "define double @\"add(float,int)\"(double %p.a, i64 %p.b)"),
            "one specialized function per argument-type combination");
      CHECK(has(o.ir, "call i64 @\"add(int,int)\"(i64 1, i64 2)") && well_formed(o.ir), "typed call arguments"); }
    { auto o = gen("func hi(n) {\n  pf n\n}\nhi(1)\nhi(2)\n");
      CHECK(o.ok && count(o.ir, "define void @\"hi(int)\"") == 1 && count(o.ir, "call void @\"hi(int)\"") == 2 && has(o.ir, "ret void"),
            "void function: one instance, two calls"); }
    { auto o = gen("func fact(n) {\n  if (n <= 1) {\n    return 1\n  }\n  return n * fact(n - 1)\n}\npf fact(5)\n");
      CHECK(o.ok && count(o.ir, "define i64 @\"fact(int)\"") == 1 && count(o.ir, "call i64 @\"fact(int)\"") == 2 && well_formed(o.ir),
            "self recursion"); }
    { auto o = gen("func unused(a) {\n  pf a\n}\npf 1\n");
      CHECK(o.ok && !has(o.ir, "unused"), "uncalled function is not emitted"); }
    { auto o = gen("pf twice(4)\nfunc twice(x) {\n  return x * 2\n}\n");
      CHECK(o.ok && has(o.ir, "define i64 @\"twice(int)\""), "function defined after its first use"); }
    { auto o = gen("func g(a) {\n  return a + 1\n}\nfunc f(a) {\n  return g(a) * 2\n}\npf f(3)\n");
      CHECK(o.ok && has(o.ir, "@\"g(int)\"") && has(o.ir, "@\"f(int)\""), "function calling a function"); }
    { auto o = gen("func greet(n) {\n  pf \"hi \" + n\n}\ngreet(\"x\")\ngreet(7)\n");
      CHECK(o.ok && has(o.ir, "define void @\"greet(string)\"(ptr %p.n)") && has(o.ir, "define void @\"greet(int)\"(i64 %p.n)"),
            "string and int specializations"); }
    { auto o = gen("func find(n) {\n  for (i = 0; i < 10; i++) {\n    if (i == n) {\n      return i\n    }\n  }\n  return 0 - 1\n}\npf find(3)\n");
      CHECK(o.ok && well_formed(o.ir), "return from inside a loop"); }
    { auto o = gen("func f(a) {\n  return a\n}\nf(1)\n");
      CHECK(o.ok, "call result discarded as a statement"); }
}

static void test_unsupported() {
    { auto o = gen("x = random(1, 5)\n");
      CHECK(o.front_ok && !o.ok && has(o.errors[0].message, "4f"), "builtins -> planned for 4f"); }
    { auto o = gen("l = [1; 2]\nfor (x in l) {\n  pf x\n}\n");
      CHECK(o.front_ok && !o.ok && has(o.errors.back().message, "foreach"), "foreach -> planned for 4e"); }
    { auto o = gen("s = \"a\" + \"b\"\n");
      CHECK(o.front_ok && !o.ok && has(o.errors[0].message, "4e"), "string concatenation outside pf -> 4e"); }
    { auto o = gen("l = [1; 2]\n");
      CHECK(o.front_ok && !o.ok && has(o.errors[0].message, "4e"), "arrays -> 4e"); }
    { auto o = gen("for (x in [1; 2]) {\n  pf x\n}\nsleep(1)\nl = [1]\n");
      CHECK(!o.ok && o.errors.size() == 3, "all unsupported constructs reported in one pass"); }
}

int main() {
    test_4a();
    test_4b();
    test_4c();
    test_4d();
    test_unsupported();
    if (fails == 0) { std::cout << "Codegen tests (4a + 4b + 4c + 4d): all passed.\n"; return 0; }
    std::cerr << fails << " check(s) FAILED.\n";
    return 1;
}
