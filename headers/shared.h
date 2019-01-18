#define DEVICE_NAME "fiber"

// for CREATE_FIBER
typedef struct fiber_arg_t{
    void* func;
    void* stack;
    void* arg;
    unsigned int ret;
}fiber_arg;
