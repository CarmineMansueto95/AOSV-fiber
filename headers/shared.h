#define DEVICE_NAME "fiber"

// for CREATE_FIBER
typedef struct fiber_arg_t{
    void* func;
    void* stack;
    void* params;
    unsigned int ret;
}fiber_arg;
