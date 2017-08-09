ccflags-y += -Wall

## Built-in
#obj-y += core/
#obj-y += platform.o userspace.o

## Module
obj-m += ilitek.o
ilitek-y := platform.o userspace.o
ilitek-y += core/config.o \
			core/finger_report.o \
			core/firmware.o \
			core/flash.o \
			core/i2c.o \
			core/mp_test.o \
			core/sup_chip.o 

KERNEL_DIR= /home/likewise-open/ILI/1061279/workplace/rk3288_sdk/kernel
all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules
clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
