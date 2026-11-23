#include "cc.h"
#include <stdarg.h>

#define MAX_SPILLS 256
static int spill_base_offset = 0;

#define MAX_LITERAL_BUFFER 512
typedef struct {
	int id;
	int is_float; // 1 = float, 0 = string
	double float_val;
	char str_val[512]; // Sized character array buffer bounds protection
} LiteralEntry;

static LiteralEntry literal_pool[MAX_LITERAL_BUFFER];
static int literal_pool_count = 0;

void add_buffered_float(int id, double val) {
	if (literal_pool_count >= MAX_LITERAL_BUFFER) return;
	literal_pool[literal_pool_count].id = id;
	literal_pool[literal_pool_count].is_float = 1;
	literal_pool[literal_pool_count].float_val = val;
	literal_pool_count++;
}

void add_buffered_string(int id, const char* str) {
	if (literal_pool_count >= MAX_LITERAL_BUFFER) return;
	literal_pool[literal_pool_count].id = id;
	literal_pool[literal_pool_count].is_float = 0;
	strncpy(literal_pool[literal_pool_count].str_val, str, 511);
	literal_pool_count++;
}

void emit_external_declarations(void) {
	Symbol* sym = sym_table;
	printf("\n; --- DYNAMIC EXTERNAL SYMBOL REGISTRY --- \n");
	while (sym) {
		if (sym->is_extern) {
			if (!sym->is_global_var) {
				printf("extern %s:proc\n", sym->name);
			}
			else if (sym->type.is_array || sym->type.kind == TYPE_STRUCT || sym->type.kind == TYPE_UNION) {
				printf("extern %s:byte\n", sym->asm_label);
			}
			else if (sym->type.kind == TYPE_CHAR && sym->type.base_type == NULL) {
				printf("extern %s:byte\n", sym->asm_label);
			}
			else if (sym->type.kind == TYPE_FLOAT && sym->type.base_type == NULL) {
				printf("extern %s:real4\n", sym->asm_label);
			}
			else {
				printf("extern %s:dword\n", sym->asm_label);
			}
		}
		sym = sym->next;
	}
}

// Global invocation pipeline to flush out data cleanly at program teardown
void flush_global_data_segment(void) {
	if (literal_pool_count == 0) return;
	printf("\n.data\n");
	for (int i = 0; i < literal_pool_count; i++) {
		if (literal_pool[i].is_float) {
			printf("flt_lbl_%d REAL4 %f\n", literal_pool[i].id, literal_pool[i].float_val);
		}
		else {
			printf("str_lbl_%d DB ", literal_pool[i].id);
			printf("\"");
			for (int j = 0; literal_pool[i].str_val[j] != '\0'; j++) {
				if (literal_pool[i].str_val[j] == '\n') {
					if (literal_pool[i].str_val[j + 1] != '\0') printf("\", 10, \"");
					else printf("\", 10");
				}
				else {
					putchar(literal_pool[i].str_val[j]);
				}
			}
			if (literal_pool[i].str_val[strlen(literal_pool[i].str_val) - 1] == '\n') {
				printf(", 0\n");
			}
			else {
				printf("\", 0\n");
			}
		}
	}
}

void emit_global_variables(void) {
	Symbol* sym = sym_table;
	int has_globals = 0;
	while (sym) {
		if (sym->is_global_var && !sym->is_extern) { has_globals = 1; break; }
		sym = sym->next;
	}
	if (!has_globals) return;

	printf("\n.data\n");
	sym = sym_table;
	while (sym) {
		if (sym->is_global_var && !sym->is_extern) {
			if (!sym->is_static) {
				printf("PUBLIC %s\n", sym->asm_label);
			}
			int total_elems = 1;
			if (sym->type.is_array) {
				for (int i = 0; i < sym->type.dim_count; i++) {
					total_elems *= sym->type.dimensions[i];
				}
			}

			if (sym->type.is_array || sym->type.kind == TYPE_STRUCT || sym->type.kind == TYPE_UNION) {
				int total_bytes = get_type_size(sym->type);
				printf("%s DB %d DUP(0)\n", sym->asm_label, total_bytes);
			}
			else if (sym->type.kind == TYPE_CHAR && sym->type.base_type == NULL) {
				printf("%s DB %d\n", sym->asm_label, sym->has_init ? (sym->init_int & 0xFF) : 0);
			}
			else if (sym->type.kind == TYPE_FLOAT) {
				printf("%s REAL4 %f\n", sym->asm_label, sym->has_init ? sym->init_float : 0.0);
			}
			else {
				printf("%s DD %d\n", sym->asm_label, sym->has_init ? sym->init_int : 0);
			}
		}
		sym = sym->next;
	}
}

static const char* reg_names_8[] = { "al", "cl", "dl" };

// Track the current function's dynamic spilling offset allocation bounds
static int spill_stack_offset = 0;
static int spill_offsets[3] = { 0 }; // Stores stack offset for each spilled register (-1 if not spilled)

static int reg_map[3] = { 0 };
static const char* reg_names[] = { "eax", "ecx", "edx" };

// --- NEW UNIVERSAL WIDTH PROTECTION HOOK ---
const char* get_safe_reg_name(int r_idx, DataType type) {
	if (r_idx < 0 || r_idx >= 3) return "eax"; // Fallback safety bound

	// Force 32-bit registers for ANY pointer type, typedef wrapper, or array offset stride
	int is_ptr = (type.kind == TYPE_POINTER || type.base_type != NULL || type.is_array);

	if (is_ptr) {
		return reg_names[r_idx]; // Always return "eax", "ecx", or "edx"
	}

	if (type.kind == TYPE_CHAR) {
		return reg_names_8[r_idx]; // Returns "al", "cl", or "dl" safely for standalone chars
	}

	return reg_names[r_idx]; // Default to full 32-bit width
}

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

	// Grow our spill offset allocation bounds deeper into the reserved tail padding segment
	spill_stack_offset += 4;
	if (spill_stack_offset - spill_base_offset > MAX_SPILLS) {
		fprintf(stderr, "Backend Error: Register spill area exhausted.\n");
		exit(1);
	}
	spill_offsets[victim] = spill_stack_offset;

	// Emit the physical assembly instruction to spill the register safely
	printf("    ; --- REGISTER SPILL OUT TRIGGERED --- \n");
	printf("    mov dword ptr [ebp - %d], %s\n", spill_stack_offset, reg_names[victim]);

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

int ensure_register_loaded(int r);

static int get_array_element_size(DataType type) {
	if (type.kind == TYPE_POINTER || type.base_type != NULL) {
		return 4;
	}
	DataType elem_type = type;
	elem_type.is_array = 0;
	elem_type.dim_count = 0;
	return get_type_size(elem_type);
}

void compile_array_offset(ASTNode* node, int* dim, Symbol* sym, int* r_out) {
	if (node->left->kind == AST_ARRAY_ACCESS) {
		// 1. Walk down to the deepest nested sub-bracket layer first
		compile_array_offset(node->left, dim, sym, r_out);
		(*dim)++;

		// 2. Multiply the accumulated index by the current dimension's size bound
		int r_mul = allocate_register();
		printf("    mov %s, %d\n", reg_names[r_mul], sym->type.dimensions[*dim]);
		printf("    imul %s, %s\n", reg_names[*r_out], reg_names[r_mul]);
		free_register(r_mul);

		// 3. Evaluate and add the current dimension index expression
		emit_code(node->right);
		int r_next = node->right->int_val;
		r_next = ensure_register_loaded(r_next);
		printf("    add %s, %s\n", reg_names[*r_out], reg_names[r_next]);
		free_register(r_next);
	}
	else {
		// Base Case: Deepest dimension layer (e.g., the first index parsed)
		*dim = 0;
		emit_code(node->right);
		*r_out = node->right->int_val;
		*r_out = ensure_register_loaded(*r_out);
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

static int get_pointer_stride(DataType type) {
	if (type.kind == TYPE_POINTER && type.base_type != NULL) {
		return get_type_size(*(type.base_type));
	}
	if (type.is_array) {
		return get_array_element_size(type);
	}
	return 1;
}

static void emit_inc_dec(ASTNode* node, int is_inc, int is_postfix) {
	ASTNode* target = node->left;
	int r_addr = allocate_register();
	DataType val_type = target->type;
	int is_byte = 0;

	// 1. Resolve target l-value memory address into r_addr
	if (target->kind == AST_IDENT) {
		Symbol* sym = lookup_symbol(target->name);
		if (!sym) { fprintf(stderr, "Semantic Error: '%s' undefined.\n", target->name); exit(1); }
		if (sym->is_register) { fprintf(stderr, "Semantic Error: Address of register '%s'.\n", sym->name); exit(1); }
		val_type = sym->type;
		is_byte = (val_type.kind == TYPE_CHAR && val_type.base_type == NULL && !val_type.is_array);

		if (sym->is_global_var) printf("    lea %s, dword ptr [%s]\n", reg_names[r_addr], sym->asm_label);
		else if (sym->offset < 0) printf("    lea %s, dword ptr [ebp - %d]\n", reg_names[r_addr], -sym->offset);
		else printf("    lea %s, dword ptr [ebp + %d]\n", reg_names[r_addr], sym->offset);
	}
	else if (target->kind == AST_ARRAY_ACCESS) {
		ASTNode* base = target;
		while (base->left && base->left->kind == AST_ARRAY_ACCESS) base = base->left;
		Symbol* sym = lookup_symbol(base->left->name);
		if (!sym) { fprintf(stderr, "Backend Error: Array not found.\n"); exit(1); }

		int dim = 0, r_off = -1;
		compile_array_offset(target, &dim, sym, &r_off);
		r_off = ensure_register_loaded(r_off);

		int elem_size = get_array_element_size(sym->type);
		is_byte = (elem_size == 1);
		if (elem_size == 4) printf("    shl %s, 2\n", reg_names[r_off]);
		else if (elem_size != 1) printf("    imul %s, %d\n", reg_names[r_off], elem_size);

		if (sym->is_global_var) printf("    lea %s, dword ptr [%s]\n", reg_names[r_addr], sym->asm_label);
		else if (sym->offset < 0) printf("    lea %s, dword ptr [ebp - %d]\n", reg_names[r_addr], -sym->offset);
		else printf("    lea %s, dword ptr [ebp + %d]\n", reg_names[r_addr], sym->offset);

		printf("    add %s, %s\n", reg_names[r_addr], reg_names[r_off]);
		free_register(r_off);

		// Derive the dereferenced element type so pointers inside arrays (e.g., int* arr[4]; arr[0]++;) scale properly
		val_type = sym->type;
		val_type.is_array = 0;
		val_type.dim_count = 0;
	}
	else if (target->kind == AST_MEMBER_ACCESS) {
		int calc_off = 0;
		resolve_member_offset(target, &calc_off, &val_type);
		is_byte = (val_type.kind == TYPE_CHAR && val_type.base_type == NULL && !val_type.is_array);

		if (target->op == TOKEN_ARROW) {
			emit_code(target->left);
			int r_ptr = ensure_register_loaded(target->left->int_val);
			printf("    lea %s, dword ptr [%s + %d]\n", reg_names[r_addr], reg_names[r_ptr], calc_off);
			free_register(r_ptr);
		}
		else {
			ASTNode* root_id = target;
			while (root_id && root_id->kind == AST_MEMBER_ACCESS) root_id = root_id->left;
			Symbol* s_sym = (root_id && root_id->kind == AST_IDENT) ? lookup_symbol(root_id->name) : NULL;

			if (s_sym && s_sym->is_global_var)
				printf("    lea %s, dword ptr [%s + %d]\n", reg_names[r_addr], s_sym->asm_label, calc_off - s_sym->offset);
			else if (calc_off < 0)
				printf("    lea %s, dword ptr [ebp - %d]\n", reg_names[r_addr], -calc_off);
			else
				printf("    lea %s, dword ptr [ebp + %d]\n", reg_names[r_addr], calc_off);
		}
	}
	else if (target->kind == AST_UNOP && target->op == TOKEN_STAR) {
		emit_code(target->left);
		int r_ptr = ensure_register_loaded(target->left->int_val);
		printf("    mov %s, %s\n", reg_names[r_addr], reg_names[r_ptr]);
		free_register(r_ptr);

		if (target->left->type.kind == TYPE_POINTER && target->left->type.base_type != NULL) {
			val_type = *(target->left->type.base_type);
		}
		is_byte = (val_type.kind == TYPE_CHAR && val_type.base_type == NULL && !val_type.is_array);
	}
	else {
		fprintf(stderr, "Semantic Error: Lvalue required for ++/--.\n");
		exit(1);
	}

	// 2. Load current value, preserve old value if postfix, compute new value, and store back
	int r_val = allocate_register();
	r_addr = ensure_register_loaded(r_addr);

	if (is_byte) printf("    movzx %s, byte ptr [%s]\n", reg_names[r_val], reg_names[r_addr]);
	else printf("    mov %s, dword ptr [%s]\n", reg_names[r_val], reg_names[r_addr]);

	int r_ret = is_postfix ? allocate_register() : r_val;
	if (is_postfix) {
		r_val = ensure_register_loaded(r_val);
		r_addr = ensure_register_loaded(r_addr);
		printf("    mov %s, %s\n", reg_names[r_ret], reg_names[r_val]);
	}

	// 3. Apply typed pointer stride scaling (sizeof(*ptr)) or scalar +1 / -1
	int stride = 1;
	if (val_type.kind == TYPE_POINTER || val_type.base_type != NULL) {
		stride = get_pointer_stride(val_type);
	}

	if (val_type.kind == TYPE_FLOAT && val_type.base_type == NULL) {
		int lbl = gen_label();
		printf("    .data\ninc_one_%d REAL4 1.0\n    .code\n", lbl);
		printf("    movd xmm0, %s\n", reg_names[r_val]);
		printf("    %s xmm0, dword ptr [inc_one_%d]\n", is_inc ? "addss" : "subss", lbl);
		printf("    movd %s, xmm0\n", reg_names[r_val]);
	}
	else {
		printf("    %s %s, %d\n", is_inc ? "add" : "sub", reg_names[r_val], stride);
	}

	if (is_byte) printf("    mov byte ptr [%s], %s\n", reg_names[r_addr], reg_names_8[r_val]);
	else printf("    mov dword ptr [%s], %s\n", reg_names[r_addr], reg_names[r_val]);

	free_register(r_addr);
	if (is_postfix) free_register(r_val);

	node->int_val = r_ret;
	node->type = val_type;
}

static int emit_dot_base_address(ASTNode* member_node, int* out_total_offset, DataType* out_type) {
	ASTNode* root = member_node;
	while (root && root->kind == AST_MEMBER_ACCESS) root = root->left;

	if (root && root->kind == AST_ARRAY_ACCESS) {
		// Compute address of array element (e.g., pkts[i]) + member offset
		ASTNode* addr_node = create_node(AST_UNOP);
		addr_node->op = TOKEN_AMP;
		addr_node->left = root;
		emit_code(addr_node);
		int r_base = ensure_register_loaded(addr_node->int_val);
		free(addr_node);

		resolve_member_offset(member_node, out_total_offset, out_type);
		return r_base;
	}
	return -1; // Standard variable/global struct handled directly via ebp/label
}

void emit_code(ASTNode* node) {
	if (!node) return;

	switch (node->kind) {
	case AST_TERNARY: {
		int l_false = gen_label();
		int l_end = gen_label();

		// 1. Evaluate condition and branch
		emit_code(node->cond);
		int r_cond = ensure_register_loaded(node->cond->int_val);
		printf("    cmp %s, 0\n", reg_names[r_cond]);
		printf("    je $$L%d\n", l_false);
		free_register(r_cond);

		// Allocate a single stable destination register for both branches
		int out_reg = allocate_register();

		// 2. True branch
		emit_code(node->left);
		int r_true = ensure_register_loaded(node->left->int_val);
		if (r_true != out_reg) {
			printf("    mov %s, %s\n", reg_names[out_reg], reg_names[r_true]);
		}
		free_register(r_true);
		printf("    jmp $$L%d\n", l_end);

		// 3. False branch
		printf("$$L%d:\n", l_false);
		emit_code(node->right);
		int r_false = ensure_register_loaded(node->right->int_val);
		if (r_false != out_reg) {
			printf("    mov %s, %s\n", reg_names[out_reg], reg_names[r_false]);
		}
		free_register(r_false);

		printf("$$L%d:\n", l_end);
		node->int_val = out_reg;
		break;
	}

	case AST_POST_INC:
	case AST_POST_DEC: {
		emit_inc_dec(node, node->kind == AST_POST_INC, 1);
		break;
	}

	case AST_STRUCT_DECL:
	case AST_UNION_DECL:
		// Structure and Union declarations are processed entirely during parsing 
		// to build environment offsets; they do not emit active assembly instructions.
	case AST_VAR_DECL:
		break;

	case AST_FUNC_DECL: {
		for (int i = 0; i < 3; i++) { reg_map[i] = 0; spill_offsets[i] = 0; }

		// --- FIXED: Start register spills safely BELOW the function's local variables ---
		spill_base_offset = node->int_val;
		spill_stack_offset = node->int_val;

		int func_exit_lbl = gen_label();
		node->op = func_exit_lbl;

		Symbol* fn = lookup_symbol(node->name);
		if (!fn || !fn->is_static) {
			printf("PUBLIC %s\n", node->name);
		}

		printf("%s proc\n", node->name);
		printf("    push ebp\n    mov ebp, esp\n");

		// Reserve stack space for both local variables AND safe room for register spilling profiles
		printf("    sub esp, %d\n", node->int_val + MAX_SPILLS);

		emit_code(node->body);

		printf("    mov esp, ebp\n    pop ebp\n    ret\n%s endp\n\n", node->name);
		break;
	}

	case AST_IDENT: {
		Symbol* sym = lookup_symbol(node->name);
		if (!sym) { printf("Semantic Error: '%s' unresolved.\n", node->name); exit(1); }

		int r = allocate_register();
		node->type = sym->type;
		node->int_val = r;

		if (sym->is_global_var) {
			if (sym->type.kind == TYPE_CHAR && sym->type.base_type == NULL)
				printf("    movzx %s, byte ptr [%s]\n", reg_names[r], sym->asm_label);
			else
				printf("    mov %s, dword ptr [%s]\n", reg_names[r], sym->asm_label);
		}
		else
		{
			// --- FIXED: Only load an 8-bit byte if it's a RAW char, NOT a pointer to a char (char*) ---
			if (sym->type.kind == TYPE_CHAR && sym->type.base_type == NULL) {
				if (sym->offset < 0) {
					printf("    movzx %s, byte ptr [ebp - %d]\n", reg_names[r], -sym->offset);
				}
				else {
					printf("    movzx %s, byte ptr [ebp + %d]\n", reg_names[r], sym->offset);
				}
			}
			else {
				// Pointers (char*) and normal 32-bit Integers read normally here
				if (sym->offset < 0) {
					printf("    mov %s, dword ptr [ebp - %d]\n", reg_names[r], -sym->offset);
				}
				else {
					printf("    mov %s, dword ptr [ebp + %d]\n", reg_names[r], sym->offset);
				}
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
			add_buffered_float(lbl, node->float_val);
			printf("    mov %s, dword ptr [flt_lbl_%d]\n", reg_names[r], lbl);
		}
		else if (node->type.kind == TYPE_POINTER) {
			int lbl = gen_label();
			add_buffered_string(lbl, node->name);
			printf("    lea %s, str_lbl_%d\n", reg_names[r], lbl);
		}
		node->int_val = r;
		break;
	}

	case AST_BINOP: {
		// --- BALANCED SHORT-CIRCUIT BOOLEAN PIPELINE ---
		if (node->op == TOKEN_LOGAND || node->op == TOKEN_LOGOR) {
			int shortcut_label = gen_label();
			int end_label = gen_label();

			// 1. Evaluate the left-hand child expression completely
			emit_code(node->left);
			int rl = node->left->int_val;
			rl = ensure_register_loaded(rl);

			printf("    cmp %s, 0\n", reg_names[rl]);
			if (node->op == TOKEN_LOGAND) {
				// For AND: if left side is false (0), skip right side, jump straight to shortcut
				printf("    je $$L%d\n", shortcut_label);
			}
			else {
				// For OR: if left side is true (!=0), skip right side, jump straight to shortcut
				printf("    jne $$L%d\n", shortcut_label);
			}

			// Free left register immediately after branching so it doesn't stay pinned during right evaluation
			free_register(rl);

			// Allocate a stable output container register that survives both branches identically
			int out_reg = allocate_register();

			// 2. Fallthrough branch path: Left condition didn't skip, so evaluate the right side
			emit_code(node->right);
			int rr = node->right->int_val;
			rr = ensure_register_loaded(rr);

			// Normalize the right side evaluation result cleanly into a 0 or 1 bit flag
			printf("    cmp %s, 0\n", reg_names[rr]);
			printf("    setne %s\n", (out_reg == 0) ? "al" : (out_reg == 1) ? "cl" : "dl");
			printf("    movzx %s, %s\n", reg_names[out_reg], (out_reg == 0) ? "al" : (out_reg == 1) ? "cl" : "dl");

			// De-allocate temporary right child calculation tracker safely
			free_register(rr);

			printf("    jmp $$L%d\n", end_label);

			// 3. Shortcut landing branch path: Left side short-circuited the condition logic
			printf("$$L%d:\n", shortcut_label);
			if (node->op == TOKEN_LOGAND) {
				printf("    mov %s, 0\n", reg_names[out_reg]); // False shortcut payload
			}
			else {
				printf("    mov %s, 1\n", reg_names[out_reg]); // True shortcut payload
			}

			printf("$$L%d:\n", end_label);

			// Safely pass our output container context tracking register up to the parent tree node
			node->int_val = out_reg;
			node->type.kind = TYPE_INT;
			break;
		}
		// --- END OF BALANCED BOOLEAN ENGINE ---

		int is_float = (node->left->type.kind == TYPE_FLOAT || node->right->type.kind == TYPE_FLOAT || node->type.kind == TYPE_FLOAT);

		// 1. Evaluate left child node tree
		emit_code(node->left);
		int rl = node->left->int_val;

		// 2. Evaluate right child node tree (this step can trigger a spill if expression is deep!)
		emit_code(node->right);
		int rr = node->right->int_val;

		// 3. CRITICAL HOOK: Recover spilled operands safely without clobbering each other
		if (rr >= 0 && rr < 3 && spill_offsets[rr] > 0) {
			int saved_rl_state = (rl >= 0 && rl < 3) ? reg_map[rl] : 0;
			if (rl >= 0 && rl < 3) reg_map[rl] = 1;
			rr = ensure_register_loaded(rr);
			if (rl >= 0 && rl < 3) reg_map[rl] = saved_rl_state;
		}
		if (rl >= 0 && rl < 3 && spill_offsets[rl] > 0) {
			int saved_rr_state = (rr >= 0 && rr < 3) ? reg_map[rr] : 0;
			if (rr >= 0 && rr < 3) reg_map[rr] = 1;
			rl = ensure_register_loaded(rl);
			if (rr >= 0 && rr < 3) reg_map[rr] = saved_rr_state;
		}

		if (!is_float) {
			// --- TYPE-AWARE BINARY OPERATION STRIDE ENGINE ---
			int left_is_ptr = (node->left->type.kind == TYPE_POINTER || node->left->type.base_type != NULL || node->left->type.is_array);
			int right_is_ptr = (node->right->type.kind == TYPE_POINTER || node->right->type.base_type != NULL || node->right->type.is_array);
			int works_as_pointer = (left_is_ptr || right_is_ptr ||
				node->type.kind == TYPE_POINTER || node->type.base_type != NULL);

			if (node->op == TOKEN_PLUS) {
				if (left_is_ptr && !right_is_ptr) {
					int stride = get_pointer_stride(node->left->type);
					if (stride == 4) printf("    shl %s, 2\n", reg_names[rr]);
					else if (stride > 1) printf("    imul %s, %d\n", reg_names[rr], stride);
					printf("    add %s, %s\n", reg_names[rl], reg_names[rr]);
				}
				else if (!left_is_ptr && right_is_ptr) {
					int stride = get_pointer_stride(node->right->type);
					if (stride == 4) printf("    shl %s, 2\n", reg_names[rl]);
					else if (stride > 1) printf("    imul %s, %d\n", reg_names[rl], stride);
					printf("    add %s, %s\n", reg_names[rl], reg_names[rr]);
				}
				else if (node->type.kind == TYPE_CHAR && !works_as_pointer) {
					printf("    add %s, %s\n", get_safe_reg_name(rl, node->left->type), get_safe_reg_name(rr, node->right->type));
				}
				else {
					printf("    add %s, %s\n", reg_names[rl], reg_names[rr]);
				}
			}
			else if (node->op == TOKEN_MINUS) {
				if (left_is_ptr && !right_is_ptr) {
					int stride = get_pointer_stride(node->left->type);
					if (stride == 4) printf("    shl %s, 2\n", reg_names[rr]);
					else if (stride > 1) printf("    imul %s, %d\n", reg_names[rr], stride);
					printf("    sub %s, %s\n", reg_names[rl], reg_names[rr]);
				}
				else if (left_is_ptr && right_is_ptr) {
					// Pointer difference: (ptr1 - ptr2) / sizeof(*ptr1)
					printf("    sub %s, %s\n", reg_names[rl], reg_names[rr]);
					int stride = get_pointer_stride(node->left->type);
					if (stride == 4) {
						printf("    sar %s, 2\n", reg_names[rl]);
					}
					else if (stride > 1) {
						int ext_eax = (reg_map[0] && rl != 0 && rr != 0);
						int ext_edx = (reg_map[2] && rl != 2 && rr != 2);
						if (ext_eax) printf("    push eax\n");
						if (ext_edx) printf("    push edx\n");
						if (rl != 0) printf("    mov eax, %s\n", reg_names[rl]);
						printf("    cdq\n");
						printf("    mov ecx, %d\n", stride);
						printf("    idiv ecx\n");
						if (rl != 0) printf("    mov %s, eax\n", reg_names[rl]);
						if (ext_edx) printf("    pop edx\n");
						if (ext_eax) printf("    pop eax\n");
					}
				}
				else if (node->type.kind == TYPE_CHAR && !works_as_pointer) {
					printf("    sub %s, %s\n", get_safe_reg_name(rl, node->left->type), get_safe_reg_name(rr, node->right->type));
				}
				else {
					printf("    sub %s, %s\n", reg_names[rl], reg_names[rr]);
				}
			}
			else if (node->op == TOKEN_STAR) {
				if (node->type.kind == TYPE_CHAR && !works_as_pointer) {
					printf("    imul %s, %s\n", get_safe_reg_name(rl, node->left->type), get_safe_reg_name(rr, node->right->type));
				}
				else {
					printf("    imul %s, %s\n", reg_names[rl], reg_names[rr]);
				}
			}
			else if (node->op == TOKEN_AMP) {
				printf("    and %s, %s\n", reg_names[rl], reg_names[rr]);
			}
			else if (node->op == TOKEN_BITOR) {
				printf("    or %s, %s\n", reg_names[rl], reg_names[rr]);
			}
			else if (node->op == TOKEN_BITXOR) {
				printf("    xor %s, %s\n", reg_names[rl], reg_names[rr]);
			}
			else if (node->op == TOKEN_LSHIFT || node->op == TOKEN_RSHIFT) {
				const char* shift_op = (node->op == TOKEN_LSHIFT) ? "shl" : "sar";
				int save_ecx = (reg_map[1] && rl != 1 && rr != 1);

				if (save_ecx) printf("    push ecx\n");

				if (rl == 1) {
					// Left operand is already in ecx: move it out before loading shift count into cl
					int tmp_reg = (rr == 0) ? 2 : 0; // Use eax or edx temporarily
					printf("    push %s\n", reg_names[tmp_reg]);
					printf("    mov %s, ecx\n", reg_names[tmp_reg]);
					printf("    mov ecx, %s\n", reg_names[rr]);
					printf("    %s %s, cl\n", shift_op, reg_names[tmp_reg]);
					printf("    mov ecx, %s\n", reg_names[tmp_reg]);
					printf("    pop %s\n", reg_names[tmp_reg]);
				}
				else {
					if (rr != 1) printf("    mov ecx, %s\n", reg_names[rr]);
					printf("    %s %s, cl\n", shift_op, reg_names[rl]);
				}

				if (save_ecx) printf("    pop ecx\n");
			}

			// --- INTEGER DIVISION & MODULO PIPELINE ---
			else if (node->op == TOKEN_SLASH || node->op == TOKEN_PERCENT) {
				int ext_eax_active = (reg_map[0] && rl != 0 && rr != 0);
				int ext_edx_active = (reg_map[2] && rl != 2 && rr != 2);

				if (ext_eax_active) printf("    push eax\n");
				if (ext_edx_active) printf("    push edx\n");

				if (rl != 0) printf("    mov eax, %s\n", reg_names[rl]);
				printf("    cdq\n");

				if (rr == 0)      printf("    idiv dword ptr [esp + %d]\n", (ext_edx_active ? 4 : 0));
				else if (rr == 2) printf("    idiv dword ptr [esp]\n");
				else              printf("    idiv %s\n", reg_names[rr]);

				if (node->op == TOKEN_SLASH) {
					if (rl != 0) printf("    mov %s, eax\n", reg_names[rl]);
				}
				else {
					if (rl != 2) printf("    mov %s, edx\n", reg_names[rl]);
				}

				if (ext_edx_active) printf("    pop edx\n");
				if (ext_eax_active) printf("    pop eax\n");

				free_register(rr);
				node->int_val = rl;
			}
			else if (node->op == TOKEN_LESS || node->op == TOKEN_GREATER ||
				node->op == TOKEN_LESS_EQUAL || node->op == TOKEN_GREATER_EQUAL ||
				node->op == TOKEN_EQUAL || node->op == TOKEN_NOT_EQUAL) {
				printf("    cmp %s, %s\n", reg_names[rl], reg_names[rr]);
				const char* bytes[] = { "al", "cl", "dl" };
				if (node->op == TOKEN_EQUAL)              printf("    sete %s\n", bytes[rl]);
				else if (node->op == TOKEN_NOT_EQUAL)     printf("    setne %s\n", bytes[rl]);
				else if (node->op == TOKEN_LESS)          printf("    setl %s\n", bytes[rl]);
				else if (node->op == TOKEN_LESS_EQUAL)    printf("    setle %s\n", bytes[rl]);
				else if (node->op == TOKEN_GREATER)       printf("    setg %s\n", bytes[rl]);
				else if (node->op == TOKEN_GREATER_EQUAL) printf("    setge %s\n", bytes[rl]);
				printf("    movzx %s, %s\n", reg_names[rl], bytes[rl]);
			}

			if (node->op != TOKEN_SLASH && node->op != TOKEN_PERCENT) {
				free_register(rr);
				node->int_val = rl;
			}
		}
		else {
			// SSE2 FLOATING-POINT PIPELINE
			if (node->left->type.kind == TYPE_INT) {
				printf("    cvtsi2ss xmm0, %s\n", reg_names[rl]);
			}
			else {
				printf("    movd xmm0, %s\n", reg_names[rl]);
			}

			if (node->right->type.kind == TYPE_INT) {
				printf("    cvtsi2ss xmm1, %s\n", reg_names[rr]);
			}
			else {
				printf("    movd xmm1, %s\n", reg_names[rr]);
			}
			free_register(rr);

			if (node->op == TOKEN_LESS || node->op == TOKEN_GREATER ||
				node->op == TOKEN_LESS_EQUAL || node->op == TOKEN_GREATER_EQUAL ||
				node->op == TOKEN_EQUAL || node->op == TOKEN_NOT_EQUAL) {
				printf("    comiss xmm0, xmm1\n");
				const char* bytes[] = { "al", "cl", "dl" };
				if (node->op == TOKEN_EQUAL)              printf("    sete %s\n", bytes[rl]);
				else if (node->op == TOKEN_NOT_EQUAL)     printf("    setne %s\n", bytes[rl]);
				else if (node->op == TOKEN_LESS)          printf("    setb %s\n", bytes[rl]);
				else if (node->op == TOKEN_LESS_EQUAL)    printf("    setbe %s\n", bytes[rl]);
				else if (node->op == TOKEN_GREATER)       printf("    seta %s\n", bytes[rl]);
				else if (node->op == TOKEN_GREATER_EQUAL) printf("    setae %s\n", bytes[rl]);
				printf("    movzx %s, %s\n", reg_names[rl], bytes[rl]);
				node->int_val = rl;
				node->type.kind = TYPE_INT;
			}
			else {
				if (node->op == TOKEN_PLUS)       printf("    addss xmm0, xmm1\n");
				else if (node->op == TOKEN_MINUS) printf("    subss xmm0, xmm1\n");
				else if (node->op == TOKEN_STAR)  printf("    mulss xmm0, xmm1\n");
				else if (node->op == TOKEN_SLASH) printf("    divss xmm0, xmm1\n");

				printf("    movd %s, xmm0\n", reg_names[rl]);
				node->int_val = rl;
				node->type.kind = TYPE_FLOAT;
			}
		}
		break;
	}

	case AST_ARRAY_ACCESS: {
		ASTNode* base = node;
		while (base->left && base->left->kind == AST_ARRAY_ACCESS) {
			base = base->left;
		}

		Symbol* sym = lookup_symbol(base->left->name);
		if (!sym) {
			printf("\n; Backend Error: Array base identifier '%s' not found!\n", base->left->name);
			exit(1);
		}

		int dim = 0, r_off = -1;
		compile_array_offset(node, &dim, sym, &r_off);
		r_off = ensure_register_loaded(r_off);

		int elem_size = get_array_element_size(sym->type);
		if (elem_size == 4) {
			printf("    shl %s, 2\n", reg_names[r_off]);
		}
		else if (elem_size != 1) {
			printf("    imul %s, %d\n", reg_names[r_off], elem_size);
		}

		int r_ad = allocate_register();
		if (sym->is_global_var) {
			printf("    lea %s, dword ptr [%s]\n", reg_names[r_ad], sym->asm_label);
			printf("    add %s, %s\n", reg_names[r_ad], reg_names[r_off]);
		}
		else if (sym->offset < 0) {
			printf("    lea %s, dword ptr [ebp - %d]\n", reg_names[r_ad], -sym->offset);
			printf("    add %s, %s\n", reg_names[r_ad], reg_names[r_off]);
		}
		else {
			printf("    lea %s, dword ptr [ebp + %d]\n", reg_names[r_ad], sym->offset);
			printf("    add %s, %s\n", reg_names[r_ad], reg_names[r_off]);
		}

		free_register(r_off);

		if (elem_size == 1) {
			printf("    movzx %s, byte ptr [%s]\n", reg_names[r_ad], reg_names[r_ad]);
			node->type.kind = TYPE_CHAR;
		}
		else {
			printf("    mov %s, dword ptr [%s]\n", reg_names[r_ad], reg_names[r_ad]);
			node->type = sym->type;
			node->type.is_array = 0;
		}

		node->int_val = r_ad;
		break;
	}

	case AST_ASSIGN: {
		node->int_val = -1; // Force uninitialized state to block downstream bugs

		// Special Case: Address-of Reference Mapping (& optimization)
		if (node->left->kind == AST_IDENT && node->right->kind == AST_UNOP && node->right->op == TOKEN_AMP &&
			node->right->left->kind == AST_IDENT) {
			Symbol* right_sym = lookup_symbol(node->right->left->name);
			Symbol* left_sym = lookup_symbol(node->left->name);

			if (!right_sym || !left_sym) {
				printf("Backend Error: Symbol resolution failure during reference mapping.\n");
				exit(1);
			}

			if (right_sym->is_register) {
				fprintf(stderr, "Semantic Error: Address of register variable '%s' requested.\n", right_sym->name);
				exit(1);
			}

			int r_addr = allocate_register();
			if (right_sym->is_global_var) {
				printf("    lea %s, dword ptr [%s]\n", reg_names[r_addr], right_sym->asm_label);
			}
			else if (right_sym->offset < 0) {
				printf("    lea %s, dword ptr [ebp - %d]\n", reg_names[r_addr], -right_sym->offset);
			}
			else {
				printf("    lea %s, dword ptr [ebp + %d]\n", reg_names[r_addr], right_sym->offset);
			}

			if (left_sym->is_global_var) {
				printf("    mov dword ptr [%s], %s\n", left_sym->asm_label, reg_names[r_addr]);
			}
			else if (left_sym->offset < 0) {
				printf("    mov dword ptr [ebp - %d], %s\n", -left_sym->offset, reg_names[r_addr]);
			}
			else {
				printf("    mov dword ptr [ebp + %d], %s\n", left_sym->offset, reg_names[r_addr]);
			}

			free_register(r_addr);
			return;
		}

		// 1. Evaluate the Right-Hand Value Expression
		emit_code(node->right);
		int rv = node->right->int_val;
		rv = ensure_register_loaded(rv);

		// 2. Safely Route Value to Target L-Value Left-Hand Variant
		if (node->left->kind == AST_IDENT) {
			Symbol* sym = lookup_symbol(node->left->name);
			if (!sym) {
				printf("\n; Backend Error: Assignment target variable '%s' not found!\n", node->left->name);
				exit(1);
			}

			// Check if the symbol is structurally a pointer context (including va_list string types)
			int target_is_pointer = (sym->type.kind == TYPE_POINTER || sym->type.base_type != NULL ||
				strcmp(sym->name, "ap") == 0 || sym->is_typedef);

			if (sym->is_global_var) {
				if (target_is_pointer) {
					printf("    mov dword ptr [%s], %s\n", sym->asm_label, reg_names[rv]);
				}
				else if (sym->type.kind == TYPE_CHAR) {
					printf("    mov byte ptr [%s], %s\n", sym->asm_label, get_safe_reg_name(rv, sym->type));
				}
				else {
					printf("    mov dword ptr [%s], %s\n", sym->asm_label, reg_names[rv]);
				}
			}
			else {
				if (target_is_pointer) {
					if (sym->offset < 0) printf("    mov dword ptr [ebp - %d], %s\n", -sym->offset, reg_names[rv]);
					else printf("    mov dword ptr [ebp + %d], %s\n", sym->offset, reg_names[rv]);
				}
				else if (sym->type.kind == TYPE_CHAR) {
					if (sym->offset < 0) printf("    mov byte ptr [ebp - %d], %s\n", -sym->offset, get_safe_reg_name(rv, sym->type));
					else printf("    mov byte ptr [ebp + %d], %s\n", sym->offset, get_safe_reg_name(rv, sym->type));
				}
				else {
					if (sym->offset < 0) printf("    mov dword ptr [ebp - %d], %s\n", -sym->offset, reg_names[rv]);
					else printf("    mov dword ptr [ebp + %d], %s\n", sym->offset, reg_names[rv]);
				}
			}
		}
		else if (node->left->kind == AST_UNOP && node->left->op == TOKEN_STAR) {
			ASTNode* addr_node = node->left->left;

			// Strip cast wrapper during indirect assignment
			if (addr_node && addr_node->kind == AST_UNOP && addr_node->op == TOKEN_TYPEDEF) {
				addr_node = addr_node->left;
			}

			emit_code(addr_node);
			int r_addr = addr_node->int_val;
			r_addr = ensure_register_loaded(r_addr);

			int is_char_deref = (node->left->type.kind == TYPE_CHAR &&
				node->left->type.base_type == NULL && !node->left->type.is_array);
			if (is_char_deref) {
				printf("    mov byte ptr [%s], %s\n", reg_names[r_addr], reg_names_8[rv]);
			}
			else {
				printf("    mov dword ptr [%s], %s\n", reg_names[r_addr], reg_names[rv]);
			}
			free_register(r_addr);
		}
		else if (node->left->kind == AST_MEMBER_ACCESS) {
			DataType target_type;
			int calculated_offset = 0;
			int is_char_member = 0;

			if (node->left->op == TOKEN_ARROW) {
				emit_code(node->left->left);
				int r_ptr = node->left->left->int_val;
				r_ptr = ensure_register_loaded(r_ptr);
				resolve_member_offset(node->left, &calculated_offset, &target_type);
				is_char_member = (target_type.kind == TYPE_CHAR && target_type.base_type == NULL);
				if (is_char_member) {
					printf("    mov byte ptr [%s + %d], %s\n", reg_names[r_ptr], calculated_offset, reg_names_8[rv]);
				}
				else {
					printf("    mov dword ptr [%s + %d], %s\n", reg_names[r_ptr], calculated_offset, reg_names[rv]);
				}
				free_register(r_ptr);
			}
			else {
				int r_arr_base = emit_dot_base_address(node->left, &calculated_offset, &target_type);
				if (r_arr_base != -1) {
					is_char_member = (target_type.kind == TYPE_CHAR && target_type.base_type == NULL);
					if (is_char_member)
						printf("    mov byte ptr [%s + %d], %s\n", reg_names[r_arr_base], calculated_offset, reg_names_8[rv]);
					else
						printf("    mov dword ptr [%s + %d], %s\n", reg_names[r_arr_base], calculated_offset, reg_names[rv]);
					free_register(r_arr_base);
				}
				else {
					ASTNode* root_id = node->left;
					while (root_id && root_id->kind == AST_MEMBER_ACCESS) {
						root_id = root_id->left;
					}
					Symbol* struct_sym = (root_id && root_id->kind == AST_IDENT) ? lookup_symbol(root_id->name) : NULL;

					resolve_member_offset(node->left, &calculated_offset, &target_type);
					is_char_member = (target_type.kind == TYPE_CHAR && target_type.base_type == NULL);

					if (struct_sym && struct_sym->is_global_var) {
						int member_disp = calculated_offset - struct_sym->offset;
						if (is_char_member) {
							printf("    mov byte ptr [%s + %d], %s\n", struct_sym->asm_label, member_disp, reg_names_8[rv]);
						}
						else {
							printf("    mov dword ptr [%s + %d], %s\n", struct_sym->asm_label, member_disp, reg_names[rv]);
						}
					}
					else if (calculated_offset < 0) {
						if (is_char_member) {
							printf("    mov byte ptr [ebp - %d], %s\n", -calculated_offset, reg_names_8[rv]);
						}
						else {
							printf("    mov dword ptr [ebp - %d], %s\n", -calculated_offset, reg_names[rv]);
						}
					}
					else {
						if (is_char_member) {
							printf("    mov byte ptr [ebp + %d], %s\n", calculated_offset, reg_names_8[rv]);
						}
						else {
							printf("    mov dword ptr [ebp + %d], %s\n", calculated_offset, reg_names[rv]);
						}
					}
				}
			}
		}
		else if (node->left->kind == AST_ARRAY_ACCESS) {
			ASTNode* base = node->left;
			while (base->left && base->left->kind == AST_ARRAY_ACCESS) base = base->left;

			Symbol* sym = lookup_symbol(base->left->name);
			if (!sym) { printf("\n; Backend Error: Array not found!\n"); exit(1); }

			int dim = 0, r_off = -1;
			compile_array_offset(node->left, &dim, sym, &r_off);
			r_off = ensure_register_loaded(r_off);

			int elem_size = get_array_element_size(sym->type);
			if (elem_size == 4) {
				printf("    shl %s, 2\n", reg_names[r_off]);
			}
			else if (elem_size != 1) {
				printf("    imul %s, %d\n", reg_names[r_off], elem_size);
			}

			int r_ad = allocate_register();
			if (sym->is_global_var) {
				printf("    lea %s, dword ptr [%s]\n", reg_names[r_ad], sym->asm_label);
				printf("    add %s, %s\n", reg_names[r_ad], reg_names[r_off]);
			}
			else if (sym->offset < 0) {
				printf("    lea %s, dword ptr [ebp - %d]\n", reg_names[r_ad], -sym->offset);
				printf("    add %s, %s\n", reg_names[r_ad], reg_names[r_off]);
			}
			else {
				printf("    lea %s, dword ptr [ebp + %d]\n", reg_names[r_ad], sym->offset);
				printf("    add %s, %s\n", reg_names[r_ad], reg_names[r_off]);
			}

			if (elem_size == 1) {
				printf("    mov byte ptr [%s], %s\n", reg_names[r_ad], reg_names_8[rv]);
			}
			else {
				printf("    mov dword ptr [%s], %s\n", reg_names[r_ad], reg_names[rv]);
			}
			free_register(r_ad);
			free_register(r_off);
		}

		// Pass the assigned register value up to the parent node context
		node->int_val = rv;
		break;
	}

	case AST_UNOP: {
		if (node->op == TOKEN_INC || node->op == TOKEN_DEC) {
			emit_inc_dec(node, node->op == TOKEN_INC, 0);
			break;
		}

		// Implicit floating-point promotion conversion node
		if (node->op == TOKEN_FLOAT_LIT) {
			emit_code(node->left);
			int rl = node->left->int_val;
			int r_dest = allocate_register();

			printf("    cvtsi2ss xmm0, %s\n", reg_names[rl]);
			printf("    movd %s, xmm0\n", reg_names[r_dest]);

			free_register(rl);
			node->int_val = r_dest;
			break;
		}

		// --- ADDRESS-OF (&) ENGINE ---
		// Handled BEFORE evaluating node->left so we don't waste a register load
		// or trigger a register spill when taking the address of a variable.
		if (node->op == TOKEN_AMP) {
			if (node->left->kind == AST_IDENT) {
				Symbol* sym = lookup_symbol(node->left->name);
				if (!sym) { fprintf(stderr, "Semantic Error: %s undefined.\n", node->left->name); exit(1); }

				if (sym->is_register) {
					fprintf(stderr, "Semantic Error: Address of register variable '%s' requested.\n", sym->name);
					exit(1);
				}

				int r = allocate_register();
				if (sym->is_global_var) {
					printf("    lea %s, dword ptr [%s]\n", reg_names[r], sym->asm_label);
				}
				else if (sym->offset < 0) {
					printf("    lea %s, dword ptr [ebp - %d]\n", reg_names[r], -sym->offset);
				}
				else {
					printf("    lea %s, dword ptr [ebp + %d]\n", reg_names[r], sym->offset);
				}
				node->int_val = r;
			}
			else if (node->left->kind == AST_ARRAY_ACCESS) {
				ASTNode* base = node->left;
				while (base->left && base->left->kind == AST_ARRAY_ACCESS) base = base->left;

				Symbol* sym = lookup_symbol(base->left->name);
				if (!sym) { fprintf(stderr, "Backend Error: Array '%s' not found.\n", base->left->name); exit(1); }

				int dim = 0, r_off = -1;
				compile_array_offset(node->left, &dim, sym, &r_off);
				r_off = ensure_register_loaded(r_off);

				int elem_size = get_array_element_size(sym->type);
				if (elem_size == 4) printf("    shl %s, 2\n", reg_names[r_off]);
				else if (elem_size != 1) printf("    imul %s, %d\n", reg_names[r_off], elem_size);

				int r_ad = allocate_register();
				if (sym->is_global_var) printf("    lea %s, dword ptr [%s]\n", reg_names[r_ad], sym->asm_label);
				else if (sym->offset < 0) printf("    lea %s, dword ptr [ebp - %d]\n", reg_names[r_ad], -sym->offset);
				else printf("    lea %s, dword ptr [ebp + %d]\n", reg_names[r_ad], sym->offset);

				printf("    add %s, %s\n", reg_names[r_ad], reg_names[r_off]);
				free_register(r_off);
				node->int_val = r_ad;
			}
			else if (node->left->kind == AST_MEMBER_ACCESS) {
				DataType target_type;
				int calc_off = 0;
				resolve_member_offset(node->left, &calc_off, &target_type);

				int r = allocate_register();
				if (node->left->op == TOKEN_ARROW) {
					emit_code(node->left->left);
					int r_ptr = ensure_register_loaded(node->left->left->int_val);
					printf("    lea %s, dword ptr [%s + %d]\n", reg_names[r], reg_names[r_ptr], calc_off);
					free_register(r_ptr);
				}
				else {
					ASTNode* root_id = node->left;
					while (root_id && root_id->kind == AST_MEMBER_ACCESS) root_id = root_id->left;
					Symbol* s_sym = (root_id && root_id->kind == AST_IDENT) ? lookup_symbol(root_id->name) : NULL;

					if (s_sym && s_sym->is_global_var)
						printf("    lea %s, dword ptr [%s + %d]\n", reg_names[r], s_sym->asm_label, calc_off - s_sym->offset);
					else if (calc_off < 0)
						printf("    lea %s, dword ptr [ebp - %d]\n", reg_names[r], -calc_off);
					else
						printf("    lea %s, dword ptr [ebp + %d]\n", reg_names[r], calc_off);
				}
				node->int_val = r;
			}
			else {
				fprintf(stderr, "Backend Error: Lvalue required for '&'.\n");
				exit(1);
			}
			break;
		}

		// 1. Single evaluation pass: Fully compute the child node's branch value first
		emit_code(node->left);
		int rl = node->left->int_val;

		// --- EXPLICIT TYPE CAST GENERATION ENGINE ---
		if (node->op == TOKEN_TYPEDEF) {
			int r_dest = allocate_register();
			int rl_loaded = ensure_register_loaded(rl);

			DataTypeKind from_type = node->left->type.kind;
			DataTypeKind to_type = node->type.kind;

			if (node->left->type.base_type != NULL || from_type == TYPE_POINTER) from_type = TYPE_INT;

			int target_is_pointer_cast = (node->type.base_type != NULL || to_type == TYPE_POINTER || node->type.is_array);
			if (target_is_pointer_cast) to_type = TYPE_INT;

			if (from_type == TYPE_INT && to_type == TYPE_FLOAT) {
				printf("    ; --- EXPLICIT CAST: INT -> FLOAT --- \n");
				printf("    cvtsi2ss xmm0, %s\n", reg_names[rl_loaded]);
				printf("    movd %s, xmm0\n", reg_names[r_dest]);
			}
			else if (from_type == TYPE_FLOAT && to_type == TYPE_INT) {
				printf("    ; --- EXPLICIT CAST: FLOAT -> INT --- \n");
				printf("    movd xmm0, %s\n", reg_names[rl_loaded]);
				printf("    cvttss2si %s, xmm0\n", reg_names[r_dest]);
			}
			else if (to_type == TYPE_CHAR && !target_is_pointer_cast) {
				printf("    ; --- EXPLICIT CAST: -> CHAR --- \n");
				printf("    mov %s, %s\n", reg_names[r_dest], reg_names[rl_loaded]);
				printf("    and %s, 0FFh\n", reg_names[r_dest]);
			}
			else {
				printf("    mov %s, %s\n", reg_names[r_dest], reg_names[rl_loaded]);
			}

			free_register(rl_loaded);
			node->int_val = r_dest;
			break;
		}

		// --- POINTER DEREFERENCE (*) ENGINE ---
		if (node->op == TOKEN_STAR) {
			int r = allocate_register();
			int r_addr = ensure_register_loaded(rl);

			if (node->type.kind == TYPE_CHAR && node->type.base_type == NULL && !node->type.is_array) {
				printf("    movzx %s, byte ptr [%s]\n", reg_names[r], reg_names[r_addr]);
			}
			else {
				printf("    mov %s, dword ptr [%s]\n", reg_names[r], reg_names[r_addr]);
			}

			free_register(r_addr);
			node->int_val = r;
		}
		// --- UNARY MINUS (-) ENGINE ---
		else if (node->op == TOKEN_MINUS) {
			int rl_loaded = ensure_register_loaded(rl);
			if (node->type.kind == TYPE_FLOAT) {
				int lbl = gen_label();
				printf("    .data\nneg_mask_%d REAL4 -1.0\n    .code\n", lbl);
				printf("    movss xmm1, dword ptr [neg_mask_%d]\n", lbl);
				printf("    movd xmm0, %s\n", reg_names[rl_loaded]);
				printf("    mulss xmm0, xmm1\n");
				printf("    movd %s, xmm0\n", reg_names[rl_loaded]);
			}
			else if (node->type.kind == TYPE_CHAR && node->type.base_type == NULL) {
				printf("    neg %s\n", reg_names_8[rl_loaded]);
				printf("    movzx %s, %s\n", reg_names[rl_loaded], reg_names_8[rl_loaded]);
			}
			else {
				printf("    neg %s\n", reg_names[rl_loaded]);
			}
			node->int_val = rl_loaded;
		}
		// --- LOGICAL NOT (!) ENGINE ---
		else if (node->op == TOKEN_NOT) {
			int rl_loaded = ensure_register_loaded(rl);
			printf("    cmp %s, 0\n", reg_names[rl_loaded]);

			int expression_is_pointer = (node->left->type.kind == TYPE_POINTER ||
				node->left->type.base_type != NULL ||
				node->type.kind == TYPE_POINTER ||
				node->type.base_type != NULL);

			if (expression_is_pointer) {
				printf("    sete %s\n", (rl_loaded == 0) ? "al" : (rl_loaded == 1) ? "cl" : "dl");
				printf("    movzx %s, %s\n", reg_names[rl_loaded], (rl_loaded == 0) ? "al" : (rl_loaded == 1) ? "cl" : "dl");
			}
			else {
				const char* target_byte = (rl_loaded == 0) ? "al" : (rl_loaded == 1) ? "cl" : "dl";
				printf("    sete %s\n", target_byte);
				printf("    movzx %s, %s\n", reg_names[rl_loaded], target_byte);
			}
			node->int_val = rl_loaded;
		}
		// --- BITWISE NOT (~) ENGINE ---
		else if (node->op == TOKEN_BITNOT) {
			int rl_loaded = ensure_register_loaded(rl);
			printf("    not %s\n", reg_names[rl_loaded]);
			node->int_val = rl_loaded;
		}
		break;
	}

	case AST_IF: {
		int l_else = gen_label(), l_end = gen_label();
		emit_code(node->cond);

		int r_cond = node->cond->int_val;
		printf("    cmp %s, 0\n    je $$L%d\n", reg_names[r_cond], l_else);
		free_register(r_cond); // Cleanly free the condition register right away!

		emit_code(node->body);
		// CRITICAL RECOVERY STEP: If the body block left behind a stray register, clean it up!
		if (node->body && node->body->int_val >= 0 && node->body->int_val < 3) {
			free_register(node->body->int_val);
			node->body->int_val = -1;
		}

		printf("    jmp $$L%d\n$$L%d:\n", l_end, l_else);
		if (node->else_body) {
			emit_code(node->else_body);
			// Clear out any leaked registers remaining inside the else statement body block
			if (node->else_body && node->else_body->int_val >= 0 && node->else_body->int_val < 3) {
				free_register(node->else_body->int_val);
				node->else_body->int_val = -1;
			}
		}
		printf("$$L%d:\n", l_end);
		break;
	}

	case AST_BLOCK: {
		ASTNode* s = node->body;
		while (s) {
			emit_code(s);

			// Clean up and free tracking registers for standalone statement expressions
			// to guarantee full register availability for subsequent code blocks.
			if (s->kind == AST_CALL || s->kind == AST_BINOP || s->kind == AST_LITERAL ||
				s->kind == AST_IDENT || s->kind == AST_ASSIGN || s->kind == AST_UNOP ||
				s->kind == AST_POST_INC || s->kind == AST_POST_DEC || s->kind == AST_TERNARY) {
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
		printf("$$L%d:\n", l_co);

		// --- FIXED: Reset the scratch registers pool before evaluating loop iterations ---
		for (int i = 0; i < 3; i++) { reg_map[i] = 0; spill_offsets[i] = 0; }

		emit_code(node->cond);
		int r_cond = node->cond->int_val;
		printf("    cmp %s, 0\n    je $$L%d\n", reg_names[r_cond], l_en);
		free_register(r_cond);

		emit_code(node->body);
		printf("    jmp $$L%d\n$$L%d:\n", l_co, l_en);
		loop_stack_top--;
		break;
	}

	case AST_FOR: {
		int l_co = gen_label(), l_st = gen_label(), l_en = gen_label();
		if (node->left) {
			emit_code(node->left);
			if (node->left->int_val >= 0 && node->left->int_val < 3) {
				free_register(node->left->int_val);
			}
		}

		loop_stack[loop_stack_top++] = (LoopFrame){ l_st, l_en };
		printf("$$L%d:\n", l_co);

		// Reset the scratch registers pool before evaluating loop iterations
		for (int i = 0; i < 3; i++) { reg_map[i] = 0; spill_offsets[i] = 0; }

		if (node->cond) {
			emit_code(node->cond);
			int r_cond = ensure_register_loaded(node->cond->int_val);
			printf("    cmp %s, 0\n    je $$L%d\n", reg_names[r_cond], l_en);
			free_register(r_cond);
		}

		emit_code(node->body);
		printf("$$L%d:\n", l_st);

		if (node->right) {
			emit_code(node->right);
			if (node->right->int_val >= 0 && node->right->int_val < 3) {
				free_register(node->right->int_val);
			}
		}

		printf("    jmp $$L%d\n$$L%d:\n", l_co, l_en);
		loop_stack_top--;
		break;
	}

	case AST_BREAK: printf("    jmp $$L%d\n", loop_stack[loop_stack_top - 1].end_lbl); break;
	case AST_CONTINUE: printf("    jmp $$L%d\n", loop_stack[loop_stack_top - 1].cond_lbl); break;

	case AST_RETURN: {
		if (node->left) {
			emit_code(node->left);
			int r_val = ensure_register_loaded(node->left->int_val);

			if (node->left->type.kind == TYPE_FLOAT) {
				printf("    sub esp, 4\n");
				printf("    mov dword ptr [esp], %s\n", reg_names[r_val]);
				printf("    fld dword ptr [esp]\n");
				printf("    add esp, 4\n");
			}
			else {
				printf("    mov eax, %s\n", reg_names[r_val]);
			}
			free_register(r_val);
		}

		printf("    mov esp, ebp\n    pop ebp\n    ret\n");
		break;
	}

	case AST_CALL: {
		ASTNode* args_array[32] = { 0 };
		int count = 0;
		ASTNode* arg = node->left;

		// 1. Gather our linked list arguments into the tracking array layout safely
		while (arg && count < 32) {
			args_array[count++] = arg;
			arg = arg->next;
		}

		// --- CALL-WIDE HIGHER SCOPE SAVE ---
		// Backup the true global register context before evaluating any arguments.
		int call_base_reg_map[3];
		int call_base_spill_offsets[3];
		int call_base_was_spilled[3];
		for (int i = 0; i < 3; i++) {
			call_base_reg_map[i] = reg_map[i];
			call_base_spill_offsets[i] = spill_offsets[i];
			call_base_was_spilled[i] = was_spilled[i];
		}

		// Save any live caller-saved registers (eax, ecx, edx) BEFORE pushing call arguments
		// so the callee arguments remain right at [esp + 0] when 'call' executes.
		int saved_caller_regs[3] = { 0 };
		for (int i = 0; i < 3; i++) {
			if (call_base_reg_map[i]) {
				printf("    push %s\n", reg_names[i]);
				saved_caller_regs[i] = 1;
			}
		}

		int total_stack_bytes = 0;

		// 2. Evaluate and IMMEDIATELY push arguments from Right to Left (Reverse order)
		// Each argument tree is fully insulated from its sibling trees.
		for (int i = count - 1; i >= 0; i--) {
			ASTNode* curr_arg = args_array[i];

			// Restore the compiler's base calling state *before* evaluating this argument.
			for (int j = 0; j < 3; j++) {
				reg_map[j] = call_base_reg_map[j];
				spill_offsets[j] = call_base_spill_offsets[j];
				was_spilled[j] = call_base_was_spilled[j];
			}

			// Generate assembly for the current argument expression line safely
			emit_code(curr_arg);
			int r_val = curr_arg->int_val;
			r_val = ensure_register_loaded(r_val);

			// Extract type details cleanly
			DataTypeKind arg_kind = curr_arg->type.kind;
			if (curr_arg->kind == AST_IDENT) {
				Symbol* sym = lookup_symbol(curr_arg->name);
				if (sym) arg_kind = sym->type.kind;
			}

			// Generate the physical push layouts immediately to protect the value on the stack frame
			if (arg_kind == TYPE_FLOAT) {
				printf("    movd xmm0, %s\n", reg_names[r_val]);
				printf("    cvtss2sd xmm0, xmm0\n");
				printf("    sub esp, 8\n");
				printf("    movsd qword ptr [esp], xmm0\n");
				total_stack_bytes += 8;
			}
			else {
				printf("    push %s\n", reg_names[r_val]);
				total_stack_bytes += 4;
			}

			// Free the scratch register now that its payload is insulated on the stack frame
			free_register(r_val);
		}

		// 3. Complete restoration to baseline right before the call instruction step
		for (int i = 0; i < 3; i++) {
			reg_map[i] = call_base_reg_map[i];
			spill_offsets[i] = call_base_spill_offsets[i];
			was_spilled[i] = call_base_was_spilled[i];
		}

		// Execute the procedural jump statement
		printf("    call %s\n", node->name);

		// Balance caller stack parameters space allocations immediately upon exit
		if (total_stack_bytes > 0) {
			printf("    add esp, %d\n", total_stack_bytes);
		}

		// Allocate a fresh register to capture the returned data
		int r = allocate_register();
		int r_was_spilled = was_spilled[r];

		// Determine if the called function returns a float
		Symbol* func_sym = lookup_symbol(node->name);
		if (func_sym && func_sym->type.kind == TYPE_FLOAT) {
			printf("    sub esp, 4\n");
			printf("    fstp dword ptr [esp]\n");
			printf("    mov %s, dword ptr [esp]\n", reg_names[r]);
			printf("    add esp, 4\n");
			node->type.kind = TYPE_FLOAT;
		}
		else {
			printf("    mov %s, eax\n", reg_names[r]);
		}

		// 4. Restore saved caller-saved registers in reverse push order
		if (r_was_spilled) {
			// 'r' is edx (index 2), whose previous value was spilled to [ebp - spill_offsets[2]]
			// and also pushed in saved_caller_regs[2]; discard the duplicate stack push for edx.
			for (int i = 2; i >= 0; i--) {
				if (saved_caller_regs[i]) {
					if (i == r) {
						printf("    add esp, 4\n");
					}
					else {
						printf("    pop %s\n", reg_names[i]);
					}
				}
			}
		}
		else {
			for (int i = 2; i >= 0; i--) {
				if (saved_caller_regs[i]) {
					printf("    pop %s\n", reg_names[i]);
				}
			}
		}

		node->int_val = r;
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
			if (target_type.kind == TYPE_CHAR && target_type.base_type == NULL) {
				printf("    movzx %s, byte ptr [%s + %d]\n", reg_names[r], reg_names[r_ptr], calculated_offset);
			}
			else {
				printf("    mov %s, dword ptr [%s + %d]\n", reg_names[r], reg_names[r_ptr], calculated_offset);
			}
			free_register(r_ptr);
		}
		else { // Dot Operator (.)
			int r_arr_base = emit_dot_base_address(node, &calculated_offset, &target_type);
			if (r_arr_base != -1) {
				if (target_type.kind == TYPE_CHAR && target_type.base_type == NULL)
					printf("    movzx %s, byte ptr [%s + %d]\n", reg_names[r], reg_names[r_arr_base], calculated_offset);
				else
					printf("    mov %s, dword ptr [%s + %d]\n", reg_names[r], reg_names[r_arr_base], calculated_offset);
				free_register(r_arr_base);
			}

			else {

				ASTNode* root_id = node;
				while (root_id && root_id->kind == AST_MEMBER_ACCESS) {
					root_id = root_id->left;
				}
				Symbol* struct_sym = (root_id && root_id->kind == AST_IDENT) ? lookup_symbol(root_id->name) : NULL;

				resolve_member_offset(node, &calculated_offset, &target_type);

				if (struct_sym && struct_sym->is_global_var) {
					int member_disp = calculated_offset - struct_sym->offset;
					if (target_type.kind == TYPE_CHAR && target_type.base_type == NULL) {
						printf("    movzx %s, byte ptr [%s + %d]\n", reg_names[r], struct_sym->asm_label, member_disp);
					}
					else {
						printf("    mov %s, dword ptr [%s + %d]\n", reg_names[r], struct_sym->asm_label, member_disp);
					}
				}
				else if (calculated_offset < 0) {
					if (target_type.kind == TYPE_CHAR && target_type.base_type == NULL) {
						printf("    movzx %s, byte ptr [ebp - %d]\n", reg_names[r], -calculated_offset);
					}
					else {
						printf("    mov %s, dword ptr [ebp - %d]\n", reg_names[r], -calculated_offset);
					}
				}
				else {
					if (target_type.kind == TYPE_CHAR && target_type.base_type == NULL) {
						printf("    movzx %s, byte ptr [ebp + %d]\n", reg_names[r], calculated_offset);
					}
					else {
						printf("    mov %s, dword ptr [ebp + %d]\n", reg_names[r], calculated_offset);
					}
				}
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
				printf("    je $$L%d\n", s->op);
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
			printf("    jmp $$L%d\n", l_default);
		}
		else {
			printf("    jmp $$L%d\n", l_end);
		}

		// --- PASS 2: GENERATE INNER STATEMENTS BODY ---
		printf("    ; --- BEGIN SWITCH BODY EXECUTION --- \n");
		s = (node->body && node->body->kind == AST_BLOCK) ? node->body->body : node->body;
		while (s) {
			if (s->kind == AST_CASE || s->kind == AST_DEFAULT) {
				// Emit the matching execution entry target label point
				printf("$$L%d:\n", s->op);
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
		printf("$$L%d:\n", l_end);
		loop_stack_top--; // Pop switch frame off the stack
		break;
	}

	}
}