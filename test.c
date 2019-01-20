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

pid_t fibers[4];

void foo1(){
	
	while(1){
		printf("In foo1 function!\n");
		sleep(1);
		SwitchTo(fibers[2]);
	}
	
	exit(0);
}

void foo2(){
	while(1){
		printf("In foo2 function!\n");
		sleep(1);
		SwitchTo(fibers[1]);
	}
	
	exit(0);
}

int main(){
	int ret;

	//Testing convert_thread
	fibers[0] = ConvertThreadToFiber();
	if(fibers[0]==0){
		printf("ConvertThreadToFiber failed!\n");
		return -1;
	}
	
	//Testing create_fiber()
	fibers[1] = CreateFiber(STACK_SIZE, foo1, NULL);
	if(fibers[1]==0){
		printf("CreateFiber failed!\n");
		return -1;
	}
	
	//Testing create_fiber()
	fibers[2] = CreateFiber(STACK_SIZE, foo2, NULL);
	if(fibers[2]==0){
		printf("CreateFiber failed!\n");
		return -1;
	}

	SwitchTo(fibers[1]);
	
	return 0;
}
