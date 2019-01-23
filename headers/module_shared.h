#define DEVICE_NAME "fiber"

// for CREATE_FIBER
typedef struct fiber_arg_t{
    void* (*routine)(void*);
    void* stack;
    void* args;
    pid_t ret;
}fiber_arg;
