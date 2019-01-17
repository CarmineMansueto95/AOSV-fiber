#include <linux/module.h>
#include <linux/kernel.h>			// needed for printk() priorities
#include <linux/device.h>			// needed for creating device class and device under /dev
#include <linux/cdev.h>				// needed for cdev_alloc()

#include <linux/kprobes.h>

#include <asm-generic/barrier.h>	// needed for smp_bp()
#include <linux/atomic.h>
#include <linux/uaccess.h>			// needed for copy_to_user()
#include <linux/slab.h>				// needed for kmalloc() and kfree()

#include "headers/fiber_module.h"	// for macros and function declarations
#include "headers/ioctl.h"

#define BITS 10

static struct cdev*		fib_cdev;
static struct class*	fib_cdevclass;
static struct device*	fib_dev;
static dev_t			fib_cdevt;	// for the return of alloc_chrdev_region. This will contain the MAJOR number and the first MINOR number (16 bit for each).

 // global counter of the fibers
static atomic_t fiber_ctr = ATOMIC_INIT(0);

//file operations for the device
const struct file_operations fops = {
	.owner	=	THIS_MODULE,
	.open	=	dev_open,					//function to be called when device is opened from Userspace
	.unlocked_ioctl = ioctl_commands	// function that is now called instead of ioctl(), it will handle ioctl commands when Userspace calls ioctl()
};

// hashtable containing entries which are process structs
DEFINE_HASHTABLE(processes, BITS);
//hash_init(processes);




//Permissions to open the device from Userspace
static char* set_permission(struct device *dev, umode_t *mode){
	if (mode)	//can be NULL
		*mode = 0666; /* set Read and Write and No-Exec permissions TO ROOT/GROUP/USER */
	return NULL; /* could override /dev name here too */
}




int dev_open(struct inode* i, struct file* f){
	printk(KERN_INFO "Device opened correctly!\n");
	return 0;
}




int convert_thread(unsigned long arg){
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
			
			//assigning id to the fiber atomically
			smp_mb();
			fib_ctx->fiber_id = (fiber_id_t) atomic_inc_return(&fiber_ctr);	//atomic_inc_return increments by 1 the atomic counter and returns the value of the counter as int, so I cast to fiber_id_t
			smp_mb();
			
			printk(KERN_INFO "Thread succesfully converted into Fiber, fiber_id=%d\n", fib_ctx->fiber_id);
			

			// adding the fiber to the hashtable of fibers of process
			hash_add_rcu(tmp->fibers, &(fib_ctx->node), fib_ctx->fiber_id);
			
			// creating the thread struct
			thread = kmalloc(sizeof(struct thread_t), GFP_KERNEL);
			thread->process = tmp;
			thread->selected_fiber = fib_ctx;
			thread->thread_id = current_pid;
			

			//adding the thread to the hashtable of threads of process
			hash_add_rcu(tmp->threads, &(thread->node), current_pid);
			
			printk(KERN_INFO "Structures in convert_thread set up!\n");
			//writing the fiber id in arg to give it to the userspace
			ret = copy_to_user((fiber_id_t*)arg, &(fib_ctx->fiber_id), sizeof(fiber_id_t)); // I have to return fiber_id to UserSpace
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
	
	//assigning id to the fiber atomically
	smp_mb();
	fib_ctx->fiber_id = (fiber_id_t) atomic_inc_return(&fiber_ctr);	//atomic_inc_return increments by 1 the atomic counter and returns the value of the counter as int, so I cast to fiber_id_t
	smp_mb();
	
	printk(KERN_INFO "Thread succesfully converted into Fiber, fiber_id=%d\n", fib_ctx->fiber_id);

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
	
	
	printk(KERN_INFO "Structures in convert_thread set up!\n");
	//writing the fiber id in arg to give it to the userspace
	ret = copy_to_user((fiber_id_t*)arg, &(fib_ctx->fiber_id), sizeof(fiber_id_t));  // I have to return fiber_id to UserSpace
	if(ret!=0){
		printk(KERN_INFO "copy_to_user failed!\n");
		return -1;
	}
	return 0;
}



long ioctl_commands(struct file* filp, unsigned int cmd, unsigned long arg){
	
	int ret;

	// going to parse the passed command
	switch(cmd){
		
		case IOCTL_TEST:
			printk(KERN_INFO "ioctl issued with IOCTL_TEST command! Nothing to do here...\n");
			break;
			
		case IOCTL_CONVERT_THREAD:
			printk(KERN_INFO "ioctl issued with IOCTL_CONVERT_THREAD command!\n");
			ret = convert_thread(arg);
			if(ret != 0){
				printk(KERN_INFO "convert_thread failed!\n");
				return -1;
			}
			printk(KERN_INFO "conver_thread success!\n");
			break;
			
			
		default:
			printk(KERN_INFO "Wrong command!!!\n");
			return -1;
			
	}

	return 0;
}




static int __init mod_init(void){
	
	int ret;

	printk(KERN_INFO "Loading module...\n");

	//Allocating device MAJOR number and first MINOR number
	//The first parameter will contain, after the execution, the MAJOR and the first MINOR
	ret = alloc_chrdev_region(&fib_cdevt, START_MINOR, NUM_MINORS, DEVICE_NAME);
	if(ret!=0){
		printk(KERN_INFO "Could not allocate MAJOR and MINOR!\n");
		return -EFAULT;
	}
	printk("The MAJOR number of the device is %d and the MINOR number is %d\n", MAJOR(fib_cdevt), MINOR(fib_cdevt));

	//Allocating a struct cdev for the device
	fib_cdev = cdev_alloc();
	if(!fib_cdev){
		printk(KERN_INFO "cdev_alloc() failed: could not allocate memory for the cdev struct!\n");
		return -EFAULT;
	}

	//Filling the struct cdev with the fops structure (could also be done manually)
	cdev_init(fib_cdev, &fops);

	//Registering device into the system (informing the kernel about our cdev structure)
	ret = cdev_add(fib_cdev, fib_cdevt, NUM_MINORS);
	if(ret<0){
		printk(KERN_INFO "Could not register device into the system!\n");
		cdev_del(fib_cdev);
	}

	/*
	 * Creating the devide under /dev by means of the udev daemon
	 * This means to assign to our device a CLASSNAME and a DEVICENAME
	 * so that it will be added into /sys in such a way that udev reads
	 * it and makes it to compare in /dev
	 */

	fib_cdevclass = class_create(THIS_MODULE, CLASS_NAME);
	if(IS_ERR(fib_cdevclass)){
		printk(KERN_INFO "Could not create the class for the device!\n");
		class_unregister(fib_cdevclass);
		class_destroy(fib_cdevclass);
		return -EFAULT;
	}
	
	//Setting the device permissions to open it from Userspace
	fib_cdevclass -> devnode = set_permission;

	fib_dev = device_create(fib_cdevclass, NULL, fib_cdevt, NULL, DEVICE_NAME);
	if(IS_ERR(fib_dev)){
		printk(KERN_INFO "Could not create the device '%s' for udev!\n", DEVICE_NAME);
		device_destroy(fib_cdevclass, fib_cdevt);
	}
	
	printk(KERN_INFO "Device correctly installed into the system!\n");
	return 0;
}




static void __exit mod_exit(void){

	printk(KERN_INFO "Removing module...\n");
	
	//Cleaning up all the stuff this module allocated
	device_destroy(fib_cdevclass, fib_cdevt);
	class_unregister(fib_cdevclass);
	class_destroy(fib_cdevclass);
	cdev_del(fib_cdev);
	unregister_chrdev_region(fib_cdevt, NUM_MINORS);
}




#define AUTHOR "Carmine Mansueto mansueto.1646454@studenti.uniroma1.it"
MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);

module_init(mod_init);
module_exit(mod_exit);

/*
 * kretprobe example
 * https://github.com/spotify/linux/blob/master/samples/kprobes/kretprobe_example.c
 */
