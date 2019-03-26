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
#include <linux/fs.h>
#include <linux/kallsyms.h>

#include "headers/fiber_utils.h"	// for macros and function declarations
#include "headers/ioctl.h"

// hashtable containing entries which are process_t structs
DEFINE_HASHTABLE(processes, HASHTABLE_BITS);
//hash_init(processes);
spinlock_t cnvtr_lock;

// pid_entry of the "fibers" directory to be exposed in /proc/PID
struct pid_entry fiber_folder = DIR("fibers", S_IRUGO | S_IXUGO, fibdir_iops, fibdir_fops);   // DIR is a macro in proc/base.c which creates a pid_entry

int convert_thread(pid_t* arg){
	/* In arg I have to write:
	   - the id of the created fiber
	   - or 0 if the thread already called convert_thread
	   - or -1 in case of error
	*/
	int ret, err, succ;
	unsigned long flags; //for spin_lock_irqsave and spin_lock_irqrestore
    struct process_t *process, *tmp;
    struct thread_t *thread, *tmp2;
	struct fiber_context_t* fib_ctx;
	pid_t current_pid, current_tgid;		// pid and tgid of the thread that called convert_thread
	struct process_t* process_f;

	current_pid = current->pid;
	current_tgid = current->tgid;

	err=-1;	//to be used with copy_to_user()
	succ=0; //to be used with copy_to_user() when "current" already called convert_thread
	tmp = NULL;
	process_f=NULL;
	process=NULL;

	spin_lock_irqsave(&cnvtr_lock, flags);

	// Going to check if the current thread already called convert_thread
	hash_for_each_possible_rcu(processes, tmp, node, current_tgid) {
		if(tmp->process_id == current_tgid){
			spin_unlock_irqrestore(&cnvtr_lock, flags);
			process_f = tmp; // process found, I store it into process_f

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
		}
	}

	if(process_f==NULL){
		// I am the first thread who call convert_thread, I have to instantiate the process struct and some critical field before others can use it
		process = (struct process_t*) kmalloc(sizeof(struct process_t), GFP_ATOMIC); // cannot sleep while holding a lock, so GFP_ATOMIC
		process->process_id = current_tgid;
		atomic_set(&(process->active_threads), 0); // initializing the atomic counter of threads
		atomic_set(&(process->active_fibers), 0);  // initializing the atomic counter of fibers
		atomic_set(&(process->fiber_ctr), 0); // initializing the atomic fiber_id counter

		hash_init(process->threads); // initializing the threads hashtable of process
		hash_init(process->fibers); // initializing the fibers hashtable of process

		hash_add_rcu(processes, &(process->node), current_tgid); // adding the struct process to the global hashtable

		spin_unlock_irqrestore(&cnvtr_lock, flags);
	}

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

	// allocating pt_regs of the fiber
	fib_ctx->regs = kmalloc(sizeof(struct pt_regs), GFP_KERNEL);
	if(!fib_ctx->regs){
		// kmalloc failed
		ret = copy_to_user((void*)arg,&(err),sizeof(int)); // I have to return -1 to UserSpace
		if(ret!=0)
			printk(KERN_INFO "copy_to_user failed!\n");
		kfree(fib_ctx);
		return -1;
	}

	// creating the thread struct
	thread = (struct thread_t*) kmalloc(sizeof(struct thread_t), GFP_KERNEL);
	if(!thread){
		// kmalloc failed
		printk(KERN_INFO "kmalloc for thread_t failed!\n");
		ret = copy_to_user((void*)arg, &(err), sizeof(int)); // I have to return -1 to UserSpace
		if(ret!=0)
			printk(KERN_INFO "copy_to_user failed!\n");
		kfree(fib_ctx->regs);
		kfree(fib_ctx);
		return -1;
	}

	// copying thread status (pt_regs and FPU) into fiber status
	memcpy(fib_ctx->regs, task_pt_regs(current), sizeof (struct pt_regs));
	fib_ctx->fpu = kzalloc(sizeof(struct fpu), GFP_KERNEL);
	copy_fxregs_to_kernel(fib_ctx->fpu); // initializing the FPU of the fiber with the one of current
			
	fib_ctx->thread = current_pid;
	spin_lock_init(&fib_ctx->lock);

	//Empty FLS
	fib_ctx->fls.size = 0;
	fib_ctx->fls.fls = NULL;
	fib_ctx->fls.bitmask = NULL;

	thread->selected_fiber = fib_ctx;
	thread->thread_id = current_pid;

	fib_ctx->entry_point = NULL;
	fib_ctx->creator = current_pid;
	fib_ctx->activations = 1;
	fib_ctx->f_activations = 0;
	fib_ctx->execution_time = 0;
	fib_ctx->last_execution = (((current->utime) + (current->stime)) / 1000000); // in ms

	if(process_f!=NULL){
		// I do not have to instantiate a process structure, it already exists
		thread->process = process_f;
		atomic_inc(&(process_f->active_threads)); // incrementing the counter of threads of the process
		atomic_inc(&(process_f->active_fibers));  // incrementing the counter of fibers of the process

		// assigning id to the fiber atomically
		smp_mb();
		fib_ctx->fiber_id = atomic_inc_return(&(process_f->fiber_ctr));	//atomic_inc_return increments by 1 the atomic counter and returns the value of the counter as int
		smp_mb();
		snprintf(fib_ctx->name, 10, "%d", fib_ctx->fiber_id);

		hash_add_rcu(process_f->fibers, &(fib_ctx->node), fib_ctx->fiber_id); // adding the fiber to the hashtable of fibers of process
		hash_add_rcu(process_f->threads, &(thread->node), current_pid); //adding the thread to the hashtable of threads of process

		//printk(KERN_INFO "Fiber succesfully converted! fiber_id = %u\n", fib_ctx->fiber_id);

		//writing the fiber id in arg to give it to the userspace
		ret = copy_to_user((int*)arg, &(fib_ctx->fiber_id), sizeof(int)); // I have to return fiber_id to UserSpace
		if(ret!=0){
			printk(KERN_INFO "copy_to_user failed!\n");
			return -1;
		}
		return 0;
	}

	thread->process = process;
	atomic_inc(&(process->active_threads));
	atomic_inc(&(process->active_fibers));

	//assigning id to the fiber atomically
	smp_mb();
	fib_ctx->fiber_id = (unsigned int) atomic_inc_return(&(process->fiber_ctr));	//atomic_inc_return increments by 1 the atomic counter and returns the value of the counter as int, so I cast to unsigned int
	smp_mb();
	snprintf(fib_ctx->name, 10, "%d", fib_ctx->fiber_id);

	hash_add_rcu(process->fibers, &(fib_ctx->node), fib_ctx->fiber_id); // adding the fiber to the fibers hashtable of process
	hash_add_rcu(process->threads, &(thread->node), current_pid); //adding the thread to the hashtable of threads of process

	//printk(KERN_INFO "Fiber succesfully converted! fiber_id = %u\n", fib_ctx->fiber_id);

	//writing the fiber id in arg to give it to the userspace
	ret = copy_to_user((int*)arg, &(fib_ctx->fiber_id), sizeof(int)); // I have to return fiber_id to UserSpace
	if(ret!=0){
		printk(KERN_INFO "copy_to_user failed!\n");
		return -1;
	}
	return 0;
}

int create_fiber(struct fiber_arg_t* arg){
	
	unsigned int err;
	struct process_t* tmp;
	struct thread_t* tmp2;
	struct fiber_context_t* fib_ctx;
	struct fiber_arg_t my_arg;
	pid_t current_pid, current_tgid;		// pid and tgid of the thread that called convert_thread

	current_pid = current->pid;
	current_tgid = current->tgid;
	
	copy_from_user(&my_arg, (const struct fiber_arg_t*) arg, sizeof(struct fiber_arg_t));
	
	err=0;	//to be used with copy_to_user(), it is 0 because userspace expects the fiber_id (which is >0) or 0 if any error happened
	
	// going to check if the "current" thread called "convert_thread" at least once, otherwise it cannot create new fibers!
	hash_for_each_possible_rcu(processes, tmp, node, current_tgid) {
		if(tmp->process_id == current_tgid){
			// I found the entry in the hashtable corresponding to the process of current thread
			// I have to check if there is the struct thread corresponding to the current thread in its "threads" hashtable
			hash_for_each_possible_rcu(tmp->threads, tmp2, node, current_pid) {
				if(tmp2->thread_id == current_pid){
					// I found it! "current" already called convert_thread, so it is already a fiber. So it can create a fiber!
					
					fib_ctx = kmalloc(sizeof(struct fiber_context_t), GFP_KERNEL);
					if(!fib_ctx) return -1;
					
					fib_ctx->regs = kmalloc(sizeof (struct pt_regs), GFP_KERNEL);
					if(!fib_ctx->regs){
						kfree(fib_ctx);
						return -1;
					}
					
					memcpy(fib_ctx->regs, task_pt_regs(current), sizeof (struct pt_regs));
					fib_ctx->regs->sp = (unsigned long)my_arg.stack;
					fib_ctx->regs->bp = (unsigned long)my_arg.stack;
					fib_ctx->regs->ip = (unsigned long)my_arg.routine;
					fib_ctx->regs->di = (unsigned long)my_arg.args;
					
					fib_ctx->fpu = kzalloc(sizeof(struct fpu), GFP_KERNEL);
					copy_fxregs_to_kernel(fib_ctx->fpu); // initializing the FPU of the fiber with the one of current

					smp_mb();
					fib_ctx->fiber_id = atomic_inc_return(&(tmp->fiber_ctr));
					smp_mb();
					
					fib_ctx->thread = 0;	// a new fiber is free, no threads are running it
					spin_lock_init(&fib_ctx->lock);

					//Empty FLS
					fib_ctx->fls.size = 0;
					fib_ctx->fls.fls = NULL;
					fib_ctx->fls.bitmask = NULL;

					fib_ctx->entry_point = (void*)fib_ctx->regs->ip;
					fib_ctx->creator = current_pid;
					fib_ctx->activations = 0;
					fib_ctx->f_activations = 0;
					fib_ctx->execution_time = 0;
					fib_ctx->last_execution = (((current->utime) + (current->stime)) / 1000000); // in ms

					snprintf(fib_ctx->name, 10, "%d", fib_ctx->fiber_id);
					
					// adding the fiber to the hashtable of fibers of process
					hash_add_rcu(tmp->fibers, &(fib_ctx->node), fib_ctx->fiber_id);
					atomic_inc(&(tmp->active_fibers));  // incrementing the counter of fibers of the process

					my_arg.ret = fib_ctx->fiber_id;

					copy_to_user((struct fiber_arg*) arg, (const struct fiber_arg_t*) &my_arg, sizeof(struct fiber_arg_t));
					
					//printk(KERN_INFO "Fiber succesfully created! fiber_id = %u\n", fib_ctx->fiber_id);

					return 0;
				}
			}
		}
	}
	
	// If I am here, the thread "current" did never call "convert_thread", so it cannot create fibers!
	printk(KERN_INFO "create_fiber not allowed. Thread did never call convert_thread!\n");
	return -1;
}

int switch_to(pid_t target_fib){
	
	struct process_t* tmp;
	struct thread_t* tmp2;
	struct fiber_context_t *tmp3, *old_fiber;
	struct pt_regs* current_regs;
	pid_t current_pid, current_tgid;	// pid and tgid of the thread that called convert_thread
	
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
								tmp3->f_activations +=1;
								printk(KERN_INFO "could not acquire lock of target fiber! Some other is trying to acquire it!");
								return -1;
							}
							if(tmp3->thread > 0){
								tmp3->f_activations +=1;
								spin_unlock(&tmp3->lock);
								printk(KERN_INFO "the target fiber is occupied by another thread!\n thread:%d | fiber:%d \n", tmp3->thread, tmp3->fiber_id);
								return -1;
							}

							if(tmp3->thread < 0){
								spin_unlock(&tmp3->lock);
								printk(KERN_INFO "the target fiber is no longer available!\n");
								return -1;
							}
							
							// If i am here, I can steal the fiber
							tmp3->thread = current_pid;
							tmp3->activations +=1;
							tmp2->selected_fiber = tmp3;
							spin_unlock(&tmp3->lock);
							
							// saving the state of the current thread into the old fiber
							memcpy(old_fiber->regs, current_regs, sizeof(struct pt_regs));
							copy_fxregs_to_kernel(old_fiber->fpu); // saving the FPU into the old fiber
							old_fiber->execution_time += (((current->utime) + (current->stime)) / 1000000) - old_fiber->last_execution;
							old_fiber->thread = 0;	// now the old fiber is free

							// storing the state of the target fiber into the current thread
							memcpy(current_regs, tmp3->regs, sizeof(struct pt_regs));
							copy_kernel_to_fxregs(&(tmp3->fpu->state.fxsave));
							tmp3->last_execution = (((current->utime) + (current->stime)) / 1000000);
							return 0;
						
						}
					}

					printk(KERN_INFO "The target fiber does not exist!\n");
					return -1;
				}

			}
		}
	}
	
	// If I am here, the thread "current" did never call "convert_thread", so it cannot switch_to!
	printk(KERN_INFO "switch_to not allowed. Thread did never call convert_thread!\n");
	return -1;			
}

int fls_alloc(unsigned long* arg){

	struct process_t* tmp;
	struct thread_t* tmp2;
	struct fiber_context_t* fib_ctx;
	struct fls_struct_t* fib_fls;

	pid_t current_pid;		// thread id of the thread that called fls_alloc
	pid_t current_tgid;		// process id of the thread that called fls_alloc

	unsigned long index;
	
	current_pid = current->pid;
	current_tgid = current->tgid;

	// going to check if the "current" thread called "convert_thread" at least once
	hash_for_each_possible_rcu(processes, tmp, node, current_tgid) {
		if(tmp->process_id == current_tgid){
			// I found the entry in the hashtable corresponding to the process of current thread
			// I have to check if there is the struct thread corresponding to the current thread in its "threads" hashtable
			hash_for_each_possible_rcu(tmp->threads, tmp2, node, current_pid) {
				if(tmp2->thread_id == current_pid){
					// I found it! "current" already called convert_thread, so it is already a fiber.
					
					fib_ctx = tmp2->selected_fiber;
					fib_fls = &(fib_ctx->fls);

					if(fib_fls->fls == NULL){
						// First time fls_alloc on this fiber, I have to allocate the array
						fib_fls->fls = kvzalloc(MAX_FLS_INDEX * sizeof(long long), GFP_KERNEL);
						fib_fls->size = 0;
						fib_fls->bitmask = kzalloc(BITS_TO_LONGS(MAX_FLS_INDEX) * sizeof(unsigned long), GFP_KERNEL);
					}

					index = find_first_zero_bit(fib_fls->bitmask, MAX_FLS_INDEX);
					if(index < MAX_FLS_INDEX && fib_fls->size < MAX_FLS_INDEX-1){
						fib_fls->size +=1;
						set_bit(index, fib_fls->bitmask);
						if(copy_to_user((void*) arg, (void*) &index, sizeof(long))){
							// Failed! Undo!
							fib_fls->size -= 1;
							clear_bit(index, fib_fls->bitmask);
							return -1;
						}
						return 0;
					}
				}
			}
		}
	}
	return -1;
}

int fls_free(unsigned long* arg){

	struct process_t* tmp;
	struct thread_t* tmp2;
	struct fiber_context_t* fib_ctx;
	struct fls_struct_t* fib_fls;

	pid_t current_pid;		// thread id of the thread that called fls_alloc
	pid_t current_tgid;		// process id of the thread that called fls_alloc

	unsigned long index;
	
	current_pid = current->pid;
	current_tgid = current->tgid;

	// getting the requested index from userspace
	if(copy_from_user((unsigned long*) &index, (unsigned long*) arg, sizeof(unsigned long))){
		return -1;
	}
	if(index >= MAX_FLS_INDEX){
		return -1;
	}

	// going to check if the "current" thread called "convert_thread" at least once
	hash_for_each_possible_rcu(processes, tmp, node, current_tgid) {
		if(tmp->process_id == current_tgid){
			// I found the entry in the hashtable corresponding to the process of current thread
			// I have to check if there is the struct thread corresponding to the current thread in its "threads" hashtable
			hash_for_each_possible_rcu(tmp->threads, tmp2, node, current_pid) {
				if(tmp2->thread_id == current_pid){
					// I found it! "current" already called convert_thread, so it is already a fiber.

					fib_ctx = tmp2->selected_fiber;
					fib_fls = &(fib_ctx->fls);

					if(fib_fls->fls == NULL || fib_fls->bitmask == NULL){
						return -1;
					}

					fib_fls->size -= 1;
					clear_bit(index, fib_fls->bitmask);

					return 0;
				}
			}
		}
	}
	return -1;
}

int fls_get(struct fls_args_t* arg){
	struct process_t* tmp;
	struct thread_t* tmp2;
	struct fiber_context_t* fib_ctx;
	struct fls_struct_t* fib_fls;

	pid_t current_pid;		// thread id of the thread that called fls_alloc
	pid_t current_tgid;		// process id of the thread that called fls_alloc

	struct fls_args_t fls_args;
	
	current_pid = current->pid;
	current_tgid = current->tgid;

	// getting the args from userspace
	if(copy_from_user((struct fls_args_t*) &fls_args, (struct fls_args_t*) arg, sizeof(struct fls_args_t))){
		return -1;
	}
	//checking if the requested index is allowed
	if(fls_args.index >= MAX_FLS_INDEX){
		return -1;
	}

	// going to check if the "current" thread called "convert_thread" at least once
	hash_for_each_possible_rcu(processes, tmp, node, current_tgid) {
		if(tmp->process_id == current_tgid){
			// I found the entry in the hashtable corresponding to the process of current thread
			// I have to check if there is the struct thread corresponding to the current thread in its "threads" hashtable
			hash_for_each_possible_rcu(tmp->threads, tmp2, node, current_pid) {
				if(tmp2->thread_id == current_pid){
					// I found it! "current" already called convert_thread, so it is already a fiber.

					fib_ctx = tmp2->selected_fiber;
					fib_fls = &(fib_ctx->fls);

					if(fib_fls->fls == NULL || fib_fls->bitmask == NULL){
						return -1;
					}

					// checking if the requested index is allowed
					if(test_bit(fls_args.index, fib_fls->bitmask)){
						fls_args.value = fib_fls->fls[fls_args.index];
						//printk(KERN_INFO "FLS_GET value %lld\n", fls_args.value);
						if(copy_to_user((struct fls_args_t*) arg, (struct fls_args_t*) &fls_args, sizeof(struct fls_args_t))){
							return -1;
						}
						return 0;
					}

					return -1;
				}
			}
		}
	}
	return -1;			

}

int fls_set(struct fls_args_t* arg){
	struct process_t* tmp;
	struct thread_t* tmp2;
	struct fiber_context_t* fib_ctx;
	struct fls_struct_t* fib_fls;

	pid_t current_pid;		// thread id of the thread that called fls_alloc
	pid_t current_tgid;		// process id of the thread that called fls_alloc

	struct fls_args_t fls_args;
	
	current_pid = current->pid;
	current_tgid = current->tgid;

	//printk(KERN_INFO "FLS_SET value: %lld\n", arg->value);

	// getting the args from userspace
	if(copy_from_user((struct fls_args_t*) &fls_args, (struct fls_args_t*) arg, sizeof(struct fls_args_t))){
		return -1;
	}
	//checking if the requested index is allowed
	if(fls_args.index >= MAX_FLS_INDEX){
		return -1;
	}

	//printk(KERN_INFO "FLS_SET value: %lld\n", fls_args.value);

	// going to check if the "current" thread called "convert_thread" at least once
	hash_for_each_possible_rcu(processes, tmp, node, current_tgid) {
		if(tmp->process_id == current_tgid){
			// I found the entry in the hashtable corresponding to the process of current thread
			// I have to check if there is the struct thread corresponding to the current thread in its "threads" hashtable
			hash_for_each_possible_rcu(tmp->threads, tmp2, node, current_pid) {
				if(tmp2->thread_id == current_pid){
					// I found it! "current" already called convert_thread, so it is already a fiber.

					fib_ctx = tmp2->selected_fiber;
					fib_fls = &(fib_ctx->fls);

					if(fib_fls->fls == NULL || fib_fls->bitmask == NULL){
						return -1;
					}

					// checking if the requested index is allowed
					if(test_bit(fls_args.index, fib_fls->bitmask)){
						fib_fls->fls[fls_args.index] = fls_args.value;
						return 0;
					}

					return -1;
				}
			}
		}
	}
	return -1;
}

int doexit_entry_handler(struct kprobe* kp, struct pt_regs* regs){

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

	// going to check if the "current" thread called "convert_thread" at least once.
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
					kfree(tmp3->fls.fls);
					kfree(tmp3->fls.bitmask);
					hash_del_rcu(&(tmp3->node));
					kfree(tmp3);
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
							// if I am here, the thread that is dying was executing a fiber, the fiber has to be marked unavailable
							tmp2->selected_fiber->thread=-1;
							atomic_dec(&(tmp->active_fibers));  // decrementing the counter of fibers of the process
						}
					}
					// actually destroying the thread
					hash_del_rcu(&(tmp2->node));
					kfree(tmp2);
				}
				return 0;
			}
		}
	}	
	return 0;
}

/*BEGIN OF PROC UTILS*/

int kprobe_proc_readdir_handler(struct kretprobe_instance *p, struct pt_regs *regs){

	// When "ls" is issued in /proc/PID or inside its subdirs, the proc_pident_readdir is called, we probed it and this is the entry_handler
    // Here we check if we are in any PID directory, if yes we check if the process has some fibers, if yes we instanziate the "fibers" directory
    
	struct file* file;
    struct pid_entry* ents;
    unsigned int nents;
    unsigned long folder_pid;
	struct process_t* tmp;
	struct kret_data* krdata;

	krdata = (struct kret_data*)p->data;
	krdata->ents=NULL;

    // view the SystemV x64 calling convention and proc_pident_readdir signature in /proc/base.c
    file = (struct file*)regs->di;
    ents = (struct pid_entry*)regs->dx;
    nents = (unsigned int)regs->cx;

	// print the directory name of the current directory in which "ls" has been issued
	//printk(KERN_INFO "file->f_path.dentry->dname.name: %s\n", file->f_path.dentry->d_name.name);

	// check if it is a PID directory
    if(kstrtoul(file->f_path.dentry->d_name.name, 10, &folder_pid))
        return 0; // we're not in /proc/PID, nothing to do here
	
	// check if process has fibers
	hash_for_each_possible_rcu(processes, tmp, node, folder_pid){
		if(tmp->process_id==folder_pid){
			// The process has fibers!
			// in pdata->ents we put all the current dir content + the "fibers" directory (the pid_entry statically initialized in this file)
			krdata->ents = kmalloc(sizeof(struct pid_entry) * (nents + 1), GFP_KERNEL);
			memcpy(krdata->ents, ents, sizeof(struct pid_entry) * nents);
			memcpy(&krdata->ents[nents], &fiber_folder, sizeof(struct pid_entry));

			// make cx and dx to point to data just allocated
			regs->dx = (unsigned long)krdata->ents;
			regs->cx = nents + 1;

			return 0;
		}
	}
    return 0;
}

int kprobe_proc_post_readdir_handler(struct kretprobe_instance *p, struct pt_regs *regs){
	// cleaning up
    struct kret_data *krdata;
    krdata = (struct kret_data *)p->data;
    if (krdata->ents)
        kfree(krdata->ents);
    return 0;
}

int kprobe_proc_lookup_handler(struct kretprobe_instance *p, struct pt_regs *regs){

	struct dentry* dentry;
    struct pid_entry* ents;
    unsigned int nents;
    unsigned long folder_pid;
	struct process_t* tmp;
	struct kret_data* krdata;

	krdata = (struct kret_data*)p->data;
	krdata->ents=NULL;

    // view the SystemV x64 calling convention and proc_pident_lookup signature in /proc/base.c
    dentry = (struct dentry*)regs->si;
    ents = (struct pid_entry*)regs->dx;
    nents = (unsigned int)regs->cx;

	//printk(KERN_INFO "dentry->d_name.name: %s\n", dentry->d_name.name);

    if (kstrtoul(dentry->d_name.name, 10, &folder_pid))
        return 0; // we're not in /proc/PID, nothing to do here

	// check if process has fibers
	hash_for_each_possible_rcu(processes, tmp, node, folder_pid){
		if(tmp->process_id==folder_pid){
			// The process has fibers!
			// in pdata->ents we put all the current dir content + the "fibers" directory (the pid_entry statically initialized in this file)
			krdata->ents = kmalloc(sizeof(struct pid_entry) * (nents + 1), GFP_KERNEL);
			memcpy(krdata->ents, ents, sizeof(struct pid_entry) * nents);
			memcpy(&krdata->ents[nents], &fiber_folder, sizeof(struct pid_entry));

			// make cx and dx to point to data just allocated
			regs->dx = (unsigned long)krdata->ents;
			regs->cx = nents + 1;

			return 0;
		}
	}
    return 0;
}

int kprobe_proc_post_lookup_handler(struct kretprobe_instance *p, struct pt_regs *regs){
	// cleaning up
    struct kret_data *krdata;
    krdata = (struct kret_data *)p->data;
    if (krdata->ents)
        kfree(krdata->ents);
    return 0;
}

// proc ksyms of "proc_pident_lookup" and "proc_pident_readdir", needed when readdir or lookup are performed on /proc/PID/fibers
static proc_pident_lookup_t ksym_proc_pident_lookup;
static proc_pident_readdir_t ksym_proc_pident_readdir;

void get_proc_ksyms(){
    ksym_proc_pident_lookup = (void *) kallsyms_lookup_name("proc_pident_lookup");
    ksym_proc_pident_readdir = (void *) kallsyms_lookup_name("proc_pident_readdir");
}

int fibdir_readdir(struct file *file, struct dir_context *ctx){

	// Called when "ls" is issued in /proc/PID/fibers directory

	unsigned long folder_pid;
    struct process_t* tmp;
    struct pid_entry* ents;
    struct fiber_context_t* fiber;
    int num_fibers, bkt, ret_val;
    unsigned int nents;

	// if ../ is a PID go on
    if (kstrtoul(file->f_path.dentry->d_parent->d_name.name, 10, &folder_pid))
        return 0;

	hash_for_each_possible_rcu(processes, tmp, node, folder_pid){
		// no need to check if tmp has fibers, we are in /proc/PID/fibers so for sure tmp has fibers
	    num_fibers = atomic_read(&(tmp->active_fibers));
	    //if(num_fibers==0) return 0;

	    ents = kmalloc(sizeof(struct pid_entry) * num_fibers, GFP_KERNEL);
	    nents = 0;

	    hash_for_each_rcu(tmp->fibers, bkt, fiber, node){
	        ents[nents].name = fiber->name;
	        ents[nents].len = strlen(fiber->name);
	        ents[nents].mode = S_IFREG | S_IRUGO;
	        ents[nents].iop = NULL;
	        ents[nents].fop = &fibentry_fops;
	        nents++;
	    }

	    // once pid_entries have been created, I have to put them into /proc/PID/fibers
	    ret_val = ksym_proc_pident_readdir(file, ctx, ents, nents);
	    kfree(ents);
	    return ret_val;
	}
	return 0;
}

struct dentry *fibdir_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags){

	unsigned long folder_pid;
    struct process_t* tmp;
    struct pid_entry* ents;
    struct fiber_context_t* fiber;
    unsigned int nents, bkt, num_fibers;
    struct dentry *ret_val;

	// if ../ is a PID go on
    if (kstrtoul(dentry->d_parent->d_name.name, 10, &folder_pid))
        return NULL;

	hash_for_each_possible_rcu(processes, tmp, node, folder_pid){
		// no need to check if tmp has fibers, we are in /proc/PID/fibers so for sure tmp has fibers
		num_fibers = atomic_read(&(tmp->active_fibers));
		//if(num_fibers==0) return 0;

		ents = kmalloc(sizeof(struct pid_entry) * num_fibers, GFP_KERNEL);
   		nents = 0;

   		hash_for_each_rcu(tmp->fibers, bkt, fiber, node){
	        ents[nents].name = fiber->name;
	        ents[nents].len = strlen(fiber->name);
	        ents[nents].mode = S_IFREG | S_IRUGO;
	        ents[nents].iop = NULL;
	        ents[nents].fop = &fibentry_fops;
	        nents++;
	    }

	    // once pid_entries have been created, I have to put them into /proc/PID/fibers
   		ret_val = ksym_proc_pident_lookup(dir, dentry, ents, nents);
   		kfree(ents);
   		return ret_val;
   	}
	return 0;
}

ssize_t fibentry_read(struct file *file, char __user *buff, size_t count, loff_t *f_pos){

	// when a read is issued on a fiber descriptor, I write its stuff in buf, then with copy_to_user I
	// send the written stuff in buff

	char buf[512];
	size_t bytes_written, offset;
	unsigned long tgid, fib_id;
	struct process_t* process;
	struct fiber_context_t* fiber;

	// getting the id of the target fiber
	if(kstrtoul(file->f_path.dentry->d_name.name, 10, &fib_id))
        return 0;

	// getting the tgid of the process holding the target fiber
    if(kstrtoul(file->f_path.dentry->d_parent->d_parent->d_name.name, 10, &tgid))
        return 0; 

	hash_for_each_possible_rcu(processes, process, node, tgid){
		if(process->process_id == tgid){
			hash_for_each_possible_rcu(process->fibers, fiber, node, fib_id){
				if(fiber->fiber_id == fib_id){
					
					// fiber found, getting its status and writing it into buf
					bytes_written = snprintf(buf, 512,
											"Fiber status: %s\n"
											"Running thread: %d (0:WAITING, -1:TERMINATED)\n"
                                            "Entry Point: 0x%lx\n" 	//lx for unsigned long
                                            "Creator PID: %d\n"
                                            "Successful Activations: %d\n"
                                            "Unsuccessful Activations: %d\n"
                                            "Total Execution Time: %llu ms\n",	//llu for unsigned long long
                    				        ((fiber->thread > 0) ? "RUNNING" : "WAITING/TERMINATED"),
											fiber->thread,
                             				(unsigned long)fiber->entry_point,
                             				fiber->creator,
											fiber->activations,
                             				fiber->f_activations,
                             				fiber->execution_time);
					if (*f_pos >= bytes_written)
						return 0;

					offset = (count < bytes_written) ? count : bytes_written;
					if (copy_to_user(buff, buf, offset))
						return -EFAULT;

					*f_pos += offset;
					return offset;
				}
			}
		}
	}
	return 0;
}