// test_parser.cpp — combined self-tests for Parts 2a (AST), 2b (expression
// parsing), and 2c (simple-statement parsing). This is a development test
// harness, not the final `novac` CLI (that lands in Part 2f as main.cpp).
// Run it after any change to lexer.cpp/parser.cpp/ast.cpp to confirm nothing
// regressed.
#include "lexer.h"
#include "parser.h"
#include "ast.h"
#include <iostream>

using namespace novac;

int fails = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::cerr << "FAIL: " << msg << "\n"; fails++; } } while(0)

static std::unique_ptr<ProgramStmt> parse_program_str(const std::string& src, Parser** out_parser = nullptr);

static ExprPtr parse_expr_str(const std::string& src, Parser** out_parser = nullptr) {
    Lexer lex(src);
    auto tokens = lex.tokenize();
    static std::vector<std::unique_ptr<Parser>> keep;
    keep.push_back(std::make_unique<Parser>(std::move(tokens)));
    if (out_parser) *out_parser = keep.back().get();
    return keep.back()->parse_expression();
}

static StmtPtr parse_stmt_str(const std::string& src, Parser** out_parser = nullptr) {
    Lexer lex(src);
    auto tokens = lex.tokenize();
    static std::vector<std::unique_ptr<Parser>> keep;
    keep.push_back(std::make_unique<Parser>(std::move(tokens)));
    if (out_parser) *out_parser = keep.back().get();
    return keep.back()->parse_statement();
}

static void run_2b_expression_tests() {
    std::cout << "== Part 2b: expression parsing ==\n";

    { // precedence
        auto e = parse_expr_str("2 + 3 * 4");
        auto* bin = static_cast<BinaryExpr*>(e.get());
        CHECK(bin->op == TokenType::PLUS, "2+3*4 top op should be +");
        CHECK(static_cast<BinaryExpr*>(bin->right.get())->op == TokenType::STAR, "right should be *");
    }
    { // parens override precedence
        auto e = parse_expr_str("(2 + 3) * 4");
        CHECK(static_cast<BinaryExpr*>(e.get())->op == TokenType::STAR, "(2+3)*4 top op should be *");
    }
    { // the reported concat bug, as an expression
        Parser* p;
        auto e = parse_expr_str(R"("Jogador tentou " + tentativas + " vezes.")", &p);
        CHECK(p->ok(), "concat expr should parse with 0 errors");
    }
    { // lista.1 (literal index)
        auto e = parse_expr_str("lista.1");
        CHECK(e->kind == ExprKind::Index, "lista.1 should be Index");
        CHECK(static_cast<IndexExpr*>(e.get())->index->kind == ExprKind::Number, "index should be Number");
    }
    { // lista.(i) -- the reported "array index only accepts a literal" limitation
        auto e = parse_expr_str("lista.(i)");
        CHECK(e->kind == ExprKind::Index, "lista.(i) should be Index");
        CHECK(static_cast<IndexExpr*>(e.get())->index->kind == ExprKind::Identifier, "index should be Identifier");
    }
    { // lista.(i + 1) -- index by a full expression, not just a bare variable
        auto e = parse_expr_str("lista.(i + 1)");
        CHECK(static_cast<IndexExpr*>(e.get())->index->kind == ExprKind::Binary, "index should be Binary");
    }
    { // fruits.length
        auto e = parse_expr_str("fruits.length");
        CHECK(e->kind == ExprKind::Property, "fruits.length should be Property");
    }
    { // texto.upper()
        auto e = parse_expr_str("texto.upper()");
        CHECK(e->kind == ExprKind::MethodCall, "texto.upper() should be MethodCall");
        CHECK(static_cast<MethodCallExpr*>(e.get())->args.empty(), "upper() takes no args");
    }
    { // frase.contains("Nova")
        auto e = parse_expr_str(R"(frase.contains("Nova"))");
        CHECK(static_cast<MethodCallExpr*>(e.get())->args.size() == 1, "contains() takes 1 arg");
    }
    { // somar(2, 3)
        auto e = parse_expr_str("somar(2, 3)");
        CHECK(e->kind == ExprKind::Call, "somar(2,3) should be Call");
        auto* c = static_cast<CallExpr*>(e.get());
        CHECK(c->callee == "somar" && c->args.size() == 2, "callee/args should match");
    }
    { // chained postfix: lista.(i).upper()
        auto e = parse_expr_str("lista.(i).upper()");
        CHECK(e->kind == ExprKind::MethodCall, "top should be MethodCall");
        CHECK(static_cast<MethodCallExpr*>(e.get())->object->kind == ExprKind::Index, "object should be Index");
    }
    { // logical chain: a == 1 && b != 2 || c
        auto e = parse_expr_str("a == 1 && b != 2 || c");
        CHECK(static_cast<BinaryExpr*>(e.get())->op == TokenType::OR, "top op should be ||");
    }
    { // unary !
        auto e = parse_expr_str("!ativo");
        CHECK(e->kind == ExprKind::Unary, "!ativo should be Unary");
    }
    { // array literal
        auto e = parse_expr_str(R"(["a"; "b"; "c"])");
        CHECK(static_cast<ArrayLiteralExpr*>(e.get())->elements.size() == 3, "should have 3 elements");
    }
    { // malformed index should be reported, not silently accepted
        Parser* p;
        parse_expr_str("lista.", &p);
        CHECK(!p->ok(), "'lista.' with nothing after should be a parse error");
    }
}

static void run_2c_statement_tests() {
    std::cout << "== Part 2c: statement parsing ==\n";

    { // let
        auto s = parse_stmt_str("let x = 5");
        CHECK(s->kind == StmtKind::VarDecl, "let x = 5 should be VarDecl");
        CHECK(static_cast<VarDeclStmt*>(s.get())->mutability == Mutability::Let, "mutability should be Let");
    }
    { // const
        auto s = parse_stmt_str(R"(const nome = "SysKore")");
        CHECK(static_cast<VarDeclStmt*>(s.get())->mutability == Mutability::Const, "mutability should be Const");
    }
    { // bare assignment -> AssignStmt, not VarDeclStmt (Part 3 decides decl-vs-reassign)
        auto s = parse_stmt_str("contador = 0");
        CHECK(s->kind == StmtKind::Assign, "bare x = v should be Assign");
    }
    { // THE bug: x += 1 must not corrupt into "x + = 1"
        Parser* p;
        auto s = parse_stmt_str("x += 1", &p);
        CHECK(p->ok(), "x += 1 should parse with 0 errors");
        auto* a = static_cast<AssignStmt*>(s.get());
        CHECK(a->name == "x" && a->op == TokenType::PLUS_ASSIGN, "name/op should be intact");
    }
    { auto s = parse_stmt_str("y -= 2"); CHECK(static_cast<AssignStmt*>(s.get())->op == TokenType::MINUS_ASSIGN, "-="); }
    { auto s = parse_stmt_str("z *= 3"); CHECK(static_cast<AssignStmt*>(s.get())->op == TokenType::STAR_ASSIGN, "*="); }
    { auto s = parse_stmt_str("w /= 4"); CHECK(static_cast<AssignStmt*>(s.get())->op == TokenType::SLASH_ASSIGN, "/="); }
    { // THE bug: x++ must not split into "x + +"
        Parser* p;
        auto s = parse_stmt_str("x++", &p);
        CHECK(p->ok(), "x++ should parse with 0 errors");
        auto* id = static_cast<IncDecStmt*>(s.get());
        CHECK(id->name == "x" && id->op == TokenType::PLUS_PLUS && !id->prefix, "postfix ++ intact");
    }
    { auto s = parse_stmt_str("y--"); CHECK(static_cast<IncDecStmt*>(s.get())->op == TokenType::MINUS_MINUS, "-- postfix"); }
    { // prefix ++/--
        auto s = parse_stmt_str("++x");
        auto* id = static_cast<IncDecStmt*>(s.get());
        CHECK(id->prefix && id->name == "x", "prefix ++ intact");
    }
    { // pf with the reported mixed-type concat bug
        Parser* p;
        auto s = parse_stmt_str(R"(pf "Jogador tentou " + tentativas + " vezes.")", &p);
        CHECK(p->ok(), "pf concat should parse with 0 errors");
        CHECK(s->kind == StmtKind::Pf, "should be Pf");
    }
    { // return with value
        auto s = parse_stmt_str("return resultado");
        CHECK(static_cast<ReturnStmt*>(s.get())->value->kind == ExprKind::Identifier, "return value should be Identifier");
    }
    { // bare return
        Lexer lex("return\npf 1");
        auto tokens = lex.tokenize();
        Parser p(std::move(tokens));
        auto s = p.parse_statement();
        CHECK(static_cast<ReturnStmt*>(s.get())->value == nullptr, "bare return should have null value");
    }
    { // free function call as a statement
        Parser* p;
        auto s = parse_stmt_str("calcnum(numero1, numero2)", &p);
        CHECK(p->ok(), "call statement should parse with 0 errors");
        CHECK(s->kind == StmtKind::ExprStmt, "should be ExprStmt");
        auto* c = static_cast<CallExpr*>(static_cast<ExprStmt*>(s.get())->expr.get());
        CHECK(c->callee == "calcnum" && c->args.size() == 2, "callee/args should match");
    }
    { // trailing semicolon is optional
        auto s = parse_stmt_str("x = 5;");
        CHECK(s->kind == StmtKind::Assign, "semicolon-terminated assign should still parse");
    }
}

static void run_2d_control_flow_tests() {
    std::cout << "== Part 2d: control flow (if/elif/else, while, for, foreach) ==\n";

    { // plain if
        Parser* p;
        auto s = parse_stmt_str("if (x == 1) {\n    pf \"um\"\n}", &p);
        CHECK(p->ok(), "plain if should parse with 0 errors");
        CHECK(s->kind == StmtKind::If, "should be If");
    }
    { // if/elif/else, compact style ("} elif (...) {" same line)
        Parser* p;
        auto s = parse_stmt_str(
            "if (contador == 1) {\n    pf \"um\"\n} elif (contador == 2) {\n    pf \"dois\"\n} else {\n    pf \"outro\"\n}", &p);
        CHECK(p->ok(), "if/elif/else compact should parse with 0 errors");
        auto* iff = static_cast<IfStmt*>(s.get());
        CHECK(iff->elifs.size() == 1 && iff->else_body.has_value(), "should have 1 elif + else");
    }
    { // if/elif/else spread across separate lines
        Parser* p;
        auto s = parse_stmt_str("if (x == 1) {\n    pf \"a\"\n}\nelif (x == 2) {\n    pf \"b\"\n}\nelse {\n    pf \"c\"\n}", &p);
        CHECK(p->ok(), "spread-out if/elif/else should parse with 0 errors");
        auto* iff = static_cast<IfStmt*>(s.get());
        CHECK(iff->elifs.size() == 1 && iff->else_body.has_value(), "should have 1 elif + else (spread form)");
    }
    { // multiple elifs
        auto s = parse_stmt_str("if (x == 1) { pf 1 } elif (x == 2) { pf 2 } elif (x == 3) { pf 3 }");
        CHECK(static_cast<IfStmt*>(s.get())->elifs.size() == 2, "should have 2 elifs");
    }
    { // while
        Parser* p;
        auto s = parse_stmt_str("while (!terminou) {\n    terminou = true\n}", &p);
        CHECK(p->ok(), "while should parse with 0 errors");
        CHECK(s->kind == StmtKind::While, "should be While");
    }
    { // classic for
        Parser* p;
        auto s = parse_stmt_str("for (i = 1; i <= 10; i++) {\n    pf i\n}", &p);
        CHECK(p->ok(), "classic for should parse with 0 errors");
        CHECK(s->kind == StmtKind::For, "should be For");
        auto* f = static_cast<ForStmt*>(s.get());
        CHECK(f->init && f->init->kind == StmtKind::Assign, "init should be Assign");
        CHECK(f->increment && f->increment->kind == StmtKind::IncDec, "increment should be IncDec");
    }
    { // for(;;) empty clauses
        auto s = parse_stmt_str("for (;;) {\n    pf 1\n}");
        auto* f = static_cast<ForStmt*>(s.get());
        CHECK(!f->init && !f->condition && !f->increment, "all clauses should be null");
    }
    { // foreach
        Parser* p;
        auto s = parse_stmt_str("for (fruit in fruits) {\n    pf fruit.upper()\n}", &p);
        CHECK(p->ok(), "foreach should parse with 0 errors");
        CHECK(s->kind == StmtKind::Foreach, "should be Foreach, not classic For");
        CHECK(static_cast<ForeachStmt*>(s.get())->item_name == "fruit", "item_name should be fruit");
    }
    { // classic for must not be misparsed as foreach
        auto s = parse_stmt_str("for (contador = 0; contador < 5; contador++) { pf contador }");
        CHECK(s->kind == StmtKind::For, "must stay classic For");
    }
    { // deep nesting: while -> if -> for
        Parser* p;
        auto s = parse_stmt_str(
            "while (ativo) {\n    if (x == 1) {\n        for (i = 0; i < 3; i++) {\n            pf i\n        }\n    }\n}", &p);
        CHECK(p->ok(), "deep nesting should parse with 0 errors");
        auto* w = static_cast<WhileStmt*>(s.get());
        CHECK(w->body[0]->kind == StmtKind::If, "while body should hold If");
        CHECK(static_cast<IfStmt*>(w->body[0].get())->then_body[0]->kind == StmtKind::For, "if body should hold For");
    }
    { // malformed if reports an error instead of hanging
        Parser* p;
        parse_stmt_str("if (x == 1 {\n pf 1 \n}", &p);
        CHECK(!p->ok(), "malformed if should be a parse error");
    }
}

static void run_2e_final_pieces_tests() {
    std::cout << "== Part 2e: func, import, input/sleep/exit, break/continue, switch, .add/.rm, full program ==\n";

    { // func with params + return
        Parser* p;
        auto s = parse_stmt_str("func somar(a, b) {\n    resultado = a + b\n    return resultado\n}", &p);
        CHECK(p->ok(), "func decl should parse with 0 errors");
        auto* fn = static_cast<FuncDeclStmt*>(s.get());
        CHECK(fn->name == "somar" && fn->params.size() == 2, "name/params should match");
    }
    { // func with brace on the next line (documented style)
        Parser* p;
        parse_stmt_str("func greet(name)\n{\n    pf \"oi\"\n}", &p);
        CHECK(p->ok(), "func with brace-on-next-line should parse with 0 errors");
    }
    { // import is a TOP-LEVEL-ONLY construct: call parse_import() directly,
      // not parse_statement() (which correctly does not recognize 'import').
        Lexer lex("import funcao.nvpp");
        auto tokens = lex.tokenize();
        Parser p(std::move(tokens));
        auto s = p.parse_import();
        CHECK(p.ok(), "import (no From) should parse with 0 errors");
        CHECK(static_cast<ImportStmt*>(s.get())->file == "funcao.nvpp", "dotted filename should reconstruct correctly");
    }
    { // import with From "dir"
        Lexer lex(R"(import funcao.nvpp From "libs")");
        auto tokens = lex.tokenize();
        Parser p(std::move(tokens));
        auto s = p.parse_import();
        CHECK(p.ok(), "import with From should parse with 0 errors");
        CHECK(*static_cast<ImportStmt*>(s.get())->from_dir == "libs", "from_dir should be libs");
    }
    { // input / sleep / exit
        auto s = parse_stmt_str("input username");
        CHECK(static_cast<InputStmt*>(s.get())->target == "username", "input target");
    }
    { auto s = parse_stmt_str("sleep(500)"); CHECK(s->kind == StmtKind::Sleep, "sleep"); }
    { auto s = parse_stmt_str("exit 0"); CHECK(static_cast<ExitStmt*>(s.get())->code != nullptr, "exit with code"); }
    { // break / continue
        CHECK(parse_stmt_str("break")->kind == StmtKind::Break, "break");
        CHECK(parse_stmt_str("continue")->kind == StmtKind::Continue, "continue");
    }
    { // switch/case/default
        Parser* p;
        auto s = parse_stmt_str(
            "switch (x) {\ncase 1:\n    pf \"um\"\n    break\ncase 2:\n    pf \"dois\"\ndefault:\n    pf \"outro\"\n}", &p);
        CHECK(p->ok(), "switch should parse with 0 errors");
        auto* sw = static_cast<SwitchStmt*>(s.get());
        CHECK(sw->cases.size() == 3, "should have 3 clauses");
        CHECK(sw->cases[2].value == nullptr, "default clause value should be null");
    }
    { // fruits.add "orange" / fruits.rm "banana" / fruits.rm 2
        Parser* p;
        auto s = parse_stmt_str(R"(fruits.add "orange")", &p);
        CHECK(p->ok(), ".add should parse with 0 errors");
        CHECK(s->kind == StmtKind::ArrayAdd, "should be ArrayAdd");
    }
    { auto s = parse_stmt_str(R"(fruits.rm "banana")"); CHECK(s->kind == StmtKind::ArrayRemove, "rm by value"); }
    { auto s = parse_stmt_str("fruits.rm 2"); CHECK(s->kind == StmtKind::ArrayRemove, "rm by index"); }
    { // '.add(' WITH parens must fall through to a normal MethodCall, not ArrayAdd
        auto s = parse_stmt_str("resultado.add(x)");
        CHECK(s->kind == StmtKind::ExprStmt, "'.add(x)' with parens should be ExprStmt/MethodCall");
        CHECK(static_cast<ExprStmt*>(s.get())->expr->kind == ExprKind::MethodCall, "inner should be MethodCall");
    }
    { // full program, mirroring the original demo.nvpp
        Parser* p;
        auto prog = parse_program_str(
            "#include <nova>\n\n"
            "func media(a, b, c) {\n    soma = a + b + c\n    resultado = soma / 3\n    return resultado\n}\n\n"
            "const TITULO = \"Boletim Nova++\"\nlet total_alunos = 0\n\npf TITULO\n\n"
            "alunos = [\"Ana\"; \"Bruno\"; \"Carla\"]\n\n"
            "for (aluno in alunos) {\n"
            "    nota = media(random(0, 10), random(0, 10), random(0, 10))\n"
            "    pf aluno.upper() + \" -> media: \" + nota\n"
            "    total_alunos += 1\n"
            "}\n\npf \"Total de alunos: \" + total_alunos\n", &p);
        CHECK(p->ok(), "full demo-like program should parse with 0 errors");
        CHECK(prog->has_nova_include, "has_nova_include");
        CHECK(prog->body.size() >= 6, "several top-level statements");
    }
    { // import + noMain, mirroring the exact user-reported scenario
        Parser* p;
        auto prog = parse_program_str(
            "#include <nova>\nimport funcao.nvpp From \"\"\n\nusing noMain\n\n"
            "numero1 = 0\nnumero2 = 0\ninput numero1\ninput numero2\n\ncalcnum(numero1, numero2)\n", &p);
        CHECK(p->ok(), "import+noMain program should parse with 0 errors");
        CHECK(prog->no_main, "no_main should be true");
        CHECK(prog->imports.size() == 1, "1 import");
    }
    { // missing #include <nova> is a reported error
        Parser* p;
        parse_program_str("pf \"oi\"\n", &p);
        CHECK(!p->ok(), "missing #include <nova> should be a parse error");
    }
}

static std::unique_ptr<ProgramStmt> parse_program_str(const std::string& src, Parser** out_parser) {
    Lexer lex(src);
    auto tokens = lex.tokenize();
    static std::vector<std::unique_ptr<Parser>> keep;
    keep.push_back(std::make_unique<Parser>(std::move(tokens)));
    if (out_parser) *out_parser = keep.back().get();
    return keep.back()->parse_program();
}

int main() {
    run_2b_expression_tests();
    run_2c_statement_tests();
    run_2d_control_flow_tests();
    run_2e_final_pieces_tests();

    if (fails == 0) {
        std::cout << "\nAll novac parser tests passed (2a AST + 2b expressions + 2c statements + 2d control flow + 2e final pieces).\n";
        return 0;
    }
    std::cerr << "\n" << fails << " check(s) FAILED.\n";
    return 1;
}
