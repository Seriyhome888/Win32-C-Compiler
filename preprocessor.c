#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_LINE_LEN 512
#define MAX_NAME_LEN 64

// Struct to store defined text-substitution macros
typedef struct MacroNode {
    char name[MAX_NAME_LEN];
    char replacement[MAX_LINE_LEN];
    struct MacroNode* next;
} MacroNode;

static MacroNode* macro_head = NULL;

// Helper to add macros to our preprocessor tracking table
void add_macro(const char* name, const char* replacement) {
    // Check if macro already exists to handle redefinitions (C89 rule)
    MacroNode* curr = macro_head;
    while (curr) {
        if (strcmp(curr->name, name) == 0) {
            strcpy(curr->replacement, replacement);
            return;
        }
        curr = curr->next;
    }

    // Allocate a new macro node
    MacroNode* node = (MacroNode*)malloc(sizeof(MacroNode));
    if (!node) {
        fprintf(stderr, "Preprocessor Error: Out of memory during macro allocation.\n");
        exit(1);
    }
    strncpy(node->name, name, MAX_NAME_LEN - 1);
    strncpy(node->replacement, replacement, MAX_LINE_LEN - 1);
    node->next = macro_head;
    macro_head = node;
}

// Helper to look up if an identifier string is a registered macro
MacroNode* lookup_macro(const char* name) {
    MacroNode* curr = macro_head;
    while (curr) {
        if (strcmp(curr->name, name) == 0) {
            return curr;
        }
        curr = curr->next;
    }
    return NULL;
}

// Scans an entire line of code and expands any recognized macro substrings
void expand_line(const char* input_line, char* output_line) {
    int i = 0;
    int out_idx = 0;
    int line_len = (int)strlen(input_line);

    while (i < line_len) {
        // Skip string literals entirely so macros aren't expanded inside text strings
        if (input_line[i] == '"') {
            output_line[out_idx++] = input_line[i++];
            while (i < line_len && input_line[i] != '"') {
                if (input_line[i] == '\\' && i + 1 < line_len) {
                    output_line[out_idx++] = input_line[i++];
                }
                output_line[out_idx++] = input_line[i++];
            }
            if (i < line_len) {
                output_line[out_idx++] = input_line[i++];
            }
            continue;
        }

        // Identify if a structural word or identifier token is starting
        if (isalpha((unsigned char)input_line[i]) || input_line[i] == '_') {
            char token_buf[MAX_NAME_LEN] = { 0 };
            int tok_len = 0;

            while (i < line_len && (isalnum((unsigned char)input_line[i]) || input_line[i] == '_')) {
                if (tok_len < MAX_NAME_LEN - 1) {
                    token_buf[tok_len++] = input_line[i];
                }
                i++;
            }
            token_buf[tok_len] = '\0';

            // Check if this token matches an active macro sequence
            MacroNode* macro = lookup_macro(token_buf);
            if (macro) {
                // Perform recursive expansion if the macro text contains another macro
                char expanded_replacement[MAX_LINE_LEN] = { 0 };
                expand_line(macro->replacement, expanded_replacement);

                int rep_len = (int)strlen(expanded_replacement);
                for (int r = 0; r < rep_len; r++) {
                    output_line[out_idx++] = expanded_replacement[r];
                }
            } else {
                // If it is not a macro, copy the token text as-is
                for (int t = 0; t < tok_len; t++) {
                    output_line[out_idx++] = token_buf[t];
                }
            }
        } else {
            // Standard punctuation, numeric, or operator symbols get passed straight through
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

// The main processing pipeline routine
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

    char line_buffer[MAX_LINE_LEN];

    while (fgets(line_buffer, sizeof(line_buffer), src)) {
        // Strip out trailing carriage returns or newlines for uniform evaluations
		// Correct way to clean trailing line transitions:
        size_t len = strlen(line_buffer);
        while (len > 0 && (line_buffer[len - 1] == '\n' || line_buffer[len - 1] == '\r')) {
            line_buffer[--len] = '\0';
        }

        // Trim leading spaces to check if this line is a preprocessor directive
        char* walk = line_buffer;
        while (*walk && isspace((unsigned char)*walk)) {
            walk++;
        }

        // If the line starts with a macro directive `#`
        if (*walk == '#') {
            walk++; // Move past '#'
            while (*walk && isspace((unsigned char)*walk)) walk++; // Skip extra spaces

            char directive[16] = { 0 };
            sscanf(walk, "%15s", directive);

            if (strcmp(directive, "define") == 0) {
                walk += 6; // Move past "define"
                char m_name[MAX_NAME_LEN] = { 0 };
                char m_val[MAX_LINE_LEN] = { 0 };

                // Parse the name and value of the macro
                if (sscanf(walk, "%63s %511[^\n]", m_name, m_val) == 2) {
                    add_macro(m_name, m_val);
                } else if (sscanf(walk, "%63s", m_name) == 1) {
                    // Handles flag macros without explicit values (e.g., #define DEBUG)
                    add_macro(m_name, "1");
                }
            }
            // Directives are consumed here and are not written to the clean code output
            continue;
        }

        // For standard code lines, expand any macro variables and write to file
        char processed_line[MAX_LINE_LEN * 2] = { 0 };
        expand_line(line_buffer, processed_line);
        fprintf(out, "%s\n", processed_line);
    }

    fclose(src);
    fclose(out);
    free_preprocessor_table();
    return 1;
}
