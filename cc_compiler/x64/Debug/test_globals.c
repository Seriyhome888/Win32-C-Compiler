#include "stdio.h"

// 1. File-scope global variables (initialized & uninitialized)
int g_counter = 10;
int g_uninit;
float g_rate = 2.5;
char g_letter = 65; // 'A'
int g_arr[4];

// 2. File-scope static variable & static helper function (internal linkage)
static int s_file_secret = 100;

static int internal_helper(int delta) {
	s_file_secret += delta;
	return s_file_secret;
}

// 3. Block-scope static variable (must retain state across multiple calls)
int next_sequence_id() {
	static int call_count = 0;
	call_count += 1;
	return call_count;
}

// 4. Separate function with same static variable name (verifies unique label mangling)
int alternate_sequence_id() {
	static int call_count = 50;
	call_count += 5;
	return call_count;
}

int main() {
	printf("--- STARTING GLOBAL & STATIC VARIABLE TESTS ---\n");

	// Test A: Initialized & uninitialized globals
	if (g_counter == 10 && g_uninit == 0) {
		printf("Test A (Global Init/Zero-Init) PASSED\n");
	}
	else {
		printf("Test A FAILED: g_counter=%d, g_uninit=%d\n", g_counter, g_uninit);
	}

	// Test B: Global float and char
	g_uninit = 25;
	if (g_rate == 2.5 && g_letter == 65 && g_uninit == 25) {
		printf("Test B (Global Float/Char/Write) PASSED\n");
	}
	else {
		printf("Test B FAILED\n");
	}

	// Test C: Global array indexing
	g_arr[0] = 11;
	g_arr[3] = 44;
	if (g_arr[0] + g_arr[3] == 55) {
		printf("Test C (Global Array) PASSED\n");
	}
	else {
		printf("Test C FAILED\n");
	}

	// Test D: Address-of (&) and pointer mutation on a global variable
	int* p_global;
	p_global = &g_counter;
	*p_global = 99;
	if (g_counter == 99) {
		printf("Test D (Global Pointer Mutation) PASSED\n");
	}
	else {
		printf("Test D FAILED: g_counter=%d\n", g_counter);
	}

	// Test E: File-scope static variable and static function
	int sec;
	sec = internal_helper(23);
	if (sec == 123 && s_file_secret == 123) {
		printf("Test E (File-Scope Static Var & Func) PASSED\n");
	}
	else {
		printf("Test E FAILED: sec=%d\n", sec);
	}

	// Test F: Block-scope static persistence & label isolation
	int id1;
	int id2;
	int id3;
	int alt1;
	id1 = next_sequence_id();      // Expected: 1
	id2 = next_sequence_id();      // Expected: 2
	id3 = next_sequence_id();      // Expected: 3
	alt1 = alternate_sequence_id(); // Expected: 55

	if (id1 == 1 && id2 == 2 && id3 == 3 && alt1 == 55) {
		printf("Test F (Block-Scope Static Persistence) PASSED\n");
	}
	else {
		printf("Test F FAILED: %d, %d, %d, %d\n", id1, id2, id3, alt1);
	}

	return 0;
}
