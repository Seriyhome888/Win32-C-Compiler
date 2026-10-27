#include "cc.h"
#include <stdarg.h>

static const char* reg_names_8[] = { "al", "cl", "dl" };

// Track the current function's dynamic spilling offset allocation bounds
static int spill_stack_offset = 0;
#define MAX_SPILLS 64
static int spill_offsets[3] = { 0 }; // Stores stack offset for each spilled register (-1 if not spilled)

static int reg_map[3] = { 0 };
static const char* reg_names[] = { "eax", "ecx", "edx" };

static int label_id = 0;
int gen_label(void) { return label_id++; }

// Tracks which register was explicitly spilled to handle automated recovery layers
static int was_spilled[3] = { 0 };

int allocate_register(void) {
	// 1. STANDARD PASS: Check if a register is immediately available
	for (int i = 0; i < 3; i++) {
		if (!reg_map[i]) {
			reg_map[i] = 1;
			was_spilled[i] = 0;
			return i;
		}
	}

	// 2. SPILL PASS: All registers are full! Select register 2 ("edx") as the spill victim
	int victim = 2;

	// Dynamically expand the function's stack frame requirements downward by 4 bytes
	spill_stack_offset += 4;
	spill_offsets[victim] = spill_stack_offset;

	// Emit the physical assembly instruction to spill the register onto the stack frame safely
	printf("    ; --- REGISTER SPILL OUT TRIGGERED --- \n");
	printf("    mov dword ptr [ebp - %d], %s\n", spill_stack_offset, reg_names[victim]);

	// Mark the victim register as structurally managed and return it to the caller
	reg_map[victim] = 1;
	was_spilled[victim] = 1;
	return victim;
}

void free_register(int r) {
	if (r >= 0 && r < 3) {
		reg_map[r] = 0;
		was_spilled[r] = 0;
		spill_offsets[r] = 0;
	}
}

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

// Ensures a register's original content is restored if it was spilled to the stack frame
int ensure_register_loaded(int r) {
	if (r >= 0 && r < 3 && spill_offsets[r] > 0) {
		printf("    ; --- REGISTER UN-SPILL RECOVERY --- \n");

		// Allocate a fresh target register to recover the data into
		reg_map[r] = 0; // Temporarily unlock the victim map boundary
		int target_reg = allocate_register();

		// Emit the assembly instruction to read the spilled data back into an active register
		printf("    mov %s, dword ptr [ebp - %d]\n", reg_names[target_reg], spill_offsets[r]);

		// Clear out tracking values for this specific slot now that it's recovered
		spill_offsets[r] = 0;
		return target_reg;
	}
	return r;
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
		for (int i = 0; i < 3; i++) { reg_map[i] = 0; spill_offsets[i] = 0; }
		spill_stack_offset = 0; // Reset spill tracking allocator for the incoming function layout frame

		printf("PUBLIC %s\n", node->name);
		printf("%s proc\n", node->name);
		printf("    push ebp\n    mov ebp, esp\n");

		// Dynamic allocation including the base calculated requirements
		printf("    sub esp, %d\n", node->int_val);

		emit_code(node->body);
		printf("    mov esp, ebp\n    pop ebp\n    ret\n%s endp\n\n", node->name);
		break;

	case AST_IDENT: {
		Symbol* sym = lookup_symbol(node->name);
		if (!sym) { printf("Semantic Error: '%s' unresolved.\n", node->name); exit(1); }

		int r = allocate_register();
		node->type = sym->type;
		node->int_val = r;

		if (sym->type.kind == TYPE_CHAR) {
			// Clean single-byte read using move-with-sign-extension (movsx) or zero-extension (movzx)
			if (sym->offset < 0) {
				printf("    movzx %s, byte ptr [ebp - %d]\n", reg_names[r], -sym->offset);
			}
			else {
				printf("    movzx %s, byte ptr [ebp + %d]\n", reg_names[r], sym->offset);
			}
		}
		else {
			// Standard 32-bit integer read pipeline
			if (sym->offset < 0) {
				printf("    mov %s, dword ptr [ebp - %d]\n", reg_names[r], -sym->offset);
			}
			else {
				printf("    mov %s, dword ptr [ebp + %d]\n", reg_names[r], sym->offset);
			}
		}
		break;
	}

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
		// --- NEW: SHORT-CIRCUIT BOOLEAN EVALUATIONS ---
		if (node->op == TOKEN_LOGAND || node->op == TOKEN_LOGOR) {
			int shortcut_label = gen_label();
			int end_label = gen_label();

			// 1. Evaluate the left side completely
			emit_code(node->left);
			int rl = node->left->int_val;

			printf("    cmp %s, 0\n", reg_names[rl]);
			if (node->op == TOKEN_LOGAND) {
				// For AND: if left side is 0 (false), skip right side, jump to shortcut (result = 0)
				printf("    je L%d\n", shortcut_label);
			}
			else {
				// For OR: if left side is NOT 0 (true), skip right side, jump to shortcut (result = 1)
				printf("    jne L%d\n", shortcut_label);
			}

			// If we didn't jump, the register 'rl' can be reused or freed for the right side
			free_register(rl);

			// 2. Evaluate the right side completely
			emit_code(node->right);
			int rr = node->right->int_val;

			// Normalize right side result into 0 or 1
			printf("    cmp %s, 0\n", reg_names[rr]);
			int target_reg = allocate_register();
			printf("    setne %s\n", (target_reg == 0) ? "al" : (target_reg == 1) ? "cl" : "dl");
			printf("    movzx %s, %s\n", reg_names[target_reg], (target_reg == 0) ? "al" : (target_reg == 1) ? "cl" : "dl");
			free_register(rr);

			printf("    jmp L%d\n", end_label);

			// 3. The shortcut point
			printf("L%d:\n", shortcut_label);
			// Re-allocate target_reg if it was cleared out
			int final_reg = allocate_register();
			if (node->op == TOKEN_LOGAND) {
				printf("    mov %s, 0\n", reg_names[final_reg]); // False fallback
			}
			else {
				printf("    mov %s, 1\n", reg_names[final_reg]); // True fallback
			}

			printf("L%d:\n", end_label);

			// Combine internal registers safely
			node->int_val = final_reg;
			node->type.kind = TYPE_INT;
			break;
		}
		// --- END OF BOOLEAN ENGINE ---

		int is_float = (node->left->type.kind == TYPE_FLOAT || node->right->type.kind == TYPE_FLOAT || node->type.kind == TYPE_FLOAT);

		// 1. Evaluate left child node tree
		emit_code(node->left);
		int rl = node->left->int_val;

		// 2. Evaluate right child node tree (this step can trigger a spill if expression is deep!)
		emit_code(node->right);
		int rr = node->right->int_val;

		// 3. CRITICAL HOOK: Verify if our left register got spilled during the right child's generation pass!
		rl = ensure_register_loaded(rl);

		if (!is_float) {
			if (node->op == TOKEN_PLUS) printf("    add %s, %s\n", reg_names[rl], reg_names[rr]);
			else if (node->op == TOKEN_MINUS) printf("    sub %s, %s\n", reg_names[rl], reg_names[rr]);
			else if (node->op == TOKEN_STAR) printf("    imul %s, %s\n", reg_names[rl], reg_names[rr]);

			// --- NEW: INTEGER DIVISION & MODULO PIPELINE ---
			else if (node->op == TOKEN_SLASH || node->op == TOKEN_PERCENT) {
				// 1. We must force the left operand (dividend) into EAX (index 0)
				// 2. We must free up EDX (index 2) so CDQ can clobber it safely

				// Save any values currently residing in eax/edx that aren't 'rl' or 'rr'
				int save_eax = (rl != 0 && rr != 0 && reg_map[0]);
				int save_edx = (rl != 2 && rr != 2 && reg_map[2]);

				if (save_eax) printf("    push eax\n");
				if (save_edx) printf("    push edx\n");

				// Move left operand into eax if it isn't already there
				if (rl != 0) {
					printf("    mov eax, %s\n", reg_names[rl]);
				}

				// Sign-extend EAX into EDX (creates a 64-bit dividend across edx:eax)
				printf("    cdq\n");

				// Execute the division. This divides edx:eax by the register rr
				// If rr happens to be eax or edx, we must account for where it went!
				if (rr == 0) {
					// Exceptional case: if right operand was eax, it's already lost or overridden
					printf("    ; Warning: potential division metadata collision\n");
				}
				printf("    idiv %s\n", reg_names[rr]);

				// Collect the target output
				if (node->op == TOKEN_SLASH) {
					// Division result is stored in EAX
					if (rl != 0) {
						printf("    mov %s, eax\n", reg_names[rl]);
					}
				}
				else {
					// Modulo remainder result is stored in EDX
					if (rl != 2) {
						printf("    mov %s, edx\n", reg_names[rl]);
					}
					else {
						// If rl was already index 2 (edx), it naturally has our remainder now!
					}
				}

				// Restore clobbered registers back from stack frames
				if (save_edx) printf("    pop edx\n");
				if (save_eax) printf("    pop eax\n");
			}
			// --- END OF NEW PIPELINE ---

			else if (node->op == TOKEN_LESS || node->op == TOKEN_GREATER || node->op == TOKEN_EQUAL) {
				printf("    cmp %s, %s\n", reg_names[rl], reg_names[rr]);
				const char* bytes[] = { "al", "cl", "dl" };
				if (node->op == TOKEN_EQUAL) printf("    sete %s\n", bytes[rl]);
				else if (node->op == TOKEN_LESS) printf("    setl %s\n", bytes[rl]);
				else if (node->op == TOKEN_GREATER) printf("    setg %s\n", bytes[rl]);
				printf("    movzx %s, %s\n", reg_names[rl], bytes[rl]);
			}
			free_register(rr);
			node->int_val = rl;
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
				else if (node->op == TOKEN_SLASH) printf("    divss xmm0, xmm1\n");

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

		Symbol* sym = lookup_symbol(base->left->name);
		if (!sym) {
			printf("\n; Backend Error: Array base identifier '%s' not found!\n", base->left->name);
			exit(1);
		}

		int dim = 0, r_off = -1;
		compile_array_offset(node, &dim, sym, &r_off);
		printf("    shl %s, 2\n", reg_names[r_off]);

		int r_ad = allocate_register();

		// FIXED: Uniformly enforce correct MASM signage for reads
		if (sym->offset < 0) {
			printf("    lea %s, dword ptr [ebp - %d]\n", reg_names[r_ad], -sym->offset);
		}
		else {
			printf("    lea %s, dword ptr [ebp + %d]\n", reg_names[r_ad], sym->offset);
		}

		printf("    add %s, %s\n", reg_names[r_ad], reg_names[r_off]);
		printf("    mov %s, dword ptr [%s]\n", reg_names[r_ad], reg_names[r_ad]);

		free_register(r_off);
		node->int_val = r_ad;
		break;
	}

	case AST_ASSIGN: {
		// 1. ADVANCED OPTIMIZATION: Check if we are capturing an address-of assignment directly (e.g., ptr = &bob;)
		if (node->left->kind == AST_IDENT && node->right->kind == AST_UNOP && node->right->op == TOKEN_AMP) {
			node->int_val = -1;

			// Resolve the inner symbol directly instead of executing right-hand child sub-nodes
			Symbol* right_sym = lookup_symbol(node->right->left->name);
			Symbol* left_sym = lookup_symbol(node->left->name);

			if (!right_sym || !left_sym) {
				printf("Backend Error: Symbol resolution failure during reference mapping.\n");
				exit(1);
			}

			int r_addr = allocate_register();
			if (right_sym->offset < 0) {
				printf("    lea %s, dword ptr [ebp - %d]\n", reg_names[r_addr], -right_sym->offset);
			}
			else {
				printf("    lea %s, dword ptr [ebp + %d]\n", reg_names[r_addr], right_sym->offset);
			}

			if (left_sym->offset < 0) {
				printf("    mov dword ptr [ebp - %d], %s\n", -left_sym->offset, reg_names[r_addr]);
			}
			else {
				printf("    mov dword ptr [ebp + %d], %s\n", left_sym->offset, reg_names[r_addr]);
			}

			free_register(r_addr);
			return;
		}

		// 2. FALLBACK STANDARD PIPELINE: Evaluate the right-hand value expression first
		node->int_val = -1;
		emit_code(node->right);
		int rv = node->right->int_val; // Register holding the right-hand evaluated value

		// Case A: Assignment to a standard local identifier variable (e.g., x = 5;)
		if (node->left->kind == AST_IDENT) {
			Symbol* sym = lookup_symbol(node->left->name);
			if (!sym) {
				printf("\n; Backend Error: Assignment target variable '%s' not found!\n", node->left->name);
				exit(1);
			}

			if (sym->type.kind == TYPE_POINTER) {
				if (sym->offset < 0) {
					printf("    mov dword ptr [ebp - %d], %s\n", -sym->offset, reg_names[rv]);
				}
				else {
					printf("    mov dword ptr [ebp + %d], %s\n", sym->offset, reg_names[rv]);
				}
			}
			else if (sym->type.kind == TYPE_CHAR) {
				// Use 8-bit sub-register array to write exactly 1 byte down to stack memory!
				if (sym->offset < 0) {
					printf("    mov byte ptr [ebp - %d], %s\n", -sym->offset, reg_names_8[rv]);
				}
				else {
					printf("    mov byte ptr [ebp + %d], %s\n", sym->offset, reg_names_8[rv]);
				}
			}
			else if (sym->type.kind == TYPE_FLOAT) {
				if (node->right->type.kind == TYPE_INT) {
					printf("    cvtsi2ss xmm0, %s\n", reg_names[rv]);
					if (sym->offset < 0) {
						printf("    movss dword ptr [ebp - %d], xmm0\n", -sym->offset);
					}
					else {
						printf("    movss dword ptr [ebp + %d], xmm0\n", sym->offset);
					}
				}
				else {
					if (sym->offset < 0) {
						printf("    mov dword ptr [ebp - %d], %s\n", -sym->offset, reg_names[rv]);
					}
					else {
						printf("    mov dword ptr [ebp + %d], %s\n", sym->offset, reg_names[rv]);
					}
				}
			}
			else {
				if (sym->offset < 0) {
					printf("    mov dword ptr [ebp - %d], %s\n", -sym->offset, reg_names[rv]);
				}
				else {
					printf("    mov dword ptr [ebp + %d], %s\n", sym->offset, reg_names[rv]);
				}
			}
			free_register(rv);
		}

		// Case B: Pointer Dereference Target Assignment (e.g., *ptr = 99;)
		else if (node->left->kind == AST_UNOP && node->left->op == TOKEN_STAR) {
			emit_code(node->left->left);
			int r_addr = node->left->left->int_val; // Register containing target address memory point

			printf("    mov dword ptr [%s], %s\n", reg_names[r_addr], reg_names[rv]);
			free_register(r_addr);
			free_register(rv);
		}

		// Case C: Structure Member Access Assignment (e.g., bob.age = 30; or ptr->age = 31;)
		else if (node->left->kind == AST_MEMBER_ACCESS) {
			DataType target_type;
			int calculated_offset = 0;

			if (node->left->op == TOKEN_ARROW) {
				emit_code(node->left->left); // Load target pointer address variable container
				int r_ptr = node->left->left->int_val;

				resolve_member_offset(node->left, &calculated_offset, &target_type);

				if (target_type.kind == TYPE_FLOAT && node->right->type.kind == TYPE_INT) {
					printf("    cvtsi2ss xmm0, %s\n", reg_names[rv]);
					printf("    movss dword ptr [%s + %d], xmm0\n", reg_names[r_ptr], calculated_offset);
				}
				else {
					printf("    mov dword ptr [%s + %d], %s\n", reg_names[r_ptr], calculated_offset, reg_names[rv]);
				}
				free_register(r_ptr);
			}
			else { // Dot Operator Struct Layout Modification Route
				resolve_member_offset(node->left, &calculated_offset, &target_type);

				if (target_type.kind == TYPE_FLOAT && node->right->type.kind == TYPE_INT) {
					printf("    cvtsi2ss xmm0, %s\n", reg_names[rv]);
					if (calculated_offset < 0) {
						printf("    movss dword ptr [ebp - %d], xmm0\n", -calculated_offset);
					}
					else {
						printf("    movss dword ptr [ebp + %d], xmm0\n", calculated_offset);
					}
				}
				else {
					if (calculated_offset < 0) {
						printf("    mov dword ptr [ebp - %d], %s\n", -calculated_offset, reg_names[rv]);
					}
					else {
						printf("    mov dword ptr [ebp + %d], %s\n", calculated_offset, reg_names[rv]);
					}
				}
			}
			free_register(rv);
		}

		// Case D: Standard / Contiguous Array Cell Target Assignment (e.g., matrix[1][2] = 88;)
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
			printf("    shl %s, 2\n", reg_names[r_off]); // Scale index coordinate displacement (4 bytes per item)

			int r_ad = allocate_register();
			if (sym->offset < 0) {
				printf("    lea %s, dword ptr [ebp - %d]\n", reg_names[r_ad], -sym->offset);
			}
			else {
				printf("    lea %s, dword ptr [ebp + %d]\n", reg_names[r_ad], sym->offset);
			}
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

			free_register(r_ad);
			free_register(r_off);
			free_register(rv);
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

				// FIXED: Enforce clean formatting output for address resolution
				if (sym->offset < 0) {
					printf("    lea %s, dword ptr [ebp - %d]\n", reg_names[r], -sym->offset);
				}
				else {
					printf("    lea %s, dword ptr [ebp + %d]\n", reg_names[r], sym->offset);
				}

				free_register(rl);
				node->int_val = r;
			}
			else {
				printf("Backend Error: Lvalue required as unary '&' operand.\n");
				exit(1);
			}
		}

		else if (node->op == TOKEN_MINUS) {
			if (node->type.kind == TYPE_FLOAT) {
				// For floating points, we XOR the register with itself or load a sign bit mask,
				// or cleanly use a helper variable to switch the float's sign bits.
				int lbl = gen_label();
				printf("    .data\nneg_mask_%d REAL4 -1.0\n    .code\n", lbl);
				printf("    cvtsi2ss xmm1, dword ptr [neg_mask_%d]\n", lbl);
				printf("    movd xmm0, %s\n", reg_names[rl]);
				printf("    mulss xmm0, xmm1\n");
				printf("    movd %s, xmm0\n", reg_names[rl]);
			}
			else {
				// Standard integer negation: flips the sign bits using native x86 hardware pipeline
				printf("    neg %s\n", reg_names[rl]);
			}
			node->int_val = rl; // The result remains in the same register
		}

		break;
	}

	case AST_MEMBER_ACCESS: {
		DataType target_type;
		int calculated_offset = 0;
		int r = allocate_register();

		if (node->op == TOKEN_ARROW) {
			emit_code(node->left);
			int r_ptr = node->left->int_val;

			resolve_member_offset(node, &calculated_offset, &target_type);
			printf("    mov %s, dword ptr [%s + %d]\n", reg_names[r], reg_names[r_ptr], calculated_offset);
			free_register(r_ptr);
		}
		else { // Dot Operator
			resolve_member_offset(node, &calculated_offset, &target_type);
			if (calculated_offset < 0) {
				printf("    mov %s, dword ptr [ebp - %d]\n", reg_names[r], -calculated_offset);
			}
			else {
				printf("    mov %s, dword ptr [ebp + %d]\n", reg_names[r], calculated_offset);
			}
		}

		node->int_val = r;
		node->type = target_type;
		break;
	}

	case AST_SWITCH: {
		int l_end = gen_label();
		int l_default = -1;

		// Push this switch block onto the loop stack frame so 'break;' knows where to jump!
		loop_stack[loop_stack_top++] = (LoopFrame){ -1, l_end };

		// Compile the controlling condition expression
		emit_code(node->left);
		int r_cond = node->left->int_val; // Register holding the evaluated condition value

		// --- PASS 1: EMIT THE CONDITIONAL JUMP CASCADE ---
		printf("    ; --- BEGIN SWITCH JUMP CASCADE --- \n");
		ASTNode* s = (node->body && node->body->kind == AST_BLOCK) ? node->body->body : node->body;
		while (s) {
			if (s->kind == AST_CASE) {
				s->op = gen_label(); // Generate and assign an assembly label ID to this specific case node
				printf("    cmp %s, %d\n", reg_names[r_cond], s->int_val);
				printf("    je L%d\n", s->op);
			}
			else if (s->kind == AST_DEFAULT) {
				s->op = gen_label(); // Generate label ID for default
				l_default = s->op;
			}
			s = s->next;
		}
		free_register(r_cond); // Free condition register immediately after the cascade pass

		// If a default block exists, jump to it if no cases matched. Otherwise, jump to the end.
		if (l_default != -1) {
			printf("    jmp L%d\n", l_default);
		}
		else {
			printf("    jmp L%d\n", l_end);
		}

		// --- PASS 2: GENERATE INNER STATEMENTS BODY ---
		printf("    ; --- BEGIN SWITCH BODY EXECUTION --- \n");
		s = (node->body && node->body->kind == AST_BLOCK) ? node->body->body : node->body;
		while (s) {
			if (s->kind == AST_CASE || s->kind == AST_DEFAULT) {
				// Emit the matching execution entry target label point
				printf("L%d:\n", s->op);
			}
			else {
				emit_code(s);
				// Clean up leaking tracking registers inside statement boundaries
				if (s->int_val >= 0 && s->int_val < 3) {
					free_register(s->int_val);
					s->int_val = -1;
				}
			}
			s = s->next;
		}

		// Emit terminating exit target frame boundary
		printf("L%d:\n", l_end);
		loop_stack_top--; // Pop switch frame off the stack
		break;
	}

	}
}