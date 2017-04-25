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

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_gpio.h>
#define DTS_INT_GPIO	"touch,irq-gpio"
#define DTS_RESET_GPIO	"touch,reset-gpio"
#endif

#include "chip.h"
#include "core/config.h"
#include "core/dbbus.h"
#include "core/firmware.h"
#include "core/fr.h"
#include "core/gesture.h"
#include "core/glove.h"
#include "core/i2c.h"

#ifndef __ADAPTER_H
#define __ADAPTER_H



typedef struct  _ilitek_device {

	struct i2c_client *client;

	const struct i2c_device_id *id;

	uint32_t chip_id;

	uint8_t *firmware_ver;

	uint16_t protocol_ver;

	TP_INFO *tp_info;

	int int_gpio;

	int reset_gpio;

} ilitek_device;

extern ilitek_device *ilitek_adapter;
extern int ilitek_read_tp_info(void);
extern int ilitek_init(struct i2c_client *client, const struct i2c_device_id *id, uint32_t *pData);

#endif
