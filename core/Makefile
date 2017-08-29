# include header files for MTK
ccflags-y += -I$(srctree)/drivers/input/touchscreen/mediatek/ipio_touch_driver/
ccflags-y += -I$(srctree)/drivers/input/touchscreen/mediatek/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/
ccflags-y += -I$(srctree)/drivers/misc/mediatek/include/mt-plat/$(MTK_PLATFORM)/include/

ccflags-y += -Wall

obj-y += sup_chip.o \
		config.o \
		i2c.o \
		firmware.o \
		finger_report.o \
		mp_test.o \
		flash.o