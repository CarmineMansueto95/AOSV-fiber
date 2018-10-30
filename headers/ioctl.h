#include <linux/ioctl.h>
#define MY_MAJOR 'k'

#define IOCTL_TEST _IO(MY_MAJOR, 0)
