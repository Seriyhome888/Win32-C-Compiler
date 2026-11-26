#include "stdio.h"

extern int shared_counter;
extern float shared_scale;
extern int shared_arr[4];
extern int compute_shared(int x);

int test_storage_classes(register int param_reg) {
    auto int a = 10;
    register int r = 20;
    extern int shared_counter;          // Block-scope extern variable (from math_lib.c)
    extern int compute_shared(int x);   // Block-scope extern prototype (from math_lib.c)

    int res = compute_shared(5);        // shared_counter: 100 -> 105, returns 115
    int sum = a + r + param_reg + res;  // 10 + 20 + 5 + 115 = 150

    if (sum == 150 && shared_counter == 105) {
        printf("auto, register, and block-scope extern PASSED!\n");
        return 0;
    }
    printf("Storage class test FAILED: sum=%d, shared_counter=%d\n", sum, shared_counter);
    return 1;
}

int main() {
    test_storage_classes(5);
    shared_counter = 100; // set to initial value

    shared_arr[2] = 50;
    int res = compute_shared(25); // shared_counter becomes 125, returns 135

    if (res == 135 && shared_counter == 125 && shared_scale == 2.5 && shared_arr[2] == 50) {
        printf("Multi-File Linking with extern PASSED!\n");
        return 0;
    }
    printf("Multi-File Linking FAILED: res=%d, counter=%d\n", res, shared_counter);

    return 1;
}
