#include "shared.h"

unsigned int ConvertThreadToFiber();
unsigned int CreateFiber(ssize_t stack_size, void* func, void* params);
int SwitchTo(unsigned int fiber_id);
