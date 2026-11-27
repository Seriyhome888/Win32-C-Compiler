#include "internal.h"

#define API_VERSION 200
#define FEATURE_ALPHA

#define CLAMP_ADD(a, b) \
    (((a) + (b)) > 100 ? \
     100 : ((a) + (b)))

int test_preproc_if_elif() {
    int mode = 0;

#if defined(FEATURE_BETA)
    mode = 1;
#elif defined FEATURE_ALPHA && \
      (API_VERSION >= 200) && \
      (UNDEF_SYMBOL == 0) && \
      (0x10 == 16) && \
      ('A' == 65)
    mode = 2;
#elif 1
    mode = 3;
#else
    mode = 4;
#endif

    // Nested #if 0 ensuring #elif inside inactive block does not activate
#if 0
    #if 0
        mode = 99;
    #elif 1
        mode = 88;
    #endif
#endif

    int val = CLAMP_ADD(60, 55); // Splices across 3 lines and evaluates to 100
    if (mode == 2 && val == 100) {
        return 1;
    }
    return 0;
}

int test_pipeline() {
    int local_buffer;
    int check_limit;
    int target_metrics;

    local_buffer = BUFFER_SCALE;
    check_limit = RETRY_LIMIT;
    target_metrics = REVENUE_TARGET;

    // This statement must be dropped entirely by the conditional stack
#ifdef STACK_THRESHOLD
    local_buffer = STACK_THRESHOLD;
#endif

    if (check_limit != 5 || target_metrics != 5000) {
        return 1;
    }

    if (!test_preproc_if_elif()) {
        return 2;
    }

    return local_buffer; // Returns 128 (BUFFER_SCALE) as expected by run_tests.bat
}

int main()
{
    return test_pipeline();
}
