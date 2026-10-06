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
	TOKEN_ASSIGN, TOKEN_EQUAL, TOKEN_LESS, TOKEN_GREATER,
	TOKEN_SEMI, TOKEN_COMMA, TOKEN_PERIOD, TOKEN_ARROW,
	TOKEN_LPAREN, TOKEN_RPAREN, TOKEN_LBRACE, TOKEN_RBRACE,
	TOKEN_LBRACKET, TOKEN_RBRACKET,
	TOKEN_IF, TOKEN_ELSE, TOKEN_WHILE, TOKEN_FOR, TOKEN_BREAK, TOKEN_CONTINUE,
	TOKEN_STRUCT, TOKEN_UNION, TOKEN_RETURN
} TokenType;

typedef struct {
	TokenType type;
	char lexeme[64]; // Sized array bounds protection
	int int_val;
	double float_val;
} Token;

typedef enum {
	TYPE_INT, TYPE_FLOAT, TYPE_POINTER, TYPE_STRUCT, TYPE_UNION
} DataTypeKind;

typedef struct DataType {
	DataTypeKind kind;
	struct DataType* base_type;
	char struct_name[64];
	int is_array;
	int dimensions[8];
	int dim_count;
} DataType;

typedef enum {
	AST_PROGRAM, AST_FUNC_DECL, AST_VAR_DECL, AST_STRUCT_DECL,
	AST_BLOCK, AST_IF, AST_WHILE, AST_FOR, AST_BREAK, AST_CONTINUE,
	AST_ASSIGN, AST_RETURN, AST_BINOP, AST_UNOP, AST_LITERAL,
	AST_IDENT, AST_MEMBER_ACCESS, AST_ARRAY_ACCESS,
	AST_CALL, AST_UNION_DECL
} ASTNodeKind;

typedef struct ASTNode {
	ASTNodeKind kind;
	int op;
	char name[64];
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
	DataType type;
	int is_local;
	int offset;
	int scope_level;
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
ASTNode* parse_program(void);
void emit_code(ASTNode* node);
ASTNode* create_node(ASTNodeKind kind);
int get_type_size(DataType type);
int align_to(int value, int alignment);

Symbol* lookup_symbol(const char* name);
void enter_scope(void);
void exit_scope(void);

extern int is_codegen_phase;

#endif
