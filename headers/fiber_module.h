#define DEVICE_NAME "fiber"
#define CLASS_NAME "fiberc"
#define NUM_MINORS 1

int dev_open (struct inode* i, struct file* f);
long ioctl_commands(struct file* filp,unsigned int cmd, unsigned long arg);
