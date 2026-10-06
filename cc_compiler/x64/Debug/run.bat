del output.*

cc_compiler.exe test.c > output.asm
ml.exe /coff output.asm /link /subsystem:console msvcrt.lib legacy_stdio_definitions.lib

output.exe
echo %errorlevel%