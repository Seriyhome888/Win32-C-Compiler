#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#define WINDOW_SIZE 3
#define MAX_LINE 256
#define MAX_LABELS 2048

static char label_registry[MAX_LABELS][64];
static int label_ref_counts[MAX_LABELS] = { 0 };
static int registered_label_count = 0;

static char window[WINDOW_SIZE][MAX_LINE];
static int window_count = 0;

static void clean_line(char* out, const char* in) {
	while (*in && isspace((unsigned char)*in)) in++;
	strcpy(out, in);
	size_t len = strlen(out);
	while (len > 0 && isspace((unsigned char)out[len - 1])) {
		out[--len] = '\0';
	}
}

void peephole_scan_labels(const char* raw_line) {
	char clean[MAX_LINE] = { 0 };
	clean_line(clean, raw_line);

	char op[16], target[64];
	if (sscanf(clean, "%15s %63s", op, target) == 2) {
		if (strcmp(op, "jmp") == 0 || op[0] == 'j') {
			target[strcspn(target, " \t\r\n")] = '\0';

			int found = 0;
			for (int i = 0; i < registered_label_count; i++) {
				if (strcmp(label_registry[i], target) == 0) {
					label_ref_counts[i]++;
					found = 1;
					break;
				}
			}
			if (!found && registered_label_count < MAX_LABELS) {
				strcpy(label_registry[registered_label_count], target);
				label_ref_counts[registered_label_count] = 1;
				registered_label_count++;
			}
		}
	}
}

static void flush_oldest(FILE* out_file) {
	if (window_count > 0) {
		fprintf(out_file, "%s\n", window[0]);
		for (int i = 1; i < window_count; i++) {
			strcpy(window[i - 1], window[i]);
		}
		memset(window[window_count - 1], 0, MAX_LINE);
		window_count--;
	}
}

void optimize_and_emit(FILE* out_file, const char* raw_line) {
	char clean[MAX_LINE] = { 0 };
	clean_line(clean, raw_line);

	// 1. FILTER: Drop unreferenced layout labels entirely before window buffering
	char lbl_name[64];
	if (sscanf(clean, "%63[^:]:", lbl_name) == 1 && strchr(clean, ':') != NULL) {
		lbl_name[strcspn(lbl_name, " \t\r\n")] = '\0';
		if (lbl_name[0] == 'L' || lbl_name[0] == '$') {
			int is_referenced = 0;
			for (int i = 0; i < registered_label_count; i++) {
				if (strcmp(label_registry[i], lbl_name) == 0 && label_ref_counts[i] > 0) {
					is_referenced = 1;
					break;
				}
			}
			if (!is_referenced) return;
		}
	}

	if (window_count >= WINDOW_SIZE) {
		flush_oldest(out_file);
	}

	strcpy(window[window_count++], raw_line);

	// 2. PASS: Apply FIFO pattern reductions over active slots
	while (1) {
		if (window_count >= 1) {
			char c0[MAX_LINE] = { 0 };
			clean_line(c0, window[window_count - 1]);

			// Rule 1: Identity Move Elimination (mov eax, eax)
			char r1[16], r2[16];
			if (sscanf(c0, "mov %15[^,], %15s", r1, r2) == 2) {
				if (strcmp(r1, r2) == 0) {
					memset(window[window_count - 1], 0, MAX_LINE);
					window_count--;
					continue;
				}
			}
		}

		if (window_count >= 2) {
			char c0[MAX_LINE] = { 0 };
			char c1[MAX_LINE] = { 0 };
			clean_line(c0, window[window_count - 2]);
			clean_line(c1, window[window_count - 1]);

			// Rule 2: Redundant Spill/Reload Cleanups (mov [mem], eax; mov eax, [mem])
			char src_reg[16], dest_mem[64], dest_reg[16], src_mem[64];
			if (sscanf(c0, "mov %63[^,], %15s", dest_mem, src_reg) == 2 &&
				sscanf(c1, "mov %15[^,], %63s", dest_reg, src_mem) == 2) {
				if (strcmp(src_reg, dest_reg) == 0 && strcmp(dest_mem, src_mem) == 0) {
					// Drop c1 out of the tracking matrix safely
					memset(window[window_count - 1], 0, MAX_LINE);
					window_count--;
					continue;
				}
			}

			// Rule 3: Dead Unconditional Jump Stripping (jmp L1; jmp L2)
			char jmp1[64], jmp2[64];
			if (sscanf(c0, "jmp %63s", jmp1) == 1 && sscanf(c1, "jmp %63s", jmp2) == 1) {
				memset(window[window_count - 1], 0, MAX_LINE);
				window_count--;
				continue;
			}
		}

		if (window_count >= 3) {
			char c0[MAX_LINE] = { 0 };
			char c1[MAX_LINE] = { 0 };
			char c2[MAX_LINE] = { 0 };
			clean_line(c0, window[window_count - 3]);
			clean_line(c1, window[window_count - 2]);
			clean_line(c2, window[window_count - 1]);

			// Rule 4: Segment Marker Deduping
			if (strcmp(c0, ".code") == 0 && strcmp(c2, ".code") == 0) {
				memset(window[window_count - 1], 0, MAX_LINE);
				window_count--;
				continue;
			}
		}

		break;
	}
}

void flush_peephole(FILE* out_file) {
	while (window_count > 0) {
		if (strlen(window[0]) > 0) {
			fprintf(out_file, "%s\n", window[0]);
		}
		for (int i = 1; i < window_count; i++) {
			strcpy(window[i - 1], window[i]);
		}
		window_count--;
	}
}
