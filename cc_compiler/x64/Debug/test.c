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
	printf("Pointer Value Verification 1: %d\n", retrieved_val);

	*ptr_addr = 99;              // Test assignment through indirect reference pointer
	retrieved_val = target_var;  // Read directly from target_var to see if it mutated
	printf("Pointer Value Verification 2 (Mutated): %d\n", retrieved_val);
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

int main()
{
	int result;
	result = calc(2, 3);
	printf("The final compiled result is: %d\n", result);

	if (result < 6) {
		printf("Condition True: X is less than Y\n");
	}
	else {
		printf("Condition False: X is greater or equal\n");
	}

	int arr;
	int res;
	arr = 42;
	res = arr;
	printf("The extracted contiguous array element value is: %d\n", res);

	pointer_dereference();

	int while_sum;
	while_sum = while_sum_test();
	printf("The loop sum total is: %d\n", while_sum);

	int for_sum;
	for_sum = for_sum_test();
	printf("The transformed for loop sum total is: %d\n", for_sum);

	int total;
	total = break_continue_test();
	printf("The final total using break and continue is: %d\n", total);

	int matrix[3][5];
	int val;
	// Target a nested cell coordinate index location
	matrix[1][2] = 88;
	val = matrix[1][2];
	printf("The structural 2D matrix dynamic value is: %d\n", val);

	float float_int_sum;
	float_int_sum = integer_to_float_promoting();
	printf("Mixed mode validation pipeline complete.%f\n", float_int_sum);

	int final_score;
	final_score = test_struct(10);
	printf("Final score is: %d\n", final_score);

	float a;
	float b;
	a = 4.5;
	b = 2.1;

	// Triggers SSE2 comiss jump pipeline evaluation!
	if (a > b) {
		printf("Float Verification Complete: 4.5 is greater than 2.1 via SSE2!\n");
	}
	else {
		printf("Float Verification Failed!\n");
	}

	return 0;
}
