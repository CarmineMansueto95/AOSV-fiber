#include "module_shared.h"

#define DEV_NAME "/dev/fiber"

void* ConvertThreadToFiber(void);
void* CreateFiber(ssize_t stack_size, void* (*routine)(void *), void *args);
void SwitchToFiber(void* fiber);

long FlsAlloc(void);
int FlsFree(long index);
long long FlsGetValue(long index);
int FlsSetValue(long index, long long value);