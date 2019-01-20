#define DEVICE_NAME "fiber"

// for CREATE_FIBER
typedef struct fiber_arg_t{
    void* func;
    void* stack;
    void* params;
    pid_t ret;
}fiber_arg;
