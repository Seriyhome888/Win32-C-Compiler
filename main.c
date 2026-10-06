#include "cc.h"

extern Token cur_tok;

int main(int argc, char* argv[]) {
	setvbuf(stdout, NULL, _IONBF, 0);
	setvbuf(stderr, NULL, _IONBF, 0);

	if (argc < 2) {
		fprintf(stderr, "Usage: cc_compiler <source_file.c>\n");
		return 1;
	}

	FILE* f = fopen(argv[1], "r");
	if (!f) {
		fprintf(stderr, "Error: Could not open source file %s\n", argv[1]);
		return 1;
	}

	// Output Assembly Preamble Directives Immediately
	printf(".model flat, c\n");
	printf(".code\n");
	printf("extern printf:proc\n\n");

	// Initialize compilation pipeline
	init_lexer(f);
	cur_tok = next_token();

	// Process file sequentially
	while (cur_tok.type != TOKEN_EOF) {
		ASTNode* ast = parse_program();
		emit_code(ast);
	}

	printf("end\n");
	fclose(f);
	return 0;
}
