// Definitions used both by kernel and userspace library

#define DEVICE_NAME "fiber"

// for CREATE_FIBER
typedef struct fiber_arg_t{
    void* (*routine)(void*);
    void* stack;
    void* args;
    pid_t ret;
}fiber_arg;

// for FLS_GET and FLS_SET
struct fls_args_t{
    unsigned long index;
    long long value;
};