cc_compiler.exe math_lib.c math_lib.asm
cc_compiler.exe main_app.c main_app.asm
ml /c /coff math_lib.asm
ml /c /coff main_app.asm
link /subsystem:console main_app.obj math_lib.obj legacy_stdio_definitions.lib ucrt.lib vcruntime.lib msvcrt.lib /out:multi_test.exe
multi_test.exe
