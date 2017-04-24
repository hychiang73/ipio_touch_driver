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
#include "core/config.h"
#include "core/dbbus.h"
#include "core/firmware.h"
#include "core/fr.h"
#include "core/gesture.h"
#include "core/glove.h"
#include "core/i2c.h"

#ifndef __ILITEK_H
#define __ILITEK_H


typedef struct _ilitek_locks {

	struct mutex MUTEX;
	spinlock_t SPIN_LOCK;

} ilitek_locks;

typedef struct  _ilitek_device {

	struct i2c_client *client;

	const struct i2c_device_id *id;

	ilitek_locks *ilitek_locks;

	uint8_t chip_id;

	uint8_t *firmware_ver;

	uint16_t protocol_ver;

	TP_INFO *tp_info;

} ilitek_device;

extern ilitek_device *ilitek_adapter;
extern int ilitek_get_keyinfo(void);
extern int ilitek_get_resolution(void);
extern uint16_t ilitek_get_protocol_ver(void);
extern uint8_t *ilitek_get_fw_ver(void);
extern uint8_t ilitek_get_chip_type(void);
extern int ilitek_init(struct i2c_client *client, const struct i2c_device_id *id);

#endif
