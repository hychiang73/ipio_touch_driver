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
#include <linux/delay.h>
#include <linux/version.h>

#include "chip.h"
#include "core/config.h"
#include "core/dbbus.h"
#include "core/firmware.h"
#include "core/finger_report.h"
#include "core/gesture.h"
#include "core/glove.h"
#include "core/i2c.h"

#ifndef __PLATFORM_H
#define __PLATFORM_H

typedef struct  _ILITEK_PLATFORM_INFO {

	struct i2c_client *client;

	const struct i2c_device_id *i2c_id;

	uint32_t chip_id;

	int int_gpio;

	int reset_gpio;

	int gpio_to_irq;

	bool isIrqEnable;

} platform_info;

extern void ilitek_platform_disable_irq(void);
extern void ilitek_platform_enable_irq(void);
extern void ilitek_platform_tp_poweron(void);
extern int ilitek_proc_init(void);
extern void ilitek_proc_remove(void);
#endif
