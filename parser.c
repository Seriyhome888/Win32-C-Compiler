#include "cc.h"

static char current_func_name[64] = "global";
static int static_var_counter = 0;

int max_function_stack_bytes = 64; // Fallback minimum size boundary

Token cur_tok;
Symbol* sym_table = NULL;
StructEnv* struct_table = NULL;
int local_offset = 0;

int current_scope_level = 0;

static int is_type_name(Token tok) {
	if (tok.type == TOKEN_CHAR) return 1;
	if (tok.type == TOKEN_STRUCT || tok.type == TOKEN_UNION) return 1;
	if (tok.type == TOKEN_IDENT) {
		if (strcmp(tok.lexeme, "int") == 0 || strcmp(tok.lexeme, "float") == 0) {
			return 1;
		}
		// Check if the identifier has been registered as a custom type alias
		Symbol* sym = lookup_symbol(tok.lexeme);
		if (sym && sym->is_typedef) {
			return 1;
		}
	}
	return 0;
}

void match(TokenType type);

static DataType parse_base_type(void) {
	DataType vt;
	memset(&vt, 0, sizeof(vt));

	if (cur_tok.type == TOKEN_STRUCT || cur_tok.type == TOKEN_UNION) {
		int is_u = (cur_tok.type == TOKEN_UNION);
		match(cur_tok.type);

		// If there's an immediate opening brace, it's an inline definition
		if (cur_tok.type == TOKEN_LBRACE) {
			// Unfortunatley parse_struct_or_union_declaration creates an AST node.
			// We can call it or lookup existing definitions if it's just a tag name:
		}

		strcpy(vt.struct_name, cur_tok.lexeme);
		match(TOKEN_IDENT);
		vt.kind = is_u ? TYPE_UNION : TYPE_STRUCT;
	}
	else if (cur_tok.type == TOKEN_CHAR) {
		vt.kind = TYPE_CHAR;
		match(TOKEN_CHAR);
	}
	else if (cur_tok.type == TOKEN_IDENT) {
		if (strcmp(cur_tok.lexeme, "int") == 0) {
			vt.kind = TYPE_INT;
			match(TOKEN_IDENT);
		}
		else if (strcmp(cur_tok.lexeme, "float") == 0) {
			vt.kind = TYPE_FLOAT;
			match(TOKEN_IDENT);
		}
		else {
			// It must be a typedef alias
			Symbol* sym = lookup_symbol(cur_tok.lexeme);
			if (sym && sym->is_typedef) {
				vt = sym->type; // Pull underlying base type context completely
				match(TOKEN_IDENT);
			}
			else {
				fprintf(stderr, "Semantic Error: Unknown type identifier '%s'\n", cur_tok.lexeme);
				exit(1);
			}
		}
	}
	return vt;
}

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
		if (current->is_local == 1 && !current->is_typedef) {
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
	while (cur_tok.type == TOKEN_PERIOD || cur_tok.type == TOKEN_ARROW || cur_tok.type == TOKEN_LBRACKET
		|| cur_tok.type == TOKEN_INC || cur_tok.type == TOKEN_DEC) {
		if (cur_tok.type == TOKEN_INC || cur_tok.type == TOKEN_DEC) {
			ASTNode* post = create_node(cur_tok.type == TOKEN_INC ? AST_POST_INC : AST_POST_DEC);
			post->op = cur_tok.type;
			post->left = n;
			post->type = n->type;
			match(cur_tok.type);
			n = post;
		}
		else if (cur_tok.type == TOKEN_LBRACKET) {
			ASTNode* arr = create_node(AST_ARRAY_ACCESS);
			arr->left = n;
			arr->type = n->type;
			arr->type.is_array = 0;
			arr->type.dim_count = 0;
			match(TOKEN_LBRACKET); arr->right = parse_expr(); match(TOKEN_RBRACKET);
			n = arr;
		}
		else {
			ASTNode* mem = create_node(AST_MEMBER_ACCESS); mem->op = cur_tok.type;
			mem->left = n; match(cur_tok.type); strcpy(mem->name, cur_tok.lexeme);
			match(TOKEN_IDENT);
			int dummy_off = 0;
			resolve_member_offset(mem, &dummy_off, &(mem->type));
			n = mem;
		}
	}
	return n;
}

// Forward declaration helper to check if a parenthesized block is a cast
static int is_cast_expression(void) {
	// We need to look ahead without permanently consuming tokens. 
	// Since our lexer doesn't support generic infinite rollback, we check if 
	// cur_tok is '(' and the next token is a valid type identifier.
	// However, since we already have 'is_type_name', we can look at the sequence.
	// If your lexer structure allows simple lookahead, implement it here.
	// For a standard predictive parser, we can check if it's an explicit cast:
	return (cur_tok.type == TOKEN_LPAREN);
}

ASTNode* parse_unary(void) {
	// --- EXPLICIT TYPE CAST DETECTION ---
	// --- ENHANCED TYPE CAST DETECTION FOR VARIADIC SUPPORT ---
	if (cur_tok.type == TOKEN_LPAREN) {
		Token next_tok_peek = peek_next_token();
		if (is_type_name(next_tok_peek)) {
			match(TOKEN_LPAREN);
			DataType cast_type = parse_base_type();

			// Correctly parse arbitrary pointer stars: e.g., (char*) or (int**)
			while (cur_tok.type == TOKEN_STAR) {
				match(TOKEN_STAR);
				DataType* base = (DataType*)calloc(1, sizeof(DataType));
				if (!base) { fprintf(stderr, "Out of memory.\n"); exit(1); }
				*base = cast_type;
				memset(&cast_type, 0, sizeof(cast_type));
				cast_type.kind = TYPE_POINTER;
				cast_type.base_type = base;
			}
			match(TOKEN_RPAREN);

			// Construct the cast node
			ASTNode* cast_node = create_node(AST_UNOP);
			cast_node->op = TOKEN_TYPEDEF; // Cast marker
			cast_node->type = cast_type;   // Target data type

			// Crucial change: Parse recursively as a unary expression 
			// so that *(type*)expr groups cleanly as *(AST_UNOP(type, expr))
			cast_node->left = parse_unary();
			cast_node->type = cast_type;
			return cast_node;
		}
	}
	// --- END OF CAST ENGINE ENGINE ---

	if (cur_tok.type == TOKEN_SIZEOF) {
		match(TOKEN_SIZEOF);

		int size_result = 0;
		match(TOKEN_LPAREN);

		// Case A: A primitive keyword or type alias name is passed (e.g., sizeof(int) or sizeof(PointType))
		if (is_type_name(cur_tok)) {
			DataType vt = parse_base_type();
			while (cur_tok.type == TOKEN_STAR) {
				match(TOKEN_STAR);
				DataType* base = (DataType*)calloc(1, sizeof(DataType));
				*base = vt;
				memset(&vt, 0, sizeof(vt));
				vt.kind = TYPE_POINTER;
				vt.base_type = base;
			}
			size_result = get_type_size(vt);
		}
		// Case B: An active expression or variable identifier is passed (e.g., sizeof(bob.age))
		else {
			ASTNode* expr_node = parse_expr();

			// If it's a plain variable identifier, pull details from the symbol table
			if (expr_node->kind == AST_IDENT) {
				Symbol* sym = lookup_symbol(expr_node->name);
				if (sym) {
					size_result = get_type_size(sym->type);
				}
				else {
					size_result = 4; // Fallback default
				}
			}
			// Handle struct member configurations (e.g., sizeof(bob.salary))
			else if (expr_node->kind == AST_MEMBER_ACCESS) {
				DataType out_type;
				int total_offset = 0;
				// Use your existing member offset resolver tool to capture the type
				resolve_member_offset(expr_node, &total_offset, &out_type);
				size_result = get_type_size(out_type);
			}
			else {
				// Fall back to evaluating the explicit evaluation node type context
				size_result = get_type_size(expr_node->type);
			}

			// Free the parsed expression sub-tree since it isn't compiled to assembly
			free(expr_node);
		}

		match(TOKEN_RPAREN);

		// LOWER TO AN AST_LITERAL: Convert the evaluated sizeof into a constant literal node!
		ASTNode* literal_node = create_node(AST_LITERAL);
		literal_node->type.kind = TYPE_INT;
		literal_node->int_val = size_result;

		return literal_node;
	}

	if (cur_tok.type == TOKEN_INC || cur_tok.type == TOKEN_DEC) {
		ASTNode* n = create_node(AST_UNOP);
		n->op = cur_tok.type;
		match(cur_tok.type);
		n->left = parse_unary();
		n->type = n->left->type;
		return n;
	}

	if (cur_tok.type == TOKEN_AMP || cur_tok.type == TOKEN_STAR ||
		cur_tok.type == TOKEN_MINUS || cur_tok.type == TOKEN_NOT ||
		cur_tok.type == TOKEN_BITNOT) {
		ASTNode* n = create_node(AST_UNOP);
		n->op = cur_tok.type;
		TokenType current_op = cur_tok.type;
		match(cur_tok.type);

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
			n->type = n->left->type;
		}
		else if (current_op == TOKEN_NOT || current_op == TOKEN_BITNOT) {
			n->type.kind = TYPE_INT;
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

		int left_is_ptr = (parent->left->type.kind == TYPE_POINTER || parent->left->type.base_type != NULL || parent->left->type.is_array);
		int right_is_ptr = (parent->right->type.kind == TYPE_POINTER || parent->right->type.base_type != NULL || parent->right->type.is_array);

		if (parent->left->type.kind == TYPE_FLOAT || parent->right->type.kind == TYPE_FLOAT) {
			parent->left = ensure_float_node(parent->left);
			parent->right = ensure_float_node(parent->right);
			parent->type.kind = TYPE_FLOAT;
		}
		else if (left_is_ptr && !right_is_ptr) {
			parent->type = parent->left->type;
		}
		else if (!left_is_ptr && right_is_ptr && parent->op == TOKEN_PLUS) {
			parent->type = parent->right->type;
		}
		else {
			parent->type.kind = TYPE_INT;
		}
		n = parent;
	}
	return n;
}

ASTNode* parse_shift(void) {
	ASTNode* n = parse_add();
	while (cur_tok.type == TOKEN_LSHIFT || cur_tok.type == TOKEN_RSHIFT) {
		ASTNode* parent = create_node(AST_BINOP);
		parent->op = cur_tok.type;
		parent->left = n;
		match(cur_tok.type);
		parent->right = parse_add();
		parent->type.kind = TYPE_INT;
		n = parent;
	}
	return n;
}

ASTNode* parse_relational(void) {
	ASTNode* n = parse_shift();
	while (cur_tok.type == TOKEN_LESS || cur_tok.type == TOKEN_GREATER ||
		cur_tok.type == TOKEN_LESS_EQUAL || cur_tok.type == TOKEN_GREATER_EQUAL ||
		cur_tok.type == TOKEN_EQUAL || cur_tok.type == TOKEN_NOT_EQUAL) {
		ASTNode* parent = create_node(AST_BINOP);
		parent->op = cur_tok.type;
		parent->left = n;
		match(cur_tok.type);
		parent->right = parse_shift();

		if (parent->left->type.kind == TYPE_FLOAT || parent->right->type.kind == TYPE_FLOAT) {
			parent->left = ensure_float_node(parent->left);
			parent->right = ensure_float_node(parent->right);
		}
		parent->type.kind = TYPE_INT;
		n = parent;
	}
	return n;
}

ASTNode* parse_bit_and(void) {
	ASTNode* n = parse_relational();
	while (cur_tok.type == TOKEN_AMP) {
		ASTNode* parent = create_node(AST_BINOP);
		parent->op = TOKEN_AMP;
		parent->left = n;
		match(TOKEN_AMP);
		parent->right = parse_relational();
		parent->type.kind = TYPE_INT;
		n = parent;
	}
	return n;
}

ASTNode* parse_bit_xor(void) {
	ASTNode* n = parse_bit_and();
	while (cur_tok.type == TOKEN_BITXOR) {
		ASTNode* parent = create_node(AST_BINOP);
		parent->op = TOKEN_BITXOR;
		parent->left = n;
		match(TOKEN_BITXOR);
		parent->right = parse_bit_and();
		parent->type.kind = TYPE_INT;
		n = parent;
	}
	return n;
}

ASTNode* parse_bit_or(void) {
	ASTNode* n = parse_bit_xor();
	while (cur_tok.type == TOKEN_BITOR) {
		ASTNode* parent = create_node(AST_BINOP);
		parent->op = TOKEN_BITOR;
		parent->left = n;
		match(TOKEN_BITOR);
		parent->right = parse_bit_xor();
		parent->type.kind = TYPE_INT;
		n = parent;
	}
	return n;
}

ASTNode* parse_log_and(void) {
	ASTNode* n = parse_bit_or();
	while (cur_tok.type == TOKEN_LOGAND) {
		ASTNode* parent = create_node(AST_BINOP);
		parent->op = cur_tok.type;
		parent->left = n;
		match(TOKEN_LOGAND);
		parent->right = parse_bit_or();
		parent->type.kind = TYPE_INT;
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
		parent->type.kind = TYPE_INT;
		n = parent;
	}
	return n;
}

ASTNode* parse_ternary(void) {
	ASTNode* cond = parse_log_or();
	if (cur_tok.type == TOKEN_QUESTION) {
		match(TOKEN_QUESTION);
		ASTNode* n = create_node(AST_TERNARY);
		n->cond = cond;
		n->left = parse_expr();       // True branch allows full expressions
		match(TOKEN_COLON);
		n->right = parse_ternary();   // Right-associative: a ? b : c ? d : e

		if (n->left->type.kind == TYPE_FLOAT || n->right->type.kind == TYPE_FLOAT) {
			n->left = ensure_float_node(n->left);
			n->right = ensure_float_node(n->right);
			n->type.kind = TYPE_FLOAT;
		}
		else {
			n->type = n->left->type;
		}
		return n;
	}
	return cond;
}

ASTNode* parse_expr(void) {
	ASTNode* n = parse_ternary();

	if (cur_tok.type == TOKEN_ASSIGN || cur_tok.type == TOKEN_ADD_ASSIGN ||
		cur_tok.type == TOKEN_SUB_ASSIGN || cur_tok.type == TOKEN_MUL_ASSIGN ||
		cur_tok.type == TOKEN_DIV_ASSIGN || cur_tok.type == TOKEN_MOD_ASSIGN ||
		cur_tok.type == TOKEN_AND_ASSIGN || cur_tok.type == TOKEN_OR_ASSIGN ||
		cur_tok.type == TOKEN_XOR_ASSIGN || cur_tok.type == TOKEN_LSHIFT_ASSIGN ||
		cur_tok.type == TOKEN_RSHIFT_ASSIGN) {

		TokenType op_type = cur_tok.type;
		match(op_type);

		ASTNode* right_expr = parse_expr();
		ASTNode* assignment_node = create_node(AST_ASSIGN);
		assignment_node->left = n;

		if (op_type == TOKEN_ASSIGN) {
			assignment_node->right = right_expr;
		}
		else {
			ASTNode* math_node = create_node(AST_BINOP);

			if (op_type == TOKEN_ADD_ASSIGN) math_node->op = TOKEN_PLUS;
			else if (op_type == TOKEN_SUB_ASSIGN) math_node->op = TOKEN_MINUS;
			else if (op_type == TOKEN_MUL_ASSIGN) math_node->op = TOKEN_STAR;
			else if (op_type == TOKEN_DIV_ASSIGN) math_node->op = TOKEN_SLASH;
			else if (op_type == TOKEN_MOD_ASSIGN) math_node->op = TOKEN_PERCENT;
			else if (op_type == TOKEN_AND_ASSIGN) math_node->op = TOKEN_AMP;
			else if (op_type == TOKEN_OR_ASSIGN) math_node->op = TOKEN_BITOR;
			else if (op_type == TOKEN_XOR_ASSIGN) math_node->op = TOKEN_BITXOR;
			else if (op_type == TOKEN_LSHIFT_ASSIGN) math_node->op = TOKEN_LSHIFT;
			else if (op_type == TOKEN_RSHIFT_ASSIGN) math_node->op = TOKEN_RSHIFT;

			math_node->left = n;
			math_node->right = right_expr;

			if (math_node->left->type.kind == TYPE_FLOAT || math_node->right->type.kind == TYPE_FLOAT) {
				math_node->left = ensure_float_node(math_node->left);
				math_node->right = ensure_float_node(math_node->right);
				math_node->type.kind = TYPE_FLOAT;
			}
			else if (math_node->left->type.kind == TYPE_POINTER || math_node->left->type.base_type != NULL) {
				math_node->type = math_node->left->type;
			}
			else if (math_node->right->type.kind == TYPE_POINTER || math_node->right->type.base_type != NULL) {
				math_node->type = math_node->right->type;
			}
			else {
				math_node->type.kind = TYPE_INT;
			}

			assignment_node->right = math_node;
		}

		assignment_node->type = n->type;
		n = assignment_node;
	}
	return n;
}

ASTNode* parse_struct_or_union_with_tag(int is_union, const char* name) {
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

	while (cur_tok.type != TOKEN_RBRACE && cur_tok.type != TOKEN_EOF) {
		// 1. Parse any valid base type (int, float, char, struct/union, typedef)
		DataType m_type = parse_base_type();

		// 2. Parse pointer levels (e.g., char* name; or struct Node* next;)
		while (cur_tok.type == TOKEN_STAR) {
			match(TOKEN_STAR);
			DataType* base = (DataType*)calloc(1, sizeof(DataType));
			if (!base) {
				fprintf(stderr, "Fatal Error: Out of memory during pointer member allocation.\n");
				exit(1);
			}
			*base = m_type;
			memset(&m_type, 0, sizeof(m_type));
			m_type.kind = TYPE_POINTER;
			m_type.base_type = base;
		}

		// 3. Extract the member name identifier
		char m_name[64];
		strcpy(m_name, cur_tok.lexeme);
		match(TOKEN_IDENT);

		// 4. Support fixed-size array members inside structs/unions
		while (cur_tok.type == TOKEN_LBRACKET) {
			match(TOKEN_LBRACKET);
			m_type.is_array = 1;
			m_type.dimensions[m_type.dim_count++] = cur_tok.int_val;
			match(TOKEN_INT_LIT);
			match(TOKEN_RBRACKET);
		}

		match(TOKEN_SEMI);

		int member_size = get_type_size(m_type);

		Member* m = (Member*)calloc(1, sizeof(Member));
		if (!m) {
			fprintf(stderr, "Fatal Error: Out of memory during member allocation.\n");
			exit(1);
		}
		strcpy(m->name, m_name);
		m->type = m_type;

		if (is_union) {
			m->offset = 0;
			if (member_size > total_size) {
				total_size = member_size;
			}
		}
		else {
			m->offset = current_offset;
			current_offset += member_size;
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

	return parse_struct_or_union_with_tag(is_union, name);
}

ASTNode* parse_program(void) {

	// Catch illegal file-scope storage classes early
	if (cur_tok.type == TOKEN_AUTO || cur_tok.type == TOKEN_REGISTER) {
		fprintf(stderr, "Semantic Error: '%s' storage class is not allowed at file scope.\n", cur_tok.lexeme);
		exit(1);
	}

	// 1. Reset localized stack tracking metrics for the incoming top-level item
	local_offset = 0;
	current_scope_level = 0;

	int is_file_static = 0;
	int is_file_extern = 0;
	int has_parsed_struct_decl_type = 0;
	DataType decl_type;
	memset(&decl_type, 0, sizeof(decl_type));
	DataTypeKind ret_kind = TYPE_INT;

	// 2. INTERNAL LOOP: Drain top-level type definitions (struct/union definitions & typedefs)
	// Also handles 'struct Tag var;' or 'extern struct Tag var;' cleanly.
	while (cur_tok.type == TOKEN_STRUCT || cur_tok.type == TOKEN_UNION ||
		cur_tok.type == TOKEN_TYPEDEF || cur_tok.type == TOKEN_STATIC || cur_tok.type == TOKEN_EXTERN) {

		if (cur_tok.type == TOKEN_TYPEDEF) {
			parse_stmt();
		}
		else if (cur_tok.type == TOKEN_STATIC || cur_tok.type == TOKEN_EXTERN) {
			if (is_file_static || is_file_extern) {
				fprintf(stderr, "Syntax Error: Multiple storage-class specifiers in declaration.\n");
				exit(1);
			}
			if (cur_tok.type == TOKEN_STATIC) {
				is_file_static = 1;
				match(TOKEN_STATIC);
			}
			else {
				is_file_extern = 1;
				match(TOKEN_EXTERN);
			}
			// If followed by struct/union, let the loop continue to parse the tag;
			// otherwise break out to parse the primitive/typedef base type.
			if (cur_tok.type != TOKEN_STRUCT && cur_tok.type != TOKEN_UNION) {
				break;
			}
		}
		else {
			int is_u = (cur_tok.type == TOKEN_UNION);
			match(cur_tok.type); // Consume 'struct' or 'union'

			char tag_name[64];
			strcpy(tag_name, cur_tok.lexeme);
			match(TOKEN_IDENT); // Consume tag name (e.g. 'Packet')

			if (cur_tok.type == TOKEN_LBRACE) {
				// It is a struct/union definition: struct Packet { ... };
				parse_struct_or_union_with_tag(is_u, tag_name);
				if (cur_tok.type == TOKEN_SEMI) {
					match(TOKEN_SEMI);
				}
			}
			else {
				// It is a global/extern/static variable or function using a struct/union tag
				decl_type.kind = is_u ? TYPE_UNION : TYPE_STRUCT;
				strcpy(decl_type.struct_name, tag_name);
				ret_kind = decl_type.kind;
				has_parsed_struct_decl_type = 1;
				break;
			}
		}
	}

	// 3. ROBUST RECOVERY: Catch EOF or trailing text debris cleanly
	if (!has_parsed_struct_decl_type) {
		if (cur_tok.type == TOKEN_EOF) {
			return create_node(AST_PROGRAM);
		}

		if (!is_type_name(cur_tok) && cur_tok.type != TOKEN_VOID) {
			ASTNode* eof_node = create_node(AST_PROGRAM);
			cur_tok.type = TOKEN_EOF;
			return eof_node;
		}

		// 4. Parse the base type (void, int, float, char, struct/union, or typedef alias)
		if (cur_tok.type == TOKEN_VOID) {
			ret_kind = TYPE_INT;
			decl_type.kind = TYPE_INT;
			match(TOKEN_VOID);
		}
		else {
			decl_type = parse_base_type();
			ret_kind = decl_type.kind;
		}
	}

	// 5. Parse pointer asterisks (e.g., int* global_ptr; or extern struct Packet* g_ptr;)
	while (cur_tok.type == TOKEN_STAR) {
		match(TOKEN_STAR);
		DataType* base = (DataType*)calloc(1, sizeof(DataType));
		if (!base) { fprintf(stderr, "Fatal Error: Out of memory.\n"); exit(1); }
		*base = decl_type;
		memset(&decl_type, 0, sizeof(decl_type));
		decl_type.kind = TYPE_POINTER;
		decl_type.base_type = base;
		ret_kind = TYPE_POINTER;
	}

	// 6. Capture the identifier name (global variable name or function name)
	char name[64];
	strcpy(name, cur_tok.lexeme);
	match(TOKEN_IDENT);

	// 7. BRANCH A: GLOBAL / STATIC / EXTERN VARIABLE DECLARATION (not followed by '(')
	if (cur_tok.type != TOKEN_LPAREN) {
		// Parse optional array dimensions (e.g., extern int g_arr[10]; or char g_chars[5];)
		while (cur_tok.type == TOKEN_LBRACKET) {
			match(TOKEN_LBRACKET);
			decl_type.is_array = 1;
			decl_type.dimensions[decl_type.dim_count++] = cur_tok.int_val;
			match(TOKEN_INT_LIT);
			match(TOKEN_RBRACKET);
		}

		// Parse optional constant initializer (disallowed on pure extern declarations)
		int has_init = 0;
		int init_int = 0;
		double init_float = 0.0;

		if (cur_tok.type == TOKEN_ASSIGN) {
			if (is_file_extern) {
				fprintf(stderr, "Semantic Error: Extern variable '%s' cannot have an initializer.\n", name);
				exit(1);
			}
			match(TOKEN_ASSIGN);
			has_init = 1;

			int sign = 1;
			if (cur_tok.type == TOKEN_MINUS) {
				sign = -1;
				match(TOKEN_MINUS);
			}

			if (cur_tok.type == TOKEN_INT_LIT) {
				init_int = sign * cur_tok.int_val;
				init_float = (double)init_int;
				match(TOKEN_INT_LIT);
			}
			else if (cur_tok.type == TOKEN_FLOAT_LIT) {
				init_float = sign * cur_tok.float_val;
				init_int = (int)init_float;
				match(TOKEN_FLOAT_LIT);
			}
			else {
				fprintf(stderr, "Semantic Error: Global variable '%s' requires a constant initializer.\n", name);
				exit(1);
			}
		}

		match(TOKEN_SEMI);

		// Avoid duplicate symbol entries if a header with 'extern int x;' is included multiple times
		Symbol* existing = lookup_symbol(name);
		if (existing && existing->is_global_var && existing->scope_level == 0) {
			if (!is_file_extern && existing->is_extern) {
				// Upgrade prior extern declaration to actual definition in this translation unit
				existing->is_extern = 0;
				existing->is_static = is_file_static;
				existing->has_init = has_init;
				existing->init_int = init_int;
				existing->init_float = init_float;
				existing->type = decl_type;
			}
			ASTNode* var_node = create_node(AST_VAR_DECL);
			strcpy(var_node->name, name);
			return var_node;
		}

		// Register the global/static/extern variable in the persistent symbol table
		Symbol* g_sym = (Symbol*)calloc(1, sizeof(Symbol));
		if (!g_sym) { fprintf(stderr, "Fatal Error: Out of memory.\n"); exit(1); }
		strcpy(g_sym->name, name);
		strcpy(g_sym->asm_label, name);
		g_sym->type = decl_type;
		g_sym->is_local = 0;
		g_sym->is_static = is_file_static;
		g_sym->is_extern = is_file_extern;
		g_sym->is_global_var = 1;
		g_sym->has_init = has_init;
		g_sym->init_int = init_int;
		g_sym->init_float = init_float;
		g_sym->scope_level = 0;
		g_sym->next = sym_table;
		sym_table = g_sym;

		// Return a variable declaration node (emitted later by emit_global_variables() or emit_external_declarations())
		ASTNode* var_node = create_node(AST_VAR_DECL);
		strcpy(var_node->name, name);
		return var_node;
	}

	// 8. BRANCH B: FUNCTION PROTOTYPE OR DEFINITION
	strcpy(current_func_name, name);
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

		int param_is_register = 0;
		if (cur_tok.type == TOKEN_REGISTER) {
			param_is_register = 1;
			match(TOKEN_REGISTER);
		}
		else if (cur_tok.type == TOKEN_AUTO || cur_tok.type == TOKEN_STATIC || cur_tok.type == TOKEN_EXTERN) {
			fprintf(stderr, "Semantic Error: Invalid storage class '%s' on parameter.\n", cur_tok.lexeme);
			exit(1);
		}

		DataType pt = parse_base_type();

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
		match(TOKEN_IDENT);

		Symbol* sym = (Symbol*)calloc(1, sizeof(Symbol));
		if (!sym) { fprintf(stderr, "Out of memory.\n"); exit(1); }
		strcpy(sym->name, p_name);
		sym->type = pt;
		sym->is_local = 1;
		sym->offset = arg_off;
		sym->scope_level = current_scope_level;
		sym->is_register = param_is_register;
		arg_off += 4;

		sym->next = sym_table;
		sym_table = sym;

		if (cur_tok.type == TOKEN_COMMA) {
			match(TOKEN_COMMA);
		}
	}
	match(TOKEN_RPAREN);

	// 9. CRITICAL HEADER PROTOTYPE LOOKAHEAD CHECK
	if (cur_tok.type == TOKEN_SEMI) {
		match(TOKEN_SEMI);

		// Wipe parameter symbols since there is no function body
		void clear_local_symbols(void);
		clear_local_symbols();

		// Avoid duplicate extern entries if a prototype is declared more than once
		Symbol* existing = lookup_symbol(name);
		if (existing && !existing->is_global_var && !existing->is_local) {
			return create_node(AST_VAR_DECL);
		}

		Symbol* func_sym = (Symbol*)calloc(1, sizeof(Symbol));
		if (!func_sym) { fprintf(stderr, "Out of memory.\n"); exit(1); }
		strcpy(func_sym->name, name);
		func_sym->type.kind = ret_kind;
		func_sym->is_local = 0;
		func_sym->is_static = is_file_static;
		func_sym->is_extern = 1;
		func_sym->is_variadic = is_var;
		func_sym->next = sym_table;
		sym_table = func_sym;

		return create_node(AST_VAR_DECL);
	}

	// 10. FUNCTION DEFINITION WITH BODY
	Symbol* existing = lookup_symbol(name);
	if (existing && !existing->is_global_var && !existing->is_local) {
		// Upgrade a prior forward declaration / extern prototype to a local definition
		existing->is_extern = 0;
		existing->is_static = is_file_static;
		existing->type.kind = ret_kind;
		existing->is_variadic = is_var;
	}
	else {
		Symbol* func_sym = (Symbol*)calloc(1, sizeof(Symbol));
		if (!func_sym) { fprintf(stderr, "Out of memory.\n"); exit(1); }
		strcpy(func_sym->name, name);
		func_sym->type.kind = ret_kind;
		func_sym->is_local = 0;
		func_sym->is_static = is_file_static;
		func_sym->is_extern = 0;
		func_sym->is_variadic = is_var;
		func_sym->next = sym_table;
		sym_table = func_sym;
	}

	ASTNode* n = create_node(AST_FUNC_DECL);
	strcpy(n->name, name);
	n->body = parse_stmt();

	// Align local variable stack allocation to 16-byte boundary
	n->int_val = ((max_function_stack_bytes + 15) & ~15) + 16;

	// Reset trackers for the next top-level declaration
	max_function_stack_bytes = 64;
	strcpy(current_func_name, "global");

	return n;
}

ASTNode* parse_stmt(void) {
	// 1. HANDLE TYPEDEF REGISTRATION DIRECTLY
	if (cur_tok.type == TOKEN_TYPEDEF) {
		match(TOKEN_TYPEDEF);

		// Parse the base type definition layer
		DataType base_t = parse_base_type();

		// Handle potential pointer symbols linked to the alias (e.g. typedef int* int_ptr;)
		while (cur_tok.type == TOKEN_STAR) {
			match(TOKEN_STAR);
			DataType* p_base = (DataType*)calloc(1, sizeof(DataType));
			*p_base = base_t;
			memset(&base_t, 0, sizeof(base_t));
			base_t.kind = TYPE_POINTER;
			base_t.base_type = p_base;
		}

		// Capture new alias identifier name
		char alias_name[64];
		strcpy(alias_name, cur_tok.lexeme);
		match(TOKEN_IDENT);
		match(TOKEN_SEMI);

		// Register the new type entry inside the compiler symbol table
		Symbol* sym = (Symbol*)calloc(1, sizeof(Symbol));
		strcpy(sym->name, alias_name);
		sym->type = base_t;
		sym->is_typedef = 1; // Explicitly tag as type definition
		sym->scope_level = current_scope_level;
		sym->next = sym_table;
		sym_table = sym;

		// Return a structural placeholder variable decl that codegen ignores safely
		return create_node(AST_VAR_DECL);
	}

	// 2. REWRITE VARIABLE DECLARATION DETECTION BY USING OUR TYPE NAME HELPER
	// 2. LOCAL DECLARATIONS: static, extern, auto, register, or type name
	if (cur_tok.type == TOKEN_STATIC || cur_tok.type == TOKEN_EXTERN ||
		cur_tok.type == TOKEN_AUTO || cur_tok.type == TOKEN_REGISTER || is_type_name(cur_tok)) {

		int is_stat = 0;
		int is_ext = 0;
		int is_reg = 0;
		int is_auto = 0;

		if (cur_tok.type == TOKEN_STATIC) { is_stat = 1; match(TOKEN_STATIC); }
		else if (cur_tok.type == TOKEN_EXTERN) { is_ext = 1; match(TOKEN_EXTERN); }
		else if (cur_tok.type == TOKEN_AUTO) { is_auto = 1; match(TOKEN_AUTO); }
		else if (cur_tok.type == TOKEN_REGISTER) { is_reg = 1; match(TOKEN_REGISTER); }

		if (cur_tok.type == TOKEN_STATIC || cur_tok.type == TOKEN_EXTERN ||
			cur_tok.type == TOKEN_AUTO || cur_tok.type == TOKEN_REGISTER) {
			fprintf(stderr, "Syntax Error: Multiple storage-class specifiers in declaration.\n");
			exit(1);
		}

		DataType vt = parse_base_type();

		while (cur_tok.type == TOKEN_STAR) {
			match(TOKEN_STAR);
			DataType* base = (DataType*)calloc(1, sizeof(DataType));
			if (!base) { fprintf(stderr, "Fatal Error: Out of memory.\n"); exit(1); }
			*base = vt;
			memset(&vt, 0, sizeof(vt));
			vt.kind = TYPE_POINTER;
			vt.base_type = base;
		}

		char name[64];
		strcpy(name, cur_tok.lexeme);
		match(TOKEN_IDENT);

		// Block-scope function prototype: e.g., extern int helper(int x);
		if (cur_tok.type == TOKEN_LPAREN) {
			if (!is_ext) {
				fprintf(stderr, "Syntax Error: Nested function definition '%s' is not allowed.\n", name);
				exit(1);
			}
			match(TOKEN_LPAREN);
			int is_var = 0;
			while (cur_tok.type != TOKEN_RPAREN) {
				if (cur_tok.type == TOKEN_ELLIPSIS) {
					is_var = 1;
					match(TOKEN_ELLIPSIS);
					break;
				}
				if (cur_tok.type == TOKEN_REGISTER) match(TOKEN_REGISTER);
				parse_base_type();
				while (cur_tok.type == TOKEN_STAR) match(TOKEN_STAR);
				if (cur_tok.type == TOKEN_IDENT) match(TOKEN_IDENT);
				if (cur_tok.type == TOKEN_COMMA) match(TOKEN_COMMA);
			}
			match(TOKEN_RPAREN);
			match(TOKEN_SEMI);

			Symbol* existing = lookup_symbol(name);
			if (!existing) {
				Symbol* func_sym = (Symbol*)calloc(1, sizeof(Symbol));
				strcpy(func_sym->name, name);
				func_sym->type.kind = vt.kind;
				func_sym->is_local = 0;
				func_sym->is_extern = 1;
				func_sym->is_variadic = is_var;
				func_sym->scope_level = 0;
				func_sym->next = sym_table;
				sym_table = func_sym;
			}
			return create_node(AST_VAR_DECL);
		}

		int total = 1;
		while (cur_tok.type == TOKEN_LBRACKET) {
			match(TOKEN_LBRACKET);
			vt.is_array = 1;
			vt.dimensions[vt.dim_count++] = cur_tok.int_val;
			total *= cur_tok.int_val;
			match(TOKEN_INT_LIT);
			match(TOKEN_RBRACKET);
		}

		int has_init = 0;
		int init_int = 0;
		double init_float = 0.0;
		ASTNode* dyn_init_expr = NULL;

		if (cur_tok.type == TOKEN_ASSIGN) {
			if (is_ext) {
				fprintf(stderr, "Semantic Error: Block-scope extern '%s' cannot have an initializer.\n", name);
				exit(1);
			}
			match(TOKEN_ASSIGN);
			has_init = 1;
			if (is_stat) {
				int sign = 1;
				if (cur_tok.type == TOKEN_MINUS) { sign = -1; match(TOKEN_MINUS); }
				if (cur_tok.type == TOKEN_INT_LIT) {
					init_int = sign * cur_tok.int_val;
					init_float = (double)init_int;
					match(TOKEN_INT_LIT);
				}
				else if (cur_tok.type == TOKEN_FLOAT_LIT) {
					init_float = sign * cur_tok.float_val;
					init_int = (int)init_float;
					match(TOKEN_FLOAT_LIT);
				}
				else {
					fprintf(stderr, "Semantic Error: Static initializer must be constant.\n");
					exit(1);
				}
			}
			else {
				dyn_init_expr = parse_expr();
			}
		}

		match(TOKEN_SEMI);

		// Handle block-scope extern variable
		if (is_ext) {
			Symbol* existing = lookup_symbol(name);
			if (existing && existing->is_global_var && existing->scope_level == 0) {
				// Already declared/defined at file scope; reuse it cleanly
				ASTNode* n = create_node(AST_VAR_DECL);
				strcpy(n->name, name);
				return n;
			}
			Symbol* ext_sym = (Symbol*)calloc(1, sizeof(Symbol));
			strcpy(ext_sym->name, name);
			strcpy(ext_sym->asm_label, name);
			ext_sym->type = vt;
			ext_sym->is_local = 0;
			ext_sym->is_extern = 1;
			ext_sym->is_global_var = 1;
			ext_sym->scope_level = 0; // Persistent across function exit so emit_external_declarations() sees it
			ext_sym->next = sym_table;
			sym_table = ext_sym;

			ASTNode* n = create_node(AST_VAR_DECL);
			strcpy(n->name, name);
			return n;
		}

		int var_size = 4;
		if (vt.kind == TYPE_CHAR && vt.base_type == NULL) {
			var_size = 1;
		}
		else if ((vt.kind == TYPE_STRUCT || vt.kind == TYPE_UNION) && vt.base_type == NULL) {
			struct StructEnv* env = lookup_struct(vt.struct_name);
			if (env) var_size = env->size;
			else { exit(1); }
		}

		Symbol* sym = (Symbol*)calloc(1, sizeof(Symbol));
		strcpy(sym->name, name);
		sym->type = vt;
		sym->scope_level = current_scope_level;
		sym->is_register = is_reg;

		if (is_stat) {
			sym->is_local = 0;
			sym->is_static = 1;
			sym->is_global_var = 1;
			sym->has_init = has_init;
			sym->init_int = init_int;
			sym->init_float = init_float;
			sprintf(sym->asm_label, "static_%s_%s_%d", current_func_name, name, static_var_counter++);
		}
		else {
			local_offset += (var_size * total);
			if (local_offset > max_function_stack_bytes) {
				max_function_stack_bytes = local_offset;
			}
			sym->is_local = 1;
			sym->offset = -local_offset;
		}

		sym->next = sym_table;
		sym_table = sym;

		if (!is_stat && dyn_init_expr != NULL) {
			ASTNode* id_node = create_node(AST_IDENT);
			strcpy(id_node->name, name);
			id_node->type = vt;

			ASTNode* assign_node = create_node(AST_ASSIGN);
			assign_node->left = id_node;
			assign_node->right = dyn_init_expr;
			assign_node->type = vt;
			return assign_node;
		}

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

int get_type_size(DataType type) {
	// If it's a pointer type, it's always 4 bytes in a 32-bit x86 architecture
	if (type.kind == TYPE_POINTER || type.base_type != NULL) {
		return 4;
	}

	int base_size = 4; // Default baseline size for int/float
	if (type.kind == TYPE_CHAR) {
		base_size = 1;
	}
	else if (type.kind == TYPE_STRUCT) {
		struct StructEnv* env = lookup_struct(type.struct_name);
		if (env) {
			base_size = env->size;
		}
		else {
			fprintf(stderr, "Semantic Error: Sizeof undefined struct '%s'\n", type.struct_name);
			exit(1);
		}
	}
	else if (type.kind == TYPE_UNION) {
		struct StructEnv* env = lookup_struct(type.struct_name);
		if (env) {
			base_size = env->size;
		}
		else {
			fprintf(stderr, "Semantic Error: Sizeof undefined union '%s'\n", type.struct_name);
			exit(1);
		}
	}

	// If it's an array, multiply the base size by all dimensions
	if (type.is_array) {
		int total_elements = 1;
		for (int i = 0; i < type.dim_count; i++) {
			total_elements *= type.dimensions[i];
		}
		return base_size * total_elements;
	}

	return base_size;
}
