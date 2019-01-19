#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <malloc.h>
#include <sys/ioctl.h>	//in order to call ioctl()

#include "headers/ioctl.h"
#include "headers/shared.h"	// for fiber_arg_t type

#define DEV_NAME "/dev/fiber"

unsigned int ConvertThreadToFiber(){
	int ret;
	unsigned int fib_id;
	int fd;
	
	fd = open(DEV_NAME, O_RDWR);
	
	ret = ioctl(fd, IOCTL_CONVERT_THREAD, &fib_id);
	if(ret || fib_id==0){
		printf("%s\n", strerror(errno));
		return 0;
	}
	return fib_id;
}

unsigned int CreateFiber(ssize_t stack_size, void* func, void* params){
	int ret;
	int fd;
	
	fd = open(DEV_NAME, O_RDWR);
	
	fiber_arg my_arg;
	my_arg.func = func;
	my_arg.params = params;
	
	void* stack = memalign(16,stack_size); // the stack must be 16 byte aligned
	/*
     * ioctl descreases the user stack pointer of 8 bytes before returning
     * since the user stack pointer must point to an address that is 16-byte alligned it is required it points exactly to the end of the alligned buffer pre-allocated
     * this is why 8 bytes are added
    */
	my_arg.stack = stack+stack_size+8;
	
	ret = ioctl(fd, IOCTL_CREATE_FIBER, &my_arg);
	
	unsigned int fib_id = my_arg.ret;
	
	if (ret || fib_id==0){
		printf("Create Fiber failed!\n");
		return 0;
	}

	printf("Create Fiber success!\n");
	return fib_id;
}

int SwitchTo(unsigned int fiber_id){
	int ret;
	int fd;
	
	unsigned int f_id = fiber_id;
	
	fd = open(DEV_NAME, O_RDWR);
	
	ret = ioctl(fd, IOCTL_SWITCH_TO, &f_id);
	
	if(ret){
		printf("SwitchTo failed!\n");
		return -1;
	}
	
	printf("SwitchTo success!\n");
	return 0;
}
