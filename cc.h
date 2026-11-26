#ifndef CC_H
#define CC_H

// FIX C4996: Disable Microsoft strict legacy string function warnings globally
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Token and AST Definitions
typedef enum {
	TOKEN_EOF, TOKEN_IDENT, TOKEN_INT_LIT, TOKEN_FLOAT_LIT, TOKEN_STRING,
	TOKEN_PLUS, TOKEN_MINUS, TOKEN_STAR, TOKEN_SLASH, TOKEN_AMP,
	TOKEN_ASSIGN, TOKEN_ADD_ASSIGN, TOKEN_SUB_ASSIGN, TOKEN_MUL_ASSIGN, TOKEN_DIV_ASSIGN, TOKEN_MOD_ASSIGN,
	TOKEN_BITOR, TOKEN_BITXOR, TOKEN_BITNOT, TOKEN_LSHIFT, TOKEN_RSHIFT,
	TOKEN_AND_ASSIGN, TOKEN_OR_ASSIGN, TOKEN_XOR_ASSIGN, TOKEN_LSHIFT_ASSIGN, TOKEN_RSHIFT_ASSIGN,
	TOKEN_QUESTION,
	TOKEN_EQUAL, TOKEN_NOT_EQUAL, TOKEN_LESS, TOKEN_GREATER, TOKEN_LESS_EQUAL, TOKEN_GREATER_EQUAL,
	TOKEN_SEMI, TOKEN_COMMA, TOKEN_PERIOD, TOKEN_ARROW,
	TOKEN_LPAREN, TOKEN_RPAREN, TOKEN_LBRACE, TOKEN_RBRACE,
	TOKEN_LBRACKET, TOKEN_RBRACKET,
	TOKEN_IF, TOKEN_ELSE, TOKEN_WHILE, TOKEN_DO, TOKEN_FOR, TOKEN_BREAK, TOKEN_CONTINUE,
	TOKEN_STRUCT, TOKEN_UNION, TOKEN_RETURN, TOKEN_VOID, TOKEN_CHAR,
	TOKEN_SWITCH, TOKEN_CASE, TOKEN_DEFAULT, TOKEN_COLON, TOKEN_PERCENT,
	TOKEN_LOGAND, // && 
	TOKEN_LOGOR,  // || 
	TOKEN_NOT, TOKEN_ELLIPSIS, TOKEN_TYPEDEF, TOKEN_SIZEOF, TOKEN_STATIC, TOKEN_EXTERN, TOKEN_AUTO, TOKEN_REGISTER,
	TOKEN_INC, TOKEN_DEC,
	TOKEN_ENUM
} TokenType;

typedef struct {
	TokenType type;
	char lexeme[512]; // Sized array bounds protection
	int int_val;
	double float_val;
} Token;

typedef enum {
	TYPE_INT, TYPE_FLOAT, TYPE_POINTER, TYPE_STRUCT, TYPE_UNION, TYPE_CHAR, TYPE_FUNC
} DataTypeKind;

typedef struct DataType {
	DataTypeKind kind;
	struct DataType* base_type;  // For TYPE_POINTER: pointee type; for TYPE_FUNC: return type
	char struct_name[64];
	int is_array;
	int dimensions[8];
	int dim_count;
} DataType;

typedef enum {
	AST_PROGRAM, AST_FUNC_DECL, AST_VAR_DECL, AST_STRUCT_DECL,
	AST_BLOCK, AST_IF, AST_WHILE, AST_DO_WHILE, AST_FOR, AST_BREAK, AST_CONTINUE,
	AST_ASSIGN, AST_RETURN, AST_BINOP, AST_UNOP, AST_LITERAL,
	AST_IDENT, AST_MEMBER_ACCESS, AST_ARRAY_ACCESS,
	AST_CALL, AST_UNION_DECL,
	AST_SWITCH, AST_CASE, AST_DEFAULT,
	AST_POST_INC, AST_POST_DEC,
	AST_TERNARY
} ASTNodeKind;

typedef struct ASTNode {
	ASTNodeKind kind;
	int op;
	int is_indirect; // 1 = indirect call via expression in node->right, 0 = direct call by node->name
	char name[512];
	int int_val;
	double float_val;
	DataType type;
	struct ASTNode* left;
	struct ASTNode* right;
	struct ASTNode* cond;
	struct ASTNode* body;
	struct ASTNode* else_body;
	struct ASTNode* next;
} ASTNode;

typedef struct Symbol {
	char name[64];
	char asm_label[64];    // MASM label for globals/statics (e.g., "g_var" or "static_foo_x_0")
	DataType type;
	int is_local;		   // 1 = stack variable ([ebp - offset]), 0 = global/static/func
	int is_static;         // 1 = static storage/internal linkage
	int is_global_var;     // 1 = global or block-static variable stored in .data
	int has_init;          // 1 if initialized at declaration
	int init_int;          // Constant integer/char initializer
	double init_float;     // Constant float initializer
	int offset;
	int scope_level;
	int is_variadic;
	int is_extern;         // 1 if it's an external prototype, 0 otherwise
	int is_typedef;        // 1 if symbol is a type alias, 0 if regular variable
	int is_register;       // 1 if declared with 'register' storage class
	int is_enum_const;	   // 1 if symbol is an enum constant (value stored in init_int)	
	struct Symbol* next;
} Symbol;

typedef struct Member {
	char name[64];
	DataType type;
	int offset;
	struct Member* next;
} Member;

typedef struct StructEnv {
	char name[64];
	Member* members;
	int size;
	struct StructEnv* next;
} StructEnv;

extern Symbol* sym_table;
extern struct StructEnv* struct_table;

// Global Compiler Prototypes
void init_lexer(FILE* src);
Token next_token(void);
Token peek_next_token(void);
ASTNode* parse_program(void);
void emit_code(ASTNode* node);
ASTNode* create_node(ASTNodeKind kind);
int get_type_size(DataType type);
int align_to(int value, int alignment);

Symbol* lookup_symbol(const char* name);
void enter_scope(void);
void exit_scope(void);

extern int is_codegen_phase;

ASTNode* parse_struct_or_union_with_tag(int is_union, const char* name);
struct StructEnv* lookup_struct(const char* name);
int resolve_member_offset(ASTNode* node, int* total_offset, DataType* out_type);

void emit_external_declarations(void);

void emit_global_variables(void);

DataType make_pointer_to(DataType target);

#endif
