#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>	//in order to call ioctl()

#include "headers/fiber_library.h"

#define DEV_NAME "/dev/fiber"
#define STACK_SIZE (4096*2)

void foo(){
	while(1){
		printf("in foo function!\n");
		sleep(2);
	}
}

int main(){
	int ret;

	//Testing convert_thread
	unsigned int fib_id = ConvertThreadToFiber();
	if(fib_id==0){
		printf("ConvertThreadToFiber failed!\n");
		return -1;
	}
	
	printf("Convert Thread success! The fiber id is %u\n", fib_id);
	
	//Testing create_fiber()
	unsigned int fib_id2 = CreateFiber(STACK_SIZE, foo, NULL);
	if(fib_id2==0){
		printf("CreateFiber failed!\n");
		return -1;
	}
	
	printf("Create Fiber success! The fiber id is %u\n", fib_id2);
	
	SwitchTo(fib_id2);
	
	return 0;
}
