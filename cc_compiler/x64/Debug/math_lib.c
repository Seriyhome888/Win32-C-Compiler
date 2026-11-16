int shared_counter = 100;
float shared_scale = 2.5;
int shared_arr[4];

static int secret_offset = 10; // Internal linkage (not visible to main.c)

int compute_shared(int x) {
    shared_counter += x;
    return shared_counter + secret_offset;
}
