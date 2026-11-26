#include "stdio.h"
#include "stdarg.h"

struct Packet {
	char flag;
	char code;
	int payload;
	float weight;
};

// Global & file-scope static structs
struct Packet g_pkt;
static struct Packet s_pkt;

// Global char array
char g_chars[5];

// 1. Verify global and static struct dot-access writes & reads
int test_global_struct() {
	g_pkt.flag = 65;      // 'A'
	g_pkt.code = 90;      // 'Z'
	g_pkt.payload = 1024;
	g_pkt.weight = 3.5;

	s_pkt.flag = 75;      // 'K'
	s_pkt.code = 77;      // 'M'
	s_pkt.payload = 2048;
	s_pkt.weight = 1.25;

	if (g_pkt.flag == 65 && g_pkt.code == 90 && g_pkt.payload == 1024 && g_pkt.weight == 3.5 &&
		s_pkt.flag == 75 && s_pkt.code == 77 && s_pkt.payload == 2048 && s_pkt.weight == 1.25) {
		printf("Test 1 (Global & Static Struct Dot-Access) PASSED\n");
		return 1;
	}
	else {
		printf("Test 1 (Global & Static Struct Dot-Access) FAILED\n");
		return 0;
	}
}

// 2. Verify char struct members do not overwrite adjacent struct fields
int test_char_struct_members() {
	struct Packet local_pkt;
	struct Packet* p_pkt;

	// Write in reverse/interleaved order to catch 32-bit dword clobbering
	local_pkt.payload = 5000;
	local_pkt.code = 66;     // 'B' (offset 1)
	local_pkt.flag = 65;     // 'A' (offset 0) - must not clobber 'code' or 'payload'!
	local_pkt.weight = 4.5;

	if (local_pkt.flag != 65 || local_pkt.code != 66 || local_pkt.payload != 5000 || local_pkt.weight != 4.5) {
		printf("Test 2a (Local Char Struct Member Dot-Access) FAILED: flag=%d, code=%d, payload=%d\n",
			local_pkt.flag, local_pkt.code, local_pkt.payload);
		return 0;
	}

	// Verify pointer arrow (->) read/write on char members
	p_pkt = &local_pkt;
	p_pkt->flag = 88;        // 'X'
	p_pkt->code = 89;        // 'Y'

	if (p_pkt->flag == 88 && p_pkt->code == 89 && local_pkt.payload == 5000 && sizeof(struct Packet) == 10) {
		printf("Test 2 (Char Struct Members & sizeof(struct Packet)==10) PASSED\n");
		return 1;
	}
	else {
		printf("Test 2b (Arrow Char Struct Member / Size) FAILED: size=%d\n", sizeof(struct Packet));
		return 0;
	}
}

// 3. Verify 1-byte stride indexing on local and global char arrays
int test_char_arrays() {
	char buf[4];

	// Write adjacent bytes in reverse order to ensure byte-granularity stores
	buf[3] = 100;
	buf[2] = 99;
	buf[1] = 98;
	buf[0] = 97;

	g_chars[4] = 5;
	g_chars[3] = 4;
	g_chars[2] = 3;
	g_chars[1] = 2;
	g_chars[0] = 1;

	int local_sum;
	int global_sum;
	local_sum = buf[0] + buf[1] + buf[2] + buf[3];
	global_sum = g_chars[0] + g_chars[1] + g_chars[2] + g_chars[3] + g_chars[4];

	if (local_sum == 394 && global_sum == 15 && sizeof(buf) == 4 && sizeof(g_chars) == 5) {
		printf("Test 3 (Local & Global Char Arrays) PASSED\n");
		return 1;
	}
	else {
		printf("Test 3 (Local & Global Char Arrays) FAILED: local=%d, global=%d\n", local_sum, global_sum);
		return 0;
	}
}

// 4. Verify runtime unary minus (-x) on float variables and expressions
int test_negative_floats() {
	float pos;
	float neg;
	float double_neg;
	float expr_neg;

	pos = 6.25;
	neg = -pos;                  // Triggers runtime AST_UNOP TOKEN_MINUS on TYPE_FLOAT
	double_neg = -neg;
	expr_neg = -(pos + 1.75);    // -8.0

	if (neg == -6.25 && double_neg == 6.25 && expr_neg == -8.0 && (pos + neg) == 0.0) {
		printf("Test 4 (Unary Float Negation) PASSED: neg=%f, expr_neg=%f\n", neg, expr_neg);
		return 1;
	}
	else {
		printf("Test 4 (Unary Float Negation) FAILED: neg=%f, double_neg=%f, expr_neg=%f\n",
			neg, double_neg, expr_neg);
		return 0;
	}
}

#define INITIAL_VALUE 42
#define STEP_INCREMENT 5

#define MULTIPLY(x, y) ((x) * (y))
#define THRESHOLD 100

#define MULTIPLY(x, y) ((x) * (y))
#define THRESHOLD 100

// Top-Level Typedef Registrations
typedef int CustomInt;
typedef char* StringPtr;

struct Point {
	int x;
	int y;
};
typedef struct Point PointType;

int macro_test() {
	int calculation;
	calculation = MULTIPLY(5, 10 + 2); // Expands to: ((5) * (10 + 2))

#undef THRESHOLD
	int THRESHOLD; // Legal C89 now, because THRESHOLD is no longer a macro!
	THRESHOLD = calculation;

	return THRESHOLD;
}

int define_test() {
	int total_score;
	int index_counter;

	total_score = INITIAL_VALUE;
	index_counter = STEP_INCREMENT;

	total_score = total_score + index_counter;
	return total_score;
}

int calc(int a, int b) {
	int x;
	x = a + b;
	return x;
}

void pointer_dereference()
{
	// ==========================================
	// 0. POINTER DEREFERENCING & ADDRESS-OF TEST
	// ==========================================
	int target_var;
	int ptr_addr; // Act as pointer container (using standard type registers)
	int retrieved_val;

	target_var = 15;
	ptr_addr = &target_var;      // Test Address-of instruction logic (&)
	retrieved_val = *ptr_addr;   // Test Pointer Dereference register load (*)
	if (retrieved_val == 15)
		printf("Pointer Value Verification 1 PASSED with value: %d\n", retrieved_val);
	else
		printf("Pointer Value Verification 1 FAILED \n");

	*ptr_addr = 99;              // Test assignment through indirect reference pointer
	retrieved_val = target_var;  // Read directly from target_var to see if it mutated
	if (retrieved_val == 99)
		printf("Pointer Value Verification 2 (Mutated) PASSED with value: %d\n", retrieved_val);
	else
		printf("Pointer Value Verification 2 (Mutated) FAILED\n");
}

int while_sum_test()
{
	// 1. traditional while loop test
	int j;
	int while_sum;
	j = 0;
	while_sum = 0;
	while (j < 5) {
		while_sum = while_sum + j;
		j = j + 1;
	}

	if (while_sum == 10)
		printf("The loop sum total test PASSED with value: %d\n", while_sum);
	else
		printf("The loop sum total test FAILED\n");

	return while_sum;
}

int for_sum_test()
{
	// 2. Transformed for loop test
	int k;
	int for_sum;
	for_sum = 0;
	for (k = 0; k < 5; k = k + 1) {
		for_sum = for_sum + k;
	}

	if (for_sum == 10)
		printf("The transformed for loop test PASSED with value: %d\n", for_sum);
	else
		printf("The transformed for loop test FAILED \n");

	return for_sum;
}

int break_continue_test()
{
	// 3. COMPLETE BREAK & CONTINUE TEST
	int i;
	int total;
	total = 0;

	for (i = 0; i < 10; i = i + 1) {
		if (i == 3) {
			continue;
		}
		if (i == 6) {
			break;
		}
		total = total + i;
	}

	if (total == 12)
		printf("The total using break and continue test PASSED with value: %d\n", total);
	else
		printf("The total using break and continue test FAILED\n");

	return total;
}

float integer_to_float_promoting()
{
	int integer_val;
	float floating_val;
	float float_int_sum;

	integer_val = 5;
	floating_val = 3.5;

	float_int_sum = integer_val + floating_val;

	if(float_int_sum == 8.5)
		printf("Mixed mode validation test PASSED with value: %f\n", float_int_sum);
	else
		printf("Mixed mode validation test FAILED\n");

	// MIXED MODE EXPRESSION: Implicitly promotes 'integer_val' to float before adding!
	return float_int_sum;
}

struct Person {
	int id;
	int age;
	int salary;
};

int test_struct(int bonus) {
	struct Person bob;
	struct Person* ptr;
	int final_score;

	// 1. Test standard member dot-access assignments
	bob.id = 101;
	bob.age = 30;
	bob.salary = 5000;

	// 2. Test assigning a structure pointer target
	ptr = &bob;

	// 3. Test arrow-access mutation through pointer dereferencing
	ptr->age = 31;

	// 4. Test complex expressions integrating member values and local variables
	final_score = bob.salary + ptr->age + bonus;

	return final_score;
}

int complex_math_test() {
	int result;
	// Requires more than 3 simultaneous active evaluation states:
	result = 1 + 2 + 3 + 4 + 5 + 6;
	return result;
}

int char_test() {
	char a;
	char b;
	int result;

	a = 65; // ASCII code for 'A'
	b = 5;

	result = a + b; // Test integral promotion and addition
	return result;
}

void string_pointer_test() {
	char* message;
	message = "Hello World\n";
	printf(message);
}

int switch_test(int val) {
	int out;
	switch (val) {
	case 1:
		out = 10;
		break;
	case 2:
		out = 20;
		break;
	case 74:
		out = 47;
		break;
	default:
		out = 99;
	}
	return out;
}

int negative_test() {
	int negative_val;
	negative_val = -15; 
	return negative_val;
}

int test_div_mod() {
	int a;
	int b;
	int div_result;
	int mod_result;

	a = 47;
	b = 5;

	// Test Division: 47 / 5 should equal 9
	div_result = a / b;
	if (div_result == 9)
		printf("Division Test (47 / 5) PASSED with value: %d\n", div_result);
	else
		printf("Division Test (47 / 5) FAILED\n");

	// Test Modulo: 47 % 5 should equal 2
	mod_result = a % b;
	if (mod_result == 2)
		printf("Modulo Test (47 %% 5) PASSED with value: %d\n", mod_result);
	else
		printf("Modulo Test (47 %% 5) FAILED\n");

	// Test with a negative dividend: -23 / 4 should equal -5, -23 % 4 should equal -3
	a = -23;
	b = 4;
	div_result = a / b;
	mod_result = a % b;

	if (div_result == -5)
		printf("Negative Div Test (-23 / 4) PASSED with value: %d\n", div_result);
	else
		printf("Negative Div Test (-23 / 4) FAILED\n");

	if (mod_result == -3)
		printf("Negative Mod Test (-23 %% 4) PASSED with value: %d\n", mod_result);
	else
		printf("Negative Mod Test (-23 %% 4) FAILED\n");

	return 0;
}

int test_float_div() {
	float x;
	float y;
	float result;

	x = 10.5;
	y = 2.0;

	// Test true float / float division (10.5 / 2.0 = 5.25)
	result = x / y;
	if (result == 5.25)
		printf("Float Division Test (10.5 / 2.0) PASSED with value: %f\n", result);
	else
		printf("Float Division Test (10.5 / 2.0) FAILED\n");

	// Test promotion float / int division (10.5 / 4 = 2.625)
	int divisor;
	divisor = 4;
	result = x / divisor;
	if (result == 2.625)
		printf("Mixed Promotion Division Test (10.5 / 4) PASSED with value: %f\n", result);
	else
		printf("Mixed Promotion Division Test (10.5 / 4) FAILED\n");

	return 0;
}

int test_logic() {
	int a;
	int b;
	a = 1;
	b = 0;

	if (a && b) {
		printf("AND check failed!\n");
	}
	else {
		printf("AND check passed!\n"); // Expected
	}

	if (a || b) {
		printf("OR check passed!\n");  // Expected
	}
	else {
		printf("OR check failed!\n");
	}

	// Short-circuit confirmation: if left side is 0, execution stops 
	// without triggering division-by-zero crashes
	int zero;
	zero = 0;
	if (zero && (10 / zero == 2)) {
		printf("Short circuit broken!\n");
	}
	else {
		printf("Short circuit validation verified safely!\n");
	}

	return 0;
}

int test_not() {
	int active;
	int disabled;

	active = 1;
	disabled = 0;

	// Test inverting true (!1 -> 0)
	if (!active) {
		printf("Test 1 Failed: !1 evaluated as true.\n");
	}
	else {
		printf("Test 1 Passed: !1 evaluated correctly as false.\n");
	}

	// Test inverting false (!0 -> 1)
	if (!disabled) {
		printf("Test 2 Passed: !0 evaluated correctly as true.\n");
	}
	else {
		printf("Test 2 Failed: !0 evaluated as false.\n");
	}

	// Test nested inline algebraic usage
	int value;
	value = 0;
	if (!(!value)) {
		printf("Test 3 Failed: Double negation failed.\n");
	}
	else {
		printf("Test 3 Passed: Double negation worked cleanly.\n");
	}

	return 0;
}

void exchange_test()
{
	int arr;
	int res;
	arr = 42;
	res = arr;
	if (res == 42)
		printf("The exchange test PASSED with value: %d\n", res);
	else
		printf("The exchange test FAILED \n");
}

void two_dimensional_matrix_test()
{
	int matrix[3][5];
	int val;
	// Target a nested cell coordinate index location
	matrix[1][2] = 88;
	val = matrix[1][2];
	if (val == 88)
		printf("The 2D matrix dynamic value test PASSED with value: %d\n", val);
	else
		printf("The 2D matrix dynamic value test FAILED\n");
}

int sum_numbers(int count, ...) {
	char* args;
	int total_sum;
	int i;
	int current_val;
	int* item_ptr; /* Declare explicit helper pointer to handle data types */

	total_sum = 0;

	/*
	   Bypass casting: &count evaluates to a pointer.
	   Compiler allows pointer + integer offset scaling out of the box!
	*/
	//args = (char*)&count + 4;
	args = &count + 1;

	for (i = 0; i < count; i = i + 1) {
		/*
		   Implicit pointer conversion: Assigning char* directly to int*
		   Your compiler's AST_ASSIGN handler doesn't enforce strict type blocking,
		   allowing this to map cleanly in raw registers!
		*/
		item_ptr = args;
		current_val = *item_ptr;

		total_sum = total_sum + current_val;
		args = args + 4;
	}

	return total_sum;
}

int test_typedef_pipeline() {
	// 1. Verify basic primitive alias mapping
	CustomInt score;
	score = 75;

	// 2. Verify structure type alias matching and member dot-access operations
	PointType coordinate;
	coordinate.x = 10;
	coordinate.y = 20;

	// 3. Verify pointer wrapper alignment logic
	StringPtr text_message;
	text_message = "Typedef Test PASSED\n";

	int final_calculation;
	final_calculation = score + coordinate.x + coordinate.y; // 75 + 10 + 20 = 105

	if (final_calculation == 105) {
		printf("Basic and Struct Typedef verification: PASSED\n");
		printf(text_message);
	}
	else {
		printf("Typedef implementation verification: FAILED\n");
	}

	return 0;
}

void test_sizeof() {
	int local_int;
	char local_char;
	int array_1d[10];
	int matrix_2d[3][5];
	struct Person normal_bob;
	struct Person* ptr_bob;

	printf("--- STARTING SIZEOF COMPILE-TIME VERIFICATION ---\n");

	// 1. Primitive Tests
	if (sizeof(int) == 4) printf("sizeof(int) PASSED\n"); else printf("sizeof(int) FAILED\n");
	if (sizeof(char) == 1) printf("sizeof(char) PASSED\n"); else printf("sizeof(char) FAILED\n");

	// 2. Variable Expression Tests
	if (sizeof(local_int) == 4) printf("sizeof(local_int) PASSED\n"); else printf("sizeof(local_int) FAILED\n");
	if (sizeof(local_char) == 1) printf("sizeof(local_char) PASSED\n"); else printf("sizeof(local_char) FAILED\n");

	// 3. Pointer Size Tests (Always 4-bytes in your 32-bit architecture)
	if (sizeof(int*) == 4) printf("sizeof(int*) PASSED\n"); else printf("sizeof(int*) FAILED\n");
	if (sizeof(ptr_bob) == 4) printf("sizeof(ptr_bob) PASSED\n"); else printf("sizeof(ptr_bob) FAILED\n");

	// 4. Array Dimension Boundary Tests
	if (sizeof(array_1d) == 40) printf("sizeof(array_1d) PASSED\n"); else printf("sizeof(array_1d) FAILED\n");
	if (sizeof(matrix_2d) == 60) printf("sizeof(matrix_2d) PASSED\n"); else printf("sizeof(matrix_2d) FAILED\n");

	// 5. Compound Struct & Struct Member Access Tests
	if (sizeof(struct Person) == 12) printf("sizeof(struct Person) PASSED\n"); else printf("sizeof(struct Person) FAILED\n");
	if (sizeof(normal_bob) == 12) printf("sizeof(normal_bob) PASSED\n"); else printf("sizeof(normal_bob) FAILED\n");
	if (sizeof(normal_bob.age) == 4) printf("sizeof(normal_bob.age) PASSED\n"); else printf("sizeof(normal_bob.age) FAILED\n");

	printf("--- ENDING SIZEOF COMPILE-TIME VERIFICATION ---\n");
}

int test_compound_operators() {
	int a;
	int b;
	float x;

	printf("--- STARTING COMPOUND ASSIGNMENT VERIFICATION ---\n");

	// 1. Test += (Integer)
	a = 10;
	a += 5; // Desugars to: a = a + 5 -> 15
	if (a == 15) printf("a += 5 PASSED with value: %d\n", a);
	else printf("a += 5 FAILED\n");

	// 2. Test -= (Integer)
	b = 20;
	b -= 7; // Desugars to: b = b - 7 -> 13
	if (b == 13) printf("b -= 7 PASSED with value: %d\n", b);
	else printf("b -= 7 FAILED\n");

	// 3. Test *= (Integer mixed with expression)
	a = 4;
	a *= (2 + 3); // Desugars to: a = a * (2 + 3) -> 20
	if (a == 20) printf("a *= (2+3) PASSED with value: %d\n", a);
	else printf("a *= (2+3) FAILED\n");

	// 4. Test /= (Integer division)
	b = 45;
	b /= 5; // Desugars to: b = b / 5 -> 9
	if (b == 9) printf("b /= 5 PASSED with value: %d\n", b);
	else printf("b /= 5 FAILED\n");

	// 5. Test Floating-Point Promotion Compatibility
	x = 10.5;
	x += 2; // Desugars to: x = x + (float)2 -> 12.5
	if (x == 12.5) printf("x += 2 (Float Promotion) PASSED with value: %f\n", x);
	else printf("x += 2 (Float Promotion) FAILED\n");

	printf("--- ENDING COMPOUND ASSIGNMENT VERIFICATION ---\n");
	return 0;
}

void cast_verification_test() {
	float pi_approximation;
	int truncated_integer;

	pi_approximation = 3.99;

	// Explicitly cast float to int (expected value: 3)
	truncated_integer = (int)pi_approximation;

	if (truncated_integer == 3) {
		printf("Explicit Float -> Int Typecast PASSED\n");
	}
	else {
		printf("Explicit Typecast FAILED\n");
	}
}

// 1. Fully dynamic variadic loop processing
int test_variadic_sum(int count, ...) {
	va_list ap;
	int total;
	int i;
	int current_val;

	total = 0;
	i = 0;
	va_start(ap, count);

	// This loop repeatedly evaluates *(int*)((ap += 4) - 4) at runtime
	while (i < count) {
		current_val = va_arg(ap, int);
		total = total + current_val;
		i = i + 1;
	}

	va_end(ap);
	return total;
}

int g_block_ext = 50;

int test_storage_classes(register int param_reg) {
	auto int a = 10;
	register int r = 20;
	extern int g_block_ext; // Block-scope extern resolving to file-scope g_block_ext

	int sum = a + r + param_reg + g_block_ext; // 10 + 20 + 5 + 50 = 85
	if (sum == 85) {
		printf("auto, register & block-scope extern PASSED\n");
		return 1;
	}
	return 0;
}

int test_inc_dec() {
	printf("--- STARTING INCREMENT & DECREMENT (++/--) VERIFICATION ---\n");

	// 1. Integers: prefix & postfix in expressions and standalone statements
	int a;
	int b;
	int pre_inc;
	int post_inc;
	int pre_dec;
	int post_dec;

	a = 10;
	pre_inc = ++a;   // a becomes 11, pre_inc = 11
	post_inc = a++;  // post_inc = 11, a becomes 12

	b = 20;
	pre_dec = --b;   // b becomes 19, pre_dec = 19
	post_dec = b--;  // post_dec = 19, b becomes 18

	a++;             // standalone postfix: a becomes 13
	--b;             // standalone prefix: b becomes 17

	if (a != 13 || b != 17 || pre_inc != 11 || post_inc != 11 || pre_dec != 19 || post_dec != 19) {
		printf("Test ++/-- (Int) FAILED: a=%d, b=%d, pre_inc=%d, post_inc=%d, pre_dec=%d, post_dec=%d\n",
			a, b, pre_inc, post_inc, pre_dec, post_dec);
		return 0;
	}

	// 2. Chars: verify 1-byte stores do not clobber adjacent variables
	char c1;
	char c2;
	char c_post;
	char c_pre;

	c1 = 65;         // 'A'
	c2 = 90;         // 'Z'
	c_post = c1++;   // c_post = 65 ('A'), c1 becomes 66 ('B')
	c_pre = --c2;    // c2 becomes 89 ('Y'), c_pre = 89 ('Y')

	if (c1 != 66 || c2 != 89 || c_post != 65 || c_pre != 89) {
		printf("Test ++/-- (Char) FAILED: c1=%d, c2=%d, c_post=%d, c_pre=%d\n",
			c1, c2, c_post, c_pre);
		return 0;
	}

	// 3. Floats: SSE2 +1.0 / -1.0 prefix & postfix
	float f1;
	float f_post;
	float f_pre;

	f1 = 2.5;
	f_post = f1++;   // f_post = 2.5, f1 becomes 3.5
	f_pre = --f1;    // f1 becomes 2.5, f_pre = 2.5
	++f1;            // f1 becomes 3.5

	if (f1 != 3.5 || f_post != 2.5 || f_pre != 2.5) {
		printf("Test ++/-- (Float) FAILED: f1=%f, f_post=%f, f_pre=%f\n",
			f1, f_post, f_pre);
		return 0;
	}

	// 4. Arrays: 1D int array & char array element indexing
	int arr[3];
	arr[0] = 10;
	arr[1] = 20;
	arr[2] = 30;

	int arr_post;
	int arr_pre;
	arr_post = arr[1]++; // arr_post = 20, arr[1] becomes 21
	arr_pre = --arr[2];  // arr[2] becomes 29, arr_pre = 29

	char carr[2];
	carr[0] = 50;
	carr[1] = 80;
	carr[0]++;           // carr[0] becomes 51, must not clobber carr[1]
	--carr[1];           // carr[1] becomes 79

	if (arr[0] != 10 || arr[1] != 21 || arr[2] != 29 || arr_post != 20 || arr_pre != 29 ||
		carr[0] != 51 || carr[1] != 79) {
		printf("Test ++/-- (Arrays) FAILED: arr[1]=%d, arr[2]=%d, carr[0]=%d, carr[1]=%d\n",
			arr[1], arr[2], carr[0], carr[1]);
		return 0;
	}

	// 5. Struct members: dot (.) and pointer arrow (->) on int, char, and float fields
	struct Packet pkt;
	struct Packet* p_pkt;
	p_pkt = &pkt;

	pkt.flag = 10;
	pkt.code = 20;
	pkt.payload = 100;
	pkt.weight = 4.5;

	int s_post;
	int s_pre;
	s_post = pkt.payload++;     // s_post = 100, pkt.payload becomes 101
	s_pre = --p_pkt->payload;   // pkt.payload becomes 100, s_pre = 100

	pkt.flag++;                 // pkt.flag becomes 11 (must not clobber pkt.code)
	--p_pkt->code;              // pkt.code becomes 19
	p_pkt->weight++;            // pkt.weight becomes 5.5

	if (pkt.flag != 11 || pkt.code != 19 || pkt.payload != 100 || pkt.weight != 5.5 ||
		s_post != 100 || s_pre != 100) {
		printf("Test ++/-- (Structs) FAILED: flag=%d, code=%d, payload=%d, weight=%f\n",
			pkt.flag, pkt.code, pkt.payload, pkt.weight);
		return 0;
	}

	// 6. Globals & Statics: verify .data label addressing
	g_pkt.payload = 500;
	g_pkt.payload++;
	--g_pkt.payload;
	if (g_pkt.payload != 500) {
		printf("Test ++/-- (Global Struct) FAILED: payload=%d\n", g_pkt.payload);
		return 0;
	}

	printf("Test ++/-- (Int, Char, Float, Array, Struct, Global) PASSED!\n");
	return 1;
}

int test_pointer_arithmetic() {
	printf("--- STARTING TYPED POINTER ARITHMETIC VERIFICATION ---\n");

	// ==========================================
	// 1. char* (1-byte stride)
	// ==========================================
	char c_arr[4];
	c_arr[0] = 10;
	c_arr[1] = 20;
	c_arr[2] = 30;
	c_arr[3] = 40;

	char* cp1;
	char* cp2;
	cp1 = &c_arr[0];

	cp2 = cp1 + 3;          // Advances by 3 * 1 = 3 bytes -> points to c_arr[3] (40)
	if (*cp2 != 40 || (cp2 - cp1) != 3) {
		printf("Test ptr (char* + / -) FAILED: *cp2=%d, diff=%d\n", *cp2, cp2 - cp1);
		return 0;
	}

	cp2 = cp2 - 2;          // Steps back by 2 * 1 = 2 bytes -> points to c_arr[1] (20)
	cp1++;                  // Postfix ++ advances by 1 byte -> points to c_arr[1] (20)
	if (*cp1 != 20 || *cp2 != 20 || (cp1 - cp2) != 0) {
		printf("Test ptr (char* ++ / -) FAILED: *cp1=%d, *cp2=%d\n", *cp1, *cp2);
		return 0;
	}

	--cp2;                  // Prefix -- steps back by 1 byte -> points to c_arr[0] (10)
	if (*cp2 != 10 || (cp1 - cp2) != 1) {
		printf("Test ptr (char* --) FAILED: *cp2=%d, diff=%d\n", *cp2, cp1 - cp2);
		return 0;
	}

	// ==========================================
	// 2. int* (4-byte stride)
	// ==========================================
	int i_arr[5];
	i_arr[0] = 100;
	i_arr[1] = 200;
	i_arr[2] = 300;
	i_arr[3] = 400;
	i_arr[4] = 500;

	int* ip1;
	int* ip2;
	ip1 = &i_arr[0];

	ip2 = ip1 + 4;          // Advances by 4 * 4 = 16 bytes -> points to i_arr[4] (500)
	if (*ip2 != 500 || (ip2 - ip1) != 4) {
		printf("Test ptr (int* + / -) FAILED: *ip2=%d, diff=%d\n", *ip2, ip2 - ip1);
		return 0;
	}

	ip2 = ip2 - 2;          // Steps back by 2 * 4 = 8 bytes -> points to i_arr[2] (300)
	ip1++;                  // Advances by 4 bytes -> points to i_arr[1] (200)
	if (*ip1 != 200 || *ip2 != 300 || (ip2 - ip1) != 1) {
		printf("Test ptr (int* ++ / -) FAILED: *ip1=%d, *ip2=%d, diff=%d\n", *ip1, *ip2, ip2 - ip1);
		return 0;
	}

	--ip2;                  // Steps back by 4 bytes -> points to i_arr[1] (200)
	if (*ip2 != 200 || (ip2 - ip1) != 0) {
		printf("Test ptr (int* --) FAILED: *ip2=%d, diff=%d\n", *ip2, ip2 - ip1);
		return 0;
	}

	// ==========================================
	// 3. struct Packet* (10-byte stride, non-power-of-2 idiv test!)
	// ==========================================
	struct Packet pkts[3];
	pkts[0].flag = 1;
	pkts[0].payload = 1000;
	pkts[0].weight = 1.5;

	pkts[1].flag = 2;
	pkts[1].payload = 2000;
	pkts[1].weight = 2.5;

	pkts[2].flag = 3;
	pkts[2].payload = 3000;
	pkts[2].weight = 3.5;

	struct Packet* sp1;
	struct Packet* sp2;
	sp1 = &pkts[0];

	sp2 = sp1 + 2;          // Advances by 2 * 10 = 20 bytes -> points to pkts[2]
	if (sp2->payload != 3000 || sp2->flag != 3 || (sp2 - sp1) != 2) {
		printf("Test ptr (struct Packet* + / -) FAILED: payload=%d, diff=%d\n",
			sp2->payload, sp2 - sp1);
		return 0;
	}

	sp1++;                  // Advances by 10 bytes -> points to pkts[1]
	sp2 = sp2 - 1;          // Steps back by 10 bytes -> points to pkts[1]
	if (sp1->payload != 2000 || sp2->payload != 2000 || (sp2 - sp1) != 0) {
		printf("Test ptr (struct Packet* ++ / -) FAILED: sp1=%d, sp2=%d\n",
			sp1->payload, sp2->payload);
		return 0;
	}

	--sp2;                  // Steps back by 10 bytes -> points to pkts[0]
	if (sp2->payload != 1000 || sp2->weight != 1.5 || (sp1 - sp2) != 1) {
		printf("Test ptr (struct Packet* --) FAILED: payload=%d, diff=%d\n",
			sp2->payload, sp1 - sp2);
		return 0;
	}

	printf("Test Typed Pointer Arithmetic (char*, int*, struct Packet*) PASSED!\n");
	return 1;
}

int test_bitwise_and_ternary() {
	int a = 12; // 0b1100
	int b = 10; // 0b1010

	if ((a & b) != 8) {
		printf("Bitwise AND (&) FAILED: expected 8, got %d\n", a & b);
		return 0;
	}
	if ((a | b) != 14) {
		printf("Bitwise OR (|) FAILED: expected 14, got %d\n", a | b);
		return 0;
	}
	if ((a ^ b) != 6) {
		printf("Bitwise XOR (^) FAILED: expected 6, got %d\n", a ^ b);
		return 0;
	}
	if ((~0) != -1) {
		printf("Bitwise NOT (~) FAILED: expected -1, got %d\n", ~0);
		return 0;
	}
	if ((3 << 4) != 48) {
		printf("Left Shift (<<) FAILED: expected 48, got %d\n", 3 << 4);
		return 0;
	}
	if ((48 >> 2) != 12) {
		printf("Right Shift (>>) FAILED: expected 12, got %d\n", 48 >> 2);
		return 0;
	}

	// Verify precedence: & binds tighter than ^, which binds tighter than |
	// 12 | 10 ^ 6 & 3  ->  12 | (10 ^ (6 & 3))  ->  12 | (10 ^ 2)  ->  12 | 8 = 12
	int prec = 12 | 10 ^ 6 & 3;
	if (prec != 12) {
		printf("Bitwise Precedence FAILED: expected 12, got %d\n", prec);
		return 0;
	}

	// Compound assignments
	int flags = 1;
	flags |= 4;      // 5
	flags <<= 2;     // 20
	flags ^= 6;      // 18
	flags &= 10;     // 2
	flags >>= 1;     // 1
	if (flags != 1) {
		printf("Bitwise Compound Assignments FAILED: expected 1, got %d\n", flags);
		return 0;
	}

	// Ternary (including short-circuiting & nested right-associativity)
	int zero = 0;
	int safe = zero ? (10 / zero) : 42;
	int nested = (a < b) ? 1 : (a == b) ? 2 : 3;
	if (safe != 42 || nested != 3) {
		printf("Ternary Operator (?:) FAILED: safe=%d (expected 42), nested=%d (expected 3)\n", safe, nested);
		return 0;
	}

	printf("Bitwise & Ternary Operators PASSED!\n");
	return 1;
}

union DataWord {
	int as_int;
	float as_float;
	char first_byte;
};

// 1. Test void return; (early exit and trailing return;)
void early_void_return(int* out_flag, int trigger) {
	if (trigger) {
		*out_flag = 42;
		return;
	}
	*out_flag = -1;
	return;
}

// 2. Test union member access (. and ->), overlapping memory, and sizeof
int test_union_ops() {
	union DataWord u;
	union DataWord* pu;
	pu = &u;

	// Write 0x41424344 ('A'=65, 'B'=66, 'C'=67, 'D'=68 in little-endian: 0x44 is LSB)
	u.as_int = 1094861636; // 0x41424344 -> lowest byte is 0x44 (68)
	if (u.first_byte != 68 || pu->as_int != 1094861636 || sizeof(union DataWord) != 4) {
		printf("Union Test (Int/Byte Overlap) FAILED: byte=%d, size=%d\n",
			u.first_byte, sizeof(union DataWord));
		return 0;
	}

	// Overwrite same 4-byte storage via pointer arrow (->) as a float
	pu->as_float = 6.5;
	if (u.as_float != 6.5 || pu->as_float != 6.5) {
		printf("Union Test (Float Arrow Write) FAILED: val=%f\n", u.as_float);
		return 0;
	}

	printf("Union Test (. / -> / Overlap / sizeof) PASSED!\n");
	return 1;
}

// 3. Test for (;;) and for (; cond; ) empty clauses
int test_for_empty_clauses() {
	// Case A: Infinite for (;;) with break and continue
	int count;
	int sum;
	count = 0;
	sum = 0;

	for (;;) {
		count++;
		if (count == 3) {
			continue;
		}
		if (count > 5) {
			break;
		}
		sum += count; // 1 + 2 + 4 + 5 = 12
	}

	if (sum != 12 || count != 6) {
		printf("For (;;) Test FAILED: sum=%d, count=%d\n", sum, count);
		return 0;
	}

	// Case B: for (; cond; ) with omitted init and step
	int i;
	int prod;
	i = 1;
	prod = 1;

	for (; i <= 4; ) {
		prod *= i; // 1 * 2 * 3 * 4 = 24
		i++;
	}

	if (prod != 24 || i != 5) {
		printf("For (; cond; ) Test FAILED: prod=%d, i=%d\n", prod, i);
		return 0;
	}

	printf("For (;; / ;cond;) Empty Clauses Test PASSED!\n");
	return 1;
}

int do_while_test() {
	// 1. False condition on entry: body must still execute once
	int runs = 0;
	int flag = 0;
	do {
		runs++;
	} while (flag);

	// 2. Loop with continue (jumps to condition check) and break
	int i = 0;
	int sum = 0;
	do {
		i++;
		if (i == 3) {
			continue; // Must jump to while (i < 6), NOT skip the condition!
		}
		if (i == 5) {
			break;
		}
		sum += i; // 1 + 2 + 4 = 7
	} while (i < 6);

	if (runs == 1 && sum == 7 && i == 5) {
		printf("Do-While (single-run, continue, break) PASSED!\n");
		return 1;
	}
	printf("Do-While FAILED: runs=%d, sum=%d, i=%d\n", runs, sum, i);
	return 0;
}

typedef int (*MathOp)(int, int);

int fp_add(int a, int b) { return a + b; }
int fp_mul(int a, int b) { return a * b; }
float fp_half(int x) { return x / 2.0; }

int apply_op(int x, int y, int (*op)(int, int)) {
	return op(x, y);
}

struct Handler {
	int id;
	MathOp callback;
};

int test_function_pointers() {
	// 1. Direct assignment & address-of (&) assignment
	int (*fn)(int, int);
	fn = fp_add;
	int r1 = fn(10, 20);          // 30

	fn = &fp_mul;
	int r2 = (*fn)(6, 7);         // 42

	// 2. Passing function pointer as callback argument
	int r3 = apply_op(8, 9, fp_add); // 17

	// 3. Array of function pointers
	MathOp ops[2];
	ops[0] = fp_add;
	ops[1] = fp_mul;
	int r4 = ops[0](3, 4) + ops[1](3, 4); // 7 + 12 = 19

	// 4. Function pointer inside a struct & float return
	struct Handler h;
	h.id = 1;
	h.callback = fp_mul;
	int r5 = h.callback(5, 5);    // 25

	float (*f_ptr)(int);
	f_ptr = fp_half;
	float rf = f_ptr(9);          // 4.5

	if (r1 == 30 && r2 == 42 && r3 == 17 && r4 == 19 && r5 == 25 && rf == 4.5) {
		printf("Function Pointers (direct, (*fp), callbacks, arrays, structs, float) PASSED!\n");
		return 1;
	}
	printf("Function Pointers FAILED: r1=%d, r2=%d, r3=%d, r4=%d, r5=%d, rf=%f\n", r1, r2, r3, r4, r5, rf);
	return 0;
}

int test_char_literals() {
	char c_plain = 'A';       // 65
	char c_nl = '\n';         // 10
	char c_tab = '\t';        // 9
	char c_null = '\0';       // 0
	char c_quote = '\'';      // 39
	char c_slash = '\\';      // 92
	char c_hex = '\x41';      // 65 ('A')
	char c_oct = '\101';      // 65 ('A')
	char c_oct_zero = '\07';  // 7 ('\a')

	if (c_plain != 65 || c_nl != 10 || c_tab != 9 || c_null != 0 ||
		c_quote != 39 || c_slash != 92 || c_hex != 'A' || c_oct != 'A' || c_oct_zero != '\a') {
		printf("Char Literals Test FAILED!\n");
		return 0;
	}

	// Verify C89 rule: sizeof('A') == sizeof(int) == 4, and 'Z' - 'A' == 25
	int diff = 'Z' - 'A';
	int matched = 0;
	switch (c_hex) {
	case 'A':
		matched = 1;
		break;
	default:
		matched = 0;
	}

	if (diff == 25 && sizeof('A') == 4 && matched == 1) {
		printf("Char Literals & Escape Sequences ('A', '\\n', '\\x41', '\\101') PASSED!\n");
		return 1;
	}
	printf("Char Literals Arithmetic/Switch FAILED!\n");
	return 0;
}

int test_string_and_char_escapes() {
	// 1. Single-quoted character constants & escapes
	char c_plain = 'A';       // 65
	char c_nl = '\n';         // 10
	char c_tab = '\t';        // 9
	char c_cr = '\r';         // 13
	char c_null = '\0';       // 0
	char c_quote = '\'';      // 39
	char c_slash = '\\';      // 92
	char c_hex = '\x41';      // 65 ('A')
	char c_oct = '\101';      // 65 ('A')

	if (c_plain != 65 || c_nl != 10 || c_tab != 9 || c_cr != 13 ||
		c_null != 0 || c_quote != 39 || c_slash != 92 || c_hex != 'A' || c_oct != 'A') {
		printf("Char Escapes FAILED!\n");
		return 0;
	}

	// 2. String literal with \t, \r, \n, \", \\, \x41, \101, and embedded \0
	char* s = "A\tB\r\n\"\\C\x44\105\0Z";

	// Verify every byte in memory via *(s + offset)
	if (*(s + 0) != 'A' ||   // 65
		*(s + 1) != '\t' ||   // 9
		*(s + 2) != 'B' ||   // 66
		*(s + 3) != '\r' ||   // 13
		*(s + 4) != '\n' ||   // 10
		*(s + 5) != '"' ||   // 34
		*(s + 6) != '\\' ||   // 92
		*(s + 7) != 'C' ||   // 67
		*(s + 8) != 'D' ||   // 68 (\x44)
		*(s + 9) != 'E' ||   // 69 (\105)
		*(s + 10) != '\0' ||  // 0  (\0)
		*(s + 11) != 'Z') {   // 90 (byte after embedded null)
		printf("String Escapes Byte Check FAILED!\n");
		return 0;
	}

	// 3. Empty string literal "" -> DB 0
	char* empty = "";
	if (*empty != '\0') {
		printf("Empty String Literal FAILED!\n");
		return 0;
	}

	// 4. Print formatted output containing \t, \", and \\ to verify printf + MASM DB
	printf("String & Char Escapes PASSED:\t\"Quoted\" \\ Backslash\n");
	return 1;
}

int main()
{
	int result;
	result = calc(2, 3);
	if (result == 5)
		printf("Test calc() PASSED with the result: %d\n", result);
	else
		printf("Test calc() FAILED \n");

	if (result < 6) {
		printf("Condition True: X (%d) is less than Y (%d)\n", result, 6);
	}
	else {
		printf("Condition False: X (%d) is greater or equal\n", result);
	}

	exchange_test();

	pointer_dereference();
	
	while_sum_test();	
	
	for_sum_test();	
	
	break_continue_test();	

	two_dimensional_matrix_test();
	
	integer_to_float_promoting();	

	int final_score;
	final_score = test_struct(10);
	if (final_score == 5041)
		printf("Test structs PASSED with value: %d\n", final_score);
	else
		printf("Test structs FAILED\n");

	float a;
	float b;
	a = 4.5;
	b = 2.1;

	// Triggers SSE2 comiss jump pipeline evaluation!
	if (a > b) {
		printf("Float Verification PASSED: 4.5 is greater than 2.1 via SSE2!\n");
	}
	else {
		printf("Float Verification FAILED\n");
	}

	int total_score;
	total_score = define_test();
	if (total_score == 47)
		printf("sum of defined test is PASSED with value: %d\n", total_score);
	else
		printf("sum of defined test is FAILED\n");

	int threshold;
	threshold = macro_test();
	if (threshold == 60)
		printf("macro test PASSED with value: %d\n", threshold);
	else
		printf("macro test FAILED\n");

	int complex_math_test_result;
	complex_math_test_result = complex_math_test();
	if (complex_math_test_result == 21)
		printf("complex math test PASSED with value: %d\n", complex_math_test_result);
	else
		printf("complex math test FAILED\n");

	int char_test_result;
	char_test_result = char_test();
	if (char_test_result == 70)
		printf("char test PASSED with value: %d\n", char_test_result);
	else
		printf("char test FAILED\n");

	string_pointer_test();

	printf("switch test result for 2 (should be 20) is: %d\n", switch_test(2));
	printf("switch test result for 12 (should be 99) is: %d\n", switch_test(12));
	printf("switch test result for 74 (should be 47) is: %d\n", switch_test(74));
	printf("switch test result for negative_test() (should be 99) is: %d\n", switch_test(negative_test()));

	printf("negative numbers operation 45 - 60 = %d\n", 45 - 60);

	test_div_mod();
	test_float_div();

	test_logic();
	test_not();

	int total;
	/* We pass '4' because we are providing four numbers to sum up */
	total = sum_numbers(4, 11, 22, 33, 44);

	if (total == 110) {
		printf("Variadic call test PASSED with value: %d\n", total);
	}
	else {
		printf("Variadic call test FAILED\n");
	}

	test_typedef_pipeline();

	test_sizeof();

	test_compound_operators();

	cast_verification_test();

	// Update the verification assertions
	int var_res1;
	int var_res2;

	// Test 1: Dynamic compilation check passing 3 values
	var_res1 = test_variadic_sum(3, 10, 20, 30);
	// Test 2: Dynamic compilation check passing 6 values
	var_res2 = test_variadic_sum(6, 1, 2, 3, 4, 5, 6);

	if (var_res1 == 60) {
		printf("Dynamic Variadic Test 1 (3 args) PASSED: %d\n", var_res1);
	}
	else {
		printf("Dynamic Variadic Test 1 (3 args) FAILED\n");
	}

	if (var_res2 == 21) {
		printf("Dynamic Variadic Test 2 (6 args) PASSED: %d\n", var_res2);
	}
	else {
		printf("Dynamic Variadic Test 2 (6 args) FAILED\n");
	}

	test_inc_dec();
	test_pointer_arithmetic();
	test_bitwise_and_ternary();

	printf("--- STARTING REGRESSION & BUGFIX VERIFICATION ---\n");

	int passed;
	passed = 0;
	passed += test_global_struct();
	passed += test_char_struct_members();
	passed += test_char_arrays();
	passed += test_negative_floats();

	passed += test_storage_classes(5);

	do_while_test();

	passed += test_function_pointers();

	test_char_literals();
	passed += test_string_and_char_escapes();

	// Verify void return;
	int flag;
	flag = 0;
	early_void_return(&flag, 1);
	if (flag == 42) {
		printf("Void return; Test PASSED!\n");
		passed += 1;
	}
	else {
		printf("Void return; Test FAILED: flag=%d\n", flag);
	}

	passed += test_union_ops();
	passed += test_for_empty_clauses();

	if (passed == 10) {
		printf("ALL 10 BUGFIX TESTS PASSED!\n");
		return 0;
	}
	else {
		printf("SOME TESTS FAILED (%d / 10 passed)\n", passed);
		return 1;
	}

	return 0;
}
