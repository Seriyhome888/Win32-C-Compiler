@echo off
setlocal enabledelayedexpansion

:: --- CONFIGURATION (Adjust paths/filenames to match your folder) ---
set CC=cc_compiler.exe
set TEST_DIR=.
set MASM_BIN=C:\masm32\bin
set MASM_LIB=C:\masm32\lib

:: Add MASM32 to PATH if installed in C:\masm32 and ml.exe is not already in PATH
where ml.exe >nul 2>&1
if errorlevel 1 (
    if exist "%MASM_BIN%\ml.exe" (
        set "PATH=%MASM_BIN%;%PATH%"
    ) else (
        echo [ERROR] ml.exe not found! Run from "x86 Native Tools Command Prompt" or set MASM_BIN.
        exit /b 1
    )
)

:: Link flags: include MASM32 msvcrt.lib/kernel32.lib if present, else standard VS ucrt/legacy_stdio
set LINK_LIBS=
if exist "%MASM_LIB%\msvcrt.lib" (
    set LINK_LIBS=/LIBPATH:"%MASM_LIB%" msvcrt.lib kernel32.lib
) else (
    set LINK_LIBS=msvcrt.lib legacy_stdio_definitions.lib kernel32.lib
)

set PASS=0
set FAIL=0

echo ============================================
echo   C COMPILER AUTOMATED TEST SUITE (MASM32)
echo ============================================

:: -------------------------------------------------------------------------
:: Test 1: Preprocessor ^& Nested Headers (expects exit code 128)
:: -------------------------------------------------------------------------
echo [RUN] Test 1: Preprocessor ^& Config Headers...
call :build_and_run "test_preproc" 128 "%TEST_DIR%\test_preproc.c"

:: -------------------------------------------------------------------------
:: Test 2: Multi-File Linking (extern + math_lib.c, expects exit code 0)
:: -------------------------------------------------------------------------
echo [RUN] Test 2: Multi-File Extern Linking...
call :build_and_run "test_extern" 0 "%TEST_DIR%\test_extern_main.c" "%TEST_DIR%\math_lib.c"

:: -------------------------------------------------------------------------
:: Test 3: Globals ^& Statics (expects exit code 0)
:: -------------------------------------------------------------------------
echo [RUN] Test 3: Globals ^& Statics...
call :build_and_run "test_globals" 0 "%TEST_DIR%\test_globals.c"

:: -------------------------------------------------------------------------
:: Test 4: Full Regression Suite (expects exit code 0)
:: -------------------------------------------------------------------------
echo [RUN] Test 4: Full Regression Suite...
call :build_and_run "test_regression" 0 "%TEST_DIR%\test_regression.c"

echo ============================================
echo   SUMMARY: !PASS! PASSED, !FAIL! FAILED
echo ============================================
exit /b !FAIL!

:: =========================================================================
:: Helper Routine: :build_and_run <out_name> <expected_exit> <src1> [src2]
:: =========================================================================
:build_and_run
set OUT_NAME=%~1
set EXPECTED=%~2
set OBJS=

del /q "%OUT_NAME%.exe" 2>nul

:compile_loop
if "%~3"=="" goto do_link
set SRC=%~3
set BASE=%~n3

if not exist "%SRC%" (
    echo [ERROR] Source file not found: %SRC%
    set /a FAIL+=1
    goto :eof
)

"%CC%" "%SRC%" "%BASE%.asm"
if errorlevel 1 (
    echo [ERROR] Compiler failed on %SRC%
    set /a FAIL+=1
    goto :eof
)

ml.exe /nologo /c /coff "%BASE%.asm"
if errorlevel 1 (
    echo [ERROR] MASM ml.exe failed on %BASE%.asm
    set /a FAIL+=1
    goto :eof
)

set OBJS=!OBJS! "%BASE%.obj"
shift
goto compile_loop

:do_link
link.exe /nologo /subsystem:console /entry:mainCRTStartup !OBJS! %LINK_LIBS% /out:"%OUT_NAME%.exe"
if errorlevel 1 (
    :: Fallback if linking against MASM32 msvcrt without CRT startup entry
    link.exe /nologo /subsystem:console !OBJS! %LINK_LIBS% /out:"%OUT_NAME%.exe"
    if errorlevel 1 (
        echo [ERROR] Linker failed for %OUT_NAME%.exe
        set /a FAIL+=1
        goto :eof
    )
)

"%OUT_NAME%.exe"
set ACTUAL=!errorlevel!
if !ACTUAL! equ %EXPECTED% (
    echo [PASS] %OUT_NAME% returned !ACTUAL!
    set /a PASS+=1
) else (
    echo [FAIL] %OUT_NAME% expected %EXPECTED%, got !ACTUAL!
    set /a FAIL+=1
)
goto :eof
