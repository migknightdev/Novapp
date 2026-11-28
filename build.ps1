# build.ps1 - builds novac, and optionally runs the tests or a .nvpp program.
#   .\build.ps1                          -> build\novac.exe
#   .\build.ps1 -Test                    -> also builds and runs every test suite
#   .\build.ps1 -Run tests\flow.nvpp     -> builds, then compiles that .nvpp to build\flow.exe (needs clang) and runs it
# Everything generated goes into build\ - you can delete that folder at any time.
param([switch]$Test, [string]$Run)

New-Item -ItemType Directory -Force build | Out-Null

$front = "src\lexer.cpp", "src\ast.cpp", "src\parser.cpp"
$sema  = $front + "src\scope.cpp", "src\modules.cpp", "src\semantic.cpp"
$full  = $sema + "src\codegen.cpp"

function Run-Step([string]$label, [scriptblock]$cmd) {
    & $cmd
    if ($LASTEXITCODE -ne 0) { Write-Host "FAILED: $label" -ForegroundColor Red; exit 1 }
}

Run-Step "novac" { g++ -std=c++20 -Wall -Wextra -Isrc @full src\main.cpp -o build\novac.exe }
Write-Host "[OK] build\novac.exe"

if ($Test) {
    Run-Step "build test_parser"   { g++ -std=c++20 -Isrc @front tests\test_parser.cpp -o build\test_parser.exe }
    Run-Step "build test_semantic" { g++ -std=c++20 -Isrc @sema  tests\test_semantic.cpp -o build\test_semantic.exe }
    Run-Step "build test_codegen"  { g++ -std=c++20 -Isrc @full  tests\test_codegen.cpp -o build\test_codegen.exe }
    Run-Step "build test_examples" { g++ -std=c++20 tests\test_examples.cpp -o build\test_examples.exe }
    Run-Step "test_parser"   { .\build\test_parser.exe }
    Run-Step "test_semantic" { .\build\test_semantic.exe }
    Run-Step "test_codegen"  { .\build\test_codegen.exe }
    Run-Step "test_examples" { .\build\test_examples.exe }
}

if ($Run) {
    # computed outside Run-Step on purpose: a scriptblock sees the *function's* variables first
    $prog = [System.IO.Path]::GetFileNameWithoutExtension($Run)
    $ll   = "build\$prog.ll"
    $exe  = "build\$prog.exe"
    Run-Step "novac --emit-ir" { .\build\novac.exe --emit-ir $Run -o $ll }
    Run-Step "clang"           { clang -Wno-override-module $ll runtime\nova_rt.c -o $exe }
    & ".\$exe"
}
