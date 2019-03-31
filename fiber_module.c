#include <linux/module.h>
#include <linux/kernel.h>			// needed for printk() priorities
#include <linux/device.h>			// needed for creating device class and device under /dev
#include <linux/cdev.h>				// needed for cdev_alloc()

#include <linux/kprobes.h>

#include <asm-generic/barrier.h>	// needed for smp_mb()
#include <linux/atomic.h>
#include <linux/uaccess.h>			// needed for copy_to_user()
#include <linux/slab.h>				// needed for kmalloc(), kzalloc() and kfree()
#include <linux/hashtable.h>

#include <linux/fs.h>				// needed for proc
#include <linux/kallsyms.h>			// needed for proc

#include "headers/fiber_module.h"	// for macros and function declarations
#include "headers/ioctl.h"

static struct cdev*		fib_cdev;
static struct class*	fib_cdevclass;
static struct device*	fib_dev;
static dev_t			fib_cdevt;		// for the return of alloc_chrdev_region. This will contain the MAJOR number and the first MINOR number (16 bit for each).

//file operations for the device
const struct file_operations fops = {
	.owner	=	THIS_MODULE,
	.open	=	dev_open,				// function to be called when device is opened from Userspace
	.unlocked_ioctl = ioctl_commands	// function that is now called instead of ioctl(), it will handle ioctl commands when Userspace calls ioctl()
};

//Permissions to open the device from Userspace
static char* set_permission(struct device *dev, umode_t *mode){
	if (mode)	//can be NULL
		*mode = 0666; /* set Read and Write and No-Exec permissions TO ROOT/GROUP/USER */
	return NULL; /* could override /dev name here too */
}

int dev_open(struct inode* i, struct file* f){
	//printk(KERN_INFO "Device opened correctly!\n");
	return 0;
}

long ioctl_commands(struct file* filp, unsigned int cmd, unsigned long arg){

	int ret;
	pid_t switchto_fib_id;

	// going to parse the passed command
	switch(cmd){
		case IOCTL_TEST:
			printk(KERN_INFO "ioctl issued with IOCTL_TEST command! Nothing to do here...\n");
			break;
		case IOCTL_CONVERT_THREAD:
			ret = convert_thread((pid_t*) arg);
			if(ret != 0){
				//printk(KERN_INFO "convert_thread failed!\n");
				return -1;
			}
			break;
		case IOCTL_CREATE_FIBER:
			ret = create_fiber((struct fiber_arg_t*)arg);
			if(ret != 0){
				//printk(KERN_INFO "create_fiber failed!\n");
				return -1;
			}
			break;
		case IOCTL_SWITCH_TO:
			copy_from_user(&switchto_fib_id, (const pid_t*) arg, sizeof(unsigned int));
			ret = switch_to(switchto_fib_id);
			if(ret != 0){
				//printk(KERN_INFO "switch_to failed!\n");
				return -1;
			}
			break;
		case IOCTL_FLS_ALLOC:
			ret = fls_alloc((unsigned long*) arg);
			if(ret != 0){
				//printk(KERN_INFO "fls_alloc failed!\n");
				return -1;
			}
			break;
		case IOCTL_FLS_FREE:
			ret = fls_free((unsigned long*) arg);
			if(ret!=0){
				//printk(KERN_INFO "fls_free failed!\n");
				return -1;
			}
			break;
		case IOCTL_FLS_GET:
			ret = fls_get((struct fls_args_t*) arg);
			if(ret!=0){
				//printk(KERN_INFO "fls_get failed!\n");
				return -1;
			}
			break;
		case IOCTL_FLS_SET:
			ret = fls_set((struct fls_args_t*) arg);
			if(ret!=0){
				//printk(KERN_INFO "fls_get failed!\n");
				return -1;
			}
			break;
		default:
			printk(KERN_INFO "Wrong command!!!\n");
			return -1;
	}
	return 0;
}

static struct kprobe my_kprobe = {
	.pre_handler = doexit_entry_handler,	// pre_handler: called before kernel calls do_exit
	.symbol_name = "do_exit"
};

static struct kretprobe readdir_probe = {
	.kp = {.symbol_name = "proc_pident_readdir"},
	.entry_handler = kprobe_proc_readdir_handler,
	.handler = kprobe_proc_post_readdir_handler,
	.data_size = sizeof(struct kret_data)
};

static struct kretprobe lookup_probe = {
	.kp = {.symbol_name = "proc_pident_lookup"},
    .entry_handler = kprobe_proc_lookup_handler,
	.handler = kprobe_proc_post_lookup_handler,
	.data_size = sizeof(struct kret_data)
};

static int __init mod_init(void){

	int ret;

	//Allocating device MAJOR number and first MINOR number
	//The first parameter will contain, after the execution, the MAJOR and the first MINOR
	ret = alloc_chrdev_region(&fib_cdevt, START_MINOR, NUM_MINORS, DEVICE_NAME);
	if(ret!=0){
		printk(KERN_INFO "Could not allocate MAJOR and MINOR!\n");
		return -EFAULT;
	}
	//printk("The MAJOR number of the device is %d and the MINOR number is %d\n", MAJOR(fib_cdevt), MINOR(fib_cdevt));

	//Allocating a struct cdev for the device
	fib_cdev = cdev_alloc();
	if(!fib_cdev){
		printk(KERN_INFO "cdev_alloc() failed: could not allocate memory for the cdev struct!\n");
		unregister_chrdev_region(fib_cdevt, NUM_MINORS);
		return -EFAULT;
	}

	//Filling the struct cdev with the fops structure (could also be done manually)
	cdev_init(fib_cdev, &fops);

	//Registering device into the system (informing the kernel about our cdev structure)
	ret = cdev_add(fib_cdev, fib_cdevt, NUM_MINORS);
	if(ret<0){
		printk(KERN_INFO "Could not register device into the system!\n");
		cdev_del(fib_cdev);
		unregister_chrdev_region(fib_cdevt, NUM_MINORS);
		return -EFAULT;
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
		cdev_del(fib_cdev);
		unregister_chrdev_region(fib_cdevt, NUM_MINORS);
		return -EFAULT;
	}

	//Setting the device permissions to open it from Userspace
	fib_cdevclass -> devnode = set_permission;

	fib_dev = device_create(fib_cdevclass, NULL, fib_cdevt, NULL, DEVICE_NAME);
	if(IS_ERR(fib_dev)){
		printk(KERN_INFO "Could not create the device '%s' for udev!\n", DEVICE_NAME);
		device_destroy(fib_cdevclass, fib_cdevt);
		class_unregister(fib_cdevclass);
		class_destroy(fib_cdevclass);
		cdev_del(fib_cdev);
		unregister_chrdev_region(fib_cdevt, NUM_MINORS);
	}

	//printk(KERN_INFO "Device correctly installed into the system!\n");

	spin_lock_init(&cnvtr_lock); // initializing the convert_thread() spinlock defined in "fiber_utils.c"

	// Registering kprobe for do_exit()
	ret = register_kprobe(&my_kprobe);
	if(ret){
		printk(KERN_INFO "Could not register kprobe!\n");
		return -EFAULT;
	}

	// Registering kretprobe for proc_pident_readdir
	ret = register_kretprobe(&readdir_probe);
	if(ret){
		printk(KERN_INFO "Could not register kretprobe!\n");
		return -EFAULT;
	}

	// Registering kretprobe for proc_pident_lookup
	ret = register_kretprobe(&lookup_probe);
	if(ret){
		printk(KERN_INFO "Could not register kretprobe!\n");
		return -EFAULT;
	}

	get_proc_ksyms();

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
	unregister_kprobe(&my_kprobe);
	unregister_kretprobe(&readdir_probe);
	unregister_kretprobe(&lookup_probe);
}

#define AUTHOR "Carmine Mansueto <mansueto.1646454@studenti.uniroma1.it>"
MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);

module_init(mod_init);
module_exit(mod_exit);

/*
 * kretprobe example
 * https://github.com/spotify/linux/blob/master/samples/kprobes/kretprobe_example.c
 */
