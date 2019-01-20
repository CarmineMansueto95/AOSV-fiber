#include "shared.h"

pid_t ConvertThreadToFiber();
pid_t CreateFiber(ssize_t stack_size, void* func, void* params);
int SwitchTo(pid_t fiber_id);
