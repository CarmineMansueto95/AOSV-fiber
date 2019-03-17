#include "module_shared.h"

#define HASHTABLE_BITS 10
#define MAX_FLS_INDEX 1024

// used to free the stuff allocated by entry_handlers
struct kret_data{
  struct pid_entry *ents;
};

struct fls_struct_t{
	unsigned long	size;
	long long*		fls;		// actual FLS array
	unsigned long*	bitmask;	// FLS bitmask
};

struct fiber_context_t{
	pid_t				fiber_id;	// id of the fiber
	struct pt_regs*		regs;		// status of the main registers
	struct fpu*			fpu;		// status of the floating point unit
	pid_t				thread;		// pid of the thread running the fiber, 0 if fiber is free, -1 if fiber is no longer available
	spinlock_t			lock;
	struct fls_struct_t	fls;		// Fiber Local Storage
	struct hlist_node	node;
};

struct process_t{
	pid_t		process_id;
	struct		hlist_node node;
	atomic_t	active_threads;

	DECLARE_HASHTABLE(threads,10);
	DECLARE_HASHTABLE(fibers,10);
};

struct thread_t{
	struct process_t*			process;
	struct fiber_context_t*		selected_fiber;
	pid_t  						thread_id;
	struct hlist_node			node;
};

int convert_thread(pid_t* arg);
int create_fiber(struct fiber_arg_t* arg);
int switch_to(pid_t target_fib);

int fls_alloc(unsigned long* arg);
int fls_free(unsigned long* arg);
int fls_get(struct fls_args_t* arg);
int fls_set(struct fls_args_t* arg);

int doexit_entry_handler(struct kprobe* kp, struct pt_regs* regs);

// *************************** PROC ********************************

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

#define NOD(NAME, MODE, IOP, FOP, OP) \
  {                                   \
    .name = (NAME),                   \
    .len = sizeof(NAME) - 1,          \
    .mode = MODE,                     \
    .iop = IOP,                       \
    .fop = FOP,                       \
    .op = OP,                         \
  }

#define DIR(NAME, MODE, iops, fops) \
  NOD(NAME, (S_IFDIR | (MODE)), &iops, &fops, {})

int kprobe_proc_readdir_handler(struct kretprobe_instance *p, struct pt_regs *regs);
int kprobe_proc_post_readdir_handler(struct kretprobe_instance *p, struct pt_regs *regs);
int kprobe_proc_lookup_handler(struct kretprobe_instance *, struct pt_regs *);
int kprobe_proc_post_lookup_handler(struct kretprobe_instance *, struct pt_regs *);

int fibdir_readdir(struct file *file, struct dir_context *ctx);
struct dentry *fibdir_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags);


// file_operations of /proc/PID/fibers
struct file_operations fibdir_fops = {
    .owner = THIS_MODULE,
    .read = generic_read_dir,
    .iterate_shared = fibdir_readdir,	// still to be implemented
    .llseek = generic_file_llseek,
};

// inode_operations of /proc/PID/fibers
struct inode_operations fibdir_iops = {
    .lookup = fibdir_lookup,			// still to be implemented
};

/*
// file_operations of any entry in /proc/PID/fibers
struct file_operations fibentry_fops = {
  .owner = THIS_MODULE,
  .read = fibentry_read;    // still to be defined
}*/