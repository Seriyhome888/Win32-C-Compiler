del output.*

cc_compiler.exe test.c output.asm
rem ml.exe /coff output.asm /link /subsystem:console msvcrt.lib legacy_stdio_definitions.lib

ml.exe /c /Cx /coff output.asm

link.exe /subsystem:console /entry:mainCRTStartup output.obj legacy_stdio_definitions.lib libucrt.lib libcmt.lib

output.exe
echo %errorlevel%

cc_compiler.exe test_globals.c test_globals.asm
ml /coff test_globals.asm /link /SUBSYSTEM:CONSOLE legacy_stdio_definitions.lib ucrt.lib vcruntime.lib msvcrt.lib
test_globals.exe
echo %errorlevel%