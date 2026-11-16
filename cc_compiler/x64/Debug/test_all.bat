@echo off
setlocal enabledelayedexpansion
set FAILED=0

echo === 1. Preprocessor / Globals Smoke Compile ===
cc_compiler.exe test_globals.c test_globals.asm
if errorlevel 1 (echo [FAIL] test_globals.c compile & set FAILED=1) else (
    ml.exe /nologo /c /coff test_globals.asm
    if errorlevel 1 (echo [FAIL] test_globals.asm assemble & set FAILED=1) else (echo [PASS] test_globals.c)
)

echo.
echo === 2. Full Feature & Regression Suite (test.c) ===
cc_compiler.exe test.c output.asm
if errorlevel 1 (echo [FAIL] test.c compile & set FAILED=1) else (
    ml.exe /nologo /coff output.asm /link /nologo /subsystem:console msvcrt.lib legacy_stdio_definitions.lib
    if errorlevel 1 (echo [FAIL] test.c link & set FAILED=1) else (
        output.exe
        if errorlevel 1 (echo [FAIL] test.exe runtime & set FAILED=1) else (echo [PASS] test.c)
    )
)

echo.
echo === 3. Multi-File Extern Linking (main_app.c + math_lib.c) ===
cc_compiler.exe math_lib.c math_lib.asm
cc_compiler.exe main_app.c main_app.asm
if errorlevel 1 (echo [FAIL] multi-file compile & set FAILED=1) else (
    ml.exe /nologo /c /coff math_lib.asm
    ml.exe /nologo /c /coff main_app.asm
    link.exe /nologo /subsystem:console main_app.obj math_lib.obj legacy_stdio_definitions.lib ucrt.lib vcruntime.lib msvcrt.lib /out:multi_test.exe
    if errorlevel 1 (echo [FAIL] multi-file link & set FAILED=1) else (
        multi_test.exe
        if errorlevel 1 (echo [FAIL] multi_test.exe runtime & set FAILED=1) else (echo [PASS] multi_test)
    )
)

echo.
if %FAILED%==0 (echo ALL SUITES PASSED!) else (echo SOME SUITES FAILED!)
exit /b %FAILED%
