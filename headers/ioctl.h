#include <linux/ioctl.h>

#define IOCTL_LETTER 'k'		//Unique identifier for this set of ioctls (ioctl-number.txt documentation)

#define IOCTL_TEST _IO(IOCTL_LETTER, 0)

#define IOCTL_CONVERT_THREAD _IOR(IOCTL_LETTER, 1, pid_t*)

#define IOCTL_CREATE_FIBER _IOWR(IOCTL_LETTER, 2, void*)

#define IOCTL_SWITCH_TO _IOW(IOCTL_LETTER, 3, pid_t*)
