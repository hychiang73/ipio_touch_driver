#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/platform_device.h>
#include <linux/kobject.h>
#include <linux/interrupt.h>

#include "chip.h"

#ifndef __ILITEK_H
#define __ILITEK_H

#define DBG_LEVEL

#define DBG_INFO(fmt, arg...) \
			printk(KERN_INFO "ILITEK: (%s): %d: " fmt "\n", __func__, __LINE__, ##arg);

#define DBG_ERR(fmt, arg...) \
			printk(KERN_ERR "ILITEK: (%s): %d: " fmt "\n", __func__, __LINE__, ##arg);


typedef struct _ilitek_locks {

	struct mutex MUTEX;
	spinlock_t SPIN_LOCK;

} ilitek_locks;

typedef struct  _ilitek_device {

	struct i2c_client *client;

	const struct i2c_device_id *id;

	ilitek_locks *ilitek_locks;

	unsigned int chip_id;

	unsigned short protocol_ver;

} ilitek_device;


ilitek_device *ilitek_init(struct i2c_client *client, const struct i2c_device_id *id);


/*
#define u8   unsigned char
#define u16  unsigned short
#define u32  unsigned int
#define s8   signed char
#define s16  signed short
#define s32  signed int
#define s64  int64_t
#define u64  uint64_t
*/




#endif
