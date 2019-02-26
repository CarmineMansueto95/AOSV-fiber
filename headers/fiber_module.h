#include "module_shared.h"

#define CLASS_NAME "fiberc"
#define START_MINOR 0	// MINOR number to start when allocating minors to device
#define NUM_MINORS 1	// # of minor numbers required by the device

// Declarations of functions
int dev_open (struct inode* i, struct file* f);
long ioctl_commands(struct file* filp, unsigned int cmd, unsigned long arg);
extern int convert_thread(pid_t* arg);
extern int create_fiber(struct fiber_arg_t* my_arg);
extern int switch_to(pid_t target_fib);

extern int fls_alloc(unsigned long* arg);
extern int fls_free(unsigned long* arg);
extern int fls_get(struct fls_args_t* arg);
extern int fls_set(struct fls_args_t* arg);

extern int kprobe_entry_handler(struct kprobe* kp, struct pt_regs* regs);
