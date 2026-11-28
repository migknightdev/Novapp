# Nova++ Development Status

**Project:** Nova++  
**Compiler:** `novac`  
**Version:** `0.1.0-beta`  
**Language:** C++20 compiler with C runtime  
**Status:** Five planned development stages completed

---

## 1. Overview

Nova++ is a standalone programming language with its own native compiler.

The current compiler, `novac`, uses a custom compilation pipeline:

```text
Source Code
    |
    v
   Lexer
    |
    v
 Parser / AST
    |
    v
Semantic Analysis
    |
    v
 LLVM IR Codegen
    |
    v
 LLVM IR
    |
    v
Clang + C Runtime
    |
    v
Native Executable
```

The project no longer follows the original line-by-line transpilation architecture. The current implementation uses lexical analysis, parsing, semantic validation, and LLVM IR generation.

## 2. Implemented Components

| Component | Status |
|---|---|
| Lexer | Completed |
| Parser and AST | Completed |
| Semantic analysis | Completed |
| Type checking | Completed |
| Variable scopes | Completed |
| Module loading and imports | Implemented |
| LLVM IR code generation | Implemented |
| C runtime | Implemented |
| Native executable generation | Implemented |
| Compiler CLI | Implemented |
| Runtime error handling | Implemented |
| Automated tests | Implemented |
| GitHub Actions CI | Implemented |

## 3. Implemented Language Features

### Types and variables

- `int` (64-bit)
- `float`
- `bool`
- Strings
- Arrays
- Type inference
- `let` and `const`
- Type-safe assignments

### Operators

- Arithmetic operators
- Comparison operators
- Logical operators
- Compound assignments
- Increment and decrement
- Short-circuit evaluation for `&&` and `||`

### Control flow

- `if`
- `elif`
- `else`
- `while`
- C-style `for`
- `foreach`
- `switch`
- `break`
- `continue`

### Functions

- Function declarations
- Parameters and return values
- Return type inference
- Generic specialization based on argument types
- Self-recursion

### Strings

- Concatenation
- Comparison
- `.length`
- `.upper()`
- `.lower()`
- `.contains()`
- `.split()`

### Arrays

- Array literals
- 1-based indexing
- `.length`
- `.add`
- `.rm`
- `foreach` iteration

### Built-in functions

- `input`
- `sleep`
- `exit`
- `random`
- `read_file`
- `write_file`
- `to_string`

## 4. Runtime and Error Handling

The C runtime provides support for operations that require runtime behavior.

Integer division and modulo are checked through:

```c
int64_t nova_idiv(int64_t a, int64_t b, int32_t line);
int64_t nova_imod(int64_t a, int64_t b, int32_t line);
```

Implemented protections include:

- Division by zero.
- Modulo by zero.
- The `INT64_MIN / -1` overflow case.
- Runtime diagnostics containing the source line number.
- Non-zero exit status when a runtime error occurs.

The LLVM IR code generator routes integer division and modulo through these runtime functions.

## 5. Compiler CLI

The compiler currently supports:

| Command | Purpose |
|---|---|
| `--help`, `-h` | Display help information |
| `--version`, `-v` | Display compiler version |
| `--check` | Validate source code |
| `--tokens` | Display lexer output |
| `--ast` | Display the generated AST |
| `--emit-ir` | Generate LLVM IR |
| `build` | Compile a native executable |
| `run` | Compile and execute a program |

The compiler's inspection and validation commands do not require Clang. Native builds and execution do.

## 6. Testing and Continuous Integration

The project includes automated tests for:

- Lexer and parser behavior.
- Semantic analysis.
- LLVM IR code generation.
- Complete example programs.

The integration tests in `tests/test_examples.cpp` compile and execute `.nvpp` programs, checking expected output and exit status.

The GitHub Actions workflow at:

```text
.github/workflows/ci.yml
```

automates compiler builds and test execution.

To run the local test suite:

```powershell
.\build.ps1 -Test
```

To build the compiler:

```powershell
.\build.ps1
```

## 7. Known Limitations

The following features are not currently supported or have implementation restrictions:

- Empty array literals (`[]`) cannot infer their element type.
- Mutual recursion is not supported.
- Nested function declarations are not supported.
- String `.length` counts bytes rather than Unicode characters.
- `.upper()` and `.lower()` only support ASCII.
- Some CLI build and run operations do not support paths containing spaces or shell-special characters.

These limitations do not invalidate the implemented compiler pipeline or the existing language features.

## 8. Development History

### Original compiler

The original implementation used a simpler transpilation approach based on text replacement.

That implementation is now maintained separately in the archived legacy repository:

https://github.com/migknightdev/novapp-old

### Current compiler: `novac`

The current implementation introduced:

- A custom lexer.
- A parser and AST.
- Semantic analysis.
- Module loading.
- LLVM IR code generation.
- A C runtime.
- Native executable compilation.

### Part 5

The fifth development stage introduced:

- Checked integer division.
- Checked integer modulo.
- Runtime diagnostics for arithmetic errors.
- Integration tests for complete `.nvpp` programs.
- GitHub Actions CI.
- CLI help and version options.
- Documentation updates.

## 9. Current Development Status

The five planned compiler development stages have been completed.

The current project is focused on maintaining and improving the existing compiler and language implementation rather than following another predefined development stage.

Potential future improvements include:

- Empty array type inference.
- Mutual recursion.
- More complete Unicode string support.
- Additional runtime and integration tests.
- Further syntax and language consistency improvements.

---

**Last updated:** November 2026  
**Project:** Nova++  
**Repository:** https://github.com/migknightdev/Novapp