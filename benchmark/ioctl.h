#include <linux/ioctl.h>

#define IOCTL_LETTER 'k'		//Unique identifier for this set of ioctls (ioctl-number.txt documentation)

#define IOCTL_TEST _IO(IOCTL_LETTER, 0)

#define IOCTL_CONVERT_THREAD _IOR(IOCTL_LETTER, 1, pid_t*)

#define IOCTL_CREATE_FIBER _IOWR(IOCTL_LETTER, 2, void*)

#define IOCTL_SWITCH_TO _IOW(IOCTL_LETTER, 3, pid_t*)

#define IOCTL_FLS_ALLOC _IOR(IOCTL_LETTER, 4, unsigned long*)

#define IOCTL_FLS_FREE _IOW(IOCTL_LETTER, 5, unsigned long*)

#define IOCTL_FLS_GET _IOWR(IOCTL_LETTER, 6, struct fls_args*)

#define IOCTL_FLS_SET _IOW(IOCTL_LETTER, 7, struct fls_args*)
