#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE_LEN 2048
#define MAX_NAME_LEN 64
#define MAX_IF_DEPTH 32
#define MAX_PARAMS 8

typedef struct MacroNode {
	char name[MAX_NAME_LEN];
	char replacement[MAX_LINE_LEN];
	int is_function_like;
	char param_names[MAX_PARAMS][MAX_NAME_LEN];
	int param_count;
	struct MacroNode* next;
} MacroNode;

static MacroNode* macro_head = NULL;

// Tracks nested #if / #ifdef / #ifndef / #elif / #else / #endif blocks
typedef struct {
	int parent_active;  // 1 if all enclosing #if blocks were active
	int condition_met;  // 1 if the current branch is active
	int branch_taken;   // 1 if any branch in this #if..#endif chain has already been taken
	int else_seen;      // 1 if #else has already been encountered at this level
} IfFrame;

static IfFrame if_stack[MAX_IF_DEPTH];
static int if_stack_top = 0;

static int should_emit_code(void) {
	if (if_stack_top == 0) return 1;
	return if_stack[if_stack_top - 1].condition_met;
}

// ------------------------------------------------------------------------
// MACRO TABLE MANAGEMENT
// ------------------------------------------------------------------------
void undefine_macro(const char* name) {
	MacroNode* curr = macro_head;
	MacroNode* prev = NULL;

	while (curr != NULL) {
		if (strcmp(curr->name, name) == 0) {
			if (prev == NULL) macro_head = curr->next;
			else prev->next = curr->next;
			free(curr);
			return;
		}
		prev = curr;
		curr = curr->next;
	}
}

void add_macro(const char* name, const char* replacement, int is_func, char params[MAX_PARAMS][MAX_NAME_LEN], int param_cnt) {
	undefine_macro(name);

	MacroNode* node = (MacroNode*)calloc(1, sizeof(MacroNode));
	if (!node) {
		fprintf(stderr, "Preprocessor Error: Out of memory.\n");
		exit(1);
	}
	strncpy(node->name, name, MAX_NAME_LEN - 1);
	strncpy(node->replacement, replacement, MAX_LINE_LEN - 1);
	node->is_function_like = is_func;
	node->param_count = param_cnt;

	for (int i = 0; i < param_cnt; i++) {
		strcpy(node->param_names[i], params[i]);
	}

	node->next = macro_head;
	macro_head = node;
}

MacroNode* lookup_macro(const char* name) {
	MacroNode* curr = macro_head;
	while (curr) {
		if (strcmp(curr->name, name) == 0) return curr;
		curr = curr->next;
	}
	return NULL;
}

void free_preprocessor_table(void) {
	MacroNode* curr = macro_head;
	while (curr) {
		MacroNode* next = curr->next;
		free(curr);
		curr = next;
	}
	macro_head = NULL;
}

// ------------------------------------------------------------------------
// LINE SPLICING (\) READER
// ------------------------------------------------------------------------
static int read_logical_line(FILE* src, char* out_buf, int max_len) {
	int total = 0;
	char phys[MAX_LINE_LEN];

	if (!fgets(phys, sizeof(phys), src)) {
		return 0; // EOF
	}

	while (1) {
		size_t len = strlen(phys);
		while (len > 0 && (phys[len - 1] == '\n' || phys[len - 1] == '\r')) {
			phys[--len] = '\0';
		}

		// Check if the physical line ends with a backslash '\'
		int has_splice = 0;
		if (len > 0 && phys[len - 1] == '\\') {
			phys[--len] = '\0';
			has_splice = 1;
		}

		if (total + (int)len >= max_len - 1) {
			len = (size_t)(max_len - 1 - total);
		}
		memcpy(out_buf + total, phys, len);
		total += (int)len;
		out_buf[total] = '\0';

		if (!has_splice) break;
		if (!fgets(phys, sizeof(phys), src)) break;
	}

	return 1;
}

// ------------------------------------------------------------------------
// MACRO EXPANSION ENGINE
// ------------------------------------------------------------------------
void replace_parameters(const char* rep_body, char params[MAX_PARAMS][MAX_NAME_LEN], char args[MAX_PARAMS][MAX_LINE_LEN], int count, char* output) {
	int i = 0, out_idx = 0;
	int len = (int)strlen(rep_body);

	while (i < len) {
		if (isalpha((unsigned char)rep_body[i]) || rep_body[i] == '_') {
			char tok[MAX_NAME_LEN] = { 0 };
			int t_len = 0;
			while (i < len && (isalnum((unsigned char)rep_body[i]) || rep_body[i] == '_')) {
				if (t_len < MAX_NAME_LEN - 1) tok[t_len++] = rep_body[i];
				i++;
			}
			tok[t_len] = '\0';

			int matched_param_idx = -1;
			for (int p = 0; p < count; p++) {
				if (strcmp(params[p], tok) == 0) {
					matched_param_idx = p;
					break;
				}
			}

			if (matched_param_idx != -1) {
				int arg_len = (int)strlen(args[matched_param_idx]);
				for (int a = 0; a < arg_len; a++) {
					if (out_idx < MAX_LINE_LEN * 2 - 1) output[out_idx++] = args[matched_param_idx][a];
				}
			}
			else {
				for (int t = 0; t < t_len; t++) {
					if (out_idx < MAX_LINE_LEN * 2 - 1) output[out_idx++] = tok[t];
				}
			}
		}
		else {
			if (out_idx < MAX_LINE_LEN * 2 - 1) output[out_idx++] = rep_body[i];
			i++;
		}
	}
	output[out_idx] = '\0';
}

void expand_line(const char* input_line, char* output_line) {
	int i = 0, out_idx = 0;
	int line_len = (int)strlen(input_line);

	while (i < line_len) {
		if (input_line[i] == '"') {
			output_line[out_idx++] = input_line[i++];
			while (i < line_len && input_line[i] != '"') {
				if (input_line[i] == '\\' && i + 1 < line_len) output_line[out_idx++] = input_line[i++];
				output_line[out_idx++] = input_line[i++];
			}
			if (i < line_len) output_line[out_idx++] = input_line[i++];
			continue;
		}

		if (isalpha((unsigned char)input_line[i]) || input_line[i] == '_') {
			char token_buf[MAX_NAME_LEN] = { 0 };
			int tok_len = 0;

			while (i < line_len && (isalnum((unsigned char)input_line[i]) || input_line[i] == '_')) {
				if (tok_len < MAX_NAME_LEN - 1) token_buf[tok_len++] = input_line[i];
				i++;
			}
			token_buf[tok_len] = '\0';

			MacroNode* macro = lookup_macro(token_buf);
			if (macro) {
				if (macro->is_function_like) {
					int save_i = i;
					while (i < line_len && isspace((unsigned char)input_line[i])) i++;

					if (i < line_len && input_line[i] == '(') {
						i++; // Consume '('
						char extracted_args[MAX_PARAMS][MAX_LINE_LEN] = { 0 };
						int arg_cnt = 0;

						while (i < line_len && input_line[i] != ')' && arg_cnt < MAX_PARAMS) {
							while (i < line_len && isspace((unsigned char)input_line[i])) i++;

							int a_idx = 0;
							int brace_depth = 0;
							while (i < line_len && (input_line[i] != ',' || brace_depth > 0) && input_line[i] != ')') {
								if (input_line[i] == '(') brace_depth++;
								if (input_line[i] == ')') brace_depth--;

								if (a_idx < MAX_LINE_LEN - 1) {
									extracted_args[arg_cnt][a_idx++] = input_line[i];
								}
								i++;
							}
							extracted_args[arg_cnt][a_idx] = '\0';

							while (a_idx > 0 && isspace((unsigned char)extracted_args[arg_cnt][a_idx - 1])) {
								extracted_args[arg_cnt][--a_idx] = '\0';
							}

							if (a_idx > 0 || input_line[i] == ',') arg_cnt++;
							if (input_line[i] == ',') i++;
						}
						if (input_line[i] == ')') i++;

						char parameterized_text[MAX_LINE_LEN * 2] = { 0 };
						replace_parameters(macro->replacement, macro->param_names, extracted_args, macro->param_count, parameterized_text);

						char recursively_expanded[MAX_LINE_LEN * 2] = { 0 };
						expand_line(parameterized_text, recursively_expanded);

						int final_len = (int)strlen(recursively_expanded);
						for (int r = 0; r < final_len; r++) output_line[out_idx++] = recursively_expanded[r];
					}
					else {
						i = save_i;
						for (int t = 0; t < tok_len; t++) output_line[out_idx++] = token_buf[t];
					}
				}
				else {
					char expanded_replacement[MAX_LINE_LEN * 2] = { 0 };
					expand_line(macro->replacement, expanded_replacement);
					int rep_len = (int)strlen(expanded_replacement);
					for (int r = 0; r < rep_len; r++) output_line[out_idx++] = expanded_replacement[r];
				}
			}
			else {
				for (int t = 0; t < tok_len; t++) output_line[out_idx++] = token_buf[t];
			}
		}
		else {
			output_line[out_idx++] = input_line[i++];
		}
	}
	output_line[out_idx] = '\0';
}

// ------------------------------------------------------------------------
// #if / #elif CONSTANT EXPRESSION EVALUATOR (WITH defined() SUPPORT)
// ------------------------------------------------------------------------
static void strip_comments_in_expr(char* line) {
	int in_str = 0, in_chr = 0;
	for (int i = 0; line[i] != '\0'; i++) {
		if (!in_chr && line[i] == '"' && (i == 0 || line[i - 1] != '\\')) in_str = !in_str;
		else if (!in_str && line[i] == '\'' && (i == 0 || line[i - 1] != '\\')) in_chr = !in_chr;
		else if (!in_str && !in_chr) {
			if (line[i] == '/' && line[i + 1] == '/') {
				line[i] = '\0';
				break;
			}
			if (line[i] == '/' && line[i + 1] == '*') {
				int j = i + 2;
				while (line[j] != '\0' && !(line[j] == '*' && line[j + 1] == '/')) j++;
				if (line[j] == '\0') {
					line[i] = '\0';
					break;
				}
				memset(line + i, ' ', (size_t)(j + 2 - i));
				i = j + 1;
			}
		}
	}
}

// Resolves `defined(IDENT)` and `defined IDENT` to `1` or `0` prior to macro expansion
static void resolve_defined_operators(const char* src, char* dst, int max_len) {
	int i = 0, out = 0;
	int len = (int)strlen(src);

	while (i < len && out < max_len - 2) {
		if (src[i] == '\'' || src[i] == '"') {
			char q = src[i];
			dst[out++] = src[i++];
			while (i < len && src[i] != q && out < max_len - 2) {
				if (src[i] == '\\' && i + 1 < len) dst[out++] = src[i++];
				dst[out++] = src[i++];
			}
			if (i < len && out < max_len - 2) dst[out++] = src[i++];
			continue;
		}

		if (isalpha((unsigned char)src[i]) || src[i] == '_') {
			int start = i;
			char ident[MAX_NAME_LEN] = { 0 };
			int ilen = 0;
			while (i < len && (isalnum((unsigned char)src[i]) || src[i] == '_')) {
				if (ilen < MAX_NAME_LEN - 1) ident[ilen++] = src[i];
				i++;
			}
			ident[ilen] = '\0';

			if (strcmp(ident, "defined") == 0) {
				while (i < len && isspace((unsigned char)src[i])) i++;
				int has_paren = 0;
				if (i < len && src[i] == '(') {
					has_paren = 1;
					i++;
					while (i < len && isspace((unsigned char)src[i])) i++;
				}

				char target[MAX_NAME_LEN] = { 0 };
				int tlen = 0;
				if (i < len && (isalpha((unsigned char)src[i]) || src[i] == '_')) {
					while (i < len && (isalnum((unsigned char)src[i]) || src[i] == '_')) {
						if (tlen < MAX_NAME_LEN - 1) target[tlen++] = src[i];
						i++;
					}
				}
				else {
					fprintf(stderr, "Preprocessor Error: Invalid identifier after 'defined'.\n");
					exit(1);
				}
				target[tlen] = '\0';

				if (has_paren) {
					while (i < len && isspace((unsigned char)src[i])) i++;
					if (i >= len || src[i] != ')') {
						fprintf(stderr, "Preprocessor Error: Missing ')' after defined(%s).\n", target);
						exit(1);
					}
					i++; // Consume ')'
				}

				dst[out++] = (lookup_macro(target) != NULL) ? '1' : '0';
				dst[out++] = ' ';
			}
			else {
				int copy_len = i - start;
				if (out + copy_len >= max_len - 1) copy_len = max_len - 1 - out;
				memcpy(dst + out, src + start, (size_t)copy_len);
				out += copy_len;
			}
		}
		else {
			dst[out++] = src[i++];
		}
	}
	dst[out] = '\0';
}

typedef struct {
	const char* p;
} ExprParser;

static void skip_ws(ExprParser* ep) {
	while (*ep->p && isspace((unsigned char)*ep->p)) ep->p++;
}

static long parse_if_ternary(ExprParser* ep);

static long parse_if_primary(ExprParser* ep) {
	skip_ws(ep);

	if (*ep->p == '(') {
		ep->p++;
		long val = parse_if_ternary(ep);
		skip_ws(ep);
		if (*ep->p != ')') {
			fprintf(stderr, "Preprocessor Error: Missing ')' in #if expression.\n");
			exit(1);
		}
		ep->p++;
		return val;
	}

	// Character constant in #if (e.g., 'A', '\n', '\x41')
	if (*ep->p == '\'') {
		ep->p++;
		long val = 0;
		if (*ep->p == '\\') {
			ep->p++;
			switch (*ep->p) {
			case 'n': val = '\n'; ep->p++; break;
			case 'r': val = '\r'; ep->p++; break;
			case 't': val = '\t'; ep->p++; break;
			case '0': val = '\0'; ep->p++; break;
			case '\\': val = '\\'; ep->p++; break;
			case '\'': val = '\''; ep->p++; break;
			case 'x': {
				ep->p++;
				while (isxdigit((unsigned char)*ep->p)) {
					int d = isdigit((unsigned char)*ep->p) ? (*ep->p - '0') : (tolower((unsigned char)*ep->p) - 'a' + 10);
					val = val * 16 + d;
					ep->p++;
				}
				break;
			}
			default: val = (unsigned char)*ep->p++; break;
			}
		}
		else if (*ep->p && *ep->p != '\'') {
			val = (unsigned char)*ep->p++;
		}
		if (*ep->p == '\'') ep->p++;
		return val;
	}

	// Integer constant (hex, octal, decimal + U/L suffixes)
	if (isdigit((unsigned char)*ep->p)) {
		char* endp = NULL;
		unsigned long uval = strtoul(ep->p, &endp, 0);
		ep->p = endp;
		while (*ep->p == 'u' || *ep->p == 'U' || *ep->p == 'l' || *ep->p == 'L') {
			ep->p++;
		}
		return (long)uval;
	}

	// C89 rule: Any remaining unexpanded identifier in #if evaluates to 0
	if (isalpha((unsigned char)*ep->p) || *ep->p == '_') {
		while (isalnum((unsigned char)*ep->p) || *ep->p == '_') ep->p++;
		return 0;
	}

	fprintf(stderr, "Preprocessor Error: Unexpected token '%s' in #if expression.\n", ep->p);
	exit(1);
}

static long parse_if_unary(ExprParser* ep) {
	skip_ws(ep);
	if (*ep->p == '!') { ep->p++; return !parse_if_unary(ep); }
	if (*ep->p == '~') { ep->p++; return ~parse_if_unary(ep); }
	if (*ep->p == '+') { ep->p++; return +parse_if_unary(ep); }
	if (*ep->p == '-') { ep->p++; return -parse_if_unary(ep); }
	return parse_if_primary(ep);
}

static long parse_if_mul(ExprParser* ep) {
	long left = parse_if_unary(ep);
	while (1) {
		skip_ws(ep);
		if (*ep->p == '*') { ep->p++; left *= parse_if_unary(ep); }
		else if (*ep->p == '/') {
			ep->p++;
			long right = parse_if_unary(ep);
			if (right == 0) { fprintf(stderr, "Preprocessor Error: Division by zero in #if.\n"); exit(1); }
			left /= right;
		}
		else if (*ep->p == '%') {
			ep->p++;
			long right = parse_if_unary(ep);
			if (right == 0) { fprintf(stderr, "Preprocessor Error: Modulo by zero in #if.\n"); exit(1); }
			left %= right;
		}
		else break;
	}
	return left;
}

static long parse_if_add(ExprParser* ep) {
	long left = parse_if_mul(ep);
	while (1) {
		skip_ws(ep);
		if (*ep->p == '+') { ep->p++; left += parse_if_mul(ep); }
		else if (*ep->p == '-') { ep->p++; left -= parse_if_mul(ep); }
		else break;
	}
	return left;
}

static long parse_if_shift(ExprParser* ep) {
	long left = parse_if_add(ep);
	while (1) {
		skip_ws(ep);
		if (ep->p[0] == '<' && ep->p[1] == '<') { ep->p += 2; left <<= parse_if_add(ep); }
		else if (ep->p[0] == '>' && ep->p[1] == '>') { ep->p += 2; left >>= parse_if_add(ep); }
		else break;
	}
	return left;
}

static long parse_if_rel(ExprParser* ep) {
	long left = parse_if_shift(ep);
	while (1) {
		skip_ws(ep);
		if (ep->p[0] == '<' && ep->p[1] == '=') { ep->p += 2; left = (left <= parse_if_shift(ep)); }
		else if (ep->p[0] == '>' && ep->p[1] == '=') { ep->p += 2; left = (left >= parse_if_shift(ep)); }
		else if (ep->p[0] == '<' && ep->p[1] != '<') { ep->p += 1; left = (left < parse_if_shift(ep)); }
		else if (ep->p[0] == '>' && ep->p[1] != '>') { ep->p += 1; left = (left > parse_if_shift(ep)); }
		else break;
	}
	return left;
}

static long parse_if_eq(ExprParser* ep) {
	long left = parse_if_rel(ep);
	while (1) {
		skip_ws(ep);
		if (ep->p[0] == '=' && ep->p[1] == '=') { ep->p += 2; left = (left == parse_if_rel(ep)); }
		else if (ep->p[0] == '!' && ep->p[1] == '=') { ep->p += 2; left = (left != parse_if_rel(ep)); }
		else break;
	}
	return left;
}

static long parse_if_bitand(ExprParser* ep) {
	long left = parse_if_eq(ep);
	while (1) {
		skip_ws(ep);
		if (ep->p[0] == '&' && ep->p[1] != '&') { ep->p += 1; left &= parse_if_eq(ep); }
		else break;
	}
	return left;
}

static long parse_if_bitxor(ExprParser* ep) {
	long left = parse_if_bitand(ep);
	while (1) {
		skip_ws(ep);
		if (*ep->p == '^') { ep->p += 1; left ^= parse_if_bitand(ep); }
		else break;
	}
	return left;
}

static long parse_if_bitor(ExprParser* ep) {
	long left = parse_if_bitxor(ep);
	while (1) {
		skip_ws(ep);
		if (ep->p[0] == '|' && ep->p[1] != '|') { ep->p += 1; left |= parse_if_bitxor(ep); }
		else break;
	}
	return left;
}

static long parse_if_logand(ExprParser* ep) {
	long left = parse_if_bitor(ep);
	while (1) {
		skip_ws(ep);
		if (ep->p[0] == '&' && ep->p[1] == '&') {
			ep->p += 2;
			long right = parse_if_bitor(ep);
			left = (left != 0 && right != 0);
		}
		else break;
	}
	return left;
}

static long parse_if_logor(ExprParser* ep) {
	long left = parse_if_logand(ep);
	while (1) {
		skip_ws(ep);
		if (ep->p[0] == '|' && ep->p[1] == '|') {
			ep->p += 2;
			long right = parse_if_logand(ep);
			left = (left != 0 || right != 0);
		}
		else break;
	}
	return left;
}

static long parse_if_ternary(ExprParser* ep) {
	long cond = parse_if_logor(ep);
	skip_ws(ep);
	if (*ep->p == '?') {
		ep->p++;
		long t_val = parse_if_ternary(ep);
		skip_ws(ep);
		if (*ep->p != ':') {
			fprintf(stderr, "Preprocessor Error: Expected ':' in #if ternary expression.\n");
			exit(1);
		}
		ep->p++;
		long f_val = parse_if_ternary(ep);
		return cond ? t_val : f_val;
	}
	return cond;
}

static int evaluate_if_expression(const char* raw_expr) {
	char clean_expr[MAX_LINE_LEN] = { 0 };
	strncpy(clean_expr, raw_expr, MAX_LINE_LEN - 1);
	strip_comments_in_expr(clean_expr);

	char def_resolved[MAX_LINE_LEN * 2] = { 0 };
	resolve_defined_operators(clean_expr, def_resolved, sizeof(def_resolved));

	char expanded[MAX_LINE_LEN * 2] = { 0 };
	expand_line(def_resolved, expanded);

	ExprParser ep = { expanded };
	long result = parse_if_ternary(&ep);
	skip_ws(&ep);
	if (*ep.p != '\0') {
		fprintf(stderr, "Preprocessor Error: Trailing tokens '%s' in #if expression.\n", ep.p);
		exit(1);
	}
	return (result != 0);
}

// ------------------------------------------------------------------------
// FILE STREAM DIRECTIVE & LINE PROCESSOR
// ------------------------------------------------------------------------
void process_file_stream(FILE* src, FILE* out) {
	char line_buffer[MAX_LINE_LEN];

	while (read_logical_line(src, line_buffer, sizeof(line_buffer))) {
		char* walk = line_buffer;
		while (*walk && isspace((unsigned char)*walk)) walk++;

		if (*walk == '#') {
			walk++; // Step past '#'
			while (*walk && isspace((unsigned char)*walk)) walk++;

			char directive[16] = { 0 };
			int chars_read = 0;
			if (sscanf(walk, "%15s%n", directive, &chars_read) != 1) {
				continue;
			}
			walk += chars_read;

			// 1. #include
			if (strcmp(directive, "include") == 0) {
				if (!should_emit_code()) continue;

				char* start_quote = strchr(walk, '"');
				if (start_quote) {
					start_quote++;
					char* end_quote = strchr(start_quote, '"');
					if (end_quote) {
						char header_filename[MAX_NAME_LEN] = { 0 };
						size_t name_len = (size_t)(end_quote - start_quote);
						if (name_len < MAX_NAME_LEN) {
							strncpy(header_filename, start_quote, name_len);

							FILE* inc_file = fopen(header_filename, "r");
							if (!inc_file) {
								fprintf(stderr, "Preprocessor Error: Cannot open include header '%s'\n", header_filename);
								exit(1);
							}
							process_file_stream(inc_file, out);
							fclose(inc_file);
							continue;
						}
					}
				}
				continue;
			}

			// 2. #if
			if (strcmp(directive, "if") == 0) {
				if (if_stack_top >= MAX_IF_DEPTH) {
					fprintf(stderr, "Preprocessor Error: Conditional nesting overflow.\n");
					exit(1);
				}
				int parent_ok = should_emit_code();
				int cond = parent_ok ? evaluate_if_expression(walk) : 0;
				if_stack[if_stack_top++] = (IfFrame){ parent_ok, cond, cond, 0 };
				continue;
			}

			// 3. #ifdef
			if (strcmp(directive, "ifdef") == 0) {
				if (if_stack_top >= MAX_IF_DEPTH) exit(1);
				char target_macro[MAX_NAME_LEN] = { 0 };
				sscanf(walk, "%63s", target_macro);
				int parent_ok = should_emit_code();
				int cond = parent_ok ? (lookup_macro(target_macro) != NULL) : 0;
				if_stack[if_stack_top++] = (IfFrame){ parent_ok, cond, cond, 0 };
				continue;
			}

			// 4. #ifndef
			if (strcmp(directive, "ifndef") == 0) {
				if (if_stack_top >= MAX_IF_DEPTH) exit(1);
				char target_macro[MAX_NAME_LEN] = { 0 };
				sscanf(walk, "%63s", target_macro);
				int parent_ok = should_emit_code();
				int cond = parent_ok ? (lookup_macro(target_macro) == NULL) : 0;
				if_stack[if_stack_top++] = (IfFrame){ parent_ok, cond, cond, 0 };
				continue;
			}

			// 5. #elif
			if (strcmp(directive, "elif") == 0) {
				if (if_stack_top == 0 || if_stack[if_stack_top - 1].else_seen) {
					fprintf(stderr, "Preprocessor Error: Misplaced #elif.\n");
					exit(1);
				}
				IfFrame* top = &if_stack[if_stack_top - 1];
				if (top->parent_active && !top->branch_taken) {
					int cond = evaluate_if_expression(walk);
					top->condition_met = cond;
					if (cond) top->branch_taken = 1;
				}
				else {
					top->condition_met = 0;
				}
				continue;
			}

			// 6. #else
			if (strcmp(directive, "else") == 0) {
				if (if_stack_top == 0 || if_stack[if_stack_top - 1].else_seen) {
					fprintf(stderr, "Preprocessor Error: Misplaced or duplicate #else.\n");
					exit(1);
				}
				IfFrame* top = &if_stack[if_stack_top - 1];
				top->else_seen = 1;
				top->condition_met = (top->parent_active && !top->branch_taken);
				top->branch_taken = 1;
				continue;
			}

			// 7. #endif
			if (strcmp(directive, "endif") == 0) {
				if (if_stack_top == 0) {
					fprintf(stderr, "Preprocessor Error: Unmatched #endif.\n");
					exit(1);
				}
				if_stack_top--;
				continue;
			}

			// 8. #undef
			if (strcmp(directive, "undef") == 0) {
				if (!should_emit_code()) continue;
				char macro_to_drop[MAX_NAME_LEN] = { 0 };
				sscanf(walk, "%63s", macro_to_drop);
				undefine_macro(macro_to_drop);
				continue;
			}

			// 9. #define
			if (strcmp(directive, "define") == 0) {
				if (!should_emit_code()) continue;

				while (*walk && isspace((unsigned char)*walk)) walk++;

				char m_name[MAX_NAME_LEN] = { 0 };
				int n_read = 0;
				sscanf(walk, "%63[^ (]%n", m_name, &n_read);
				walk += n_read;

				int is_func = 0;
				char params[MAX_PARAMS][MAX_NAME_LEN] = { 0 };
				int param_cnt = 0;

				if (*walk == '(') {
					is_func = 1;
					walk++;
					while (*walk && *walk != ')') {
						while (*walk && (isspace((unsigned char)*walk) || *walk == ',')) walk++;
						if (*walk && *walk != ')') {
							char p_name[MAX_NAME_LEN] = { 0 };
							sscanf(walk, "%63[A-Za-z0-9_]%n", p_name, &n_read);
							walk += n_read;
							if (strlen(p_name) > 0 && param_cnt < MAX_PARAMS) {
								strcpy(params[param_cnt++], p_name);
							}
						}
					}
					if (*walk == ')') walk++;
				}

				char m_val[MAX_LINE_LEN] = { 0 };
				while (*walk && isspace((unsigned char)*walk)) walk++;
				strncpy(m_val, walk, MAX_LINE_LEN - 1);

				add_macro(m_name, m_val, is_func, params, param_cnt);
				continue;
			}

			continue;
		}

		if (should_emit_code()) {
			char processed_line[MAX_LINE_LEN * 2] = { 0 };
			expand_line(line_buffer, processed_line);
			fprintf(out, "%s\n", processed_line);
		}
	}
}

int run_preprocessor(const char* input_filename, const char* output_filename) {
	FILE* src = fopen(input_filename, "r");
	if (!src) {
		fprintf(stderr, "Preprocessor Error: Could not open source file %s\n", input_filename);
		return 0;
	}
	FILE* out = fopen(output_filename, "w");
	if (!out) {
		fprintf(stderr, "Preprocessor Error: Could not generate preprocessed staging file %s\n", output_filename);
		fclose(src);
		return 0;
	}

	if_stack_top = 0;
	process_file_stream(src, out);

	if (if_stack_top > 0) {
		fprintf(stderr, "Preprocessor Error: Unclosed conditional compilation block. Missing #endif.\n");
		fclose(src);
		fclose(out);
		return 0;
	}

	fclose(src);
	fclose(out);
	free_preprocessor_table();
	return 1;
}
