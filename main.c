#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <io.h>
#include "cc.h"

#define MAX_LINE 256

extern Token cur_tok;
void optimize_and_emit(FILE* out_file, const char* raw_line);
void flush_peephole(FILE* out_file);

int main(int argc, char* argv[]) {
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	// 1. Update the Usage manual to accept an output destination file path
	if (argc < 2) {
		fprintf(stderr, "Usage: cc_compiler <source_file.c> [output_file.asm]\n");
		return 1;
	}

	FILE* f = fopen(argv[1], "r");
	if (!f) {
		fprintf(stderr, "Error: Could not open source file %s\n", argv[1]);
		return 1;
	}

	// Determine where the final optimized code goes (defaults to console screen)
	FILE* final_output = stdout;
	if (argc >= 3) {
		final_output = fopen(argv[2], "w");
		if (!final_output) {
			fprintf(stderr, "Error: Could not create output file %s\n", argv[2]);
			fclose(f);
			return 1;
		}
	}

	// 2. Clone standard out handles and redirect to intermediate temp file
	int orig_stdout_fd = _dup(_fileno(stdout));

	if (freopen("cc_compiler_temp.asm", "w+", stdout) == NULL) {
		fprintf(stderr, "Compiler internal file streaming failure.\n");
		fclose(f);
		if (final_output != stdout) fclose(final_output);
		return 1;
	}

	// All internal compiler printfs hit this temporary layout automatically
	printf(".model flat, c\n");
	printf(".code\n");
	printf("extern printf:proc\n\n");

	init_lexer(f);
	cur_tok = next_token();

	while (cur_tok.type != TOKEN_EOF) {
		ASTNode* ast = parse_program();
		emit_code(ast);
		void clear_local_symbols(void);
		clear_local_symbols();
	}

	// --- Existing Function Token Code Parsing Pipeline Above ---
	printf("end\n");
	fflush(stdout);

	_dup2(orig_stdout_fd, _fileno(stdout));
	_close(orig_stdout_fd);
	freopen("CONOUT$", "w", stdout);

	// PEEPHOLE PROCESSING ENGINE
	FILE* raw_asm = fopen("cc_compiler_temp.asm", "r");
	if (!raw_asm) {
		fprintf(stderr, "Error: Could not open unoptimized assembly stream file.\n");
		fclose(f);
		if (final_output != stdout) fclose(final_output);
		return 1;
	}

	// PASS 1: Scan the file entirely to register and map out active jump targets
	char line_buffer[MAX_LINE];
	void peephole_scan_labels(const char* raw_line);
	while (fgets(line_buffer, sizeof(line_buffer), raw_asm)) {
		peephole_scan_labels(line_buffer);
	}

	// PASS 2: Rewind and apply structural sliding window rules to build the output file
	rewind(raw_asm);
	while (fgets(line_buffer, sizeof(line_buffer), raw_asm)) {
		line_buffer[strcspn(line_buffer, "\r\n")] = '\0';
		optimize_and_emit(final_output, line_buffer);
	}
	flush_peephole(final_output);

	fclose(raw_asm);
	fclose(f);
	if (final_output != stdout) fclose(final_output);
	remove("cc_compiler_temp.asm");

	return 0;
}
