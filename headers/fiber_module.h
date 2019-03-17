#include "module_shared.h"

// declarations for device stuff
#define DEVICE_NAME "fiber"
#define CLASS_NAME "fiberc"
#define START_MINOR 0	// MINOR number to start when allocating minors to device
#define NUM_MINORS 1	// # of minor numbers required by the device

// used to free the stuff allocated by entry_handlers
struct kret_data{
  struct pid_entry *ents;
};

// declarations of module functions
int dev_open (struct inode* i, struct file* f);
long ioctl_commands(struct file* filp, unsigned int cmd, unsigned long arg);

// Declarations of utils functions (defined in "fiber_utils.c")
extern int convert_thread(pid_t* arg);
extern int create_fiber(struct fiber_arg_t* arg);
extern int switch_to(pid_t target_fib);

extern int fls_alloc(unsigned long* arg);
extern int fls_free(unsigned long* arg);
extern int fls_get(struct fls_args_t* arg);
extern int fls_set(struct fls_args_t* arg);

extern int doexit_entry_handler(struct kprobe* kp, struct pt_regs* regs);
extern int kprobe_proc_readdir_handler(struct kretprobe_instance *p, struct pt_regs *regs);
extern int kprobe_proc_post_readdir_handler(struct kretprobe_instance *p, struct pt_regs *regs);
extern int kprobe_proc_lookup_handler(struct kretprobe_instance *, struct pt_regs *);
extern int kprobe_proc_post_lookup_handler(struct kretprobe_instance *, struct pt_regs *);