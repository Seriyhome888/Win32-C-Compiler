#include "cc.h"

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

Symbol* lookup_symbol(const char* name) {
	Symbol* sym = sym_table;
	while (sym) {
		if (strcmp(sym->name, name) == 0) {
			// During codegen, current_scope_level resets back to 0. 
			// If a symbol has a higher scope level, it means it's a valid local variable!
			// We allow lookups to find it if we are in the codegen phase, 
			// OR if we are actively parsing and it belongs to an accessible scope layer.
			if (current_scope_level == 0 || sym->scope_level <= current_scope_level) {
				return sym;
			}
		}
		sym = sym->next;
	}
	return NULL;
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
	// 1. Clear out localized variables from the PREVIOUS function, 
	// leaving global identifiers/functions intact!
	//clear_local_symbols();
	local_offset = 0;
	current_scope_level = 0;

	// 2. Safely parse structural layers and catch pure metadata wrappers
	while (cur_tok.type == TOKEN_STRUCT || cur_tok.type == TOKEN_UNION) {
		ASTNode* decl = parse_struct_or_union_declaration(cur_tok.type == TOKEN_UNION);
		if (cur_tok.type == TOKEN_SEMI) match(TOKEN_SEMI);

		// If the file hits EOF right after a global structure declaration, return it
		if (cur_tok.type == TOKEN_EOF) {
			return decl;
		}
	}

	// 3. Gracefully wrap up if we hit the end of the file safely
	if (cur_tok.type == TOKEN_EOF) {
		ASTNode* eof_node = create_node(AST_PROGRAM);
		return eof_node;
	}

	// 4. Safely evaluate the function return type identifier
	if (cur_tok.type == TOKEN_IDENT) {
		match(TOKEN_IDENT);
	}
	else {
		printf("Parsing Error: Expected function return type or declaration at global scope, got: %s\n", cur_tok.lexeme);
		exit(1);
	}

	// 5. Capture the name of the function
	char name[64];
	strcpy(name, cur_tok.lexeme);
	match(TOKEN_IDENT);

	// Add function record to the global symbol table so it can be called elsewhere!
	Symbol* func_sym = (Symbol*)calloc(1, sizeof(Symbol));
	strcpy(func_sym->name, name);
	func_sym->type.kind = TYPE_INT; // Default return tracking
	func_sym->is_local = 0;         // Set explicitly as global function
	func_sym->scope_level = 0;
	func_sym->next = sym_table;
	sym_table = func_sym;

	// 6. Parse function argument lists
	match(TOKEN_LPAREN);
	int arg_off = 8;
	while (cur_tok.type != TOKEN_RPAREN) {
		DataType pt;
		pt.kind = TYPE_INT;
		match(TOKEN_IDENT);

		char p_name[64];
		strcpy(p_name, cur_tok.lexeme);
		match(TOKEN_IDENT);

		Symbol* sym = (Symbol*)calloc(1, sizeof(Symbol));
		strcpy(sym->name, p_name);
		sym->type = pt;
		sym->is_local = 1; // Marked local so it clears when function ends
		sym->offset = arg_off;
		sym->scope_level = current_scope_level;
		arg_off += 4;

		sym->next = sym_table;
		sym_table = sym;

		if (cur_tok.type == TOKEN_COMMA) match(TOKEN_COMMA);
	}
	match(TOKEN_RPAREN);

	// 7. Generate execution AST structure
	ASTNode* n = create_node(AST_FUNC_DECL);
	strcpy(n->name, name);
	n->body = parse_stmt();
	return n;
}
