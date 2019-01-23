#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <sys/ioctl.h>	//in order to call ioctl()

#include "headers/ioctl.h"
#include "headers/fiber_library.h"

void* ConvertThreadToFiber(){
	int ret;
	pid_t* fib_id;
	int fd;
	
	fd = open(DEV_NAME, O_RDWR);
	
	fib_id = (pid_t*) malloc(sizeof(pid_t));
	
	ret = ioctl(fd, IOCTL_CONVERT_THREAD, fib_id);
	if(ret || *fib_id==0){
		printf("%s\n", strerror(errno));
		return 0;
	}
	return fib_id;
}

void* CreateFiber(ssize_t stack_size, void* (*routine)(void*), void* args){
	
	int ret;
	int fd;
	pid_t* fib_id;
	
	fiber_arg my_arg;
	
	fd = open(DEV_NAME, O_RDWR);
	
	my_arg.routine = routine;
	my_arg.args = args;
	
	void* stack = memalign(16,stack_size); // the stack must be 16 byte aligned
	/*
     * ioctl descreases the user stack pointer of 8 bytes before returning
     * since the user stack pointer must point to an address that is 16-byte alligned it is required it points exactly to the end of the alligned buffer pre-allocated
     * this is why 8 bytes are added
    */
	my_arg.stack = stack+stack_size+8;
	
	fib_id = (pid_t*) malloc(sizeof(pid_t));
	
	ret = ioctl(fd, IOCTL_CREATE_FIBER, &my_arg);
	
	*fib_id = my_arg.ret;
	
	if (ret || *fib_id==0)
		return 0;

	return fib_id;
}

void SwitchToFiber(void* fiber){
	int ret;
	int fd;
	pid_t* f_id;
	
	f_id = fiber;
	
	fd = open(DEV_NAME, O_RDWR);
	
	ret = ioctl(fd, IOCTL_SWITCH_TO, f_id);
	
	if(ret)
		printf("SwitchTo failed!!!\n");
}
