#include "stdio.h"

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
	   Your compiler allows pointer + integer offset scaling out of the box!
	*/
	args = &count + 4;

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

// Add this near your other test functions
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

	return 0;
}
