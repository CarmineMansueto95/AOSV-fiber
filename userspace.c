#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>	//in order to call ioctl()

#include "headers/ioctl.h"

#define DEV_NAME "/dev/fiber"

int main(){
	int ret,fd;

	//Open file in Read/Write mode
	fd = open(DEV_NAME, O_RDWR);
	if(fd<0){
		printf("%s\n", strerror(errno));
		return -1;
	}

	printf("%s correctly opened!!!\n", DEV_NAME);

	//Testing the communication between Userspace and fiber_module, issuing the IOCTL_TEST command to /dev/fiber by means of ioctl()
	ret = ioctl(fd, IOCTL_TEST);
	if(ret){
		printf("%s\n", strerror(errno));
		return -1;
	}

	//Testing convert_thread
	unsigned int fib_id;
	ret = ioctl(fd, IOCTL_CONVERT_THREAD, &fib_id);
	if(ret){
		printf("%s\n", strerror(errno));
		return -1;
	}
	else
		printf("Convert Thread success! The fiber id is %u\n", fib_id);
	

	ret = close(fd);
	if(ret<0){
		printf("%s\n", strerror(errno));
		return -1;
	}
	
	printf("%s correctly closed!\n", DEV_NAME);

	return 0;
}
