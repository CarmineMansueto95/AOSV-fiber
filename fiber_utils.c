#include <linux/module.h>
#include <linux/kernel.h>			// needed for printk() priorities
#include <linux/device.h>			// needed for creating device class and device under /dev
#include <linux/cdev.h>				// needed for cdev_alloc()

#include <linux/kprobes.h>

#include <asm-generic/barrier.h>	// needed for smp_bp()
#include <linux/atomic.h>
#include <linux/uaccess.h>			// needed for copy_to_user()
#include <linux/slab.h>				// needed for kmalloc() and kfree()
#include <linux/hashtable.h>
#include <linux/spinlock.h>
#include <asm/fpu/internal.h>

#include "headers/fiber_utils.h"	// for macros and function declarations
#include "headers/ioctl.h"

#define BITS 10
// hashtable containing entries which are process structs
DEFINE_HASHTABLE(processes, BITS);
//hash_init(processes);

 // global counter of the fibers
static atomic_t fiber_ctr = ATOMIC_INIT(0);	// static because it has to be allocated when program starts and live for all program life

int convert_thread(pid_t* arg){
	/* In arg I have to write:
	   - the id of the created fiber
	   - or 0 if the thread already called convert_thread
	   - or -1 in case of error
	*/
	int ret;
	int err;
	int succ;
	struct fiber_context_t* fib_ctx;
	struct thread_t* thread;
	struct process_t* process;
	
	struct process_t* tmp;
	struct thread_t* tmp2;
	
	pid_t current_pid;		// thread id of the thread that called convert_thread
	pid_t current_tgid;		// process id of the thread that called convert_thread
	
	current_pid = current->pid;
	current_tgid = current->tgid;
	
	printk(KERN_INFO "CONVERT_THREAD: current_pid = %u | current_tgid = %u\n", current_pid, current_tgid);
	
	err=-1;	//to be used with copy_to_user()
	succ=0; //to be used with copy_to_user()
	
	// Going to check if the current thread already called convert_thread
	hash_for_each_possible_rcu(processes, tmp, node, current_tgid) {
		if(tmp->process_id == current_tgid){
			// I found the entry in the hashtable corresponding to the process of current thread
			// I have to check if there is the struct thread corresponding to the current thread in its "threads" hashtable
			hash_for_each_possible_rcu(tmp->threads, tmp2, node, current_pid) {
				if(tmp2->thread_id == current_pid){
					// I found it! "current" already called convert_thread, so it is already a fiber. I have to return 0 to UserSpace
					ret = copy_to_user((void*)arg, &(succ), sizeof(int));
					if(ret!=0){
						printk(KERN_INFO "copy_to_user failed!\n");
						return -1;
					}
					return 0;
				}
			}
			
			// If I am here, the process of current exists in my data structures but current threads does not exists
			// So I have to allocate only the struct thread and the fiber context
			
			// allocating the fiber_context_t
			fib_ctx = (struct fiber_context_t*) kmalloc(sizeof(struct fiber_context_t), GFP_KERNEL);
			if(!fib_ctx){
				// kmalloc failed
				printk(KERN_INFO "kmalloc for fiber_context_t failed!\n");
				ret = copy_to_user((void*)arg, &(err), sizeof(int)); // I have to return -1 to UserSpace
				if(ret!=0)
					printk(KERN_INFO "copy_to_user failed!\n");
				return -1;
			}
			// allocating pt_regs of the fiber, to be putted into fiber_context_t of the fiber
			fib_ctx->regs = kmalloc(sizeof(struct pt_regs), GFP_KERNEL);
			if(!fib_ctx->regs){
				// kmalloc failed
				ret = copy_to_user((void*)arg,&(err),sizeof(int)); // I have to return -1 to UserSpace
				if(ret!=0)
					printk(KERN_INFO "copy_to_user failed!\n");
				kfree(fib_ctx);
				return -1;
			}
			// copying thread status into fiber status
			memcpy(fib_ctx->regs, task_pt_regs(current), sizeof (struct pt_regs));
			fib_ctx->fpu=NULL;
			
			fib_ctx->fpu = kmalloc(sizeof(struct fpu), GFP_KERNEL);
			copy_fxregs_to_kernel(fib_ctx->fpu); // initializing the FPU of the fiber with the one of current
			
			//assigning id to the fiber atomically
			smp_mb();
			fib_ctx->fiber_id = (unsigned int) atomic_inc_return(&fiber_ctr);	//atomic_inc_return increments by 1 the atomic counter and returns the value of the counter as int, so I cast to unsigned int
			smp_mb();
			
			fib_ctx->thread = current_pid;
			spin_lock_init(&fib_ctx->lock);
			
			printk(KERN_INFO "Thread succesfully converted into Fiber, fiber_id=%u\n", fib_ctx->fiber_id);
			

			// adding the fiber to the hashtable of fibers of process
			hash_add_rcu(tmp->fibers, &(fib_ctx->node), fib_ctx->fiber_id);
			
			// creating the thread struct
			thread = kmalloc(sizeof(struct thread_t), GFP_KERNEL);
			thread->process = tmp;
			thread->selected_fiber = fib_ctx;
			thread->thread_id = current_pid;
			

			//adding the thread to the hashtable of threads of process
			hash_add_rcu(tmp->threads, &(thread->node), current_pid);
			
			atomic_inc(&(tmp->active_threads));	// incrementing the counter of threads of the process
			
			printk(KERN_INFO "Structures in convert_thread set up!\n");
			//writing the fiber id in arg to give it to the userspace
			ret = copy_to_user((unsigned int*)arg, &(fib_ctx->fiber_id), sizeof(unsigned int)); // I have to return fiber_id to UserSpace
			if(ret!=0){
				printk(KERN_INFO "copy_to_user failed!\n");
				return -1;
			}
			return 0;
		}
	}
	
	// If I am here, there were not even the struct of the process of the current thread in my data structures
	// I have to allocate everything!
	process = kmalloc(sizeof(struct process_t), GFP_KERNEL);
	thread = kmalloc(sizeof(struct thread_t), GFP_KERNEL);
	
	fib_ctx = kmalloc(sizeof(struct fiber_context_t), GFP_KERNEL);
	if(!fib_ctx){
		// kmalloc failed
		printk(KERN_INFO "kmalloc for fiber_context_t failed!\n");
		ret = copy_to_user((void*)arg, &(err), sizeof(int));  // I have to return -1 to UserSpace
		if(ret!=0)
			printk(KERN_INFO "copy_to_user failed!\n");
		return -1;
	}

	// allocating pt_regs of the fiber, to be putted into fiber_context_t of the fiber
	fib_ctx->regs = kmalloc(sizeof(struct pt_regs), GFP_KERNEL);
	if(!fib_ctx->regs){
		// kmalloc failed
		ret = copy_to_user((void*)arg,&(err),sizeof(int));  // I have to return -1 to UserSpace
		if(ret!=0)
			printk(KERN_INFO "copy_to_user failed!\n");
		kfree(fib_ctx);
		return -1;
	}

	// copying thread status into fiber status
	memcpy(fib_ctx->regs, task_pt_regs(current), sizeof (struct pt_regs));
	fib_ctx->fpu=NULL;
	
	fib_ctx->fpu = kmalloc(sizeof(struct fpu), GFP_KERNEL);
	copy_fxregs_to_kernel(fib_ctx->fpu); // initializing the FPU of the fiber with the one of current
	
	//assigning id to the fiber atomically
	smp_mb();
	fib_ctx->fiber_id = (unsigned int) atomic_inc_return(&fiber_ctr);	//atomic_inc_return increments by 1 the atomic counter and returns the value of the counter as int, so I cast to unsigned int
	smp_mb();
	
	fib_ctx->thread = current_pid;
	spin_lock_init(&fib_ctx->lock);
	
	printk(KERN_INFO "Thread succesfully converted into Fiber, fiber_id=%u\n", fib_ctx->fiber_id);

	process->process_id = current_tgid;
	thread->process = process;
	thread->selected_fiber = fib_ctx;
	thread->thread_id = current_pid;
	// initializing the fibers hashtable of process
	hash_init(process->fibers);
	// adding the fiber to the fibers hashtable of process
	hash_add_rcu(process->fibers, &(fib_ctx->node), fib_ctx->fiber_id);
	// initializing the threads hashtable of process
	hash_init(process->threads);
	//adding the thread to the hashtable of threads of process
	hash_add_rcu(process->threads, &(thread->node), current_pid);
	
	hash_add_rcu(processes, &(process->node), current_tgid);
	
	atomic_set(&(process->active_threads), 1);	// initializing the atomic counter of threads
	
	printk(KERN_INFO "Structures in convert_thread set up!\n");
	//writing the fiber id in arg to give it to the userspace
	ret = copy_to_user((unsigned int*)arg, &(fib_ctx->fiber_id), sizeof(unsigned int));  // I have to return fiber_id to UserSpace
	if(ret!=0){
		printk(KERN_INFO "copy_to_user failed!\n");
		return -1;
	}
	return 0;
}

int create_fiber(fiber_arg* my_arg){
	
	unsigned int err;
	struct fiber_context_t* fib_ctx;
	
	struct process_t* tmp;
	struct thread_t* tmp2;
	
	pid_t current_pid;		// thread id of the thread that called convert_thread
	pid_t current_tgid;		// process id of the thread that called convert_thread
	
	current_pid = current->pid;
	current_tgid = current->tgid;
	
	printk(KERN_INFO "CREATE_FIBER: current_pid = %u | current_tgid = %u\n", current_pid, current_tgid);
	
	err=0;	//to be used with copy_to_user(), it is 0 because userspace expects the fiber_id (which is >0) or 0 if any error happened
	
	// going to check if the "current" thread called "convert_thread" at least once, otherwise it cannot create new fibers!
	hash_for_each_possible_rcu(processes, tmp, node, current_tgid) {
		if(tmp->process_id == current_tgid){
			// I found the entry in the hashtable corresponding to the process of current thread
			// I have to check if there is the struct thread corresponding to the current thread in its "threads" hashtable
			hash_for_each_possible_rcu(tmp->threads, tmp2, node, current_pid) {
				if(tmp2->thread_id == current_pid){
					// I found it! "current" already called convert_thread, so it is already a fiber. So it can create a fiber!
					
					fib_ctx = kmalloc(sizeof(fiber_context_t), GFP_KERNEL);
					if(!fib_ctx) return -1;
					
					fib_ctx->regs = kmalloc(sizeof (struct pt_regs), GFP_KERNEL);
					if(!fib_ctx->regs) return -1;
					
					memcpy(fib_ctx->regs, task_pt_regs(current), sizeof (struct pt_regs));
					fib_ctx->regs->sp = (unsigned long)my_arg->stack;
					fib_ctx->regs->bp = (unsigned long)my_arg->stack;
					fib_ctx->regs->ip = (unsigned long)my_arg->routine;
					fib_ctx->regs->di = (unsigned long)my_arg->args;
					
					fib_ctx->fpu = kmalloc(sizeof(struct fpu), GFP_KERNEL); // I have not to initialize it, a new fiber has to have an empty FPU
					
					smp_mb();
					fib_ctx->fiber_id = atomic_inc_return(&fiber_ctr);
					smp_mb();
					
					fib_ctx->thread = 0;	// a new fiber is free, no threads are running it
					spin_lock_init(&fib_ctx->lock);
					
					// adding the fiber to the hashtable of fibers of process
					hash_add_rcu(tmp->fibers, &(fib_ctx->node), fib_ctx->fiber_id);
					
					my_arg->ret = fib_ctx->fiber_id;
					
					printk(KERN_INFO "Fiber succesfully created! fiber_id = %u\n", fib_ctx->fiber_id);

					return 0;
				}
			}
		}
	}
	
	// If I am here, the thread "current" did never called "convert_thread", so it cannot create fibers!
	printk(KERN_INFO "create_fiber not allowed. Thread did never call convert_thread!\n");
	return -1;
	
}

int switch_to(pid_t target_fib){
	
	struct process_t* tmp;
	struct thread_t* tmp2;
	struct fiber_context_t* tmp3;
	struct fiber_context_t* old_fiber;
	
	struct pt_regs* current_regs;
	
	pid_t current_pid;		// thread id of the thread that called convert_thread
	pid_t current_tgid;		// process id of the thread that called convert_thread
	
	current_pid = current->pid;
	current_tgid = current->tgid;
	
	current_regs = task_pt_regs(current);
	
	// going to check if the "current" thread called "convert_thread" at least once, otherwise it cannot do switch_to!
	hash_for_each_possible_rcu(processes, tmp, node, current_tgid) {
		if(tmp->process_id == current_tgid){
			// I found the entry in the hashtable corresponding to the process of current thread
			// I have to check if there is the struct thread corresponding to the current thread in its "threads" hashtable
			hash_for_each_possible_rcu(tmp->threads, tmp2, node, current_pid) {
				if(tmp2->thread_id == current_pid){
					// I found it! "current" already called convert_thread, so it is already a fiber.
					
					old_fiber = tmp2->selected_fiber;
					
					//Going to check if the target fiber exists within the fibers of the current process
					hash_for_each_possible_rcu(tmp->fibers, tmp3, node, target_fib) {
						if(tmp3->fiber_id == target_fib){
							// The fiber I want to switch to exists! It is tmp3
							
							if(!spin_trylock(&tmp3->lock)){
								printk(KERN_INFO "could not acquire lock of target fiber! Some other is trying to acquire it!");
								return -1;
							}
							if(tmp3->thread != 0){
								printk(KERN_INFO "the fiber is occupied by another thread!");
								return -1;
							}
							
							// If i am here, I can steal the fiber
							tmp3->thread = current_pid;
							
							spin_unlock(&tmp3->lock);
							
							// saving the state of the current thread into the old fiber
							spin_lock(&old_fiber->lock);
							old_fiber->thread = 0;	// now the old fiber is free
							memcpy(old_fiber->regs, current_regs, sizeof(struct pt_regs));
							copy_fxregs_to_kernel(old_fiber->fpu); // saving the FPU into the old fiber
							spin_unlock(&old_fiber->lock);
							
							// storing the state of the target fiber into the current thread
							tmp2->selected_fiber = tmp3;
							memcpy(current_regs, tmp3->regs, sizeof(struct pt_regs));
							copy_kernel_to_fxregs(&(tmp3->fpu->state.fxsave));
							return 0;
						
						}
					}

					printk(KERN_INFO "The target fiber does not exist!\n");
					return -1;
				}

			}
		}
	}
	
	printk(KERN_INFO "The current thread never called convert_thread! Cannot do switch_to!\n");
	return -1;				
}

int kprobe_entry_handler(struct kprobe* kp, struct pt_regs* regs){
	
	int bkt;
	
	struct process_t* tmp;
	struct thread_t* tmp2;
	
	struct fiber_context_t* tmp3;
	
	pid_t current_pid;		// thread id of the thread that called convert_thread
	pid_t current_tgid;		// process id of the thread that called convert_thread
	
	struct pt_regs* current_regs;
	
	current_pid = current->pid;
	current_tgid = current->tgid;
	
	current_regs = task_pt_regs(current);
	
	// going to check if the "current" thread called "convert_thread" at least once, otherwise it cannot do switch_to!
	hash_for_each_possible_rcu(processes, tmp, node, current_tgid) {
		if(tmp->process_id == current_tgid){
			// I found the entry in the hashtable corresponding to the process of current thread
			// I have to check if there is the struct thread corresponding to the current thread in its "threads" hashtable
			
			if(atomic_read(&(tmp->active_threads))==1){
				// The thread that just exited was the last one!
				// destroying all fibers
				hash_for_each_rcu(tmp->fibers, bkt, tmp3, node){
					kfree(tmp3->regs);
					kfree(tmp3->fpu);
					hash_del_rcu(&(tmp3->node));
					kfree(tmp3);
					atomic_dec(&(fiber_ctr));	// decrementing the global counter of fibers
				}
				// destroying all threads (actually just one)
				hash_for_each_rcu(tmp->threads, bkt, tmp2, node){
					hash_del_rcu(&(tmp2->node));
					kfree(tmp2);
				}
				
				//destroying the process
				hash_del_rcu(&(tmp->node));
				kfree(tmp);
				
				return 0;		
			}
			else{
				// The thread that just exited wasn't the last one!
				
				// destroying the thread
				hash_for_each_possible_rcu(tmp->threads, tmp2, node, current_pid){
					if(tmp2->thread_id == current_pid){
						if(tmp2->selected_fiber!=NULL){
							// Saving the context to the fiber
							memcpy(tmp2->selected_fiber->regs, current_regs, sizeof(struct pt_regs));
							copy_fxregs_to_kernel(tmp2->selected_fiber->fpu); // saving the FPU into the old fiber
							spin_lock(&(tmp2->selected_fiber->lock));
							tmp2->selected_fiber->thread=0;
							spin_unlock(&(tmp2->selected_fiber->lock));
						}
					}
					hash_del_rcu(&(tmp2->node));
					kfree(tmp2);
				}
				return 0;
			}
		}
	}	
	return 0;
}
