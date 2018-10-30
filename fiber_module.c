#include <linux/module.h>
#include <linux/kernel.h>	//needed for printk() priorities
#include <linux/device.h> //needed for creating device class and device under /dev
#include <linux/cdev.h>
#include "headers/fiber_module.h"	//for macros and function declarations
#include "headers/ioctl.h"

#define AUTHOR "Carmine Mansueto"

static struct cdev* fib_cdev;
static struct class* fib_cdevclass;
static struct device* fib_dev;
static dev_t fib_cdevt;			//for the return of alloc_chrdev_region. This will contain the MAJOR number and the first MINOR number (16 bit for each).

//file operations for the device
const struct file_operations fops = {
	.owner = THIS_MODULE,
	//.open = my_open;
	.unlocked_ioctl = ioctl_commands // function that is now called instead of ioctl(), this handles the ioctl commands when Userspace calls ioctl()
																		// it is a function pointer to long (*unlocked_ioctl) (struct file* filp, unsigned int cmd, unsigned long arg);
																		// indeed ioctl_commands is a function which reflects the firm of unlocked_ioctl
};

long ioctl_commands(struct file* filp, unsigned int cmd, unsigned long arg){

	//going to check the passed command
	switch(cmd){
		case IOCTL_TEST:
			printk(KERN_INFO "ioctl issued with IOCTL_TEST command!\n");
			break;
		default:
			printk(KERN_INFO "Wrong command!!!\n");
			return 1;
	}

	return 0;
}

static int __init mod_init(void){
	
	int ret;

	printk(KERN_INFO "Loading module...\n");

	//Allocating device MAJOR number and first MINOR number
	//The first parameter will contain, after the execution, the MAJOR and the first MINOR
	ret = alloc_chrdev_region(&fib_cdevt, 0, NUM_MINORS, DEVICE_NAME);
	if(ret!=0){
		printk(KERN_INFO "Could not allocate MAJOR and MINOR!\n");
		return -EFAULT;
	}
	printk("The MAJOR number of the device is %d\n", MAJOR(fib_cdevt));

	//Allocating  a struct cdev for the device
	fib_cdev = cdev_alloc();
	if(!fib_cdev){
		printk(KERN_INFO "cdev_alloc() failed: could not allocate memory for the cdev struct!\n");
		return -EFAULT;
	}

	//Filling the struct cdev with the fops structure
	cdev_init(fib_cdev, &fops);

	//Registering device into the system (informing the kernel about our cdev structure)
	ret = cdev_add(fib_cdev, fib_cdevt, NUM_MINORS);
	if(ret<0){
		printk(KERN_INFO "Could not register device into the system!\n");
		cdev_del(fib_cdev);
	}


	//Creating the devide under /dev by means of the udev daemon

	fib_cdevclass = class_create(THIS_MODULE, CLASS_NAME);
	if(IS_ERR(fib_cdevclass)){
		printk(KERN_INFO "Could not create the class for the device!\n");
		class_unregister(fib_cdevclass);
		class_destroy(fib_cdevclass);
		return -EFAULT;
	}

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
	class_destroy(fib_cdevclass);
	cdev_del(fib_cdev);
	unregister_chrdev_region(fib_cdevt, 1);
}


MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);

module_init(mod_init);
module_exit(mod_exit);
