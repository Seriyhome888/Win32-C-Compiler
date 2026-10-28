del output.*

cc_compiler.exe test.c output.asm
rem ml.exe /coff output.asm /link /subsystem:console msvcrt.lib legacy_stdio_definitions.lib

ml.exe /c /Cx /coff output.asm

link.exe /subsystem:console /entry:mainCRTStartup output.obj legacy_stdio_definitions.lib libucrt.lib libcmt.lib

output.exe
echo %errorlevel%