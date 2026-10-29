#include "cc.h"

int max_function_stack_bytes = 64; // Fallback minimum size boundary

Token cur_tok;
Symbol* sym_table = NULL;
StructEnv* struct_table = NULL;
int local_offset = 0;

int current_scope_level = 0;

static ASTNode* ensure_float_node(ASTNode* node) {
	if (!node) return NULL;
	if (node->type.kind == TYPE_INT || node->type.kind == TYPE_CHAR) {
		// Create an explicit implicit-promotion type node
		ASTNode* cast_node = create_node(AST_UNOP);
		cast_node->op = TOKEN_FLOAT_LIT; // Reuse internal token representation as a marker
		cast_node->left = node;
		memset(&(cast_node->type), 0, sizeof(DataType));
		cast_node->type.kind = TYPE_FLOAT;
		return cast_node;
	}
	return node;
}

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
		fprintf(stderr, "Syntax Error: Expected token type %d, got %s\n", type, cur_tok.lexeme);
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
		char name[64];
		strcpy(name, cur_tok.lexeme);
		match(TOKEN_IDENT);

		if (cur_tok.type == TOKEN_LPAREN) { // Function Call
			match(TOKEN_LPAREN);
			n = create_node(AST_CALL);
			strcpy(n->name, name);
			ASTNode** arg_tail = &(n->left);
			while (cur_tok.type != TOKEN_RPAREN) {
				*arg_tail = parse_expr();
				(*arg_tail)->next = NULL;
				arg_tail = &((*arg_tail)->next);
				if (cur_tok.type == TOKEN_COMMA) match(TOKEN_COMMA);
			}
			match(TOKEN_RPAREN);
			return n;
		}

		// FIXED: Create identifier node and completely clean type layout
		n = create_node(AST_IDENT);
		strcpy(n->name, name);

		Symbol* sym = lookup_symbol(name);
		if (sym) {
			n->type = sym->type; // Pull verified dimensions and base type context
		}
		else {
			// Safe explicit clean slate fallback structure configuration
			memset(&(n->type), 0, sizeof(DataType));
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
	if (cur_tok.type == TOKEN_AMP || cur_tok.type == TOKEN_STAR || cur_tok.type == TOKEN_MINUS || cur_tok.type == TOKEN_NOT) {
		ASTNode* n = create_node(AST_UNOP);
		n->op = cur_tok.type;
		TokenType current_op = cur_tok.type;
		match(cur_tok.type);

		// Recursively parse the rest of the expression
		n->left = parse_unary();

		if (current_op == TOKEN_AMP) {
			n->type.kind = TYPE_POINTER;
			n->type.base_type = (DataType*)calloc(1, sizeof(DataType));
			*(n->type.base_type) = n->left->type;
		}
		else if (current_op == TOKEN_STAR) {
			if (n->left->type.kind == TYPE_POINTER && n->left->type.base_type) {
				n->type = *(n->left->type.base_type);
			}
			else {
				n->type.kind = TYPE_INT;
			}
		}
		else if (current_op == TOKEN_MINUS) {
			// Propagate the child node's type context (int or float) to the negation node
			n->type = n->left->type;
		}
		else if (current_op == TOKEN_NOT) {
			n->type.kind = TYPE_INT; // Results of logical evaluations are always integer booleans
		}
		
		return n;
	}
	return parse_postfix();
}

ASTNode* parse_mul(void) {
	ASTNode* n = parse_unary();
	while (cur_tok.type == TOKEN_STAR || cur_tok.type == TOKEN_SLASH || cur_tok.type == TOKEN_PERCENT) {
		ASTNode* parent = create_node(AST_BINOP);
		parent->op = cur_tok.type;
		parent->left = n;
		match(cur_tok.type);
		parent->right = parse_unary();

		// FIXED: Force underlying operand sub-trees to promote up explicitly
		if (parent->left->type.kind == TYPE_FLOAT || parent->right->type.kind == TYPE_FLOAT) {
			parent->left = ensure_float_node(parent->left);
			parent->right = ensure_float_node(parent->right);
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
		ASTNode* parent = create_node(AST_BINOP);
		parent->op = cur_tok.type;
		parent->left = n;
		match(cur_tok.type);
		parent->right = parse_mul();

		// FIXED: Force underlying operand sub-trees to promote up explicitly
		if (parent->left->type.kind == TYPE_FLOAT || parent->right->type.kind == TYPE_FLOAT) {
			parent->left = ensure_float_node(parent->left);
			parent->right = ensure_float_node(parent->right);
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

		// FIXED: Promote children but force the evaluation condition itself to remain TYPE_INT
		if (parent->left->type.kind == TYPE_FLOAT || parent->right->type.kind == TYPE_FLOAT) {
			parent->left = ensure_float_node(parent->left);
			parent->right = ensure_float_node(parent->right);
		}
		parent->type.kind = TYPE_INT; // Boolean validation outputs are integer flags (0 or 1)
		n = parent;
	}
	return n;
}

ASTNode* parse_log_and(void) {
	ASTNode* n = parse_relational();
	while (cur_tok.type == TOKEN_LOGAND) {
		ASTNode* parent = create_node(AST_BINOP);
		parent->op = cur_tok.type;
		parent->left = n;
		match(TOKEN_LOGAND);
		parent->right = parse_relational();
		parent->type.kind = TYPE_INT; // Boolean evaluation result is an int
		n = parent;
	}
	return n;
}

ASTNode* parse_log_or(void) {
	ASTNode* n = parse_log_and();
	while (cur_tok.type == TOKEN_LOGOR) {
		ASTNode* parent = create_node(AST_BINOP);
		parent->op = cur_tok.type;
		parent->left = n;
		match(TOKEN_LOGOR);
		parent->right = parse_log_and();
		parent->type.kind = TYPE_INT; // Boolean evaluation result is an int
		n = parent;
	}
	return n;
}

ASTNode* parse_expr(void) {
	ASTNode* n = parse_log_or();
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
	if (!env) {
		fprintf(stderr, "Fatal Error: Out of memory during struct environment allocation.\n");
		exit(1);
	}
	strcpy(env->name, name);
	Member** mem_tail = &(env->members);

	int total_size = 0;
	int current_offset = 0;

	// FIXED LOOP PIPELINE
	while (cur_tok.type != TOKEN_RBRACE && cur_tok.type != TOKEN_EOF) {
		DataType m_type;
		memset(&m_type, 0, sizeof(m_type));

		// 1. Check field type lexeme without consuming it prematurely
		m_type.kind = (strcmp(cur_tok.lexeme, "float") == 0) ? TYPE_FLOAT : TYPE_INT;

		// 2. Consume the type keyword identifier token ("int" or "float") safely
		match(TOKEN_IDENT);

		// 3. Extract the member name identifier string (e.g., "id", "age")
		char m_name[64];
		strcpy(m_name, cur_tok.lexeme);

		// 4. Consume the member name identifier token 
		match(TOKEN_IDENT);

		// 5. Consume the terminating semicolon syntax marker
		match(TOKEN_SEMI);

		// Allocate and populate member attributes
		Member* m = (Member*)calloc(1, sizeof(Member));
		if (!m) {
			fprintf(stderr, "Fatal Error: Out of memory during member allocation.\n");
			exit(1);
		}
		strcpy(m->name, m_name);
		m->type = m_type;

		if (is_union) {
			// Union members share stack alignment offset 0
			m->offset = 0;
			if (4 > total_size) {
				total_size = 4; // Track widest element primitive bounds size
			}
		}
		else {
			// Struct members increment sequential byte layouts linearly
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

	ASTNode* n = create_node(is_union ? AST_UNION_DECL : AST_STRUCT_DECL);
	strcpy(n->name, name);
	return n;
}

ASTNode* parse_stmt(void) {

	int is_struct_var = (cur_tok.type == TOKEN_STRUCT);
	int is_primitive_var = (cur_tok.type == TOKEN_IDENT && (strcmp(cur_tok.lexeme, "int") == 0 || strcmp(cur_tok.lexeme, "float") == 0))
		||
		(cur_tok.type == TOKEN_CHAR);

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
			if (cur_tok.type == TOKEN_CHAR) {
				vt.kind = TYPE_CHAR;
				match(TOKEN_CHAR);
			}
			else {
				vt.kind = (strcmp(cur_tok.lexeme, "int") == 0) ? TYPE_INT : TYPE_FLOAT;
				match(TOKEN_IDENT); // Consumes primitive type keyword ("int" or "float")
			}
		}

		// Check for pointer asterisks (e.g., struct Person* ptr;)
		while (cur_tok.type == TOKEN_STAR) {
			match(TOKEN_STAR);
			// Wrap current type into a pointer base layer
			DataType* base = (DataType*)calloc(1, sizeof(DataType));
			if (!base) { fprintf(stderr, "Fatal Error: Out of memory.\n"); exit(1); }
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
		if (vt.kind == TYPE_CHAR && vt.base_type == NULL) {
			var_size = 1; // True char instance occupies exactly 1 byte!
		}
		else if (vt.kind == TYPE_STRUCT && vt.base_type == NULL) { // True structural instance, not a pointer
			struct StructEnv* env = lookup_struct(vt.struct_name);
			if (env) {
				var_size = env->size;
			}
			else {
				fprintf(stderr, "Semantic Error: Local variable '%s' uses undefined struct type '%s'.\n", name, vt.struct_name);
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

	if (cur_tok.type == TOKEN_SWITCH) {
		ASTNode* n = create_node(AST_SWITCH);
		match(TOKEN_SWITCH);
		match(TOKEN_LPAREN);
		n->left = parse_expr(); // The central controlling condition expression
		match(TOKEN_RPAREN);
		n->body = parse_stmt(); // Usually an AST_BLOCK containing cases
		return n;
	}

	if (cur_tok.type == TOKEN_CASE) {
		ASTNode* n = create_node(AST_CASE);
		match(TOKEN_CASE);
		if (cur_tok.type != TOKEN_INT_LIT) {
			fprintf(stderr, "Syntax Error: C89 requires case labels to be constant integer literals.\n");
			exit(1);
		}
		n->int_val = cur_tok.int_val; // Capture the constant value
		match(TOKEN_INT_LIT);
		match(TOKEN_COLON);
		return n;
	}

	if (cur_tok.type == TOKEN_DEFAULT) {
		ASTNode* n = create_node(AST_DEFAULT);
		match(TOKEN_DEFAULT);
		match(TOKEN_COLON);
		return n;
	}

	ASTNode* n = parse_expr();
	match(TOKEN_SEMI);

	//if (n) {
	//	n->next = NULL; // FIX: Force-clear lookahead pointers to prevent infinite block linking loops!
	//}

	return n;
}

ASTNode* parse_program(void) {
	// 1. Reset localized stack tracking metrics for the incoming function frame
	local_offset = 0;
	current_scope_level = 0;

	// 2. INTERNAL LOOP: Drain ALL top-level type definitions (structs/unions)
	// and register them into the environment structures before parsing code.
	while (cur_tok.type == TOKEN_STRUCT || cur_tok.type == TOKEN_UNION) {
		parse_struct_or_union_declaration(cur_tok.type == TOKEN_UNION);
		if (cur_tok.type == TOKEN_SEMI) {
			match(TOKEN_SEMI);
		}
	}

	// 3. ROBUST RECOVERY: Aggressively catch EOF or trailing text debris
	// (like "this is test file" at the bottom) to block unhandled lookahead crashes.
	if (cur_tok.type == TOKEN_EOF) {
		ASTNode* eof_node = create_node(AST_PROGRAM);
		return eof_node;
	}

	if (cur_tok.type != TOKEN_IDENT && cur_tok.type != TOKEN_VOID && cur_tok.type != TOKEN_CHAR) {
		// A rogue layout token was hit out of scope. Force a clean exit state.
		ASTNode* eof_node = create_node(AST_PROGRAM);
		cur_tok.type = TOKEN_EOF;
		return eof_node;
	}

	// 4. Evaluate the function return type identifier
	DataTypeKind ret_kind = TYPE_INT; // Default fallback
	if (cur_tok.type == TOKEN_IDENT || cur_tok.type == TOKEN_VOID || cur_tok.type == TOKEN_CHAR) {
		if (cur_tok.type == TOKEN_IDENT && strcmp(cur_tok.lexeme, "float") == 0) {
			ret_kind = TYPE_FLOAT;
		}
		else if (cur_tok.type == TOKEN_CHAR) {
			ret_kind = TYPE_CHAR;
		}
		// Consume whichever primitive return keyword token was matched
		match(cur_tok.type);
	}

	// 5. Capture the exact name string identifier of the function
	char name[64];
	strcpy(name, cur_tok.lexeme);
	match(TOKEN_IDENT);

	// 6. Parse Function Parameters List 
	match(TOKEN_LPAREN);
	int arg_off = 8; // Standard x86 32-bit EBP offset tracking for arguments (EBP+8, EBP+12...)
	int is_var = 0;

	while (cur_tok.type != TOKEN_RPAREN) {
		if (cur_tok.type == TOKEN_ELLIPSIS) {
			is_var = 1;
			match(TOKEN_ELLIPSIS);
			if (cur_tok.type != TOKEN_RPAREN) {
				fprintf(stderr, "Parsing Error: Ellipsis must be the final parameter.\n");
				exit(1);
			}
			break;
		}

		DataType pt;
		memset(&pt, 0, sizeof(pt));

		if (cur_tok.type == TOKEN_CHAR) {
			pt.kind = TYPE_CHAR;
			match(TOKEN_CHAR);
		}
		else {
			pt.kind = (strcmp(cur_tok.lexeme, "float") == 0) ? TYPE_FLOAT : TYPE_INT;
			match(TOKEN_IDENT); // Consume type name string
		}

		// Check for pointer argument layers (e.g., char* message)
		while (cur_tok.type == TOKEN_STAR) {
			match(TOKEN_STAR);
			DataType* base = (DataType*)calloc(1, sizeof(DataType));
			if (!base) { fprintf(stderr, "Fatal Error: Out of memory.\n"); exit(1); }
			*base = pt;
			memset(&pt, 0, sizeof(pt));
			pt.kind = TYPE_POINTER;
			pt.base_type = base;
		}

		char p_name[64];
		strcpy(p_name, cur_tok.lexeme);
		match(TOKEN_IDENT); // Consume parameter identifier string

		Symbol* sym = (Symbol*)calloc(1, sizeof(Symbol));
		if (!sym) { fprintf(stderr, "Out of memory.\n"); exit(1); }
		strcpy(sym->name, p_name);
		sym->type = pt;
		sym->is_local = 1; // Temporarily link as local stack tracker variable
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

	// 7. CRITICAL HEADER PROTOTYPE LOOKAHEAD CHECK
	if (cur_tok.type == TOKEN_SEMI) {
		match(TOKEN_SEMI); // Consume terminating ';'

		// Wipe the parameter symbols we just loaded since there's no actual execution body block
		void clear_local_symbols(void);
		clear_local_symbols();

		// Append an explicit reference block to the symbol table tagged as an external import
		Symbol* func_sym = (Symbol*)calloc(1, sizeof(Symbol));
		if (!func_sym) { fprintf(stderr, "Out of memory.\n"); exit(1); }
		strcpy(func_sym->name, name);
		func_sym->type.kind = ret_kind;
		func_sym->is_local = 0;
		func_sym->is_extern = 1; // <-- Marked as External!
		func_sym->is_variadic = is_var;
		func_sym->next = sym_table;
		sym_table = func_sym;

		// Return an unmapped variable node frame that code generation naturally bypasses
		ASTNode* empty_proto = create_node(AST_VAR_DECL);
		return empty_proto;
	}

	// 8. Otherwise: It's a localized function containing a physical declaration body
	Symbol* func_sym = (Symbol*)calloc(1, sizeof(Symbol));
	if (!func_sym) { fprintf(stderr, "Out of memory.\n"); exit(1); }
	strcpy(func_sym->name, name);
	func_sym->type.kind = ret_kind;
	func_sym->is_local = 0;
	func_sym->is_extern = 0; // Local execution frame
	func_sym->is_variadic = is_var;
	func_sym->next = sym_table;
	sym_table = func_sym;

	// Parse out statement body blocks cleanly
	ASTNode* n = create_node(AST_FUNC_DECL);
	strcpy(n->name, name);
	n->body = parse_stmt();

	// Calculate and align total internal local variables requirements to a 16-byte stack boundary
	n->int_val = ((max_function_stack_bytes + 15) & ~15) + 16;

	// Reset variable layout width bounds trackers back for the next function declaration
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
