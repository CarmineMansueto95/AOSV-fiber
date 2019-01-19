#include "shared.h"

typedef struct fiber_context_t{
	unsigned int fiber_id;	// id of the fiber, an unsigned integer
	struct pt_regs* regs;	// status of the main registers
	struct fpu* fpu;		// status of the floating point unit
	pid_t thread;			// pid of the thread running the fiber
	spinlock_t lock;
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

int convert_thread(unsigned int* arg);
int create_fiber(fiber_arg* my_arg);
int switch_to(unsigned int target_fib);
