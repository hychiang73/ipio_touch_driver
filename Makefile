ccflags-y += -Wall
INCLUDE=core

obj-$(CONFIG_TOUCHSCREEN_ILITEK) +=  platform.o userspace.o \
								$(INCLUDE)/sup_chip.o \
								$(INCLUDE)/config.o \
								$(INCLUDE)/i2c.o \
								$(INCLUDE)/firmware.o \
								$(INCLUDE)/finger_report.o \

#obj-m += ilitek.o

#ilitek-objs :=  platform.o userspace.o \
        $(INCLUDE)/sup_chip.o \
        $(INCLUDE)/config.o \
        $(INCLUDE)/i2c.o \
        $(INCLUDE)/firmware.o \
        $(INCLUDE)/finger_report.o \

#TOOLCHAIN = /home/ilisa/Workplace/rk3288_sdk/prebuilts/gcc/linux-x86/arm/arm-eabi-4.6/bin
#CC = $(TOOLCHAIN)/arm-eabi-gcc
#KERNEL_DIR= /home/ilisa/Workplace/rk3288_sdk/kernel

#all:
#	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules
#clean:
#	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean

