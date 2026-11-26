#include "cc.h"
#include <ctype.h>

static FILE* src_file = NULL;
static int ch = ' ';

void init_lexer(FILE* src) {
	src_file = src;
	ch = fgetc(src_file);
}

static int parse_escape_sequence(void) {
	int esc = fgetc(src_file);
	if (esc == EOF) {
		fprintf(stderr, "Lexical Error: Unexpected EOF in escape sequence.\n");
		exit(1);
	}

	switch (esc) {
	case 'a':  ch = fgetc(src_file); return '\a';
	case 'b':  ch = fgetc(src_file); return '\b';
	case 'f':  ch = fgetc(src_file); return '\f';
	case 'n':  ch = fgetc(src_file); return '\n';
	case 'r':  ch = fgetc(src_file); return '\r';
	case 't':  ch = fgetc(src_file); return '\t';
	case 'v':  ch = fgetc(src_file); return '\v';
	case '\\': ch = fgetc(src_file); return '\\';
	case '\'': ch = fgetc(src_file); return '\'';
	case '"':  ch = fgetc(src_file); return '\"';
	case '?':  ch = fgetc(src_file); return '\?';

	case 'x': { // Hexadecimal escape: \xHH...
		int val = 0;
		int digits = 0;
		ch = fgetc(src_file);
		while (isxdigit((unsigned char)ch)) {
			int d = isdigit((unsigned char)ch) ? (ch - '0') : (tolower((unsigned char)ch) - 'a' + 10);
			val = (val * 16) + d;
			digits++;
			ch = fgetc(src_file);
		}
		if (digits == 0) {
			fprintf(stderr, "Lexical Error: \\x used with no following hex digits.\n");
			exit(1);
		}
		return val & 0xFF;
	}

	default:
		// Octal escape: \OOO (up to 3 octal digits: 0-7)
		if (esc >= '0' && esc <= '7') {
			int val = esc - '0';
			int count = 1;
			ch = fgetc(src_file);
			while (count < 3 && ch >= '0' && ch <= '7') {
				val = (val * 8) + (ch - '0');
				count++;
				ch = fgetc(src_file);
			}
			return val & 0xFF;
		}
		// Unknown escape fallback
		ch = fgetc(src_file);
		return esc;
	}
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
					fprintf(stderr, "Lexical Error: Unexpected EOF inside block comment.\n");
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
				ch = fgetc(src_file);
				if (ch == '=') { tok.type = TOKEN_DIV_ASSIGN; ch = fgetc(src_file); }
				else { tok.type = TOKEN_SLASH; }
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
				int decoded = parse_escape_sequence();
				if (len < 511) tok.lexeme[len++] = (char)decoded;
				continue;
			}
			if (len < 511) tok.lexeme[len++] = (char)ch;
			ch = fgetc(src_file);
		}
		if (ch != '"') {
			fprintf(stderr, "Lexical Error: Unterminated string literal.\n");
			exit(1);
		}
		tok.lexeme[len] = '\0';
		tok.int_val = len; // Preserve exact byte length including embedded '\0'
		ch = fgetc(src_file);
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
		else if (strcmp(tok.lexeme, "do") == 0) tok.type = TOKEN_DO;
		else if (strcmp(tok.lexeme, "for") == 0) tok.type = TOKEN_FOR;
		else if (strcmp(tok.lexeme, "break") == 0) tok.type = TOKEN_BREAK;
		else if (strcmp(tok.lexeme, "continue") == 0) tok.type = TOKEN_CONTINUE;
		else if (strcmp(tok.lexeme, "struct") == 0) tok.type = TOKEN_STRUCT;
		else if (strcmp(tok.lexeme, "union") == 0) tok.type = TOKEN_UNION;
		else if (strcmp(tok.lexeme, "return") == 0) tok.type = TOKEN_RETURN;
		else if (strcmp(tok.lexeme, "void") == 0) tok.type = TOKEN_VOID;
		else if (strcmp(tok.lexeme, "char") == 0) tok.type = TOKEN_CHAR;
		else if (strcmp(tok.lexeme, "switch") == 0) tok.type = TOKEN_SWITCH;   
		else if (strcmp(tok.lexeme, "case") == 0) tok.type = TOKEN_CASE;       
		else if (strcmp(tok.lexeme, "default") == 0) tok.type = TOKEN_DEFAULT; 
		else if (strcmp(tok.lexeme, "typedef") == 0) tok.type = TOKEN_TYPEDEF;
		else if (strcmp(tok.lexeme, "sizeof") == 0) tok.type = TOKEN_SIZEOF;
		else if (strcmp(tok.lexeme, "static") == 0) tok.type = TOKEN_STATIC;
		else if (strcmp(tok.lexeme, "extern") == 0) tok.type = TOKEN_EXTERN;
		else if (strcmp(tok.lexeme, "auto") == 0) tok.type = TOKEN_AUTO;
		else if (strcmp(tok.lexeme, "register") == 0) tok.type = TOKEN_REGISTER;
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
	case '\'': {
		ch = fgetc(src_file);
		if (ch == '\'' || ch == '\n' || ch == EOF) {
			fprintf(stderr, "Lexical Error: Empty or invalid character literal.\n");
			exit(1);
		}

		int char_val = 0;
		if (ch == '\\') {
			char_val = parse_escape_sequence();
		}
		else {
			char_val = (unsigned char)ch;
			ch = fgetc(src_file);
		}

		if (ch != '\'') {
			fprintf(stderr, "Lexical Error: Multi-character or unterminated character constant.\n");
			exit(1);
		}
		ch = fgetc(src_file); // Consume closing single quote

		tok.type = TOKEN_INT_LIT; // C89 character constants evaluate as int
		tok.int_val = char_val;
		snprintf(tok.lexeme, sizeof(tok.lexeme), "%d", char_val);
		return tok;
	}

	case '+':
		ch = fgetc(src_file);
		if (ch == '+') { tok.type = TOKEN_INC; ch = fgetc(src_file); }
		else if (ch == '=') { tok.type = TOKEN_ADD_ASSIGN; ch = fgetc(src_file); }
		else { tok.type = TOKEN_PLUS; }
		break;

	case '*':
		ch = fgetc(src_file);
		if (ch == '=') { tok.type = TOKEN_MUL_ASSIGN; ch = fgetc(src_file); }
		else { tok.type = TOKEN_STAR; }
		break;

	case '%':
		ch = fgetc(src_file);
		if (ch == '=') { tok.type = TOKEN_MOD_ASSIGN; ch = fgetc(src_file); }
		else { tok.type = TOKEN_PERCENT; }
		break;

	case '!':
		ch = fgetc(src_file);
		if (ch == '=') { tok.type = TOKEN_NOT_EQUAL; ch = fgetc(src_file); }
		else { tok.type = TOKEN_NOT; }
		break;

	case '&':
		ch = fgetc(src_file);
		if (ch == '&') { tok.type = TOKEN_LOGAND; ch = fgetc(src_file); }
		else if (ch == '=') { tok.type = TOKEN_AND_ASSIGN; ch = fgetc(src_file); }
		else { tok.type = TOKEN_AMP; }
		break;

	case '|':
		ch = fgetc(src_file);
		if (ch == '|') { tok.type = TOKEN_LOGOR; ch = fgetc(src_file); }
		else if (ch == '=') { tok.type = TOKEN_OR_ASSIGN; ch = fgetc(src_file); }
		else { tok.type = TOKEN_BITOR; }
		break;

	case '^':
		ch = fgetc(src_file);
		if (ch == '=') { tok.type = TOKEN_XOR_ASSIGN; ch = fgetc(src_file); }
		else { tok.type = TOKEN_BITXOR; }
		break;

	case '~':
		tok.type = TOKEN_BITNOT;
		ch = fgetc(src_file);
		break;

	case '?':
		tok.type = TOKEN_QUESTION;
		ch = fgetc(src_file);
		break;

	case ':': tok.type = TOKEN_COLON; ch = fgetc(src_file); break;
	case ';': tok.type = TOKEN_SEMI; ch = fgetc(src_file); break;
	case ',': tok.type = TOKEN_COMMA; ch = fgetc(src_file); break;

	case '.':
	{
		int next1 = fgetc(src_file);
		if (next1 == '.') {
			int next2 = fgetc(src_file);
			if (next2 == '.') {
				tok.type = TOKEN_ELLIPSIS;
				strcpy(tok.lexeme, "...");
				ch = fgetc(src_file); // advance to next character
				return tok;
			}
			ungetc(next2, src_file);
		}
		ungetc(next1, src_file);
		tok.type = TOKEN_PERIOD;
		ch = fgetc(src_file);
		break;
	}

	case '(': tok.type = TOKEN_LPAREN; ch = fgetc(src_file); break;
	case ')': tok.type = TOKEN_RPAREN; ch = fgetc(src_file); break;
	case '{': tok.type = TOKEN_LBRACE; ch = fgetc(src_file); break;
	case '}': tok.type = TOKEN_RBRACE; ch = fgetc(src_file); break;
	case '[': tok.type = TOKEN_LBRACKET; ch = fgetc(src_file); break;
	case ']': tok.type = TOKEN_RBRACKET; ch = fgetc(src_file); break;

	case '<':
		ch = fgetc(src_file);
		if (ch == '<') {
			ch = fgetc(src_file);
			if (ch == '=') { tok.type = TOKEN_LSHIFT_ASSIGN; ch = fgetc(src_file); }
			else { tok.type = TOKEN_LSHIFT; }
		}
		else if (ch == '=') { tok.type = TOKEN_LESS_EQUAL; ch = fgetc(src_file); }
		else { tok.type = TOKEN_LESS; }
		break;

	case '>':
		ch = fgetc(src_file);
		if (ch == '>') {
			ch = fgetc(src_file);
			if (ch == '=') { tok.type = TOKEN_RSHIFT_ASSIGN; ch = fgetc(src_file); }
			else { tok.type = TOKEN_RSHIFT; }
		}
		else if (ch == '=') { tok.type = TOKEN_GREATER_EQUAL; ch = fgetc(src_file); }
		else { tok.type = TOKEN_GREATER; }
		break;

	case '-':
		ch = fgetc(src_file);
		if (ch == '-') { tok.type = TOKEN_DEC; ch = fgetc(src_file); }
		else if (ch == '>') { tok.type = TOKEN_ARROW; ch = fgetc(src_file); }
		else if (ch == '=') { tok.type = TOKEN_SUB_ASSIGN; ch = fgetc(src_file); }
		else tok.type = TOKEN_MINUS;
		break;

	case '=':
		ch = fgetc(src_file);
		if (ch == '=') { tok.type = TOKEN_EQUAL; ch = fgetc(src_file); }
		else tok.type = TOKEN_ASSIGN;
		break;
	default:
		fprintf(stderr, "Lexical Error: Unknown character: %c\n", ch);
		exit(1);
	}
	return tok;
}

// --- PRECISE LEXER STREAM LOOKAHEAD ENGINE ---
Token peek_next_token(void) {
	// 1. Snapshot the complete active state of the lexer stream
	long original_file_pos = ftell(src_file);
	int original_ch = ch;

	// 2. Extract the true next token using your existing lexer pipeline
	Token peeked_token = next_token();

	// 3. Perfectly restore the structural stream state back to the snapshot point
	fseek(src_file, original_file_pos, SEEK_SET);
	ch = original_ch;

	// 4. Return the captured token frame safely to the parser
	return peeked_token;
}
// --- END OF LOOKAHEAD ENGINE ---
