#include "config.h"

#ifdef REVENUE_TARGET
    #define RETRY_LIMIT 5
#endif

// This block must be dropped because EXPERIMENTAL_OPTIMIZE is omitted
#ifdef EXPERIMENTAL_OPTIMIZE
    #define STACK_THRESHOLD 9999
    int run_unstable_pass();
#endif
