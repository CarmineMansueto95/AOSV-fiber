#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>	//in order to call ioctl()

#include <headers/ioctl.h>

#define DEV_NAME "/dev/fiber"

int main(){
	int ret,fd;

	fd = open(DEV_NAME, O_RDWR);
	if(fd<0){
		printf("%s\n", strerror(errno));
		return -1;
	}

	printf("%s correctly opened!!!\n", DEV_NAME);

	//Testing the communication between Userspace and fiber_module
	ret = ioctl(fd, IOCTL_TEST);
	if(ret){
		printf("%s\n", strerror(errno))
	}

	ret = close(fd);
	if(ret<0){
		printf("%s\n", strerror(errno));
		return -1;
	}
	
	printf("%s correctly closed!\n", DEV_NAME);

	return 0;
}
