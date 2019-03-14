// definitions used both by kernel and userspace library

// for CREATE_FIBER
struct fiber_arg_t{
    void* (*routine)(void*);
    void* stack;
    void* args;
    pid_t ret;
};

// for FLS_GET and FLS_SET
struct fls_args_t{
    unsigned long index;
    long long value;
};