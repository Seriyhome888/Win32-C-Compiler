#include "cc.h"

int max_function_stack_bytes = 64; // Fallback minimum size boundary

Token cur_tok;
Symbol* sym_table = NULL;
StructEnv* struct_table = NULL;
int local_offset = 0;

int current_scope_level = 0;

void enter_scope(void) {
	current_scope_level++;
}

void exit_scope(void) {
	// DO NOT free the symbols here! Codegen still needs them!
	// Just step back up a scope level layer.
	current_scope_level--;
}

// Only clear out localized stack variables, keeping function names alive!
void clear_local_symbols(void) {
	Symbol* current = sym_table;
	Symbol* prev = NULL;

	while (current) {
		if (current->is_local == 1) {
			// Unlink and free local variable tracker
			Symbol* to_free = current;
			if (prev == NULL) {
				sym_table = current->next;
			}
			else {
				prev->next = current->next;
			}
			current = current->next;
			free(to_free);
		}
		else {
			prev = current;
			current = current->next;
		}
	}
}

// Define the state tracking flag inside parser.c as well if needed, or rely on extern
Symbol* lookup_symbol(const char* name) {
	Symbol* sym = sym_table;
	Symbol* best_match = NULL;

	while (sym) {
		if (strcmp(sym->name, name) == 0) {
			if (is_codegen_phase) {
				// DURING CODE GENERATION:
				// Local variables are safely retained but current_scope_level is 0.
				// We want to grab the most local variant available (highest scope_level).
				if (best_match == NULL || sym->scope_level > best_match->scope_level) {
					best_match = sym;
				}
			}
			else {
				// DURING PARSING PHASE:
				// The variable must belong to an accessible outer scope layer 
				// relative to where the parser is currently positioned.
				if (sym->scope_level <= current_scope_level) {
					// Always prioritize the inner-most nested definition (highest scope_level)
					if (best_match == NULL || sym->scope_level > best_match->scope_level) {
						best_match = sym;
					}
				}
			}
		}
		sym = sym->next;
	}
	return best_match;
}

void match(TokenType type) {
	if (cur_tok.type == type) cur_tok = next_token();
	else {
		printf("Syntax Error: Expected token type %d, got %s\n", type, cur_tok.lexeme);
		exit(1);
	}
}

ASTNode* create_node(ASTNodeKind kind) {
	ASTNode* n = (ASTNode*)calloc(1, sizeof(ASTNode));
	n->kind = kind;
	return n;
}

// Forward Grammar Declarations
ASTNode* parse_expr(void);
ASTNode* parse_stmt(void);

ASTNode* parse_primary(void) {
	ASTNode* n;
	if (cur_tok.type == TOKEN_INT_LIT) {
		n = create_node(AST_LITERAL); n->type.kind = TYPE_INT;
		n->int_val = cur_tok.int_val; match(TOKEN_INT_LIT); return n;
	}
	if (cur_tok.type == TOKEN_FLOAT_LIT) {
		n = create_node(AST_LITERAL); n->type.kind = TYPE_FLOAT;
		n->float_val = cur_tok.float_val; match(TOKEN_FLOAT_LIT); return n;
	}
	if (cur_tok.type == TOKEN_STRING) {
		n = create_node(AST_LITERAL); n->type.kind = TYPE_POINTER;
		strcpy(n->name, cur_tok.lexeme); match(TOKEN_STRING); return n;
	}

	if (cur_tok.type == TOKEN_IDENT) {
		char name[64]; strcpy(name, cur_tok.lexeme); match(TOKEN_IDENT);
		if (cur_tok.type == TOKEN_LPAREN) { // Function Call
			match(TOKEN_LPAREN); n = create_node(AST_CALL); strcpy(n->name, name);
			ASTNode** arg_tail = &(n->left);
			while (cur_tok.type != TOKEN_RPAREN) {
				*arg_tail = parse_expr(); (*arg_tail)->next = NULL;
				arg_tail = &((*arg_tail)->next);
				if (cur_tok.type == TOKEN_COMMA) match(TOKEN_COMMA);
			}
			match(TOKEN_RPAREN); return n;
		}

		// FIXED: Create identifier node and instantly bind its verified type from the symbol table
		n = create_node(AST_IDENT);
		strcpy(n->name, name);

		Symbol* sym = lookup_symbol(name);
		if (sym) {
			n->type = sym->type; // Pull dimensions, pointer kind, and base type info
		}
		else {
			// Fallback default for variables declared later or global externs
			n->type.kind = TYPE_INT;
		}
		return n;
	}


	if (cur_tok.type == TOKEN_LPAREN) {
		match(TOKEN_LPAREN); n = parse_expr(); match(TOKEN_RPAREN); return n;
	}
	return NULL;
}

ASTNode* parse_postfix(void) {
	ASTNode* n = parse_primary();
	while (cur_tok.type == TOKEN_PERIOD || cur_tok.type == TOKEN_ARROW || cur_tok.type == TOKEN_LBRACKET) {
		if (cur_tok.type == TOKEN_LBRACKET) {
			ASTNode* arr = create_node(AST_ARRAY_ACCESS); arr->left = n;
			match(TOKEN_LBRACKET); arr->right = parse_expr(); match(TOKEN_RBRACKET);
			n = arr;
		}
		else {
			ASTNode* mem = create_node(AST_MEMBER_ACCESS); mem->op = cur_tok.type;
			mem->left = n; match(cur_tok.type); strcpy(mem->name, cur_tok.lexeme);
			match(TOKEN_IDENT); n = mem;
		}
	}
	return n;
}

ASTNode* parse_unary(void) {
	if (cur_tok.type == TOKEN_AMP || cur_tok.type == TOKEN_STAR) {
		ASTNode* n = create_node(AST_UNOP);
		n->op = cur_tok.type;
		TokenType current_op = cur_tok.type;
		match(cur_tok.type);

		n->left = parse_unary();

		if (current_op == TOKEN_AMP) {
			n->type.kind = TYPE_POINTER;
			n->type.base_type = &(n->left->type);
		}
		else if (current_op == TOKEN_STAR) {
			if (n->left->type.kind == TYPE_POINTER && n->left->type.base_type) {
				n->type = *(n->left->type.base_type);
			}
			else {
				// Default fallback base promotion for standard primitive registers
				n->type.kind = TYPE_INT;
			}
		}
		return n;
	}
	return parse_postfix();
}

ASTNode* parse_mul(void) {
	ASTNode* n = parse_unary();
	while (cur_tok.type == TOKEN_STAR || cur_tok.type == TOKEN_SLASH) {
		ASTNode* parent = create_node(AST_BINOP); parent->op = cur_tok.type;
		parent->left = n; match(cur_tok.type); parent->right = parse_unary();

		// TYPE PROPAGATION: If either side is a float, the result is a float
		if (parent->left->type.kind == TYPE_FLOAT || parent->right->type.kind == TYPE_FLOAT) {
			parent->type.kind = TYPE_FLOAT;
		}
		else {
			parent->type.kind = TYPE_INT;
		}

		n = parent;
	}
	return n;
}

ASTNode* parse_add(void) {
	ASTNode* n = parse_mul();
	while (cur_tok.type == TOKEN_PLUS || cur_tok.type == TOKEN_MINUS) {
		ASTNode* parent = create_node(AST_BINOP); parent->op = cur_tok.type;
		parent->left = n; match(cur_tok.type); parent->right = parse_mul();

		// TYPE PROPAGATION: If either side is a float, the result is a float
		if (parent->left->type.kind == TYPE_FLOAT || parent->right->type.kind == TYPE_FLOAT) {
			parent->type.kind = TYPE_FLOAT;
		}
		else {
			parent->type.kind = TYPE_INT;
		}

		n = parent;
	}
	return n;
}

ASTNode* parse_relational(void) {
	ASTNode* n = parse_add();
	while (cur_tok.type == TOKEN_LESS || cur_tok.type == TOKEN_GREATER || cur_tok.type == TOKEN_EQUAL) {
		ASTNode* parent = create_node(AST_BINOP);
		parent->op = cur_tok.type;
		parent->left = n;
		match(cur_tok.type);
		parent->right = parse_add();

		// FORCE PROPAGATION: If either compared side is a float, flag the node!
		if (parent->left->type.kind == TYPE_FLOAT || parent->right->type.kind == TYPE_FLOAT) {
			parent->type.kind = TYPE_FLOAT;
		}
		else {
			parent->type.kind = TYPE_INT;
		}
		n = parent;
	}
	return n;
}

ASTNode* parse_expr(void) {
	ASTNode* n = parse_relational();
	if (cur_tok.type == TOKEN_ASSIGN) {
		ASTNode* parent = create_node(AST_ASSIGN);
		parent->left = n;
		match(TOKEN_ASSIGN);
		parent->right = parse_expr();

		// FIXED: Propagate expression type completely to prevent mismatched assignments
		parent->type = n->type;
		n = parent;
	}
	return n;
}

ASTNode* parse_struct_or_union_declaration(int is_union) {
	// Match either struct or union keyword based on the flag passed
	if (is_union) {
		match(TOKEN_UNION);
	}
	else {
		match(TOKEN_STRUCT);
	}

	char name[64];
	strcpy(name, cur_tok.lexeme);
	match(TOKEN_IDENT);
	match(TOKEN_LBRACE);

	StructEnv* env = (StructEnv*)calloc(1, sizeof(StructEnv));
	strcpy(env->name, name);
	Member** mem_tail = &(env->members);

	int total_size = 0;
	int current_offset = 0;

	while (cur_tok.type != TOKEN_RBRACE && cur_tok.type != TOKEN_EOF) {
		DataType m_type;
		memset(&m_type, 0, sizeof(m_type));

		m_type.kind = (strcmp(cur_tok.lexeme, "float") == 0) ? TYPE_FLOAT : TYPE_INT;
		match(TOKEN_IDENT);

		char m_name[64];
		strcpy(m_name, cur_tok.lexeme);
		match(TOKEN_IDENT);
		match(TOKEN_SEMI);

		Member* m = (Member*)calloc(1, sizeof(Member));
		strcpy(m->name, m_name);
		m->type = m_type;

		if (is_union) {
			// Union members all start at offset 0
			m->offset = 0;
			if (4 > total_size) {
				total_size = 4; // Track maximum member size (assuming 4 bytes for primitives)
			}
		}
		else {
			// Struct members accumulate offsets sequentially
			m->offset = current_offset;
			current_offset += 4;
			total_size = current_offset;
		}

		*mem_tail = m;
		mem_tail = &(m->next);
	}
	match(TOKEN_RBRACE);

	env->size = total_size;
	env->next = struct_table;
	struct_table = env;

	// Return the correct AST node flavor type
	ASTNode* n = create_node(is_union ? AST_UNION_DECL : AST_STRUCT_DECL);
	strcpy(n->name, name);
	return n;
}

ASTNode* parse_stmt(void) {

	int is_struct_var = (cur_tok.type == TOKEN_STRUCT);
	int is_primitive_var = (cur_tok.type == TOKEN_IDENT && (strcmp(cur_tok.lexeme, "int") == 0 || strcmp(cur_tok.lexeme, "float") == 0));

	if (is_primitive_var || is_struct_var) {
		DataType vt;
		memset(&vt, 0, sizeof(vt));

		char s_name[64] = { 0 };

		if (is_struct_var) {
			match(TOKEN_STRUCT); // Consume 'struct' keyword
			strcpy(s_name, cur_tok.lexeme);
			match(TOKEN_IDENT);  // Consume the struct type name (e.g., "Person")

			vt.kind = TYPE_STRUCT;
			strcpy(vt.struct_name, s_name);
		}
		else {
			vt.kind = (strcmp(cur_tok.lexeme, "int") == 0) ? TYPE_INT : TYPE_FLOAT;
			match(TOKEN_IDENT); // Consumes primitive type keyword ("int" or "float")
		}

		// Check for pointer asterisks (e.g., struct Person* ptr;)
		while (cur_tok.type == TOKEN_STAR) {
			match(TOKEN_STAR);
			// Wrap current type into a pointer base layer
			DataType* base = (DataType*)calloc(1, sizeof(DataType));
			*base = vt;

			memset(&vt, 0, sizeof(vt));
			vt.kind = TYPE_POINTER;
			vt.base_type = base;
		}

		// Capture the actual variable name identifier
		char name[64];
		strcpy(name, cur_tok.lexeme);
		match(TOKEN_IDENT); // Consumes the variable name identifier

		// Calculate total base allocations multiplier width
		int total = 1;
		while (cur_tok.type == TOKEN_LBRACKET) {
			match(TOKEN_LBRACKET);
			vt.is_array = 1;
			vt.dimensions[vt.dim_count++] = cur_tok.int_val;
			total *= cur_tok.int_val;
			match(TOKEN_INT_LIT);
			match(TOKEN_RBRACKET);
		}
		match(TOKEN_SEMI);

		// Calculate how many bytes this variable actually needs on the stack frame
		int var_size = 4; // Default primitives and pointers take 4 bytes in 32-bit x86
		if (vt.kind == TYPE_STRUCT && vt.base_type == NULL) { // True structural instance, not a pointer
			struct StructEnv* env = lookup_struct(vt.struct_name);
			if (env) {
				var_size = env->size;
			}
			else {
				printf("Semantic Error: Local variable '%s' uses undefined struct type '%s'.\n", name, vt.struct_name);
				exit(1);
			}
		}

		// Update stack frame tracking parameters relative to custom calculated size bounds
		local_offset += (var_size * total);

		if (local_offset > max_function_stack_bytes) {
			max_function_stack_bytes = local_offset;
		}

		// Register the new variable inside the compiler symbol table
		Symbol* sym = (Symbol*)calloc(1, sizeof(Symbol));
		strcpy(sym->name, name);
		sym->type = vt;
		sym->is_local = 1;
		sym->offset = -local_offset;
		sym->scope_level = current_scope_level;
		sym->next = sym_table;
		sym_table = sym;

		ASTNode* n = create_node(AST_VAR_DECL);
		strcpy(n->name, name);
		return n;
	}

	if (cur_tok.type == TOKEN_IDENT && (strcmp(cur_tok.lexeme, "int") == 0 || strcmp(cur_tok.lexeme, "float") == 0)) {
		DataType vt;
		// Safely zero-initialize the local variable structure memory cell
		memset(&vt, 0, sizeof(vt));

		vt.kind = (strcmp(cur_tok.lexeme, "int") == 0) ? TYPE_INT : TYPE_FLOAT;
		match(TOKEN_IDENT); // Consumes the type keyword identifier ("int" or "float")

		char name[64];
		strcpy(name, cur_tok.lexeme);
		match(TOKEN_IDENT); // Consumes the variable name identifier

		int total = 1;
		// Handle optional array bracketing dimensions sequentially
		while (cur_tok.type == TOKEN_LBRACKET) {
			match(TOKEN_LBRACKET);
			vt.is_array = 1;
			vt.dimensions[vt.dim_count++] = cur_tok.int_val;
			total *= cur_tok.int_val;
			match(TOKEN_INT_LIT);
			match(TOKEN_RBRACKET);
		}
		match(TOKEN_SEMI);

		// Update stack frame tracking parameters
		local_offset += (4 * total);

		// Register the new variable inside the compiler symbol table
		Symbol* sym = (Symbol*)calloc(1, sizeof(Symbol));
		strcpy(sym->name, name);
		sym->type = vt;
		sym->is_local = 1;
		sym->offset = -local_offset;
		sym->scope_level = current_scope_level;
		sym->next = sym_table;
		sym_table = sym;

		ASTNode* n = create_node(AST_VAR_DECL);
		strcpy(n->name, name);
		return n;
	}

	// ------------------------------------------------------------------------
	// Normal C Statements resume safely below:
	// ------------------------------------------------------------------------
	if (cur_tok.type == TOKEN_LBRACE) {
		match(TOKEN_LBRACE);
		enter_scope();

		ASTNode* n = create_node(AST_BLOCK);
		ASTNode** tail = &(n->body);
		while (cur_tok.type != TOKEN_RBRACE && cur_tok.type != TOKEN_EOF) {
			*tail = parse_stmt();
			tail = &((*tail)->next);
		}
		match(TOKEN_RBRACE);
		exit_scope();
		return n;
	}

	if (cur_tok.type == TOKEN_IF) {
		ASTNode* n = create_node(AST_IF); match(TOKEN_IF); match(TOKEN_LPAREN);
		n->cond = parse_expr(); match(TOKEN_RPAREN); n->body = parse_stmt();
		if (cur_tok.type == TOKEN_ELSE) { match(TOKEN_ELSE); n->else_body = parse_stmt(); }
		return n;
	}
	if (cur_tok.type == TOKEN_WHILE) {
		ASTNode* n = create_node(AST_WHILE); match(TOKEN_WHILE); match(TOKEN_LPAREN);
		n->cond = parse_expr(); match(TOKEN_RPAREN); n->body = parse_stmt(); return n;
	}
	if (cur_tok.type == TOKEN_FOR) {
		ASTNode* n = create_node(AST_FOR); match(TOKEN_FOR); match(TOKEN_LPAREN);
		n->left = parse_expr(); match(TOKEN_SEMI); n->cond = parse_expr(); match(TOKEN_SEMI);
		n->right = parse_expr(); match(TOKEN_RPAREN); n->body = parse_stmt(); return n;
	}
	if (cur_tok.type == TOKEN_BREAK) { ASTNode* n = create_node(AST_BREAK); match(TOKEN_BREAK); match(TOKEN_SEMI); return n; }
	if (cur_tok.type == TOKEN_CONTINUE) { ASTNode* n = create_node(AST_CONTINUE); match(TOKEN_CONTINUE); match(TOKEN_SEMI); return n; }
	if (cur_tok.type == TOKEN_RETURN) {
		ASTNode* n = create_node(AST_RETURN); match(TOKEN_RETURN); n->left = parse_expr(); match(TOKEN_SEMI); return n;
	}

	ASTNode* n = parse_expr();
	match(TOKEN_SEMI);

	if (n) {
		n->next = NULL; // FIX: Force-clear lookahead pointers to prevent infinite block linking loops!
	}

	return n;
}

ASTNode* parse_program(void) {
	// 1. Reset localized variables and offsets for the incoming function frame
	local_offset = 0;
	current_scope_level = 0;

	// 2. INTERNAL LOOP: Drain ALL top-level type definitions (structs/unions) 
	// until we reach an actual function declaration or EOF.
	while (cur_tok.type == TOKEN_STRUCT || cur_tok.type == TOKEN_UNION) {
		parse_struct_or_union_declaration(cur_tok.type == TOKEN_UNION);
		if (cur_tok.type == TOKEN_SEMI) {
			match(TOKEN_SEMI);
		}
	}

	// 3. Gracefully handle files that end after global structure definitions
	if (cur_tok.type == TOKEN_EOF) {
		ASTNode* eof_node = create_node(AST_PROGRAM);
		return eof_node;
	}

	// 4. Safely evaluate the function return type identifier
	DataTypeKind ret_kind = TYPE_INT; // Default fallback
	if (cur_tok.type == TOKEN_IDENT) {
		if (strcmp(cur_tok.lexeme, "float") == 0) {
			ret_kind = TYPE_FLOAT;
		}
		match(TOKEN_IDENT);
	}
	else {
		printf("Parsing Error: Expected function return type, got: %s\n", cur_tok.lexeme);
		exit(1);
	}

	// 5. Capture the name of the function
	char name[64];
	strcpy(name, cur_tok.lexeme);
	match(TOKEN_IDENT);

	// Add function record to the global symbol table with the accurate return type!
	Symbol* func_sym = (Symbol*)calloc(1, sizeof(Symbol));
	if (!func_sym) {
		fprintf(stderr, "Out of memory during symbol allocation.\n");
		exit(1);
	}
	strcpy(func_sym->name, name);
	func_sym->type.kind = ret_kind; // Dynamic return type tracking
	func_sym->is_local = 0;
	func_sym->scope_level = 0;
	func_sym->next = sym_table;
	sym_table = func_sym;

	// 6. Parse function argument lists
	match(TOKEN_LPAREN);
	int arg_off = 8; // Standard EBP offset tracking for arguments (EBP+8, EBP+12...)
	while (cur_tok.type != TOKEN_RPAREN) {
		DataType pt;
		memset(&pt, 0, sizeof(pt));
		pt.kind = TYPE_INT;

		if (strcmp(cur_tok.lexeme, "float") == 0) {
			pt.kind = TYPE_FLOAT;
		}
		match(TOKEN_IDENT); // Consume type keyword

		char p_name[64];
		strcpy(p_name, cur_tok.lexeme);
		match(TOKEN_IDENT); // Consume argument variable name

		Symbol* sym = (Symbol*)calloc(1, sizeof(Symbol));
		if (!sym) {
			fprintf(stderr, "Out of memory during symbol allocation.\n");
			exit(1);
		}
		strcpy(sym->name, p_name);
		sym->type = pt;
		sym->is_local = 1; // Marked local so it clears safely when function ends
		sym->offset = arg_off;
		sym->scope_level = current_scope_level;
		arg_off += 4;

		sym->next = sym_table;
		sym_table = sym;

		if (cur_tok.type == TOKEN_COMMA) {
			match(TOKEN_COMMA);
		}
	}
	match(TOKEN_RPAREN);

	// 7. Generate execution AST structure for the function body
	ASTNode* n = create_node(AST_FUNC_DECL);
	strcpy(n->name, name);
	n->body = parse_stmt();


	// NEW: Save the calculated size inside the function node's value property
		// Round it up to a 16-byte boundary to keep the x86 stack aligned properly
	n->int_val = (max_function_stack_bytes + 15) & ~15;

	// Reset tracker back to 64 for the next function declaration
	max_function_stack_bytes = 64;

	return n;
}

struct StructEnv* lookup_struct(const char* name) {
	struct StructEnv* env = struct_table;
	while (env) {
		if (strcmp(env->name, name) == 0) {
			return env;
		}
		env = env->next;
	}
	return NULL;
}
