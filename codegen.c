#include "cc.h"
#include <stdarg.h>

static int reg_map[3] = { 0 };
static const char* reg_names[] = { "eax", "ecx", "edx" };

static int label_id = 0;
int gen_label(void) { return label_id++; }

int allocate_register(void) {
	for (int i = 0; i < 3; i++) {
		if (!reg_map[i]) { reg_map[i] = 1; return i; }
	}
	printf("Backend Error: Out of general purpose registers.\n");
	exit(1);
}
void free_register(int r) { if (r >= 0 && r < 3) reg_map[r] = 0; }

typedef struct { int cond_lbl; int end_lbl; } LoopFrame;
static LoopFrame loop_stack[32];
static int loop_stack_top = 0;

void compile_array_offset(ASTNode* node, int* dim, Symbol* sym, int* r_out) {
	if (node->left->kind == AST_ARRAY_ACCESS) {
		// 1. Traverse down the left side first to process higher-level dimensions
		compile_array_offset(node->left, dim, sym, r_out);
		(*dim)++;

		// 2. Allocate temporary register to load the current dimension's multiplier width
		int r_mul = allocate_register();
		printf("    mov %s, %d\n", reg_names[r_mul], sym->type.dimensions[*dim]);
		printf("    imul %s, %s\n", reg_names[*r_out], reg_names[r_mul]);
		free_register(r_mul);

		// 3. Compile the right-side index expression of the current sub-bracket
		emit_code(node->right);
		int r_next = node->right->int_val;
		printf("    add %s, %s\n", reg_names[*r_out], reg_names[r_next]);
		free_register(r_next);
	}
	else {
		// Base case: We reached the deepest array access node (the identifier array base)
		// Compile the outermost row index first
		emit_code(node->right);
		*r_out = node->right->int_val;
		*dim = 0; // Initialize dimension depth pointer to dimension index 0
	}
}

void emit_code(ASTNode* node) {
	if (!node) return;

	switch (node->kind) {

	case AST_STRUCT_DECL:
	case AST_UNION_DECL:
		// Structure and Union declarations are processed entirely during parsing 
		// to build environment offsets; they do not emit active assembly instructions.
	case AST_VAR_DECL:
		break;

	case AST_FUNC_DECL:
		// Reset register allocations cleanly for the new function scope frame
		for (int i = 0; i < 3; i++) reg_map[i] = 0;

		printf("PUBLIC %s\n", node->name);
		printf("%s proc\n", node->name);
		printf("    push ebp\n    mov ebp, esp\n    sub esp, 64\n");
		emit_code(node->body);
		printf("    mov esp, ebp\n    pop ebp\n    ret\n%s endp\n\n", node->name);
		break;

	case AST_IDENT: {
		Symbol* sym = lookup_symbol(node->name);
		if (!sym) {
			// FIXED: Throw a hard error instead of failing silently!
			printf("Semantic Compiler Error: Local variable '%s' could not be resolved in the symbol table.\n", node->name);
			exit(1);
		}
		int r = allocate_register();
		printf("    mov %s, dword ptr [ebp + (%d)]\n", reg_names[r], sym->offset);
		node->int_val = r;
		node->type = sym->type;
		break;
	}
				  // Inside codegen.c -> update your AST_LITERAL block layout
	case AST_LITERAL: {
		int r = allocate_register();
		if (node->type.kind == TYPE_INT) {
			printf("    mov %s, %d\n", reg_names[r], node->int_val);
		}
		else if (node->type.kind == TYPE_FLOAT) {
			int lbl = gen_label();
			// Define the raw float value in the data section
			printf("    .data\nflt_lbl_%d REAL4 %f\n    .code\n", lbl, node->float_val);

			// FIXED: Load the actual value stored at the label, not the address pointer!
			printf("    mov %s, dword ptr [flt_lbl_%d]\n", reg_names[r], lbl);
		}
		else if (node->type.kind == TYPE_POINTER) {
			int lbl = gen_label();
			printf("    .data\nstr_lbl_%d DB ", lbl);
			printf("\"");
			for (int i = 0; node->name[i] != '\0'; i++) {
				if (node->name[i] == '\n') {
					if (node->name[i + 1] != '\0') printf("\", 10, \"");
					else printf("\", 10");
				}
				else {
					putchar(node->name[i]);
				}
			}
			if (node->name[strlen(node->name) - 1] == '\n') printf(", 0\n    .code\n");
			else printf("\", 0\n    .code\n");

			printf("    lea %s, str_lbl_%d\n", reg_names[r], lbl);
		}
		node->int_val = r;
		break;
	}

	case AST_BINOP: {
		int is_float = (node->left->type.kind == TYPE_FLOAT || node->right->type.kind == TYPE_FLOAT || node->type.kind == TYPE_FLOAT);
		emit_code(node->left); emit_code(node->right);
		int rl = node->left->int_val, rr = node->right->int_val;

		if (!is_float) {
			if (node->op == TOKEN_PLUS) printf("    add %s, %s\n", reg_names[rl], reg_names[rr]);
			else if (node->op == TOKEN_MINUS) printf("    sub %s, %s\n", reg_names[rl], reg_names[rr]);
			else if (node->op == TOKEN_STAR) printf("    imul %s, %s\n", reg_names[rl], reg_names[rr]);
			else if (node->op == TOKEN_LESS || node->op == TOKEN_GREATER || node->op == TOKEN_EQUAL) {
				printf("    cmp %s, %s\n", reg_names[rl], reg_names[rr]);
				const char* bytes[] = { "al", "cl", "dl" };
				if (node->op == TOKEN_EQUAL) printf("    sete %s\n", bytes[rl]);
				else if (node->op == TOKEN_LESS) printf("    setl %s\n", bytes[rl]);
				else if (node->op == TOKEN_GREATER) printf("    setg %s\n", bytes[rl]);
				printf("    movzx %s, %s\n", reg_names[rl], bytes[rl]);
			}
			free_register(rr); node->int_val = rl;
		}
		else {
			// FIXED SSE2 FLOATING-POINT PIPELINE
			if (node->left->type.kind == TYPE_INT) {
				printf("    cvtsi2ss xmm0, %s\n", reg_names[rl]);
			}
			else {
				// Move the raw raw float bits directly from the general-purpose register into xmm0
				printf("    movd xmm0, %s\n", reg_names[rl]);
			}

			if (node->right->type.kind == TYPE_INT) {
				printf("    cvtsi2ss xmm1, %s\n", reg_names[rr]);
			}
			else {
				// Move the raw float bits directly from the general-purpose register into xmm1
				printf("    movd xmm1, %s\n", reg_names[rr]);
			}
			free_register(rr); // Free right register early

			if (node->op == TOKEN_LESS || node->op == TOKEN_GREATER || node->op == TOKEN_EQUAL) {
				printf("    comiss xmm0, xmm1\n");
				const char* bytes[] = { "al", "cl", "dl" };
				if (node->op == TOKEN_EQUAL) printf("    sete %s\n", bytes[rl]);
				// setb maps to < for floats, seta maps to > for floats
				else if (node->op == TOKEN_LESS) printf("    setb %s\n", bytes[rl]);
				else if (node->op == TOKEN_GREATER) printf("    seta %s\n", bytes[rl]);
				printf("    movzx %s, %s\n", reg_names[rl], bytes[rl]);
				node->int_val = rl;
				node->type.kind = TYPE_INT; // Boolean evaluation results are integers
			}
			else {
				if (node->op == TOKEN_PLUS) printf("    addss xmm0, xmm1\n");
				else if (node->op == TOKEN_MINUS) printf("    subss xmm0, xmm1\n");
				else if (node->op == TOKEN_STAR) printf("    mulss xmm0, xmm1\n");

				// Safely extract the float results back out into a general-purpose register
				// WITHOUT mutating or spilling onto 'esp'
				printf("    movd %s, xmm0\n", reg_names[rl]);
				node->int_val = rl;
				node->type.kind = TYPE_FLOAT;
			}
		}
		break;
	}

	case AST_ARRAY_ACCESS: {
		ASTNode* base = node;
		while (base->left->kind == AST_ARRAY_ACCESS) base = base->left;

		// Clean, direct lookup. No trailing while loop needed!
		Symbol* sym = lookup_symbol(base->left->name);
		if (!sym) {
			printf("\n; Backend Error: Array base identifier '%s' not found!\n", base->left->name);
			exit(1);
		}

		int dim = 0, r_off = -1;
		compile_array_offset(node, &dim, sym, &r_off);
		printf("    shl %s, 2\n", reg_names[r_off]);

		int r_ad = allocate_register();
		printf("    lea %s, dword ptr [ebp + (%d)]\n", reg_names[r_ad], sym->offset);
		printf("    add %s, %s\n", reg_names[r_ad], reg_names[r_off]);
		printf("    mov %s, dword ptr [%s]\n", reg_names[r_ad], reg_names[r_ad]);

		free_register(r_off);
		node->int_val = r_ad;
		break;
	}

	case AST_ASSIGN: {
		node->int_val = -1; // FIX: Prevent garbage data register cleanup bugs!
		emit_code(node->right);
		int rv = node->right->int_val;

		if (node->left->kind == AST_IDENT) {
			Symbol* sym = lookup_symbol(node->left->name);

			if (!sym) {
				printf("\n; Backend Error: Assignment target variable '%s' not found!\n", node->left->name);
				exit(1);
			}

			if (sym->type.kind == TYPE_FLOAT) {
				if (node->right->type.kind == TYPE_INT) {
					printf("    cvtsi2ss xmm0, %s\n", reg_names[rv]);
					// Store float value cleanly from XMM into the local variable offset
					printf("    movss dword ptr [ebp + (%d)], xmm0\n", sym->offset);
				}
				else {
					// Value is already a float stored inside the general purpose register bits
					printf("    mov dword ptr [ebp + (%d)], %s\n", sym->offset, reg_names[rv]);
				}
				free_register(rv);
			}
			else {
				printf("    mov dword ptr [ebp + (%d)], %s\n", sym->offset, reg_names[rv]);
				free_register(rv);
			}
		}
		else if (node->left->kind == AST_UNOP && node->left->op == TOKEN_STAR) { // Target is *ptr = expr;
			emit_code(node->left->left);
			int r_addr = node->left->left->int_val;
			printf("    mov dword ptr [%s], %s\n", reg_names[r_addr], reg_names[rv]);
			free_register(r_addr);
			free_register(rv);
		}
		else if (node->left->kind == AST_ARRAY_ACCESS) {
			ASTNode* base = node->left;
			while (base->left->kind == AST_ARRAY_ACCESS) base = base->left;

			Symbol* sym = lookup_symbol(base->left->name);
			if (!sym) {
				printf("\n; Backend Error: Assignment array target '%s' not found!\n", base->left->name);
				exit(1);
			}

			int dim = 0, r_off = -1;
			compile_array_offset(node->left, &dim, sym, &r_off);
			printf("    shl %s, 2\n", reg_names[r_off]);
			int r_ad = allocate_register();
			printf("    lea %s, dword ptr [ebp + (%d)]\n", reg_names[r_ad], sym->offset);
			printf("    add %s, %s\n", reg_names[r_ad], reg_names[r_off]);

			if (sym->type.kind == TYPE_FLOAT) {
				if (node->right->type.kind == TYPE_INT) {
					printf("    cvtsi2ss xmm0, %s\n", reg_names[rv]);
					printf("    movss dword ptr [%s], xmm0\n", reg_names[r_ad]);
				}
				else {
					printf("    mov dword ptr [%s], %s\n", reg_names[r_ad], reg_names[rv]);
				}
			}
			else {
				printf("    mov dword ptr [%s], %s\n", reg_names[r_ad], reg_names[rv]);
			}
			free_register(r_ad); free_register(r_off); free_register(rv);
		}
		break;
	}

	case AST_IF: {
		int l_else = gen_label(), l_end = gen_label();
		emit_code(node->cond);

		int r_cond = node->cond->int_val;
		printf("    cmp %s, 0\n    je L%d\n", reg_names[r_cond], l_else);
		free_register(r_cond); // Cleanly free the condition register right away!

		emit_code(node->body);
		// CRITICAL RECOVERY STEP: If the body block left behind a stray register, clean it up!
		if (node->body && node->body->int_val >= 0 && node->body->int_val < 3) {
			free_register(node->body->int_val);
			node->body->int_val = -1;
		}

		printf("    jmp L%d\nL%d:\n", l_end, l_else);
		if (node->else_body) {
			emit_code(node->else_body);
			// Clear out any leaked registers remaining inside the else statement body block
			if (node->else_body && node->else_body->int_val >= 0 && node->else_body->int_val < 3) {
				free_register(node->else_body->int_val);
				node->else_body->int_val = -1;
			}
		}
		printf("L%d:\n", l_end);
		break;
	}

	case AST_BLOCK: {
		ASTNode* s = node->body;
		while (s) {
			emit_code(s);

			// Clean up and free tracking registers for standalone statement expressions
			// to guarantee full register availability for subsequent code blocks.
			if (s->kind == AST_CALL || s->kind == AST_BINOP || s->kind == AST_LITERAL || s->kind == AST_IDENT || s->kind == AST_ASSIGN) { // <-- Added AST_ASSIGN here!
				if (s->int_val >= 0 && s->int_val < 3) {
					free_register(s->int_val);
					s->int_val = -1;
				}
			}
			s = s->next;
		}
		break;
	}

	case AST_WHILE: {
		int l_co = gen_label(), l_en = gen_label();
		loop_stack[loop_stack_top++] = (LoopFrame){ l_co, l_en };
		printf("L%d:\n", l_co);

		emit_code(node->cond);
		int r_cond = node->cond->int_val; // Explicitly track the condition register
		printf("    cmp %s, 0\n    je L%d\n", reg_names[r_cond], l_en);
		free_register(r_cond);            // Free right away before executing the loop body!

		emit_code(node->body);
		printf("    jmp L%d\nL%d:\n", l_co, l_en);
		loop_stack_top--;
		break;
	}

	case AST_FOR: {
		int l_co = gen_label(), l_st = gen_label(), l_en = gen_label();
		emit_code(node->left);
		if (node->left && node->left->int_val >= 0) free_register(node->left->int_val);

		loop_stack[loop_stack_top++] = (LoopFrame){ l_st, l_en };
		printf("L%d:\n", l_co);

		emit_code(node->cond);
		int r_cond = node->cond->int_val; // Explicitly track the condition register
		printf("    cmp %s, 0\n    je L%d\n", reg_names[r_cond], l_en);
		free_register(r_cond);            // Free right away before executing the loop body!

		emit_code(node->body);
		printf("L%d:\n", l_st);

		emit_code(node->right);
		if (node->right && node->right->int_val >= 0) free_register(node->right->int_val);

		printf("    jmp L%d\nL%d:\n", l_co, l_en);
		loop_stack_top--;
		break;
	}

	case AST_BREAK: printf("    jmp L%d\n", loop_stack[loop_stack_top - 1].end_lbl); break;
	case AST_CONTINUE: printf("    jmp L%d\n", loop_stack[loop_stack_top - 1].cond_lbl); break;

	case AST_RETURN: {
		emit_code(node->left);
		int r_val = node->left->int_val;

		if (node->left->type.kind == TYPE_FLOAT) {
			// Allocate a 4-byte temporary space on the stack to bridge to the FPU stack
			printf("    sub esp, 4\n");
			printf("    mov dword ptr [esp], %s\n", reg_names[r_val]);

			// FLD loads the 32-bit float from memory onto the top of the FPU stack ST(0)
			printf("    fld dword ptr [esp]\n");
			printf("    add esp, 4\n");
		}
		else {
			// Standard integer or pointer return route
			printf("    mov eax, %s\n", reg_names[r_val]);
		}

		free_register(r_val);
		break;
	}

	case AST_CALL: {
		ASTNode* args_array[32];
		int count = 0;
		ASTNode* arg = node->left;

		while (arg && count < 32) {
			args_array[count++] = arg;
			arg = arg->next;
		}

		int total_stack_bytes = 0;

		// Push arguments onto the stack in REVERSE order (Right to Left)
		for (int i = count - 1; i >= 0; i--) {
			ASTNode* curr_arg = args_array[i];
			emit_code(curr_arg);
			int r_val = curr_arg->int_val;

			// CRITICAL FIX: If it's an identifier, pull its true type directly from the symbol table
			DataTypeKind arg_kind = curr_arg->type.kind;
			if (curr_arg->kind == AST_IDENT) {
				Symbol* sym = lookup_symbol(curr_arg->name);
				if (sym) {
					arg_kind = sym->type.kind;
				}
			}

			// Perform the push layout using the verified type context data
			if (arg_kind == TYPE_FLOAT) {
				// 1. Move the 32-bit raw float bits to XMM
				printf("    movd xmm0, %s\n", reg_names[r_val]);

				// 2. Perform hardware promotion from 32-bit Float to 64-bit Double
				printf("    cvtss2sd xmm0, xmm0\n");

				// 3. Substack allocation space for the 8-byte double payload
				printf("    sub esp, 8\n");

				// 4. Move 64-bit double directly onto the stack layout frame
				printf("    movsd qword ptr [esp], xmm0\n");

				total_stack_bytes += 8;
			}
			else {
				// Standard 32-bit Integer variable or String base memory address label pointer
				printf("    push %s\n", reg_names[r_val]);
				total_stack_bytes += 4;
			}
			free_register(r_val);
		}

		printf("    call %s\n", node->name);

		if (total_stack_bytes > 0) {
			printf("    add esp, %d\n", total_stack_bytes);
		}

		int r = allocate_register();


		// FIXED: Determine if the called function is registered as returning a float
		Symbol* func_sym = lookup_symbol(node->name);
		if (func_sym && func_sym->type.kind == TYPE_FLOAT) {
			// Pull the float off the hardware FPU stack ST(0) into a temporary stack slot
			printf("    sub esp, 4\n");
			printf("    fstp dword ptr [esp]\n");

			// Move the raw float bits back into our managed general purpose register
			printf("    mov %s, dword ptr [esp]\n", reg_names[r]);
			printf("    add esp, 4\n");

			node->type.kind = TYPE_FLOAT; // Ensure the call expression node is flagged as a float!
		}
		else {
			// Standard integer or pointer payload collection
			printf("    mov %s, eax\n", reg_names[r]);
		}
		//printf("    mov %s, eax\n", reg_names[r]);


		node->int_val = r;
		break;
	}

	case AST_UNOP: {
		emit_code(node->left);
		int rl = node->left->int_val; // Register containing child node's evaluated result

		if (node->op == TOKEN_STAR) { // Pointer Dereference (*ptr)
			int r = allocate_register();
			// Perform indirect register displacement memory fetch: mov dest, dword ptr [src_reg]
			printf("    mov %s, dword ptr [%s]\n", reg_names[r], reg_names[rl]);
			free_register(rl);
			node->int_val = r;
		}
		else if (node->op == TOKEN_AMP) { // Address-of Operator (&var)
			if (node->left->kind == AST_IDENT) {
				Symbol* sym = lookup_symbol(node->left->name);
				if (!sym) {
					printf("Semantic Error: Variable %s undefined for address-of operator.\n", node->left->name);
					exit(1);
				}
				int r = allocate_register();
				// Load effective localized base pointer frame address: lea dest, [ebp + offset]
				printf("    lea %s, dword ptr [ebp + (%d)]\n", reg_names[r], sym->offset);
				free_register(rl);
				node->int_val = r;
			}
			else {
				printf("Backend Error: Lvalue required as unary '&' operand.\n");
				exit(1);
			}
		}
		break;
	}
	}
}
