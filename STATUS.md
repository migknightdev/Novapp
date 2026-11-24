# novac — Nova++ native compiler

novac is the native compiler for Nova++, implementing a complete compilation pipeline:
`Source Code → Lexer → Parser/AST → Semantic Analysis → LLVM IR`
It replaces the previous line-by-line text substitution architecture used by `vcomp.cpp` with a structured compiler implementation written in C++20.
The original `vcomp.cpp` implementation belongs to the legacy repository and is not part of the current `novac` codebase.

## Roadmap

| Part | Status |
|---|---|
| 1. Lexer | ✅ |
| 2. Parser + AST | ✅ |
| 3. Semantic analysis (functions, scopes, types, imports) | ✅ |
| 4. Codegen → LLVM IR | ✅ 4a ✅ `pf "text"` · 4b ✅ variables/arithmetic · 4c ✅ control flow · 4d ✅ functions · 4e ✅ strings & arrays · 4f ✅ input/sleep/exit/random/files + `build`/`run` |
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
.\build\novac.exe run tests\flow.nvpp     # compile + run in one step (needs clang)
.\build\novac.exe build tests\flow.nvpp -o flow.exe
.\build.ps1                         # build\novac.exe
.\build.ps1 -Test                   # + build and run the 3 test suites
.\build.ps1 -Run tests\flow.nvpp    # .nvpp -> IR -> build\flow.exe, then runs it
.\build\novac.exe --check tests\demo.nvpp      # validate only (syntax + semantics)
.\build\novac.exe --emit-ir tests\hello.nvpp -o hello.ll
```
clang on Windows: `pacman -S mingw-w64-ucrt-x86_64-clang` (MSYS2) or `winget install LLVM.LLVM`.
Accents in the Windows console may need `chcp 65001` until the runtime sets the code page (4f).

## Implemented features — current status

This table is the single source of truth for features implemented in the current compiler. The roadmap below tracks development stages; this table tracks the actual language and compiler capabilities.

| Area | Implemented features | Status |
|---|---|---|
| Lexer and parser | Tokenization, parsing, AST construction, source locations, syntax diagnostics | ✅ Implemented |
| Semantic analysis | Block scopes, `const`, static types, function arity and return checks, imports, module resolution, tree-shaking | ✅ Implemented |
| Variables and types | `let`, `const`, `int` (64-bit), `float`, `bool`, strings, assignments, compound assignments (`+=`, `-=`, `*=`, `/=`), increment/decrement (`++`, `--`) | ✅ Implemented |
| Expressions | Arithmetic, typed `+`, comparisons, `&&` and `||` with short-circuit evaluation, string literals and concatenation | ✅ Implemented |
| Control flow | `if`, `elif`, `else`, `while`, `for`, `switch`, `break`, `continue`, `foreach` | ✅ Implemented |
| Functions | Function calls, `return`, inferred return types, generic function instantiation from argument types, self-recursion, functions defined after use | ✅ Implemented |
| Strings | `.upper()`, `.lower()`, `.contains()`, `.split()`, `.length`, `to_string(x)`, string comparisons and string `switch` | ✅ Implemented |
| Arrays | Array literals for `int`, `float`, `bool` and `string`; 1-based access with `lista.(i)` or `lista.N`; `.add`, `.rm`; `foreach` iteration | ✅ Implemented |
| Input and process control | `input` for `string`, `int`, `float` and `bool`; `exit N` with an exit status | ✅ Implemented |
| Runtime utilities | `sleep(ms)`, `random(lo, hi)`, `read_file(path)`, `write_file(path, content)` | ✅ Implemented |
| Code generation | LLVM IR generation for the implemented language features | ✅ Implemented |
| CLI | `--check`, `--emit-ir`, `build`, and `run` commands | ✅ Implemented |
| Native execution | Linking generated LLVM IR with `runtime\nova_rt.c` through Clang | ✅ Implemented |

The entries above describe implemented functionality, not an assertion that every edge case is supported. See **Known limitations and behavior** below for current restrictions, including empty arrays, mutual recursion, integer division by zero, and string Unicode behavior.

## Language decisions made on the way (compared with the legacy vcomp)

- Typed `+`: `pf 1 + 2` prints `3`; it concatenates only when a string is involved (`pf "a" + 1 + 2` → `a12`).
- `pf` prints bools as `true`/`false`; floats like C++ streams (`4.5`, `3`, `1e+06`).
- `switch` has **no fallthrough**: each case runs alone (a `break` inside is still allowed); `default` runs when nothing matched.
- Blocks have their own scope: a variable first assigned inside a block does not exist after it; `let` inside a block shadows.
- Stricter checks: `x = 5` then `x = 2.5` is an error; `return` types must agree; functions can't see caller variables.
- **Functions are generic by instantiation.** Parameters are untyped, so each distinct list of argument types gets its own typed copy: `soma(2, 3)` and `soma(1.5, 2)` are `soma(int,int)` and `soma(float,int)` (LLVM symbols `@"soma(int,int)"`). Semantic analysis type-checks each instance (errors point at the line inside the function); only instances actually called are emitted. Parameters are passed **by value** (vcomp's C++ `auto&&` could modify the caller's variable).
- Return type is inferred from the `return` statements and must agree; a function with no `return <value>` is void. **Self-recursion** works (return type found from the non-recursive returns); **mutual recursion** is a clear error for now. Functions can't be declared inside other functions.
- Integer division by zero is not checked yet (crashes at runtime).

## Part 4 is complete

Every statement and expression of the language now compiles. Next: Part 5 (polish: compound features, diagnostics, packaging).

## Part 4e notes

- Runtime in C (`runtime/nova_rt.c`); arrays are heap handles with 8-byte slots, so passing an array to a function **shares** it (a function can `.add` to the caller's array, like vcomp). Strings are immutable; memory is never freed (the OS reclaims it at exit).
- `.rm`: an **int argument removes by position** (1-based); any other value removes the first equal element. Out-of-range index = runtime error with the line number, exit status 1.
- `.length` on a string counts bytes; `.upper()/.lower()` are ASCII only. Empty array literals (`[]`) are not supported yet (element type unknown).
- Manual link: `clang -Wno-override-module program.ll runtime/nova_rt.c -o program`.

## Part 4f notes

- `novac run <file>` / `novac build <file> [-o out] [--keep-ir]`: emits IR, calls `clang` with `runtime\nova_rt.c`, runs it. The runtime is found next to `novac.exe`, one folder up (`build\` -> `runtime\`), or via `NOVAC_RUNTIME`. Paths may not contain spaces/shell characters.
- `input x` reads **one line**: a `string` variable gets the whole line, `int`/`float`/`bool` variables parse it (invalid input = runtime error with the line number, exit status 1). (vcomp used `cin >>`, i.e. one word.)
- `exit N` exits immediately with status N. Unlike vcomp, programs no longer wait for ENTER at the end or on `exit`.
- `random(lo, hi)` is inclusive (xorshift, seeded from the clock). `read_file` gives `""` when the file can't be opened; `write_file` returns `true`/`false`. `sleep(ms)` accepts int or float.
- On Windows the runtime switches the console to UTF-8 at startup, so accents print correctly (no more `chcp 65001`).
