#include "cc.h"
#include <ctype.h>

static FILE* src_file = NULL;
static int ch = ' ';

void init_lexer(FILE* src) {
	src_file = src;
	ch = fgetc(src_file);
}

Token next_token(void) {
	Token tok;
	memset(&tok, 0, sizeof(tok));

	while (1) {
		while (isspace((unsigned char)ch)) ch = fgetc(src_file);

		if (ch == EOF) {
			tok.type = TOKEN_EOF;
			return tok;
		}

		if (ch == '/') {
			int next_ch = fgetc(src_file);

			if (next_ch == '*') { // Block Comment /* */
				ch = fgetc(src_file);
				while (ch != EOF) {
					if (ch == '*') {
						ch = fgetc(src_file);
						if (ch == '/') { ch = fgetc(src_file); break; }
					}
					else {
						ch = fgetc(src_file);
					}
				}
				if (ch == EOF) { // Fix: Prevent infinite loop if comment is unclosed
					printf("Lexical Error: Unexpected EOF inside block comment.\n");
					exit(1);
				}
				continue;
			}

			else if (next_ch == '/') { // Line Comment //
				while (ch != '\n' && ch != EOF) ch = fgetc(src_file);
				continue;
			}
			else {
				ungetc(next_ch, src_file);
				tok.type = TOKEN_SLASH;
				ch = fgetc(src_file);
				return tok;
			}
		}
		break;
	}

	if (ch == '"') { // String Literals
		ch = fgetc(src_file);
		int len = 0;
		while (ch != '"' && ch != EOF) {
			if (ch == '\\') {
				int next_ch = fgetc(src_file);
				if (next_ch == 'n') if (len < 63) tok.lexeme[len++] = '\n';
				else {
					if (len < 63) tok.lexeme[len++] = ch;
					if (len < 63) tok.lexeme[len++] = next_ch;
				}
				ch = fgetc(src_file);
				continue;
			}
			if (len < 63) tok.lexeme[len++] = ch;
			ch = fgetc(src_file);
		}
		tok.lexeme[len] = '\0';
		if (ch == '"') ch = fgetc(src_file);
		tok.type = TOKEN_STRING;
		return tok;
	}

	if (isalpha((unsigned char)ch) || ch == '_') { // Identifiers / Keywords
		int len = 0;
		while (isalnum((unsigned char)ch) || ch == '_') {
			if (len < 63) tok.lexeme[len++] = ch;
			ch = fgetc(src_file);
		}
		tok.lexeme[len] = '\0';

		if (strcmp(tok.lexeme, "if") == 0) tok.type = TOKEN_IF;
		else if (strcmp(tok.lexeme, "else") == 0) tok.type = TOKEN_ELSE;
		else if (strcmp(tok.lexeme, "while") == 0) tok.type = TOKEN_WHILE;
		else if (strcmp(tok.lexeme, "for") == 0) tok.type = TOKEN_FOR;
		else if (strcmp(tok.lexeme, "break") == 0) tok.type = TOKEN_BREAK;
		else if (strcmp(tok.lexeme, "continue") == 0) tok.type = TOKEN_CONTINUE;
		else if (strcmp(tok.lexeme, "struct") == 0) tok.type = TOKEN_STRUCT;
		else if (strcmp(tok.lexeme, "union") == 0) tok.type = TOKEN_UNION;
		else if (strcmp(tok.lexeme, "return") == 0) tok.type = TOKEN_RETURN;
		else tok.type = TOKEN_IDENT;
		return tok;
	}

	if (isdigit((unsigned char)ch)) { // Numerics
		int len = 0;
		int is_float = 0;
		while (isdigit((unsigned char)ch) || ch == '.') {
			if (ch == '.') is_float = 1;
			if (len < 63) tok.lexeme[len++] = ch;
			ch = fgetc(src_file);
		}
		tok.lexeme[len] = '\0';
		if (is_float) {
			tok.type = TOKEN_FLOAT_LIT;
			tok.float_val = atof(tok.lexeme);
		}
		else {
			tok.type = TOKEN_INT_LIT;
			tok.int_val = atoi(tok.lexeme);
		}
		return tok;
	}

	switch (ch) {
	case '+': tok.type = TOKEN_PLUS; ch = fgetc(src_file); break;
	case '*': tok.type = TOKEN_STAR; ch = fgetc(src_file); break;
	case '&': tok.type = TOKEN_AMP; ch = fgetc(src_file); break;
	case ';': tok.type = TOKEN_SEMI; ch = fgetc(src_file); break;
	case ',': tok.type = TOKEN_COMMA; ch = fgetc(src_file); break;
	case '.': tok.type = TOKEN_PERIOD; ch = fgetc(src_file); break;
	case '(': tok.type = TOKEN_LPAREN; ch = fgetc(src_file); break;
	case ')': tok.type = TOKEN_RPAREN; ch = fgetc(src_file); break;
	case '{': tok.type = TOKEN_LBRACE; ch = fgetc(src_file); break;
	case '}': tok.type = TOKEN_RBRACE; ch = fgetc(src_file); break;
	case '[': tok.type = TOKEN_LBRACKET; ch = fgetc(src_file); break;
	case ']': tok.type = TOKEN_RBRACKET; ch = fgetc(src_file); break;
	case '<': tok.type = TOKEN_LESS; ch = fgetc(src_file); break;
	case '>': tok.type = TOKEN_GREATER; ch = fgetc(src_file); break;
	case '-':
		ch = fgetc(src_file);
		if (ch == '>') { tok.type = TOKEN_ARROW; ch = fgetc(src_file); }
		else tok.type = TOKEN_MINUS;
		break;
	case '=':
		ch = fgetc(src_file);
		if (ch == '=') { tok.type = TOKEN_EQUAL; ch = fgetc(src_file); }
		else tok.type = TOKEN_ASSIGN;
		break;
	default:
		printf("Lexical Error: Unknown character: %c\n", ch);
		exit(1);
	}
	return tok;
}
