#include "module_shared.h"

#define MAX_FLS_INDEX 1024

typedef struct fls_struct_t{
	unsigned long	size;
	long long*		fls;		// actual FLS array
	unsigned long*	bitmask;	// FLS bitmask
}fls_struct_t;

typedef struct fiber_context_t{
	pid_t				fiber_id;	// id of the fiber
	struct pt_regs*		regs;		// status of the main registers
	struct fpu*			fpu;		// status of the floating point unit
	pid_t				thread;		// pid of the thread running the fiber
	spinlock_t			lock;
	struct fls_struct_t	fls;		// Fiber Local Storage
	struct hlist_node	node;
}fiber_context_t;

typedef struct process_t{
	pid_t		process_id;
	struct		hlist_node node;
	atomic_t	active_threads;

	DECLARE_HASHTABLE(threads,10);
	DECLARE_HASHTABLE(fibers,10);
}process_t;

typedef struct thread_t{
	struct process_t*			process;
	struct fiber_context_t*		selected_fiber;
	pid_t  						thread_id;
	struct hlist_node			node;
}thread_t;

int convert_thread(pid_t* arg);
int create_fiber(fiber_arg* my_arg);
int switch_to(pid_t target_fib);

int fls_alloc(unsigned long* arg);
int fls_free(unsigned long* arg);
int fls_get(struct fls_args* arg);
int fls_set(struct fls_args* arg);

int kprobe_entry_handler(struct kprobe* kp, struct pt_regs* regs);
