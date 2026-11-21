# novac — Nova++ native compiler

Real compiler pipeline for Nova++ (lexer → parser/AST → semantic analysis →
LLVM IR), replacing `vcomp.cpp`'s line-by-line text substitution. `vcomp.cpp`
keeps working unchanged until `novac` reaches parity.

## Roadmap

| Part | Status |
|---|---|
| 1. Lexer | ✅ |
| 2. Parser + AST | ✅ |
| 3. Semantic analysis (functions, scopes, types, imports) | ✅ |
| 4. Codegen → LLVM IR | 🚧 4a ✅ `pf "text"` · 4b ✅ variables/arithmetic · 4c ✅ control flow · 4d ✅ functions · 4e ✅ strings & arrays · 4f input/sleep/exit/random/files + `build`/`run` |
| 5. Runtime + CLI polish | ⬜ |

## Layout

```
build.ps1        build / test / run helper (PowerShell)
src\             the compiler: lexer, ast, parser, types, scope, modules, semantic, codegen, main
tests\           test_*.cpp (unit tests) and *.nvpp fixtures (+ lib\)
build\           generated: novac.exe, test exes, .ll, program exes  -> safe to delete anytime
```

## Build & run (needs g++; clang only to turn IR into an exe)

```powershell
.\build.ps1                         # build\novac.exe
.\build.ps1 -Test                   # + build and run the 3 test suites
.\build.ps1 -Run tests\flow.nvpp    # .nvpp -> IR -> build\flow.exe, then runs it
.\build\novac.exe --check tests\demo.nvpp      # validate only (syntax + semantics)
.\build\novac.exe --emit-ir tests\hello.nvpp -o hello.ll
```
clang on Windows: `pacman -S mingw-w64-ucrt-x86_64-clang` (MSYS2) or `winget install LLVM.LLVM`.
Accents in the Windows console may need `chcp 65001` until the runtime sets the code page (4f).

## What works today

- **Syntax + semantics (Parts 1–3):** every error reported in one pass with a source snippet; block scopes, `const`, static types, arity/return checks, `import` with tree-shaking.
- **Codegen (4a–4c):** `pf`, variables (`let`/`const`, `+= -= *= /=`, `++ --`), `int` (64-bit) / `float` / `bool` / string literals, arithmetic, comparisons, `&&`/`||` with short-circuit, `if/elif/else`, `while`, `for`, `switch`, `break`, `continue`; **functions (4d)**: calls, `return`, recursion, functions defined after use.
- **Strings & arrays (4e):** concatenation anywhere, comparisons, `.upper()/.lower()/.contains()/.split()`, `.length`, array literals (int/float/bool/string), `lista.(i)` / `lista.N` (1-based, bounds-checked with the line number), `.add`, `.rm`, `foreach`, string `switch`, `to_string(x)`. Needs the C runtime: `runtime\nova_rt.c` is linked with the `.ll` (`.\build.ps1 -Run` does it).
- Not yet (clear "planned for 4x" errors): `input`/`sleep`/`exit` and builtins `random`/`read_file`/`write_file`/`to_string` (4f).

## Language decisions made on the way (differences from vcomp)

- Typed `+`: `pf 1 + 2` prints `3`; it concatenates only when a string is involved (`pf "a" + 1 + 2` → `a12`).
- `pf` prints bools as `true`/`false`; floats like C++ streams (`4.5`, `3`, `1e+06`).
- `switch` has **no fallthrough**: each case runs alone (a `break` inside is still allowed); `default` runs when nothing matched.
- Blocks have their own scope: a variable first assigned inside a block does not exist after it; `let` inside a block shadows.
- Stricter checks: `x = 5` then `x = 2.5` is an error; `return` types must agree; functions can't see caller variables.
- **Functions are generic by instantiation.** Parameters are untyped, so each distinct list of argument types gets its own typed copy: `soma(2, 3)` and `soma(1.5, 2)` are `soma(int,int)` and `soma(float,int)` (LLVM symbols `@"soma(int,int)"`). Semantic analysis type-checks each instance (errors point at the line inside the function); only instances actually called are emitted. Parameters are passed **by value** (vcomp's C++ `auto&&` could modify the caller's variable).
- Return type is inferred from the `return` statements and must agree; a function with no `return <value>` is void. **Self-recursion** works (return type found from the non-recursive returns); **mutual recursion** is a clear error for now. Functions can't be declared inside other functions.
- Integer division by zero is not checked yet (crashes at runtime).

## Part 4 remaining

(4d is done: functions via typed instances.) 4e strings/arrays runtime (heap, concat, split, upper…). 4f `input`/`sleep`/`exit`/`random`/files, console UTF-8, `novac build`/`run`.

## Part 4e notes

- Runtime in C (`runtime/nova_rt.c`); arrays are heap handles with 8-byte slots, so passing an array to a function **shares** it (a function can `.add` to the caller's array, like vcomp). Strings are immutable; memory is never freed (the OS reclaims it at exit).
- `.rm`: an **int argument removes by position** (1-based); any other value removes the first equal element. Out-of-range index = runtime error with the line number, exit status 1.
- `.length` on a string counts bytes; `.upper()/.lower()` are ASCII only. Empty array literals (`[]`) are not supported yet (element type unknown).
- Manual link: `clang -Wno-override-module program.ll runtime/nova_rt.c -o program`.
