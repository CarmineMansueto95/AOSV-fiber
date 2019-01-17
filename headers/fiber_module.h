#include <linux/hashtable.h>

#define DEVICE_NAME "fiber"
#define CLASS_NAME "fiberc"
#define START_MINOR 0	// MINOR number to start when allocating minors to device
#define NUM_MINORS 1	// # of minor numbers required by the device

typedef unsigned int fiber_id_t;	// id of the fiber, like the PID


typedef struct fiber_context_t{
	fiber_id_t fiber_id;		// id of the fiber, an unsigned integer
	struct pt_regs* regs;	// status of the main registers
	void* fpu;			// status of the floating point unit
	struct hlist_node node;
}fiber_context_t;

typedef struct process_t{
	pid_t process_id;
	struct hlist_node node;
	atomic_long_t active_threads;
	DECLARE_HASHTABLE(threads,10);
	DECLARE_HASHTABLE(fibers,10);
}process_t;

typedef struct thread_t{
	struct process_t* process;
	struct fiber_context_t* selected_fiber;
	pid_t thread_id;
	struct hlist_node node;
}thread_t;


// Declarations of functions
int dev_open (struct inode* i, struct file* f);
static char* set_permission(struct device *dev, umode_t *mode);
long ioctl_commands(struct file* filp, unsigned int cmd, unsigned long arg);
int convert_thread(unsigned long arg);