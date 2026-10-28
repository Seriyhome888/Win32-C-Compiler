typedef char* va_list;

// Start right after the last named parameter (pointing to the first variadic argument)
#define va_start(ap, last_arg) (ap = (va_list)&last_arg + sizeof(last_arg))

// Fetch the current value cast to the requested type, then advance ap by 4 bytes
#define va_arg(ap, type)       (*(type*)((ap += 4) - 4))

#define va_end(ap)             (ap = (va_list)0)
