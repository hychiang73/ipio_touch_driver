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

#include "../common.h"
#include "finger_report.h"
#include "gesture.h"
#include "protocol.h"
#include "config.h"	

/* The example for the gesture virtual keys */
#define GESTURE_DOUBLECLICK			    0x0
#define GESTURE_UP						0x1
#define GESTURE_DOWN					0x2
#define GESTURE_LEFT					0x3
#define GESTURE_RIGHT					0x4
#define GESTURE_M						0x5
#define GESTURE_W						0x6
#define GESTURE_C						0x7
#define GESTURE_E						0x8
#define GESTURE_V						0x9
#define GESTURE_O						0xA
#define GESTURE_S						0xB
#define GESTURE_Z						0xC

#define KEY_GESTURE_D					KEY_D
#define KEY_GESTURE_UP					KEY_UP
#define KEY_GESTURE_DOWN				KEY_DOWN
#define KEY_GESTURE_LEFT				KEY_LEFT
#define KEY_GESTURE_RIGHT				KEY_RIGHT
#define KEY_GESTURE_O					KEY_O
#define KEY_GESTURE_E					KEY_E
#define KEY_GESTURE_M					KEY_M
#define KEY_GESTURE_W					KEY_W
#define KEY_GESTURE_S					KEY_S
#define KEY_GESTURE_V					KEY_V
#define KEY_GESTURE_C					KEY_C
#define KEY_GESTURE_Z					KEY_Z

int core_load_gesture_code(void)
{
	int res = 0, i = 0;
	uint8_t temp[12] = {0};
	uint32_t gesture_size = 0, gesture_addr = 0;
	temp[0] = 0x01;
	temp[0] = 0x0A;
	temp[1] = 0x07;
	if ((core_write(core_config->slave_i2c_addr, temp, 2)) < 0) {
		ipio_err("write command error\n");
	}
	if ((core_read(core_config->slave_i2c_addr, temp, 12)) < 0) {
		ipio_err("Read command error\n");
	}
	gesture_addr = (temp[6] << 24) + (temp[7] << 16) + (temp[8] << 8) + temp[9];
	gesture_size = (temp[10] << 8) + temp[11];
	printk("gesture_addr = 0x%x, gesture_size = 0x%x\n", gesture_addr, gesture_size);
	for(i = 0; i < 12; i++)
	{
		printk("0x%x,", temp[i]);
	}
	printk("\n");
	for(i = 0; i < 100; i++)
	{
		ipio_info("i = %d\n", i);
		temp[0] = 0x01;
		temp[1] = 0x0A;
		temp[2] = 0x00;
		if ((core_write(core_config->slave_i2c_addr, temp, 3)) < 0) {
			ipio_err("write command error\n");
		}

		temp[0] = protocol->cmd_read_ctrl;
		temp[1] = protocol->cmd_cdc_busy;

		mdelay(1);
		core_write(core_config->slave_i2c_addr, temp, 2);
		mdelay(1);
		core_write(core_config->slave_i2c_addr, &temp[1], 1);
		mdelay(1);
		core_read(core_config->slave_i2c_addr, temp, 1);
		if (temp[0] == 0x41 || temp[0] == 0x51) {
			ipio_info("Check busy is free\n");
			res = 0;
			break;
		}
	}
	if(i == 100 && temp[0] != 0x41)
		ipio_info("Check busy is busy\n");
	/* check system busy */
	// if (core_config_check_cdc_busy(50) < 0)
	// 	ipio_err("Check busy is timout !\n");
	temp[0] = 0x01;
	temp[1] = 0x0A;
	temp[2] = 0x03;
	if ((core_write(core_config->slave_i2c_addr, temp, 3)) < 0) {
		ipio_err("write command error\n");
	}	
	temp[0] = 0x01;
	temp[1] = 0x0A;
	temp[2] = 0x01;
	if ((core_write(core_config->slave_i2c_addr, temp, 3)) < 0) {
		ipio_err("write command error\n");
	}
	for(i = 0; i < 1000; i++)
	{
		temp[0] = 0x01;
		temp[1] = 0x0A;
		temp[2] = 0x05;
		if ((core_write(core_config->slave_i2c_addr, temp, 3)) < 0) {
			ipio_err("write command error\n");
		}
		if ((core_read(core_config->slave_i2c_addr, temp, 1)) < 0) {
			ipio_err("Read command error\n");
		}
		if(temp[0] == 0x1)
		{
			ipio_info("check fw ready\n");
			break;
		}
	}
	if(i == 1000 && temp[0] != 0x01) 
			ipio_err("FW is busy, error\n");
	temp[0] = 0x01;
	temp[1] = 0x0A;
	temp[2] = 0x06;
	if ((core_write(core_config->slave_i2c_addr, temp, 3)) < 0) {
		ipio_err("write command error\n");
	}
	return res;
}
int core_gesture_key(uint8_t gdata)
{
	int gcode;

	switch (gdata) {
	case GESTURE_LEFT:
		gcode = KEY_GESTURE_LEFT;
		break;
	case GESTURE_RIGHT:
		gcode = KEY_GESTURE_RIGHT;
		break;
	case GESTURE_UP:
		gcode = KEY_GESTURE_UP;
		break;
	case GESTURE_DOWN:
		gcode = KEY_GESTURE_DOWN;
		break;
	case GESTURE_DOUBLECLICK:
		gcode = KEY_GESTURE_D;
		break;
	case GESTURE_O:
		gcode = KEY_GESTURE_O;
		break;
	case GESTURE_W:
		gcode = KEY_GESTURE_W;
		break;
	case GESTURE_M:
		gcode = KEY_GESTURE_M;
		break;
	case GESTURE_E:
		gcode = KEY_GESTURE_E;
		break;
	case GESTURE_S:
		gcode = KEY_GESTURE_S;
		break;
	case GESTURE_V:
		gcode = KEY_GESTURE_V;
		break;
	case GESTURE_Z:
		gcode = KEY_GESTURE_Z;
		break;
	case GESTURE_C:
		gcode = KEY_GESTURE_C;
		break;
	default:
		gcode = -1;
		break;
	}

	ipio_debug(DEBUG_GESTURE, "gcode = %d\n", gcode);
	return gcode;
}
EXPORT_SYMBOL(core_gesture_key);

void core_gesture_init(struct core_fr_data *fr_data)
{
	struct input_dev *input_dev = fr_data->input_device;

	if (input_dev != NULL) {
		input_set_capability(input_dev, EV_KEY, KEY_POWER);
		input_set_capability(input_dev, EV_KEY, KEY_GESTURE_UP);
		input_set_capability(input_dev, EV_KEY, KEY_GESTURE_DOWN);
		input_set_capability(input_dev, EV_KEY, KEY_GESTURE_LEFT);
		input_set_capability(input_dev, EV_KEY, KEY_GESTURE_RIGHT);
		input_set_capability(input_dev, EV_KEY, KEY_GESTURE_O);
		input_set_capability(input_dev, EV_KEY, KEY_GESTURE_E);
		input_set_capability(input_dev, EV_KEY, KEY_GESTURE_M);
		input_set_capability(input_dev, EV_KEY, KEY_GESTURE_W);
		input_set_capability(input_dev, EV_KEY, KEY_GESTURE_S);
		input_set_capability(input_dev, EV_KEY, KEY_GESTURE_V);
		input_set_capability(input_dev, EV_KEY, KEY_GESTURE_Z);
		input_set_capability(input_dev, EV_KEY, KEY_GESTURE_C);

		__set_bit(KEY_POWER, input_dev->keybit);
		__set_bit(KEY_GESTURE_UP, input_dev->keybit);
		__set_bit(KEY_GESTURE_DOWN, input_dev->keybit);
		__set_bit(KEY_GESTURE_LEFT, input_dev->keybit);
		__set_bit(KEY_GESTURE_RIGHT, input_dev->keybit);
		__set_bit(KEY_GESTURE_O, input_dev->keybit);
		__set_bit(KEY_GESTURE_E, input_dev->keybit);
		__set_bit(KEY_GESTURE_M, input_dev->keybit);
		__set_bit(KEY_GESTURE_W, input_dev->keybit);
		__set_bit(KEY_GESTURE_S, input_dev->keybit);
		__set_bit(KEY_GESTURE_V, input_dev->keybit);
		__set_bit(KEY_GESTURE_Z, input_dev->keybit);
		__set_bit(KEY_GESTURE_C, input_dev->keybit);
		return;
	}

	ipio_err("GESTURE: input dev is NULL\n");
}
EXPORT_SYMBOL(core_gesture_init);
