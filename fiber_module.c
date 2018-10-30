#include <linux/module.h>
#include <linux/kernel.h>	//needed for printk() priorities
#include "headers/fiber_module.h"	//for macros and function declarations

#define AUTHOR "Carmine Mansueto"

static struct cdev* fib_cdev;
static dev_t fib_cdevt;			//for the return of alloc_chrdev_region. This will contain the MAJOR number and the first MINOR number (16 bit for each).

//file operations for the device
const struct file_operations fops = {
	.open = my_open;
	.unlocked_ioctl = ioctl_commands; //function that handles the ioctl calls
}

long ioctl_commands(struct file* filp, unsigned int cmd, unsigned long arg){
	switch(cmd){
		case IOCTL_TEST:
			printk(KERN_INFO "ioctl issued with IOCTL_TEST command!\n");
	}
	
	return 0;
}

static int __init mod_init(void){
	
	printk(KERN_INFO "Loading module...\n");
	
	int ret;
	
	//Allocating device MAJOR number and first MINOR number
	//The first parameter will contain, after the execution, the MAJOR and the first MINOR
	ret = alloc_chrdev_region(&fib_cdevt, 1, NUM_MINORS, DEVICE_NAME);
	if(ret!=0){
		printk(KERN_INFO "Could not allocate MAJOR and MINOR!\n");
		return -EFAULT;
	}
	
	//Allocating  a struct cdev for the device
	fib_cdev = cdev_alloc();
	if(!fib_cdev){
		printk(KERN_INFO "Could not allocate memory for the device!\n");
		return -EFAULT;
	}
	
	//Initialize the struct cdev with fops, such that from now on the devices has some associated operations
	cdev_init(fib_cdev, &fops);
	
	//Registering device into the system
	ret = cdev_add(fib_cdev, fib_cdevt, NUM_MINORS)
	if(ret<0){
		printk(Kern_INFO "Could not register device into the system!\n");
		cdev_del(fib_cdev);
	
	return 0;
}

static void __exit mod_exit(void){
	
	printk(KERN_INFO "Removing module...\n");
}



MODULE_LICENSE("GPL");
MODULE_AUTHOR(AUTHOR);

module_init(mod_init);
module_exit(mod_exit);
