
# Nova++

<p align="center">
  <strong>A programming language evolving beyond transpilation.</strong>
</p>

<p align="center">
  A standalone programming language with its own compiler, semantic analysis, and native code generation through LLVM IR.
</p>

---

## About

**Nova++** is a programming language designed to move beyond traditional transpilation.

Unlike its earlier architecture, Nova++ now features its own compiler pipeline, including lexical analysis, parsing, abstract syntax trees (AST), semantic analysis, and LLVM IR code generation.

The project aims to provide a structured, expressive, and efficient programming language with a compiler architecture designed for future expansion.

## Features

- **Custom Lexer:** Tokenization and lexical error reporting.
- **Parser & AST:** Structured syntax analysis and abstract syntax tree generation.
- **Semantic Analysis:** Type checking, scope management, and function validation.
- **LLVM IR Code Generation:** Generates LLVM Intermediate Representation.
- **Variables & Types:** Supports integers, floating-point numbers, booleans, and strings.
- **Functions:** Function declarations, return values, recursion, and type inference through instantiation.
- **Control Flow:** `if`, `elif`, `else`, `while`, `for`, and `switch`.
- **Arrays:** Array indexing, iteration, and modification.
- **String Operations:** Concatenation, comparison, splitting, and built-in methods.
- **Error Reporting:** Source-aware diagnostics with line and column information.

## Example

```nvpp
#include <nova>

func factorial(n) {
    if (n <= 1) {
        return 1
    }

    return n * factorial(n - 1)
}

func greet(name) {
    pf "Hello, " + name + "!"
}

numbers = [10; 20; 30]

numbers.add 40

greet("Nova++")

pf "Factorial: " + factorial(5)
pf "Array length: " + numbers.length

for (number in numbers) {
    pf number
}
```

## Compiler Architecture

Nova++ follows a multi-stage compiler pipeline:

```text
Source Code (.nvpp)
       |
       v
     Lexer
       |
       v
    Parser
       |
       v
   AST Generation
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
  Native Executable
```

The compiler is implemented in C++20, while the runtime is written in C.

## Requirements

- C++20 compatible compiler (G++)
- LLVM Clang
- Windows with PowerShell (current build script)

## Building

Clone the repository:

```bash
git clone YOUR_REPOSITORY_URL
cd novac
```

Build the compiler:

```powershell
.\build.ps1
```

Run the test suites:

```powershell
.\build.ps1 -Test
```

Compile and run a Nova++ program:

```powershell
.\build.ps1 -Run tests\hello.nvpp
```

**Note:** Clang is required to link the generated LLVM IR with the Nova++ runtime and produce a native executable.

## Project Structure

```text
novac/
├── src/
│   ├── lexer.cpp
│   ├── parser.cpp
│   ├── ast.cpp
│   ├── semantic.cpp
│   ├── codegen.cpp
│   └── main.cpp
│
├── runtime/
│   └── nova_rt.c
│
├── tests/
│   ├── test_parser.cpp
│   ├── test_semantic.cpp
│   ├── test_codegen.cpp
│   └── *.nvpp
│
├── build.ps1
└── STATUS.md
```

## Development Status

Nova++ is actively under development.

The lexer, parser, AST, semantic analysis, and core LLVM IR code generation are implemented.

Additional language features, runtime improvements, compiler tooling, and CLI enhancements are planned.

Some built-in functions and runtime operations are not implemented yet.

See [STATUS.md](STATUS.md) for the current implementation status and roadmap.

## Philosophy

Nova++ started with a different architecture, but its evolution led to a much more ambitious direction.

The goal is no longer simply to translate code into another language.

**The goal is to build a programming language with its own identity and compiler.**

---

## License

Nova++ is licensed under the [MIT License](LICENSE).
