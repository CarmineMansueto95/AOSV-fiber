#include "module_shared.h"

// declarations for device stuff
#define DEVICE_NAME "fiber"
#define CLASS_NAME "fiberc"
#define START_MINOR 0	// MINOR number to start when allocating minors to device
#define NUM_MINORS 1	// # of minor numbers required by the device

struct kret_data{
  struct pid_entry *ents;
};

union proc_op {
  int (*proc_get_link)(struct dentry *, struct path *);
  int (*proc_show)(struct seq_file *m, struct pid_namespace *ns, struct pid *pid, struct task_struct *task);
};

struct pid_entry{
  const char *name;
  unsigned int len;
  umode_t mode;
  const struct inode_operations *iop;
  const struct file_operations *fop;
  union proc_op op;
};

// declarations of module functions
int dev_open (struct inode* i, struct file* f);
long ioctl_commands(struct file* filp, unsigned int cmd, unsigned long arg);
int kprobe_proc_readdir_handler(struct kretprobe_instance *p, struct pt_regs *regs);
int kprobe_proc_post_readdir_handler(struct kretprobe_instance *p, struct pt_regs *regs);
int kprobe_proc_lookup_handler(struct kretprobe_instance *, struct pt_regs *);
int kprobe_proc_post_lookup_handler(struct kretprobe_instance *, struct pt_regs *);

// Declarations of utils functions (defined in "fiber_utils.c")
extern int convert_thread(pid_t* arg);
extern int create_fiber(struct fiber_arg_t* arg);
extern int switch_to(pid_t target_fib);

extern int fls_alloc(unsigned long* arg);
extern int fls_free(unsigned long* arg);
extern int fls_get(struct fls_args_t* arg);
extern int fls_set(struct fls_args_t* arg);

extern int kprobe_entry_handler(struct kprobe* kp, struct pt_regs* regs);