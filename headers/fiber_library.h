#define DEV_NAME "/dev/fiber"

// for CREATE_FIBER
typedef struct fiber_arg_t{
    void* (*routine)(void*);
    void* stack;
    void* args;
    pid_t ret;
}fiber_arg;

/*
My Old Ones
pid_t ConvertThreadToFiber();
pid_t CreateFiber(ssize_t stack_size, void* func, void* params);
int SwitchTo(pid_t fiber_id);
*/

void* ConvertThreadToFiber(void);
void* CreateFiber(ssize_t stack_size, void* (*routine)(void *), void *args);
void SwitchToFiber(void* fiber);
