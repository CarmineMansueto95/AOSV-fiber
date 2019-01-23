#define DEV_NAME "/dev/fiber"

// for CREATE_FIBER
typedef struct fiber_arg_t{
    void* func;
    void* stack;
    void* params;
    pid_t ret;
}fiber_arg;


pid_t ConvertThreadToFiber();
pid_t CreateFiber(ssize_t stack_size, void* func, void* params);
int SwitchTo(pid_t fiber_id);
