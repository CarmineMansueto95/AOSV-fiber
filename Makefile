obj-m += fiber.o
fiber-objs := fiber_module.o fiber_utils.o

module:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean_module:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
