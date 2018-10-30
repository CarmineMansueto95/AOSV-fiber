#define DEVICE_NAME "fiber"
#define CLASS_NAME "fiberc"
#define NUM_MINORS 1

long ioctl_commands(struct file *filp,unsigned int cmd, unsigned long arg);
