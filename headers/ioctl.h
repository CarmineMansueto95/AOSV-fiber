#include <linux/ioctl.h>
#define IOCTL_LETTER 'k'		//Unique identifier for this set of ioctls (ioctl-number.txt documentation)

#define IOCTL_TEST _IO(IOCTL_LETTER, 0)
