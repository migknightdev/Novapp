# after next update status.md will be removed

important parts will be moved to the [readme](./README.md)



# next update
| item | correction |
|---|---|
| 1 | syntax standardization |
| 2 | division by 0 |
| 3 | empty arrays |

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
| 5. Runtime + CLI polish | ✅ |

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

## Implemented features — current status

This table tracks the actual language and compiler capabilities.

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
| CLI | `--check`, `--emit-ir`, `build`, `--tokens`, `--ast`, and `run` commands | ✅ Implemented |
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

## Part 5 is complete

CLI and compiler polish completed. No further roadmap stages are planned.
