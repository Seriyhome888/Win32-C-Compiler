#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE_LEN 512
#define MAX_NAME_LEN 64
#define MAX_IF_DEPTH 32

// Struct to store defined text-substitution macros
#define MAX_PARAMS 8

typedef struct MacroNode {
	char name[MAX_NAME_LEN];
	char replacement[MAX_LINE_LEN];

	// Advanced Parameter Tracking Properties
	int is_function_like;                  // 0 = Object-like (#define PI 3.14), 1 = Function-like (#define ADD(x,y))
	char param_names[MAX_PARAMS][MAX_NAME_LEN];
	int param_count;

	struct MacroNode* next;
} MacroNode;

static MacroNode* macro_head = NULL;

// Structural stack frames to manage nested #ifdef / #ifndef blocks
typedef struct {
	int condition_met;  // 1 if this block's condition evaluates to true, 0 if false
	int else_seen;      // 1 if we have already encountered a #else inside this block frame
} IfFrame;

static IfFrame if_stack[MAX_IF_DEPTH];
static int if_stack_top = 0;

// Helper to determine if we should currently write code lines to the output stream
static int should_emit_code(void) {
	for (int i = 0; i < if_stack_top; i++) {
		if (!if_stack[i].condition_met) {
			return 0; // If any parent block condition is false, skip execution tracking
		}
	}
	return 1;
}

// ------------------------------------------------------------------------
// NEW FUNCTIONALITY: #undef IMPLEMENTATION
// ------------------------------------------------------------------------
void undefine_macro(const char* name) {
	MacroNode* curr = macro_head;
	MacroNode* prev = NULL;

	while (curr != NULL) {
		if (strcmp(curr->name, name) == 0) {
			if (prev == NULL) {
				macro_head = curr->next;
			}
			else {
				prev->next = curr->next;
			}
			free(curr);
			return; // Successfully unlinked and dropped from memory
		}
		prev = curr;
		curr = curr->next;
	}
}

void add_macro(const char* name, const char* replacement, int is_func, char params[MAX_PARAMS][MAX_NAME_LEN], int param_cnt) {
	// If macro already exists, overwrite it (C89 rule)
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

// Helper to look up an parameter token and substitute it inside the replacement block
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

			// Check if this structural identifier is a macro parameter name
			int matched_param_idx = -1;
			for (int p = 0; p < count; p++) {
				if (strcmp(params[p], tok) == 0) {
					matched_param_idx = p;
					break;
				}
			}

			if (matched_param_idx != -1) {
				// Swap parameter out for the evaluated incoming argument text segment string
				int arg_len = (int)strlen(args[matched_param_idx]);
				for (int a = 0; a < arg_len; a++) {
					output[out_idx++] = args[matched_param_idx][a];
				}
			}
			else {
				for (int t = 0; t < t_len; t++) output[out_idx++] = tok[t];
			}
		}
		else {
			output[out_idx++] = rep_body[i++];
		}
	}
	output[out_idx] = '\0';
}

// ------------------------------------------------------------------------
// ADVANCED REWRITTEN LINE EXPANSION PIPELINE
// ------------------------------------------------------------------------
void expand_line(const char* input_line, char* output_line) {
	int i = 0, out_idx = 0;
	int line_len = (int)strlen(input_line);

	while (i < line_len) {
		if (input_line[i] == '"') { // Protect literal strings
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
				// CASE A: FUNCTION-LIKE MACRO MATCHED
				if (macro->is_function_like) {
					// Skip any optional layout whitespace tokens to locate opening parenthesis '('
					while (i < line_len && isspace((unsigned char)input_line[i])) i++;

					if (i < line_len && input_line[i] == '(') {
						i++; // Consume '('
						char extracted_args[MAX_PARAMS][MAX_LINE_LEN] = { 0 };
						int arg_cnt = 0;

						// Parse out arguments separated by commas, tracking matched nested brackets safely
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

							// Trim trailing layout whitespace from parsed argument
							while (a_idx > 0 && isspace((unsigned char)extracted_args[arg_cnt][a_idx - 1])) {
								extracted_args[arg_cnt][--a_idx] = '\0';
							}

							if (a_idx > 0) arg_cnt++;
							if (input_line[i] == ',') i++; // Consume comma
						}
						if (input_line[i] == ')') i++; // Consume closing ')'

						// Inject arguments into macro replacement template string
						char parameterized_text[MAX_LINE_LEN * 2] = { 0 };
						replace_parameters(macro->replacement, macro->param_names, extracted_args, macro->param_count, parameterized_text);

						// Recursively pass the newly expanded text through the expansion engine 
						char recursively_expanded[MAX_LINE_LEN * 2] = { 0 };
						expand_line(parameterized_text, recursively_expanded);

						int final_len = (int)strlen(recursively_expanded);
						for (int r = 0; r < final_len; r++) output_line[out_idx++] = recursively_expanded[r];
					}
					else {
						// Function-like macro named without brackets used as normal text string token
						for (int t = 0; t < tok_len; t++) output_line[out_idx++] = token_buf[t];
					}
				}
				else {
					// CASE B: OBJECT-LIKE MACRO MATCHED
					char expanded_replacement[MAX_LINE_LEN] = { 0 };
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

// Cleanly unlinks and frees the macro table memory when processing finishes
void free_preprocessor_table(void) {
	MacroNode* curr = macro_head;
	while (curr) {
		MacroNode* next = curr->next;
		free(curr);
		curr = next;
	}
	macro_head = NULL;
}

// Forward declaration to allow recursive processing for nested file insertions (#include)
void process_file_stream(FILE* src, FILE* out);

void process_file_stream(FILE* src, FILE* out) {
	char line_buffer[MAX_LINE_LEN];

	while (fgets(line_buffer, sizeof(line_buffer), src)) {
		// Clean trailing transitions perfectly
		size_t len = strlen(line_buffer);
		while (len > 0 && (line_buffer[len - 1] == '\n' || line_buffer[len - 1] == '\r')) {
			line_buffer[--len] = '\0';
		}

		// Trim leading spaces
		char* walk = line_buffer;
		while (*walk && isspace((unsigned char)*walk)) {
			walk++;
		}

		if (*walk == '#') {
			walk++; // Step past '#'
			while (*walk && isspace((unsigned char)*walk)) walk++;

			// Extract the directive keyword cleanly
			char directive[16] = { 0 };
			int chars_read = 0;
			if (sscanf(walk, "%15s%n", directive, &chars_read) != 1) {
				continue;
			}
			walk += chars_read; // Advance past the directive word safely

			// 1. HANDLE INCLUDE DIRECTIVE
			if (strcmp(directive, "include") == 0) {
				if (!should_emit_code()) continue;

				char* start_quote = strchr(walk, '"');
				if (start_quote) {
					start_quote++;
					char* end_quote = strchr(start_quote, '"');
					if (end_quote) {
						char header_filename[MAX_NAME_LEN] = { 0 };
						size_t name_len = end_quote - start_quote;
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

			// 2. HANDLE IFDEF
			if (strcmp(directive, "ifdef") == 0) {
				char target_macro[MAX_NAME_LEN] = { 0 };
				sscanf(walk, "%63s", target_macro);
				if (if_stack_top >= MAX_IF_DEPTH) exit(1);
				int exists = (lookup_macro(target_macro) != NULL);
				if_stack[if_stack_top++] = (IfFrame){ exists, 0 };
				continue;
			}

			// 3. HANDLE IFNDEF
			if (strcmp(directive, "ifndef") == 0) {
				char target_macro[MAX_NAME_LEN] = { 0 };
				sscanf(walk, "%63s", target_macro);
				if (if_stack_top >= MAX_IF_DEPTH) exit(1);
				int not_exists = (lookup_macro(target_macro) == NULL);
				if_stack[if_stack_top++] = (IfFrame){ not_exists, 0 };
				continue;
			}

			// 4. HANDLE ELSE
			if (strcmp(directive, "else") == 0) {
				if (if_stack_top == 0) exit(1);
				if_stack[if_stack_top - 1].condition_met = !if_stack[if_stack_top - 1].condition_met;
				continue;
			}

			// 5. HANDLE ENDIF
			if (strcmp(directive, "endif") == 0) {
				if (if_stack_top == 0) exit(1);
				if_stack_top--;
				continue;
			}

			// 6. HANDLE DEFINE
// ------------------------------------------------------------------------
// Drop-in updates for Directive Parsing inside process_file_stream:
// ------------------------------------------------------------------------

// HANDLE #undef DIRECTIVE
			if (strcmp(directive, "undef") == 0) {
				if (!should_emit_code()) continue;
				char macro_to_drop[MAX_NAME_LEN] = { 0 };
				sscanf(walk, "%63s", macro_to_drop);
				undefine_macro(macro_to_drop);
				continue;
			}

			// REWRITTEN ENHANCED #define DIRECTIVE
			if (strcmp(directive, "define") == 0) {
				if (!should_emit_code()) continue;

				while (*walk && isspace((unsigned char)*walk)) walk++;

				char m_name[MAX_NAME_LEN] = { 0 };
				int chars_read = 0;
				sscanf(walk, "%63[^ (]%n", m_name, &chars_read);
				walk += chars_read;

				int is_func = 0;
				char params[MAX_PARAMS][MAX_NAME_LEN] = { 0 };
				int param_cnt = 0;

				// Look for an immediate opening parenthesis (no spaces allowed for function-like macros!)
				if (*walk == '(') {
					is_func = 1;
					walk++; // Step past '('
					while (*walk && *walk != ')') {
						while (*walk && (isspace((unsigned char)*walk) || *walk == ',')) walk++;
						if (*walk && *walk != ')') {
							char p_name[MAX_NAME_LEN] = { 0 };
							sscanf(walk, "%63[A-Za-z0-9_]%n", p_name, &chars_read);
							walk += chars_read;
							if (strlen(p_name) > 0 && param_cnt < MAX_PARAMS) {
								strcpy(params[param_cnt++], p_name);
							}
						}
					}
					if (*walk == ')') walk++;
				}

				char m_val[MAX_LINE_LEN] = { 0 };
				while (*walk && isspace((unsigned char)*walk)) walk++;
				strcpy(m_val, walk); // The remainder of the line forms the substitution value template

				add_macro(m_name, m_val, is_func, params, param_cnt);
				continue;
			}

			continue;
		}

		// Emit text only if condition stack is valid
		if (should_emit_code()) {
			char processed_line[MAX_LINE_LEN * 2] = { 0 };
			expand_line(line_buffer, processed_line);
			fprintf(out, "%s\n", processed_line);
		}
	}
}

// Global invocation entry routine
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
	// Reset processing parameters
	if_stack_top = 0;
	// Run processing pass
	process_file_stream(src, out);
	// Enforce C89 file block balance restrictions
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