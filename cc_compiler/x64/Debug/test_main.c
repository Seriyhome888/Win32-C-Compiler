#include "internal.h"

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

    return local_buffer;
}
