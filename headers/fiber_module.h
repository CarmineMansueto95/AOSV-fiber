#include "shared.h"

#define CLASS_NAME "fiberc"
#define START_MINOR 0	// MINOR number to start when allocating minors to device
#define NUM_MINORS 1	// # of minor numbers required by the device

// Declarations of functions
int dev_open (struct inode* i, struct file* f);
long ioctl_commands(struct file* filp, unsigned int cmd, unsigned long arg);
extern int convert_thread(unsigned int* arg);
extern int create_fiber(fiber_arg* my_arg);
