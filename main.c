#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <io.h>
#include "cc.h"

#define MAX_LINE 256

int is_codegen_phase = 0;

extern Token cur_tok;
void optimize_and_emit(FILE* out_file, const char* raw_line);
void flush_peephole(FILE* out_file);

// Declare the external entry point from your cohesive preprocessor.c utility
extern int run_preprocessor(const char* input_filename, const char* output_filename);

int main(int argc, char* argv[]) {
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	// 1. Validate incoming operational parameters
	if (argc < 2) {
		fprintf(stderr, "Usage: cc_compiler <source_file.c> [output_file.asm]\n");
		return 1;
	}

	// --- PHASE 1: STANDALONE COHESIVE MACRO PREPROCESSOR PASS ---
	const char* staging_file = "cc_preprocessed_source.i";

	// Execute preprocessor pass to resolve macros, conditional stacks, and header include tracks
	if (!run_preprocessor(argv[1], staging_file)) {
		fprintf(stderr, "Fatal Error: Macro preprocessing phase failed.\n");
		return 1;
	}

	// Open the freshly generated preprocessed token stream file for core compilation
	FILE* f = fopen(staging_file, "r");
	if (!f) {
		fprintf(stderr, "Error: Could not open preprocessed staging source stream.\n");
		return 1;
	}

	// Determine where the final optimized assembly output goes (defaults to the console)
	FILE* final_output = stdout;
	if (argc >= 3) {
		final_output = fopen(argv[2], "w");
		if (!final_output) {
			fprintf(stderr, "Error: Could not create output assembly file %s\n", argv[2]);
			fclose(f);
			remove(staging_file);
			return 1;
		}
	}

	// 2. Clone standard out handles and redirect output to intermediate temp file
	int orig_stdout_fd = _dup(_fileno(stdout));

	if (freopen("cc_compiler_temp.asm", "w+", stdout) == NULL) {
		fprintf(stderr, "Compiler internal file streaming failure.\n");
		fclose(f);
		remove(staging_file);
		if (final_output != stdout) fclose(final_output);
		return 1;
	}

	// All internal compiler printfs hit this temporary layout automatically
	printf(".model flat, c\n");
	printf("option casemap:none\n"); //Preserves case sensitivity for link matches
	printf(".code\n\n");

	is_codegen_phase = 0;

	// --- PHASE 2: CORE LEXICAL AND PARSING COMPILER PIPELINE ---
	// Feed the preprocessed file handle 'f' directly into the lexer!
	init_lexer(f);
	cur_tok = next_token();

	is_codegen_phase = 0;

	while (cur_tok.type != TOKEN_EOF) {
		ASTNode* ast = parse_program();

		is_codegen_phase = 1;
		emit_code(ast);
		is_codegen_phase = 0;

		void clear_local_symbols(void);
		clear_local_symbols();
	}

	// Dynamic Externs replacement hook
	void emit_external_declarations(void);
	emit_external_declarations(); 

	void flush_global_data_segment(void);
	flush_global_data_segment();

	printf("end\n");
	fflush(stdout);

	// Restore original stdout stream targets back from file redirection modes
	_dup2(orig_stdout_fd, _fileno(stdout));
	_close(orig_stdout_fd);
	freopen("CONOUT$", "w", stdout);

	// Close the preprocessed input stream handle now that parsing is complete
	fclose(f);
	// Clean up the temporary preprocessed file right away
	remove(staging_file);

	// --- PHASE 3: TWO-PASS PEEPHOLE OPTIMIZATION ENGINE ---
	FILE* raw_asm = fopen("cc_compiler_temp.asm", "r");
	if (!raw_asm) {
		fprintf(stderr, "Error: Could not open unoptimized assembly stream file.\n");
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

	// Final close out operations
	fclose(raw_asm);
	if (final_output != stdout) fclose(final_output);
	remove("cc_compiler_temp.asm");

	return 0;
}
