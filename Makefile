obj-m += ilitek.o

INCLUDE=core
ilitek-objs :=  rk3288.o ilitek_adapter.o ilitek_userspace.o \
				$(INCLUDE)/config.o \
				$(INCLUDE)/dbbus.o \
				$(INCLUDE)/i2c.o \
				$(INCLUDE)/firmware.o \
				$(INCLUDE)/fr.o \
				$(INCLUDE)/gesture.o \
				$(INCLUDE)/glove.o \

TOOLCHAIN = /home/ilisa/Workplace/rk3288_sdk/prebuilts/gcc/linux-x86/arm/arm-eabi-4.6/bin
CC = $(TOOLCHAIN)/arm-eabi-gcc
KERNEL_DIR= /home/ilisa/Workplace/rk3288_sdk/kernel
EXTRA_CFLAGS += -fno-common
EXTRA_LDFLAGS += -dp
all:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) modules
clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD) clean
