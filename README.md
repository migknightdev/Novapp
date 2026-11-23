# Nova++

<p align="center">
  <strong>A standalone programming language with its own native compiler.</strong>
</p>

<p align="center">
  Nova++ uses a custom compiler pipeline, semantic analysis, and LLVM IR code generation.
</p>

---

## About

**Nova++** is an actively developed programming language built around its own compiler, `novac`.

The project has moved beyond its original transpiler architecture. Instead of translating source code through line-by-line text substitution, `novac` processes Nova++ source files through lexical analysis, parsing, an Abstract Syntax Tree (AST), semantic analysis, and LLVM Intermediate Representation (IR) generation.

The compiler is written in **C++20**. The language runtime is written in **C**, and **Clang** is used to turn generated LLVM IR and the runtime into native executables.

## Features

### Compiler

- **Lexer:** tokenization and source-aware lexical diagnostics.
- **Parser and AST:** structured parsing and an Abstract Syntax Tree.
- **Semantic analysis:** variable scopes, mutability, type checking, function validation, and import resolution.
- **LLVM IR code generation:** emits textual LLVM IR without requiring LLVM development libraries to build the compiler itself.
- **Diagnostics:** reports lexical, syntax, semantic, and code-generation errors with file, line, column, and source snippets.
- **Inspection modes:** token stream, AST, semantic validation, and LLVM IR output.

### Language

- **Types:** `int`, `float`, `bool`, strings, and arrays.
- **Variables:** inferred declarations, `let`, and `const`.
- **Operators:** arithmetic, comparisons, logical operators, compound assignments, and increment/decrement.
- **Control flow:** `if`, `elif`, `else`, `while`, C-style `for`, `foreach`, and `switch`.
- **Functions:** parameters, return values, recursion, and generic function instantiation based on argument types.
- **Strings:** concatenation, comparison, `.length`, `.upper()`, `.lower()`, `.contains()`, and `.split()`.
- **Arrays:** literals, 1-based indexing, `.length`, `.add`, `.rm`, and `foreach` iteration.
- **Built-ins:** `input`, `sleep`, `exit`, `random`, `read_file`, `write_file`, and `to_string`.
- **Modules:** imports with recursive loading and tree-shaking of unused imported functions.

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

const TITLE = "Nova++ demo"
numbers = [10; 20; 30]

numbers.add 40

greet("Nova++")
pf TITLE
pf "Factorial: " + factorial(5)
pf "Array length: " + numbers.length

for (number in numbers) {
    pf number
}
```

Every Nova++ source file must begin with the `#include <nova>` directive.

## Language overview

### Variables and types

```nvpp
#include <nova>

name = "Nova++"
let version = 1
const RELEASE = "beta"

count = 10
ratio = 2.5
enabled = true
```

Variable types are inferred from their assigned values. `let` declares a block-scoped variable and `const` declares an immutable variable. Reassignments must respect the variable's type, with `int` values allowed where a `float` is expected.

### Functions

```nvpp
#include <nova>

func add(a, b) {
    return a + b
}

pf add(10, 20)
pf add(1.5, 2)
```

Function parameters do not have explicit type annotations. The semantic analyzer creates a specialized function instance for each distinct argument-type combination, such as `add(int,int)` and `add(float,int)`.

Self-recursion is supported. Mutual recursion and nested function declarations are not currently supported.

### Control flow

```nvpp
#include <nova>

score = 8

if (score >= 7) {
    pf "Approved"
} elif (score >= 5) {
    pf "Recovery"
} else {
    pf "Failed"
}

for (i = 1; i <= 5; i++) {
    pf i
}

switch (score) {
case 8:
    pf "Score is eight"
default:
    pf "Another score"
}
```

`switch` cases do not fall through into the next case.

### Strings and arrays

```nvpp
#include <nova>

message = "Hello, Nova++"
pf message.upper()
pf message.contains("Nova")

words = "one,two,three".split(",")
pf words.length
pf words.(2)

numbers = [10; 20; 30]
numbers.add 40
numbers.rm 1

for (number in numbers) {
    pf number
}
```

Array indexes are **1-based**. An integer argument to `.rm` removes an element by position; a non-integer argument removes the first matching value.

### Input, files, and runtime functions

```nvpp
#include <nova>

name = ""
input name

pf "Hello, " + name
sleep(500)

write_file("message.txt", "Saved by Nova++")
content = read_file("message.txt")
pf content

value = random(1, 10)
pf value
```

- `input variable` reads one line and parses it according to the variable's type.
- `sleep(milliseconds)` pauses execution.
- `exit(code)` terminates the program with the given exit status.
- `random(min, max)` returns an integer in the inclusive range.
- `read_file(path)` returns the file contents, or an empty string if the file cannot be opened.
- `write_file(path, content)` returns `true` on success and `false` on failure.
- `to_string(value)` converts a supported value to a string.

## Compiler architecture

```text
Nova++ source (.nvpp)
        |
        v
      Lexer
        |
        v
  Parser and AST
        |
        v
 Semantic analysis
        |
        v
  LLVM IR codegen
        |
        v
   LLVM IR (.ll)
        |
        +------ runtime/nova_rt.c
        |
        v
      Clang
        |
        v
 Native executable
```

The compiler emits LLVM IR as text. Clang links that IR with the Nova++ C runtime to produce the executable.

## Requirements

- **G++** with C++20 support.
- **Clang** available on `PATH` to build native executables.
- **PowerShell** to use the included `build.ps1` helper script.

The compiler's `--check`, `--tokens`, `--ast`, and `--emit-ir` modes do not need Clang. The `build` and `run` commands do.

### Installing the toolchain on Windows

With MSYS2 UCRT64:

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-clang
```

Alternatively, install LLVM Clang using:

```powershell
winget install LLVM.LLVM
```

Make sure `g++` and `clang` are available in your `PATH`.

## Getting started

Clone the repository:

```bash
git clone https://github.com/migknightdev/Novapp.git
cd Novapp
```

### Build the compiler

```powershell
.\build.ps1
```

The compiler executable is generated at:

```text
build/novac.exe
```

### Run the test suites

```powershell
.\build.ps1 -Test
```

This builds and runs the parser, semantic analysis, and code-generation test suites.

### Compile and run a program with the helper script

```powershell
.\build.ps1 -Run tests\hello.nvpp
```

### Use the compiler directly

Validate syntax and semantics:

```powershell
.\build\novac.exe --check tests\demo.nvpp
```

Print tokens:

```powershell
.\build\novac.exe --tokens tests\demo.nvpp
```

Print the AST:

```powershell
.\build\novac.exe --ast tests\demo.nvpp
```

Emit LLVM IR:

```powershell
.\build\novac.exe --emit-ir tests\hello.nvpp -o hello.ll
```

Build a native executable:

```powershell
.\build\novac.exe build tests\hello.nvpp -o hello.exe
```

Build and immediately run a program:

```powershell
.\build\novac.exe run tests\hello.nvpp
```

Keep the generated LLVM IR alongside the executable:

```powershell
.\build\novac.exe build tests\hello.nvpp --keep-ir
```

The `build` and `run` commands locate `runtime/nova_rt.c` next to the compiler, one directory above it, or in the current working directory. You can also set the `NOVAC_RUNTIME` environment variable to the runtime file's path.

**Path limitation:** the current CLI rejects paths containing spaces or shell-special characters for build and run operations.

## Project structure

```text
Novapp/
├── src/
│   ├── lexer.cpp / lexer.h
│   ├── ast.cpp / ast.h
│   ├── parser.cpp / parser.h
│   ├── types.h
│   ├── scope.cpp / scope.h
│   ├── modules.cpp / modules.h
│   ├── semantic.cpp / semantic.h
│   ├── codegen.cpp / codegen.h
│   ├── main.cpp
│   ├── test_semantic.cpp
│   └── test_codegen.cpp
├── runtime/
│   └── nova_rt.c
├── tests/
│   ├── test_parser.cpp
│   ├── test_semantic.cpp
│   ├── test_codegen.cpp
│   └── *.nvpp
├── build.ps1
├── STATUS.md
├── LICENSE
└── README.md
```

Generated files and executables are placed in `build/` when using the provided build script.

## Current status

The current codebase includes the lexer, parser and AST, semantic analysis, module loading, LLVM IR code generation, the C runtime, and the compiler CLI.

The implemented language and runtime features include control flow, functions, strings, arrays, console input, timing, process exit, random numbers, and file operations. The CLI supports validation, token and AST inspection, IR emission, native builds, and execution.

Nova++ is still under active development. For the detailed implementation status, known limitations, and roadmap, see [STATUS.md](STATUS.md).

## License

Nova++ is licensed under the [MIT License](LICENSE).
