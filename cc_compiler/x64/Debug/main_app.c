#include "stdio.h"

extern int shared_counter;
extern float shared_scale;
extern int shared_arr[4];
extern int compute_shared(int x);

int main() {
    shared_arr[2] = 50;
    int res = compute_shared(25); // shared_counter becomes 125, returns 135

    if (res == 135 && shared_counter == 125 && shared_scale == 2.5 && shared_arr[2] == 50) {
        printf("Multi-File Linking with extern PASSED!\n");
        return 0;
    }
    printf("Multi-File Linking FAILED: res=%d, counter=%d\n", res, shared_counter);
    return 1;
}
