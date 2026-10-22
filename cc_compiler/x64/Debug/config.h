#ifndef CONFIG_H
#define CONFIG_H

#define REVENUE_TARGET 5000
#define HIGH_PERFORMANCE_MODE

#ifdef HIGH_PERFORMANCE_MODE
    #define BUFFER_SCALE 128
#else
    #define BUFFER_SCALE 32
#endif

#endif
