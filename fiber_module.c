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

#include "headers/fiber_module.h"	// for macros and function declarations
#include "headers/ioctl.h"

static struct cdev*		fib_cdev;
static struct class*	fib_cdevclass;
static struct device*	fib_dev;
static dev_t			fib_cdevt;	// for the return of alloc_chrdev_region. This will contain the MAJOR number and the first MINOR number (16 bit for each).

//file operations for the device
const struct file_operations fops = {
	.owner	=	THIS_MODULE,
	.open	=	dev_open,					//function to be called when device is opened from Userspace
	.unlocked_ioctl = ioctl_commands	// function that is now called instead of ioctl(), it will handle ioctl commands when Userspace calls ioctl()
};



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


long ioctl_commands(struct file* filp, unsigned int cmd, unsigned long arg){
	
	int ret;
	
	fiber_arg my_arg;
	
	unsigned int switchto_fib_id;
	
	// going to parse the passed command
	switch(cmd){
		
		case IOCTL_TEST:
			printk(KERN_INFO "ioctl issued with IOCTL_TEST command! Nothing to do here...\n");
			break;
			
		case IOCTL_CONVERT_THREAD:
			printk(KERN_INFO "ioctl issued with IOCTL_CONVERT_THREAD command!\n");
			ret = convert_thread((unsigned int*) arg);
			if(ret != 0){
				printk(KERN_INFO "convert_thread failed!\n");
				return -1;
			}
			printk(KERN_INFO "convert_thread success!\n");
			break;
		
		case IOCTL_CREATE_FIBER:
			printk(KERN_INFO "ioctl issued with IOCTL_CREATE_FIBER command!\n");
			copy_from_user(&my_arg, (const struct fiber_arg*) arg, sizeof(fiber_arg));
			ret = create_fiber(&my_arg);
			if(ret != 0){
				printk(KERN_INFO "create_fiber failed!\n");
				return -1;
			}
			copy_to_user((struct fiber_arg*) arg, (const struct fiber_arg*) &my_arg, sizeof(fiber_arg));
			
			printk(KERN_INFO "create_fiber success!\n");
			break;
		
		case IOCTL_SWITCH_TO:
			printk(KERN_INFO "ioctl issued with IOCTL_SWITCH_TO command!\n");
			copy_from_user(&switchto_fib_id, (const unsigned int*) arg, sizeof(unsigned int));
			ret = switch_to(switchto_fib_id);
			if(ret != 0){
				printk(KERN_INFO "switch_to failed!\n");
				return -1;
			}
			printk(KERN_INFO "switch_to success!\n");
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
