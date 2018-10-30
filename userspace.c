#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#define DEV_NAME "/dev/fiber"

int main(){
	int ret,fd;

	fd = open("/dev/fiber", O_RDWR);
	if(fd<0){
		printf("%s\n", strerror(errno));
		return -1;
	}

	printf("%s correctly opened!!!\n", DEV_NAME);

	ret = close(fd);
	if(ret<0){
		printf("%s\n", strerror(errno));
		return -1;
	}
	
	printf("%s correctly closed!\n", DEV_NAME);

	return 0;
}
