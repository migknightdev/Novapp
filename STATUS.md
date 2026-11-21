
# novac — Nova++ Native Compiler

**novac** is the native compiler for Nova++, implementing a complete compilation pipeline:

`Source Code → Lexer → Parser/AST → Semantic Analysis → LLVM IR`

It replaces the previous line-by-line text substitution architecture used by `vcomp.cpp` with a structured compiler implementation written in C++20.

The original `vcomp.cpp` implementation belongs to the legacy repository and is not part of the current `novac` codebase.

---

## Roadmap

| Part | Component | Status |
|---|---|---|
| 1 | Lexer | ✅ Complete |
| 2 | Parser + AST | ✅ Complete |
| 3 | Semantic Analysis (functions, scopes, types, imports) | ✅ Complete |
| 4a | Basic output (`pf "text"`) | ✅ Complete |
| 4b | Variables and arithmetic | ✅ Complete |
| 4c | Control flow | ✅ Complete |
| 4d | Functions and recursion | ✅ Complete |
| 4e | Strings and arrays | ✅ Complete |
| 4f | Input, sleep, exit, random, file operations | 🚧 In progress |
| 5 | Runtime improvements and CLI polish | ⬜ Planned |

---

## Project Structure

```text
novac/
├── build.ps1
├── src/
│   ├── lexer.cpp
│   ├── parser.cpp
│   ├── ast.cpp
│   ├── types.cpp
│   ├── scope.cpp
│   ├── modules.cpp
│   ├── semantic.cpp
│   ├── codegen.cpp
│   └── main.cpp
│
├── runtime/
│   └── nova_rt.c
│
├── tests/
│   ├── test_*.cpp
│   ├── *.nvpp
│   └── lib/
│
└── build/
    └── Generated compiler binaries, LLVM IR and executables
```

The `build/` directory contains generated files and can be safely deleted and recreated.

---

## Build & Run

### Requirements

- G++ with C++20 support.
- LLVM Clang (required for generating native executables).
- Windows with PowerShell (current build script).

### Build the compiler

```powershell
.\build.ps1
```

### Run the test suites

```powershell
.\build.ps1 -Test
```

### Compile and execute a program

```powershell
.\build.ps1 -Run tests\flow.nvpp
```

This compiles the `.nvpp` source into LLVM IR, links it with the C runtime, generates `build\flow.exe`, and executes it.

### Validate a source file

```powershell
.\build\novac.exe --check tests\demo.nvpp
```

Checks syntax and semantics without generating an executable.

### Generate LLVM IR

```powershell
.\build\novac.exe --emit-ir tests\hello.nvpp -o hello.ll
```

### Installing Clang on Windows

Using MSYS2:

```bash
pacman -S mingw-w64-ucrt-x86_64-clang
```

Or using Windows Package Manager:

```powershell
winget install LLVM.LLVM
```

**Console encoding:** Accented characters in the Windows console may require UTF-8 encoding:

```powershell
chcp 65001
```

UTF-8 runtime configuration is still part of the planned improvements.

---

## Implemented Features

### Lexer, Parser & Semantic Analysis

- Lexical analysis and tokenization.
- Parser and Abstract Syntax Tree (AST) generation.
- Semantic analysis with source-aware diagnostics.
- Block scopes and variable validation.
- Static type checking.
- `const` declarations.
- Function parameter and return validation.
- Import handling with tree-shaking.
- Multiple diagnostics in a single compilation pass.

### Variables & Types

- `let` and `const` declarations.
- Integers (64-bit), floats, booleans and strings.
- Arithmetic and comparison operators.
- Compound assignments: `+=`, `-=`, `*=`, `/=`.
- Increment and decrement: `++`, `--`.
- Logical operators: `&&`, `||`.
- Short-circuit evaluation.

### Control Flow

- `if`, `elif` and `else`.
- `while` loops.
- `for` loops.
- `switch` statements.
- `break` and `continue`.
- String-based `switch` comparisons.

### Functions

- Function declarations and calls.
- Return values.
- Recursive functions.
- Functions defined after their usage.
- Generic functions through type-based instantiation.
- Return type inference.

### Strings & Arrays

- String concatenation and comparisons.
- `.upper()`, `.lower()`, `.contains()` and `.split()`.
- `.length`.
- `to_string(x)`.
- Array literals supporting integers, floats, booleans and strings.
- Array indexing with `lista.(i)` and `lista.N` (1-based).
- `.add` and `.rm`.
- `foreach` iteration.
- Runtime bounds checking with source line information.

Strings and arrays use the C runtime implemented in `runtime/nova_rt.c`, which is linked with generated LLVM IR.

---

## Language Design Decisions

The following decisions define Nova++ behavior and distinguish its semantics from the legacy implementation.

### Typed Addition

The `+` operator performs arithmetic unless a string is involved.

```nvpp
pf 1 + 2
```

Output:

```text
3
```

String concatenation:

```nvpp
pf "a" + 1 + 2
```

Output:

```text
a12
```

### Boolean and Float Output

- Booleans are printed as `true` or `false`.
- Floats follow C++ stream formatting conventions, such as `4.5`, `3` and `1e+06`.

### Switch Behavior

`switch` does not support fallthrough.

Each matching case executes independently, and `default` executes when no case matches.

A `break` inside a case is allowed.

### Block Scopes

Every block has its own scope.

- Variables first assigned inside a block are unavailable outside it.
- `let` declarations inside a block can shadow variables from outer scopes.

### Strict Type Checking

- Assigning a different type to an existing variable is an error.
- Return types must be consistent.
- Functions cannot access variables from their caller's scope.

### Generic Functions

Nova++ implements generic functions through type-based instantiation.

Function parameters are untyped, and each distinct argument type combination produces its own typed function instance.

Example:

```nvpp
soma(2, 3)
soma(1.5, 2)
```

These calls generate separate instances:

```text
soma(int,int)
soma(float,int)
```

Only function instances actually used are emitted into LLVM IR.

Semantic analysis validates each instance independently, with errors pointing to the corresponding source line inside the function.

Function parameters are passed by value.

### Return Type Inference

Return types are inferred from `return` statements.

- All return statements must agree on their types.
- Functions without a value-returning statement are considered void.
- Self-recursion is supported.
- Mutual recursion currently produces an explicit error.
- Nested function declarations are not supported.

---

## Runtime & Memory Model

The Nova++ runtime is implemented in C:

`runtime/nova_rt.c`

### Arrays

Arrays are represented as heap handles with 8-byte slots.

Passing an array to a function shares the underlying array, meaning modifications such as `.add` can affect the caller's array.

### Strings

Strings are immutable.

Memory allocated for strings and arrays is not explicitly freed during execution. The operating system reclaims process memory upon termination.

### Array Removal

The `.rm` operation supports two behaviors:

- An integer argument removes an element by position (1-based).
- Other values remove the first matching element.

Out-of-range indexes produce a runtime error containing the source line number and exit status `1`.

### String Limitations

- `.length` counts bytes, not Unicode characters.
- `.upper()` and `.lower()` currently support ASCII only.
- Empty array literals (`[]`) are not supported because their element type cannot yet be inferred.

---

## Known Limitations

The following features are not yet implemented:

- `input`
- `sleep`
- `exit`
- `random`
- `read_file`
- `write_file`
- Console UTF-8 runtime configuration.
- `novac build` and `novac run` commands.

Integer division by zero is not checked at compile time and can cause a runtime failure.

---

## Manual LLVM IR Linking

To manually generate a native executable from LLVM IR:

```bash
clang -Wno-override-module program.ll runtime/nova_rt.c -o program
```

On Windows, the resulting executable will use the `.exe` extension.

---

## Development Status

Nova++ is actively under development.

The core compiler pipeline, semantic analysis, functions, control flow, strings and arrays are implemented.

Development is currently focused on completing Part 4f, improving the runtime, and expanding the compiler's CLI.

The roadmap reflects the current implementation status and will be updated as development progresses.
