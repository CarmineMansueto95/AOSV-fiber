#define DEVICE_NAME "fiber"
#define CLASS_NAME "fiberc"
#define START_MINOR 0	// MINOR number to start when allocating minors to device
#define NUM_MINORS 1	// # of minor numbers required by the device

//Declarations of functions for struct file_operations of the device
int dev_open (struct inode* i, struct file* f);
long ioctl_commands(struct file* filp, unsigned int cmd, unsigned long arg);
