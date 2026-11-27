#include "cc.h"

static char current_func_name[64] = "global";
static int static_var_counter = 0;

int max_function_stack_bytes = 64; // Fallback minimum size boundary

Token cur_tok;
Symbol* sym_table = NULL;
StructEnv* struct_table = NULL;
int local_offset = 0;

int current_scope_level = 0;

static int anon_tag_counter = 0;

void match(TokenType type);
ASTNode* parse_expr(void);
static ASTNode* ensure_float_node(ASTNode* node);
static int parse_array_bound(void);

static void append_init_elem(InitElem** list, int* count, int* cap, InitElem elem) {
	if (*count >= *cap) {
		*cap = (*cap == 0) ? 16 : (*cap * 2);
		*list = (InitElem*)realloc(*list, sizeof(InitElem) * (*cap));
		if (!*list) { fprintf(stderr, "Fatal Error: Out of memory.\n"); exit(1); }
	}
	(*list)[(*count)++] = elem;
}

// Parses '{ ... }' or single scalar/string initializer into either:
//   - Flat InitElem array (for global/static .data storage, is_static_storage = 1)
//   - AST_ASSIGN statements (for local stack storage, is_static_storage = 0)
static void parse_init_recursive(
	DataType type,
	int byte_offset,
	int is_static_storage,
	InitElem** elems, int* elem_cnt, int* elem_cap,
	ASTNode* root_ident,
	ASTNode*** stmt_tail
) {
	// 1. Char array initialized by string literal: char buf[6] = "abc"; or char buf[] = "abc";
	if (type.is_array && type.kind == TYPE_CHAR && type.base_type == NULL && cur_tok.type == TOKEN_STRING) {
		int str_len = cur_tok.int_val;
		int total_bytes = (type.dimensions[0] == 0) ? (str_len + 1) : get_type_size(type);
		for (int i = 0; i < total_bytes; i++) {
			int ch_val = (i < str_len) ? (unsigned char)cur_tok.lexeme[i] : 0;
			if (is_static_storage) {
				InitElem e = { byte_offset + i, 1, 0, ch_val, 0.0 };
				append_init_elem(elems, elem_cnt, elem_cap, e);
			}
			else {
				ASTNode* idx = create_node(AST_LITERAL);
				idx->type.kind = TYPE_INT; idx->int_val = byte_offset + i;
				ASTNode* acc = create_node(AST_ARRAY_ACCESS);
				acc->left = root_ident; acc->right = idx; acc->type.kind = TYPE_CHAR;
				ASTNode* val = create_node(AST_LITERAL);
				val->type.kind = TYPE_INT; val->int_val = ch_val;
				ASTNode* asg = create_node(AST_ASSIGN);
				asg->left = acc; asg->right = val; asg->type.kind = TYPE_CHAR;
				**stmt_tail = asg; *stmt_tail = &(asg->next);
			}
		}
		match(TOKEN_STRING);
		return;
	}

	// 2. Array type (1D or multi-dimensional, fixed or unsized outer bound)
	if (type.is_array) {
		int has_braces = (cur_tok.type == TOKEN_LBRACE);
		if (has_braces) match(TOKEN_LBRACE);

		int unsized = (type.dimensions[0] == 0);
		int count = type.dimensions[0];
		DataType sub_type = type;
		if (sub_type.dim_count > 1) {
			for (int d = 0; d < sub_type.dim_count - 1; d++)
				sub_type.dimensions[d] = sub_type.dimensions[d + 1];
			sub_type.dim_count--;
		}
		else {
			sub_type.is_array = 0;
			sub_type.dim_count = 0;
		}
		int stride = get_type_size(sub_type);

		int i = 0;
		while (unsized || i < count) {
			if (has_braces && cur_tok.type == TOKEN_RBRACE) {
				if (!unsized) {
					int rem_bytes = (count - i) * stride;
					if (is_static_storage) {
						for (int b = 0; b < rem_bytes; b++) {
							InitElem z = { byte_offset + i * stride + b, 1, 0, 0, 0.0 };
							append_init_elem(elems, elem_cnt, elem_cap, z);
						}
					}
				}
				break;
			}
			parse_init_recursive(sub_type, byte_offset + i * stride, is_static_storage,
				elems, elem_cnt, elem_cap, root_ident, stmt_tail);
			i++;
			if (cur_tok.type == TOKEN_COMMA) {
				match(TOKEN_COMMA);
				if (has_braces && cur_tok.type == TOKEN_RBRACE) break;
			}
			else if (!has_braces) {
				break;
			}
		}

		if (has_braces) {
			if (cur_tok.type != TOKEN_RBRACE) {
				fprintf(stderr, "Semantic Error: Too many initializers for array.\n");
				exit(1);
			}
			match(TOKEN_RBRACE);
		}
		return;
	}

	// 3. Struct or Union type
	if ((type.kind == TYPE_STRUCT || type.kind == TYPE_UNION) && type.base_type == NULL) {
		struct StructEnv* env = lookup_struct(type.struct_name);
		if (!env) { fprintf(stderr, "Semantic Error: Undefined struct/union '%s'.\n", type.struct_name); exit(1); }

		int has_braces = (cur_tok.type == TOKEN_LBRACE);
		if (has_braces) match(TOKEN_LBRACE);

		Member* m = env->members;
		while (m) {
			if (has_braces && cur_tok.type == TOKEN_RBRACE) break;
			parse_init_recursive(m->type, byte_offset + m->offset, is_static_storage,
				elems, elem_cnt, elem_cap, root_ident, stmt_tail);
			if (type.kind == TYPE_UNION) break; // C89 initializes first union member only
			m = m->next;
			if (m && cur_tok.type == TOKEN_COMMA) {
				match(TOKEN_COMMA);
				if (has_braces && cur_tok.type == TOKEN_RBRACE) break;
			}
			else if (!has_braces) {
				break;
			}
		}

		if (has_braces) {
			if (cur_tok.type != TOKEN_RBRACE) {
				fprintf(stderr, "Semantic Error: Excess elements in struct/union initializer.\n");
				exit(1);
			}
			match(TOKEN_RBRACE);
		}
		return;
	}

	// 4. Scalar leaf (int, char, float, pointer)
	int optional_brace = (cur_tok.type == TOKEN_LBRACE);
	if (optional_brace) match(TOKEN_LBRACE);

	if (is_static_storage) {
		int sign = 1;
		if (cur_tok.type == TOKEN_MINUS) { sign = -1; match(TOKEN_MINUS); }
		InitElem e = { 0 };
		e.offset = byte_offset;
		e.size = (type.kind == TYPE_CHAR && type.base_type == NULL) ? 1 : 4;

		if (cur_tok.type == TOKEN_INT_LIT) {
			e.int_val = sign * cur_tok.int_val;
			e.float_val = (double)e.int_val;
			e.is_float = (type.kind == TYPE_FLOAT && type.base_type == NULL);
			match(TOKEN_INT_LIT);
		}
		else if (cur_tok.type == TOKEN_FLOAT_LIT) {
			e.float_val = sign * cur_tok.float_val;
			e.int_val = (int)e.float_val;
			e.is_float = (type.kind == TYPE_FLOAT && type.base_type == NULL);
			match(TOKEN_FLOAT_LIT);
		}
		else if (cur_tok.type == TOKEN_IDENT) {
			Symbol* sym = lookup_symbol(cur_tok.lexeme);
			if (sym && sym->is_enum_const) {
				e.int_val = sign * sym->init_int;
				e.float_val = (double)e.int_val;
				match(TOKEN_IDENT);
			}
			else {
				fprintf(stderr, "Semantic Error: Non-constant initializer '%s'.\n", cur_tok.lexeme);
				exit(1);
			}
		}
		else {
			fprintf(stderr, "Semantic Error: Constant scalar initializer expected.\n");
			exit(1);
		}
		append_init_elem(elems, elem_cnt, elem_cap, e);
	}
	else {
		// Local stack variable: assign *(type*)((char*)&root_ident + byte_offset) = expr
		ASTNode* rhs = parse_expr();
		if (type.kind == TYPE_FLOAT && type.base_type == NULL) rhs = ensure_float_node(rhs);

		ASTNode* addr = create_node(AST_UNOP);
		addr->op = TOKEN_AMP; addr->left = root_ident;
		addr->type = make_pointer_to(root_ident->type);

		if (byte_offset > 0) {
			ASTNode* cast_char_p = create_node(AST_UNOP);
			cast_char_p->op = TOKEN_TYPEDEF;
			DataType cp = { 0 }; cp.kind = TYPE_CHAR;
			cast_char_p->type = make_pointer_to(cp);
			cast_char_p->left = addr;

			ASTNode* off_lit = create_node(AST_LITERAL);
			off_lit->type.kind = TYPE_INT; off_lit->int_val = byte_offset;

			ASTNode* add = create_node(AST_BINOP);
			add->op = TOKEN_PLUS; add->left = cast_char_p; add->right = off_lit;
			add->type = cast_char_p->type;
			addr = add;
		}

		ASTNode* deref = create_node(AST_UNOP);
		deref->op = TOKEN_STAR; deref->left = addr; deref->type = type;

		ASTNode* asg = create_node(AST_ASSIGN);
		asg->left = deref; asg->right = rhs; asg->type = type;
		**stmt_tail = asg; *stmt_tail = &(asg->next);
	}

	if (optional_brace) match(TOKEN_RBRACE);
}

static void parse_array_dimensions(DataType* vt) {
	while (cur_tok.type == TOKEN_LBRACKET) {
		match(TOKEN_LBRACKET);
		vt->is_array = 1;
		if (cur_tok.type == TOKEN_RBRACKET) {
			if (vt->dim_count != 0) {
				fprintf(stderr, "Syntax Error: Only the first array dimension may be omitted.\n");
				exit(1);
			}
			vt->dimensions[vt->dim_count++] = 0; // Deduced from initializer
		}
		else {
			vt->dimensions[vt->dim_count++] = parse_array_bound();
		}
		match(TOKEN_RBRACKET);
	}
}

// Emits zero-initialization assignments for every 4-byte or 1-byte slot of a local aggregate
static void emit_local_zero_fill(ASTNode* root_ident, int total_bytes, ASTNode*** stmt_tail) {
	int offset = 0;
	while (offset < total_bytes) {
		int chunk = (total_bytes - offset >= 4) ? 4 : 1;
		DataType chunk_type = { 0 };
		chunk_type.kind = (chunk == 4) ? TYPE_INT : TYPE_CHAR;

		ASTNode* addr = create_node(AST_UNOP);
		addr->op = TOKEN_AMP;
		addr->left = root_ident;
		addr->type = make_pointer_to(root_ident->type);

		if (offset > 0) {
			ASTNode* cast_cp = create_node(AST_UNOP);
			cast_cp->op = TOKEN_TYPEDEF;
			DataType cp = { 0 }; cp.kind = TYPE_CHAR;
			cast_cp->type = make_pointer_to(cp);
			cast_cp->left = addr;

			ASTNode* off_lit = create_node(AST_LITERAL);
			off_lit->type.kind = TYPE_INT;
			off_lit->int_val = offset;

			ASTNode* add = create_node(AST_BINOP);
			add->op = TOKEN_PLUS;
			add->left = cast_cp;
			add->right = off_lit;
			add->type = cast_cp->type;
			addr = add;
		}

		ASTNode* deref = create_node(AST_UNOP);
		deref->op = TOKEN_STAR;
		deref->left = addr;
		deref->type = chunk_type;

		ASTNode* zero = create_node(AST_LITERAL);
		zero->type.kind = TYPE_INT;
		zero->int_val = 0;

		ASTNode* asg = create_node(AST_ASSIGN);
		asg->left = deref;
		asg->right = zero;
		asg->type = chunk_type;

		**stmt_tail = asg;
		*stmt_tail = &(asg->next);
		offset += chunk;
	}
}

static void register_enum_constant(const char* name, int value) {
	Symbol* sym = (Symbol*)calloc(1, sizeof(Symbol));
	if (!sym) { fprintf(stderr, "Fatal Error: Out of memory.\n"); exit(1); }
	strcpy(sym->name, name);
	sym->type.kind = TYPE_INT;
	sym->is_enum_const = 1;
	sym->init_int = value;
	sym->scope_level = current_scope_level;
	sym->next = sym_table;
	sym_table = sym;
}

// Parses: enum [Tag] { IDENT [= [-]INT], ... }
// Expects TOKEN_ENUM to have already been matched.
static DataType parse_enum_specifier(void) {
	if (cur_tok.type == TOKEN_IDENT) {
		match(TOKEN_IDENT); // Optional enum tag name
	}

	if (cur_tok.type == TOKEN_LBRACE) {
		match(TOKEN_LBRACE);
		int next_val = 0;

		while (cur_tok.type != TOKEN_RBRACE && cur_tok.type != TOKEN_EOF) {
			char const_name[64];
			strcpy(const_name, cur_tok.lexeme);
			match(TOKEN_IDENT);

			if (cur_tok.type == TOKEN_ASSIGN) {
				match(TOKEN_ASSIGN);
				int sign = 1;
				if (cur_tok.type == TOKEN_MINUS) {
					sign = -1;
					match(TOKEN_MINUS);
				}
				if (cur_tok.type == TOKEN_INT_LIT) {
					next_val = sign * cur_tok.int_val;
					match(TOKEN_INT_LIT);
				}
				else if (cur_tok.type == TOKEN_IDENT) {
					// Allow referencing an earlier enum constant: e.g. B = A + 1
					Symbol* ref = lookup_symbol(cur_tok.lexeme);
					if (ref && ref->is_enum_const) {
						next_val = sign * ref->init_int;
						match(TOKEN_IDENT);
					}
					else {
						fprintf(stderr, "Semantic Error: Enum initializer '%s' is not a constant.\n", cur_tok.lexeme);
						exit(1);
					}
				}
				else {
					fprintf(stderr, "Syntax Error: Expected integer constant in enum '%s'.\n", const_name);
					exit(1);
				}
			}

			register_enum_constant(const_name, next_val);
			next_val++;

			if (cur_tok.type == TOKEN_COMMA) {
				match(TOKEN_COMMA);
				// Allow trailing comma before '}' (C99/common C89 extension used in cc.h)
				if (cur_tok.type == TOKEN_RBRACE) break;
			}
			else {
				break;
			}
		}
		match(TOKEN_RBRACE);
	}

	DataType dt;
	memset(&dt, 0, sizeof(dt));
	dt.kind = TYPE_INT;
	return dt;
}

static int is_type_name(Token tok) {
	if (tok.type == TOKEN_CHAR) return 1;
	if (tok.type == TOKEN_STRUCT || tok.type == TOKEN_UNION || tok.type == TOKEN_ENUM) return 1;
	if (tok.type == TOKEN_IDENT) {
		if (strcmp(tok.lexeme, "int") == 0 || strcmp(tok.lexeme, "float") == 0) {
			return 1;
		}
		Symbol* sym = lookup_symbol(tok.lexeme);
		if (sym && sym->is_typedef) {
			return 1;
		}
	}
	return 0;
}

static DataType parse_base_type(void) {
	DataType vt;
	memset(&vt, 0, sizeof(vt));

	if (cur_tok.type == TOKEN_ENUM) {
		match(TOKEN_ENUM);
		return parse_enum_specifier();
	}
	else if (cur_tok.type == TOKEN_STRUCT || cur_tok.type == TOKEN_UNION) {
		int is_u = (cur_tok.type == TOKEN_UNION);
		match(cur_tok.type);

		char tag_name[64];
		if (cur_tok.type == TOKEN_IDENT) {
			strcpy(tag_name, cur_tok.lexeme);
			match(TOKEN_IDENT);
		}
		else {
			// Anonymous struct/union: e.g., typedef struct { ... } Token;
			snprintf(tag_name, sizeof(tag_name), "__anon_tag_%d", anon_tag_counter++);
		}

		// Inline body definition: e.g., typedef struct ASTNode { ... } ASTNode;
		if (cur_tok.type == TOKEN_LBRACE) {
			ASTNode* decl_ast = parse_struct_or_union_with_tag(is_u, tag_name);
			free(decl_ast);
		}

		strcpy(vt.struct_name, tag_name);
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
			Symbol* sym = lookup_symbol(cur_tok.lexeme);
			if (sym && sym->is_typedef) {
				vt = sym->type;
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

DataType make_pointer_to(DataType target) {
	DataType* base = (DataType*)calloc(1, sizeof(DataType));
	if (!base) { fprintf(stderr, "Fatal Error: Out of memory.\n"); exit(1); }
	*base = target;
	DataType ptr;
	memset(&ptr, 0, sizeof(ptr));
	ptr.kind = TYPE_POINTER;
	ptr.base_type = base;
	return ptr;
}

static DataType make_func_returning(DataType ret_type) {
	DataType* ret = (DataType*)calloc(1, sizeof(DataType));
	if (!ret) { fprintf(stderr, "Fatal Error: Out of memory.\n"); exit(1); }
	*ret = ret_type;
	DataType fn;
	memset(&fn, 0, sizeof(fn));
	fn.kind = TYPE_FUNC;
	fn.base_type = ret;
	return fn;
}

// Consumes "(param_types...)" in a function pointer signature
static void skip_func_ptr_params(void) {
	match(TOKEN_LPAREN);
	if (cur_tok.type == TOKEN_VOID && peek_next_token().type == TOKEN_RPAREN) {
		match(TOKEN_VOID);
		match(TOKEN_RPAREN);
		return;
	}
	while (cur_tok.type != TOKEN_RPAREN && cur_tok.type != TOKEN_EOF) {
		if (cur_tok.type == TOKEN_ELLIPSIS) {
			match(TOKEN_ELLIPSIS);
			break;
		}
		if (cur_tok.type == TOKEN_REGISTER) match(TOKEN_REGISTER);
		parse_base_type();
		while (cur_tok.type == TOKEN_STAR) match(TOKEN_STAR);
		if (cur_tok.type == TOKEN_IDENT) match(TOKEN_IDENT);
		while (cur_tok.type == TOKEN_LBRACKET) {
			match(TOKEN_LBRACKET);
			if (cur_tok.type == TOKEN_INT_LIT) match(TOKEN_INT_LIT);
			match(TOKEN_RBRACKET);
		}
		if (cur_tok.type == TOKEN_COMMA) match(TOKEN_COMMA);
	}
	match(TOKEN_RPAREN);
}

static int parse_array_bound(void) {
	if (cur_tok.type == TOKEN_INT_LIT) {
		int val = cur_tok.int_val;
		match(TOKEN_INT_LIT);
		return val;
	}
	if (cur_tok.type == TOKEN_IDENT) {
		Symbol* sym = lookup_symbol(cur_tok.lexeme);
		if (sym && sym->is_enum_const) {
			int val = sym->init_int;
			match(TOKEN_IDENT);
			return val;
		}
	}
	fprintf(stderr, "Syntax Error: Expected constant integer array dimension, got '%s'.\n", cur_tok.lexeme);
	exit(1);
}

// Parses standard declarators AND function pointer declarators: e.g. (*fp)(int, int) or (*ops[4])(int)
static DataType parse_declarator(DataType base_type, char* out_name) {
	while (cur_tok.type == TOKEN_STAR) {
		match(TOKEN_STAR);
		base_type = make_pointer_to(base_type);
	}

	// Check for function pointer grouping: (*name...)
	if (cur_tok.type == TOKEN_LPAREN && peek_next_token().type == TOKEN_STAR) {
		match(TOKEN_LPAREN);
		int ptr_depth = 0;
		while (cur_tok.type == TOKEN_STAR) {
			match(TOKEN_STAR);
			ptr_depth++;
		}
		strcpy(out_name, cur_tok.lexeme);
		match(TOKEN_IDENT);

		// Optional array on the function pointer itself: int (*ops[4])(int)
		int arr_dims[8] = { 0 };
		int arr_count = 0;
		while (cur_tok.type == TOKEN_LBRACKET) {
			match(TOKEN_LBRACKET);
			arr_dims[arr_count++] = parse_array_bound();
			match(TOKEN_RBRACKET);
		}
		match(TOKEN_RPAREN);

		// Build TYPE_FUNC returning base_type, wrapped in pointer(s), then optional array
		DataType full_type = make_func_returning(base_type);
		for (int i = 0; i < ptr_depth; i++) {
			full_type = make_pointer_to(full_type);
		}
		if (arr_count > 0) {
			full_type.is_array = 1;
			full_type.dim_count = arr_count;
			for (int i = 0; i < arr_count; i++) full_type.dimensions[i] = arr_dims[i];
		}

		skip_func_ptr_params();
		return full_type;
	}

	strcpy(out_name, cur_tok.lexeme);
	match(TOKEN_IDENT);
	return base_type;
}

// Parses abstract pointer and function-pointer suffixes (no identifier name):
// e.g., "*", "**", "(*)(int, int)", "(**)(void)"
static DataType parse_abstract_declarator(DataType base_type) {
	while (cur_tok.type == TOKEN_STAR) {
		match(TOKEN_STAR);
		base_type = make_pointer_to(base_type);
	}

	// Check for abstract function pointer grouping: (*)(params...)
	if (cur_tok.type == TOKEN_LPAREN && peek_next_token().type == TOKEN_STAR) {
		match(TOKEN_LPAREN);
		int ptr_depth = 0;
		while (cur_tok.type == TOKEN_STAR) {
			match(TOKEN_STAR);
			ptr_depth++;
		}
		match(TOKEN_RPAREN);

		DataType full_type = make_func_returning(base_type);
		for (int i = 0; i < ptr_depth; i++) {
			full_type = make_pointer_to(full_type);
		}

		skip_func_ptr_params();
		return full_type;
	}

	return base_type;
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
		memcpy(n->name, cur_tok.lexeme, cur_tok.int_val + 1);
		n->int_val = cur_tok.int_val;
		match(TOKEN_STRING); return n;
	}

	if (cur_tok.type == TOKEN_IDENT) {
		char name[64];
		strcpy(name, cur_tok.lexeme);
		match(TOKEN_IDENT);

		Symbol* sym = lookup_symbol(name);
		if (sym && sym->is_enum_const) {
			n = create_node(AST_LITERAL);
			n->type.kind = TYPE_INT;
			n->int_val = sym->init_int;
			return n;
		}

		n = create_node(AST_IDENT);
		strcpy(n->name, name);
		if (sym) {
			n->type = sym->type;
		}
		else {
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
		|| cur_tok.type == TOKEN_INC || cur_tok.type == TOKEN_DEC || cur_tok.type == TOKEN_LPAREN) {

		if (cur_tok.type == TOKEN_LPAREN) {
			match(TOKEN_LPAREN);
			ASTNode* call = create_node(AST_CALL);

			// Unwrap explicit dereference syntax: (*fp)(args) -> evaluate fp as target address
			ASTNode* callee = n;
			if (callee->kind == AST_UNOP && callee->op == TOKEN_STAR) {
				callee = callee->left;
			}

			// Direct call to a declared function symbol vs. indirect call through a pointer/expression
			Symbol* fn_sym = (callee->kind == AST_IDENT) ? lookup_symbol(callee->name) : NULL;
			if (fn_sym && fn_sym->type.kind == TYPE_FUNC) {
				call->is_indirect = 0;
				strcpy(call->name, callee->name);
				if (fn_sym->type.base_type) call->type = *(fn_sym->type.base_type);
			}
			else {
				call->is_indirect = 1;
				call->right = callee;
				// Extract return type from TYPE_POINTER -> TYPE_FUNC -> return_type
				DataType t = callee->type;
				while (t.kind == TYPE_POINTER && t.base_type) t = *(t.base_type);
				if (t.kind == TYPE_FUNC && t.base_type) call->type = *(t.base_type);
				else call->type.kind = TYPE_INT;
			}

			ASTNode** arg_tail = &(call->left);
			while (cur_tok.type != TOKEN_RPAREN && cur_tok.type != TOKEN_EOF) {
				*arg_tail = parse_expr();
				(*arg_tail)->next = NULL;
				arg_tail = &((*arg_tail)->next);
				if (cur_tok.type == TOKEN_COMMA) match(TOKEN_COMMA);
			}
			match(TOKEN_RPAREN);
			n = call;
		}
		else if (cur_tok.type == TOKEN_INC || cur_tok.type == TOKEN_DEC) {
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
			if (n->type.is_array) {
				arr->type = n->type;
				if (arr->type.dim_count > 1) {
					// Peel off the outermost dimension for multi-dimensional arrays
					for (int d = 0; d < arr->type.dim_count - 1; d++) {
						arr->type.dimensions[d] = arr->type.dimensions[d + 1];
					}
					arr->type.dim_count--;
				}
				else {
					arr->type.is_array = 0;
					arr->type.dim_count = 0;
				}
			}
			else if (n->type.kind == TYPE_POINTER && n->type.base_type != NULL) {
				arr->type = *(n->type.base_type);
			}
			else {
				arr->type = n->type;
				arr->type.is_array = 0;
				arr->type.dim_count = 0;
			}
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

ASTNode* parse_unary(void) {
	// --- ENHANCED TYPE CAST DETECTION FOR VARIADIC SUPPORT ---
	// --- EXPLICIT TYPE CAST DETECTION ---
	if (cur_tok.type == TOKEN_LPAREN) {
		Token next_tok_peek = peek_next_token();
		if (is_type_name(next_tok_peek) || next_tok_peek.type == TOKEN_VOID) {
			match(TOKEN_LPAREN);
			DataType cast_type;
			memset(&cast_type, 0, sizeof(cast_type));
			if (cur_tok.type == TOKEN_VOID) {
				cast_type.kind = TYPE_INT;
				match(TOKEN_VOID);
			}
			else {
				cast_type = parse_base_type();
			}

			// Handles standard pointers (char*), double pointers (int**), AND function pointers (int (*)(int, int))
			cast_type = parse_abstract_declarator(cast_type);
			match(TOKEN_RPAREN);

			// Construct the cast node
			ASTNode* cast_node = create_node(AST_UNOP);
			cast_node->op = TOKEN_TYPEDEF; // Cast marker
			cast_node->type = cast_type;   // Target data type

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
		if (is_type_name(cur_tok) || cur_tok.type == TOKEN_VOID) {
			DataType vt;
			memset(&vt, 0, sizeof(vt));
			if (cur_tok.type == TOKEN_VOID) {
				vt.kind = TYPE_INT;
				match(TOKEN_VOID);
			}
			else {
				vt = parse_base_type();
			}
			vt = parse_abstract_declarator(vt);
			size_result = get_type_size(vt);
		}

		// Case B: An active expression or variable identifier is passed (e.g., sizeof(bob.age))
		else {
			ASTNode* expr_node = parse_expr();

			// String literals have array type char[len + 1] in C89 (includes '\0')
			if (expr_node->kind == AST_LITERAL && expr_node->type.kind == TYPE_POINTER) {
				size_result = expr_node->int_val + 1;
			}
			// If it's a plain variable identifier, pull details from the symbol table
			else if (expr_node->kind == AST_IDENT) {
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
				resolve_member_offset(expr_node, &total_offset, &out_type);
				size_result = get_type_size(out_type);
			}
			else {
				// Fall back to evaluating the explicit evaluation node type context
				size_result = get_type_size(expr_node->type);
			}

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

static ASTNode* clone_ast(ASTNode* src) {
	if (!src) return NULL;
	ASTNode* dst = (ASTNode*)malloc(sizeof(ASTNode));
	if (!dst) { fprintf(stderr, "Fatal Error: Out of memory.\n"); exit(1); }
	*dst = *src;
	dst->left = clone_ast(src->left);
	dst->right = clone_ast(src->right);
	dst->cond = clone_ast(src->cond);
	dst->body = clone_ast(src->body);
	dst->else_body = clone_ast(src->else_body);
	dst->next = clone_ast(src->next);
	return dst;
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

			math_node->left = clone_ast(n);
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

static int get_type_alignment(DataType type) {
	if (type.kind == TYPE_POINTER || type.base_type != NULL) return 4;
	if (type.kind == TYPE_CHAR) return 1;
	if (type.kind == TYPE_STRUCT || type.kind == TYPE_UNION) {
		struct StructEnv* env = lookup_struct(type.struct_name);
		return (env && env->alignment > 0) ? env->alignment : 4;
	}
	return 4; // int, float
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
	int max_align = 1;

	while (cur_tok.type != TOKEN_RBRACE && cur_tok.type != TOKEN_EOF) {
		// 1. Parse the shared base type once per declaration line (int, float, char, struct/union, typedef)
		DataType base_m_type = parse_base_type();

		// 2. Loop across comma-separated member declarators: e.g., int x, *y, z;
		while (1) {
			char m_name[64];
			DataType m_type = parse_declarator(base_m_type, m_name);

			// 3. Support fixed-size array members inside structs/unions
			while (cur_tok.type == TOKEN_LBRACKET) {
				match(TOKEN_LBRACKET);
				m_type.is_array = 1;
				m_type.dimensions[m_type.dim_count++] = parse_array_bound();
				match(TOKEN_RBRACKET);
			}

			int member_size = get_type_size(m_type);
			int member_align = get_type_alignment(m_type);
			if (member_align > max_align) {
				max_align = member_align;
			}

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
				current_offset = (current_offset + member_align - 1) & ~(member_align - 1);
				m->offset = current_offset;
				current_offset += member_size;
				total_size = current_offset;
			}

			*mem_tail = m;
			mem_tail = &(m->next);

			if (cur_tok.type == TOKEN_COMMA) {
				match(TOKEN_COMMA);
			}
			else {
				break;
			}
		}

		match(TOKEN_SEMI);
	}
	match(TOKEN_RBRACE);

	env->alignment = max_align;
	env->size = (total_size + max_align - 1) & ~(max_align - 1);
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
	while (cur_tok.type == TOKEN_STRUCT || cur_tok.type == TOKEN_UNION || cur_tok.type == TOKEN_ENUM ||
		cur_tok.type == TOKEN_TYPEDEF || cur_tok.type == TOKEN_STATIC || cur_tok.type == TOKEN_EXTERN) {

		if (cur_tok.type == TOKEN_TYPEDEF) {
			parse_stmt();
		}
		else if (cur_tok.type == TOKEN_ENUM) {
			match(TOKEN_ENUM);
			DataType enum_dt = parse_enum_specifier();
			if (cur_tok.type == TOKEN_SEMI) {
				// Standalone enum definition: enum TokenType { ... };
				match(TOKEN_SEMI);
			}
			else {
				// File-scope variable of enum type: enum Color bg_color = RED;
				decl_type = enum_dt;
				ret_kind = TYPE_INT;
				has_parsed_struct_decl_type = 1;
				break;
			}
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

	// 5 & 6. Parse pointer asterisks, function-pointer declarator (*name)(...), and identifier name
	// 5. Save the shared base type (e.g. 'int', 'struct Point') before parsing declarators
	DataType base_decl_type = decl_type;

	char name[64];
	decl_type = parse_declarator(base_decl_type, name);
	ret_kind = decl_type.kind;

	// 7. BRANCH A: GLOBAL / STATIC / EXTERN VARIABLE DECLARATION (not followed by '(')
	if (cur_tok.type != TOKEN_LPAREN) {
		while (1) {
			parse_array_dimensions(&decl_type);

			int has_init = 0;
			int init_int = 0;
			double init_float = 0.0;
			InitElem* init_elems = NULL;
			int init_elem_cnt = 0;
			int init_elem_cap = 0;

			if (cur_tok.type == TOKEN_ASSIGN) {
				if (is_file_extern) {
					fprintf(stderr, "Semantic Error: Extern variable '%s' cannot have an initializer.\n", name);
					exit(1);
				}
				match(TOKEN_ASSIGN);
				has_init = 1;

				int is_agg = (decl_type.is_array ||
					((decl_type.kind == TYPE_STRUCT || decl_type.kind == TYPE_UNION) && decl_type.base_type == NULL));

				if (is_agg || cur_tok.type == TOKEN_LBRACE) {
					ASTNode** dummy_tail = NULL;
					parse_init_recursive(decl_type, 0, 1, &init_elems, &init_elem_cnt, &init_elem_cap, NULL, &dummy_tail);

					// Deduce outer dimension if declared as T arr[] = ...
					if (decl_type.is_array && decl_type.dimensions[0] == 0) {
						DataType sub = decl_type;
						if (sub.dim_count > 1) {
							for (int d = 0; d < sub.dim_count - 1; d++) sub.dimensions[d] = sub.dimensions[d + 1];
							sub.dim_count--;
						}
						else {
							sub.is_array = 0;
							sub.dim_count = 0;
						}
						int sub_sz = get_type_size(sub);
						int max_end = 0;
						for (int k = 0; k < init_elem_cnt; k++) {
							int end = init_elems[k].offset + init_elems[k].size;
							if (end > max_end) max_end = end;
						}
						decl_type.dimensions[0] = (max_end + sub_sz - 1) / sub_sz;
					}
				}
				else {
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
			}

			if (decl_type.is_array && decl_type.dimensions[0] == 0 && !is_file_extern) {
				fprintf(stderr, "Semantic Error: Array '%s' has incomplete size.\n", name);
				exit(1);
			}

			Symbol* existing = lookup_symbol(name);
			if (existing && existing->is_global_var && existing->scope_level == 0) {
				if (!is_file_extern && existing->is_extern) {
					existing->is_extern = 0;
					existing->is_static = is_file_static;
					existing->has_init = has_init;
					existing->init_int = init_int;
					existing->init_float = init_float;
					existing->init_elems = init_elems;
					existing->init_elem_count = init_elem_cnt;
					existing->type = decl_type;
				}
			}
			else {
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
				g_sym->init_elems = init_elems;
				g_sym->init_elem_count = init_elem_cnt;
				g_sym->scope_level = 0;
				g_sym->next = sym_table;
				sym_table = g_sym;
			}

			if (cur_tok.type == TOKEN_COMMA) {
				match(TOKEN_COMMA);
				decl_type = parse_declarator(base_decl_type, name);
			}
			else {
				break;
			}
		}

		match(TOKEN_SEMI);
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

		char p_name[64];
		pt = parse_declarator(pt, p_name);

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

	// Build function type wrapping its return type (decl_type)
	DataType func_type = make_func_returning(decl_type);

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
		func_sym->type = func_type;
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
		existing->type = func_type;
		existing->is_variadic = is_var;
	}
	else {
		Symbol* func_sym = (Symbol*)calloc(1, sizeof(Symbol));
		if (!func_sym) { fprintf(stderr, "Out of memory.\n"); exit(1); }
		strcpy(func_sym->name, name);
		func_sym->type = func_type;
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
	// 1. HANDLE TYPEDEF REGISTRATION DIRECTLY (supports comma-separated aliases: typedef int A, *B;)
	if (cur_tok.type == TOKEN_TYPEDEF) {
		match(TOKEN_TYPEDEF);

		DataType base_t = parse_base_type();

		while (1) {
			char alias_name[64];
			DataType full_t = parse_declarator(base_t, alias_name);

			Symbol* sym = (Symbol*)calloc(1, sizeof(Symbol));
			if (!sym) { fprintf(stderr, "Fatal Error: Out of memory.\n"); exit(1); }
			strcpy(sym->name, alias_name);
			sym->type = full_t;
			sym->is_typedef = 1;
			sym->scope_level = current_scope_level;
			sym->next = sym_table;
			sym_table = sym;

			if (cur_tok.type == TOKEN_COMMA) {
				match(TOKEN_COMMA);
			}
			else {
				break;
			}
		}

		match(TOKEN_SEMI);
		return create_node(AST_VAR_DECL);
	}

	// 2. LOCAL DECLARATIONS: static, extern, auto, register, or type name (supports multi-declarators)
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

		DataType base_vt = parse_base_type();

		char name[64];
		DataType vt = parse_declarator(base_vt, name);

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
				if (!func_sym) { fprintf(stderr, "Fatal Error: Out of memory.\n"); exit(1); }
				strcpy(func_sym->name, name);
				func_sym->type = make_func_returning(vt);
				func_sym->is_local = 0;
				func_sym->is_extern = 1;
				func_sym->is_variadic = is_var;
				func_sym->scope_level = 0;
				func_sym->next = sym_table;
				sym_table = func_sym;
			}
			return create_node(AST_VAR_DECL);
		}

		ASTNode* init_block = create_node(AST_BLOCK);
		ASTNode** init_tail = &(init_block->body);
		int has_any_dyn_init = 0;

		while (1) {
			parse_array_dimensions(&vt);

			int has_init = 0;
			int init_int = 0;
			double init_float = 0.0;
			InitElem* init_elems = NULL;
			int init_elem_cnt = 0;
			int init_elem_cap = 0;
			ASTNode* dyn_init_expr = NULL;
			int is_brace_or_str_init = 0;

			if (cur_tok.type == TOKEN_ASSIGN) {
				if (is_ext) {
					fprintf(stderr, "Semantic Error: Block-scope extern '%s' cannot have an initializer.\n", name);
					exit(1);
				}
				match(TOKEN_ASSIGN);
				has_init = 1;

				int is_agg = (vt.is_array ||
					((vt.kind == TYPE_STRUCT || vt.kind == TYPE_UNION) && vt.base_type == NULL));

				if (is_stat) {
					if (is_agg || cur_tok.type == TOKEN_LBRACE) {
						ASTNode** dummy_tail = NULL;
						parse_init_recursive(vt, 0, 1, &init_elems, &init_elem_cnt, &init_elem_cap, NULL, &dummy_tail);
						if (vt.is_array && vt.dimensions[0] == 0) {
							DataType sub = vt;
							if (sub.dim_count > 1) {
								for (int d = 0; d < sub.dim_count - 1; d++) sub.dimensions[d] = sub.dimensions[d + 1];
								sub.dim_count--;
							}
							else {
								sub.is_array = 0;
								sub.dim_count = 0;
							}
							int sub_sz = get_type_size(sub);
							int max_end = 0;
							for (int k = 0; k < init_elem_cnt; k++) {
								int end = init_elems[k].offset + init_elems[k].size;
								if (end > max_end) max_end = end;
							}
							vt.dimensions[0] = (max_end + sub_sz - 1) / sub_sz;
						}
					}
					else {
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
				}
				else {
					if (is_agg || cur_tok.type == TOKEN_LBRACE) {
						is_brace_or_str_init = 1;
					}
					else {
						dyn_init_expr = parse_expr();
					}
				}
			}

			// Handle block-scope extern variable
			if (is_ext) {
				Symbol* existing = lookup_symbol(name);
				if (!(existing && existing->is_global_var && existing->scope_level == 0)) {
					Symbol* ext_sym = (Symbol*)calloc(1, sizeof(Symbol));
					if (!ext_sym) { fprintf(stderr, "Fatal Error: Out of memory.\n"); exit(1); }
					strcpy(ext_sym->name, name);
					strcpy(ext_sym->asm_label, name);
					ext_sym->type = vt;
					ext_sym->is_local = 0;
					ext_sym->is_extern = 1;
					ext_sym->is_global_var = 1;
					ext_sym->scope_level = 0;
					ext_sym->next = sym_table;
					sym_table = ext_sym;
				}
			}
			else {
				Symbol* sym = (Symbol*)calloc(1, sizeof(Symbol));
				if (!sym) { fprintf(stderr, "Fatal Error: Out of memory.\n"); exit(1); }
				strcpy(sym->name, name);
				sym->type = vt;
				sym->scope_level = current_scope_level;
				sym->is_register = is_reg;

				if (is_stat) {
					if (vt.is_array && vt.dimensions[0] == 0) {
						fprintf(stderr, "Semantic Error: Static array '%s' has incomplete size.\n", name);
						exit(1);
					}
					sym->is_local = 0;
					sym->is_static = 1;
					sym->is_global_var = 1;
					sym->has_init = has_init;
					sym->init_int = init_int;
					sym->init_float = init_float;
					sym->init_elems = init_elems;
					sym->init_elem_count = init_elem_cnt;
					sprintf(sym->asm_label, "static_%s_%s_%d", current_func_name, name, static_var_counter++);
					sym->next = sym_table;
					sym_table = sym;
				}
				else if (is_brace_or_str_init) {
					// Parse local initializer list first so unsized arrays (int a[] = {1,2,3};) deduce dimensions[0]
					ASTNode* sub_block = create_node(AST_BLOCK);
					ASTNode** sub_tail = &(sub_block->body);

					ASTNode* root_id = create_node(AST_IDENT);
					strcpy(root_id->name, name);
					root_id->type = vt;

					// Temporarily register symbol so nested AST nodes can resolve it
					sym->is_local = 1;
					sym->next = sym_table;
					sym_table = sym;

					parse_init_recursive(vt, 0, 0, NULL, NULL, NULL, root_id, &sub_tail);

					// Deduce unsized outer dimension from generated AST assignments
					if (vt.is_array && vt.dimensions[0] == 0) {
						DataType sub = vt;
						if (sub.dim_count > 1) {
							for (int d = 0; d < sub.dim_count - 1; d++) sub.dimensions[d] = sub.dimensions[d + 1];
							sub.dim_count--;
						}
						else {
							sub.is_array = 0;
							sub.dim_count = 0;
						}
						int sub_sz = get_type_size(sub);
						int elem_cnt = 0;
						for (ASTNode* s = sub_block->body; s; s = s->next) elem_cnt++;
						int total_bytes = elem_cnt * ((vt.kind == TYPE_CHAR && vt.dim_count == 1) ? 1 : 4);
						vt.dimensions[0] = (total_bytes + sub_sz - 1) / sub_sz;
						sym->type = vt;
						root_id->type = vt;
					}

					int total_bytes = get_type_size(vt);
					local_offset += total_bytes;
					if (local_offset > max_function_stack_bytes) {
						max_function_stack_bytes = local_offset;
					}
					sym->offset = -local_offset;

					// Zero-fill entire local aggregate first so omitted elements are 0
					emit_local_zero_fill(root_id, total_bytes, &init_tail);

					// Append element assignments after zero-fill
					if (sub_block->body) {
						*init_tail = sub_block->body;
						while (*init_tail) init_tail = &((*init_tail)->next);
					}
					free(sub_block);
					has_any_dyn_init = 1;
				}
				else {
					if (vt.is_array && vt.dimensions[0] == 0) {
						fprintf(stderr, "Semantic Error: Local array '%s' has incomplete size.\n", name);
						exit(1);
					}
					int total_bytes = get_type_size(vt);
					local_offset += total_bytes;
					if (local_offset > max_function_stack_bytes) {
						max_function_stack_bytes = local_offset;
					}
					sym->is_local = 1;
					sym->offset = -local_offset;
					sym->next = sym_table;
					sym_table = sym;

					if (dyn_init_expr != NULL) {
						ASTNode* id_node = create_node(AST_IDENT);
						strcpy(id_node->name, name);
						id_node->type = vt;

						ASTNode* assign_node = create_node(AST_ASSIGN);
						assign_node->left = id_node;
						assign_node->right = dyn_init_expr;
						assign_node->type = vt;

						*init_tail = assign_node;
						init_tail = &(assign_node->next);
						has_any_dyn_init = 1;
					}
				}
			}

			if (cur_tok.type == TOKEN_COMMA) {
				match(TOKEN_COMMA);
				vt = parse_declarator(base_vt, name);
			}
			else {
				break;
			}
		}

		match(TOKEN_SEMI);

		if (has_any_dyn_init) {
			return init_block;
		}
		free(init_block);

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

	if (cur_tok.type == TOKEN_DO) {
		ASTNode* n = create_node(AST_DO_WHILE);
		match(TOKEN_DO);
		n->body = parse_stmt();
		match(TOKEN_WHILE);
		match(TOKEN_LPAREN);
		n->cond = parse_expr();
		match(TOKEN_RPAREN);
		match(TOKEN_SEMI);
		return n;
	}

	if (cur_tok.type == TOKEN_FOR) {
		ASTNode* n = create_node(AST_FOR);
		match(TOKEN_FOR);
		match(TOKEN_LPAREN);
		if (cur_tok.type != TOKEN_SEMI) n->left = parse_expr();
		match(TOKEN_SEMI);
		if (cur_tok.type != TOKEN_SEMI) n->cond = parse_expr();
		match(TOKEN_SEMI);
		if (cur_tok.type != TOKEN_RPAREN) n->right = parse_expr();
		match(TOKEN_RPAREN);
		n->body = parse_stmt();
		return n;
	}
	if (cur_tok.type == TOKEN_BREAK) { ASTNode* n = create_node(AST_BREAK); match(TOKEN_BREAK); match(TOKEN_SEMI); return n; }
	if (cur_tok.type == TOKEN_CONTINUE) { ASTNode* n = create_node(AST_CONTINUE); match(TOKEN_CONTINUE); match(TOKEN_SEMI); return n; }
	if (cur_tok.type == TOKEN_RETURN) {
		ASTNode* n = create_node(AST_RETURN);
		match(TOKEN_RETURN);
		if (cur_tok.type != TOKEN_SEMI) {
			n->left = parse_expr();
		}
		match(TOKEN_SEMI);
		return n;
	}

	if (cur_tok.type == TOKEN_SWITCH) {
		ASTNode* n = create_node(AST_SWITCH);
		match(TOKEN_SWITCH);
		match(TOKEN_LPAREN);
		n->left = parse_expr();
		match(TOKEN_RPAREN);
		n->body = parse_stmt();
		return n;
	}

	if (cur_tok.type == TOKEN_CASE) {
		ASTNode* n = create_node(AST_CASE);
		match(TOKEN_CASE);
		int sign = 1;
		if (cur_tok.type == TOKEN_MINUS) {
			sign = -1;
			match(TOKEN_MINUS);
		}
		if (cur_tok.type == TOKEN_INT_LIT) {
			n->int_val = sign * cur_tok.int_val;
			match(TOKEN_INT_LIT);
		}
		else if (cur_tok.type == TOKEN_IDENT) {
			Symbol* sym = lookup_symbol(cur_tok.lexeme);
			if (sym && sym->is_enum_const) {
				n->int_val = sign * sym->init_int;
				match(TOKEN_IDENT);
			}
			else {
				fprintf(stderr, "Syntax Error: C89 requires case labels to be constant integer expressions.\n");
				exit(1);
			}
		}
		else {
			fprintf(stderr, "Syntax Error: C89 requires case labels to be constant integer literals.\n");
			exit(1);
		}
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
		if (type.is_array) {
			int total_elements = 1;
			for (int i = 0; i < type.dim_count; i++) total_elements *= type.dimensions[i];
			return 4 * total_elements;
		}

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
