/*
 * ILITEK Touch IC driver
 *
 * Copyright (C) 2011 ILI Technology Corporation.
 *
 * Author: Dicky Chiang <dicky_chiang@ilitek.com>
 * Based on TDD v7.0 implemented by Mstar & ILITEK
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */


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
#include "core/i2c.h"
#include "core/firmware.h"
#include "core/finger_report.h"

#ifndef __PLATFORM_H
#define __PLATFORM_H

typedef struct  _ILITEK_PLATFORM_INFO {

	struct i2c_client *client;

	const struct i2c_device_id *i2c_id;

	uint32_t chip_id;

	int int_gpio;

	int reset_gpio;

	int gpio_to_irq;
	
	int delay_time_high;

	int delay_time_low;

	bool isIrqEnable;

} platform_info;

extern void ilitek_platform_disable_irq(void);
extern void ilitek_platform_enable_irq(void);
extern void ilitek_platform_ic_reset(void);
extern int ilitek_proc_init(void);
extern void ilitek_proc_remove(void);
#endif
