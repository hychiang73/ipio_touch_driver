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

#define DEBUG

#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/i2c.h>

#include "../chip.h"
#include "../platform.h"
#include "config.h"
#include "i2c.h"
#include "finger_report.h"
#include "mp_test.h"

extern uint32_t SUP_CHIP_LIST[];
extern int nums_chip;

/*
 * It represents an id and its position in each fingers
 */
struct mutual_touch_point
{
	uint16_t id;
	uint16_t x;
	uint16_t y;
	uint16_t pressure;
};

/*
 * It represents keys and their code with each fingers
 */
struct mutual_touch_info
{
	uint8_t key_count;
	uint8_t key_code;
	struct mutual_touch_point mtp[10];
};

struct mutual_touch_info mti;

// record the status of touch being pressed or released currently and previosuly.
uint8_t CurrentTouch[MAX_TOUCH_NUM];
uint8_t PreviousTouch[MAX_TOUCH_NUM];

// Either B TYPE or A Type in MTP
#define USE_TYPE_B_PROTOCOL

//#define ENABLE_GESTURE_WAKEUP

// Whether to detect the value of pressure in finger touch
//#define FORCE_TOUCH

// set up width and heigth of a screen
#define TOUCH_SCREEN_X_MIN 0
#define TOUCH_SCREEN_Y_MIN 0
#define TOUCH_SCREEN_X_MAX 1080
#define TOUCH_SCREEN_Y_MAX 1920

#define TPD_HEIGHT 2048
#define TPD_WIDTH 2048

struct core_fr_data *core_fr;

/*
 * Calculate the check sum of each packet reported by firmware 
 *
 * @pMsg: packet come from firmware
 * @nLength : the length of its packet
 */
static uint8_t cal_fr_checksum(uint8_t *pMsg, uint32_t nLength)
{
	int i;
	int32_t nCheckSum = 0;

	for (i = 0; i < nLength; i++)
	{
		nCheckSum += pMsg[i];
	}

	return (uint8_t)((-nCheckSum) & 0xFF);
}

/*
 * It'd be called when a finger's touching down a screen. It'll notify the event
 * to the uplayer from input device.
 *
 * @x: the axis of X
 * @y: the axis of Y
 * @pressure: the value of pressue on a screen
 * @id: an id represents a finger pressing on a screen
 */
static void core_fr_touch_press(int32_t x, int32_t y, uint32_t pressure, int32_t id)
{
	DBG("point touch pressed");

	DBG("id = %d, x = %d, y = %d", id, x, y);

#ifdef USE_TYPE_B_PROTOCOL
	input_mt_slot(core_fr->input_device, id);
	input_mt_report_slot_state(core_fr->input_device, MT_TOOL_FINGER, true);
	input_report_abs(core_fr->input_device, ABS_MT_POSITION_X, x);
	input_report_abs(core_fr->input_device, ABS_MT_POSITION_Y, y);

#ifdef FORCE_TOUCH
	input_report_abs(core_fr->input_device, ABS_MT_PRESSURE, pressure);
#endif

#else // for A protocol
	input_report_key(core_fr->input_device, BTN_TOUCH, 1);

	// ABS_MT_TRACKING_ID is used for ILI7807/ILI21xx only
	input_report_abs(core_fr->input_device, ABS_MT_TRACKING_ID, id);

	input_report_abs(core_fr->input_device, ABS_MT_TOUCH_MAJOR, 1);
	input_report_abs(core_fr->input_device, ABS_MT_WIDTH_MAJOR, 1);
	input_report_abs(core_fr->input_device, ABS_MT_POSITION_X, x);
	input_report_abs(core_fr->input_device, ABS_MT_POSITION_Y, y);

#ifdef FORCE_TOUCH
	input_report_abs(core_fr->input_device, ABS_MT_PRESSURE, pressure);
#endif

	input_mt_sync(core_fr->input_device);
#endif
}

/*
 * It'd be called when a finger's touched up from a screen. It'll notify
 * the event to the uplayer from input device.
 *
 * @x: the axis of X
 * @y: the axis of Y
 * @id: an id represents a finger leaving from a screen.
 */
static void core_fr_touch_release(int32_t x, int32_t y, int32_t id)
{
	DBG("point touch released");

	DBG("id = %d, x = %d, y = %d", id, x, y);

#ifdef USE_TYPE_B_PROTOCOL
	input_mt_slot(core_fr->input_device, id);
	input_mt_report_slot_state(core_fr->input_device, MT_TOOL_FINGER, false);
#else // for A protocol
	input_report_key(core_fr->input_device, BTN_TOUCH, 0);
	input_mt_sync(core_fr->input_device);
#endif
}

static int parse_touch_package_v3_2(uint8_t *fr_packet, int mode)
{
	DBG_INFO("Not implemented yet");
	return 0;
}

/*
 * It mainly parses the packet assembled by protocol v5.0
 *
 * @fr_data: the packet of finger report come from firmware.
 * @mode: a mode on the current firmware. (demo or debug)
 * @pInfo: a struct of mutual touch information.
 * @rpl: the lenght of packet of finger report.
 */
static int parse_touch_package_v5_0(uint8_t *fr_data, struct mutual_touch_info *pInfo, int rpl)
{
	int i, res = 0;
	uint8_t check_sum = 0;
	uint32_t nX = 0, nY = 0;

	for (i = 0; i < 9; i++)
		DBG("fr_data[%d] = %x", i, fr_data[i]);

	//TODO: calculate report rate

	check_sum = cal_fr_checksum(&fr_data[0], (rpl - 1));
	DBG("fr_data = %x  ;  check_sum : %x ", fr_data[rpl - 1], check_sum);

	if (fr_data[rpl - 1] != check_sum)
	{
		DBG_ERR("Wrong checksum");
		return -1;
	}

	//TODO: parse packets for gesture/glove features if they're enabled

	// start to parsing the packet of finger report
	if (fr_data[0] == P5_0_DEMO_PACKET_ID)
	{
		DBG(" **** Parsing DEMO packets ****");

		for (i = 0; i < MAX_TOUCH_NUM; i++)
		{
			if ((fr_data[(4 * i) + 1] == 0xFF) && (fr_data[(4 * i) + 2] && 0xFF) && (fr_data[(4 * i) + 3] == 0xFF))
			{
#ifdef USE_TYPE_B_PROTOCOL
				CurrentTouch[i] = 0;
#endif
				continue;
			}

			nX = (((fr_data[(4 * i) + 1] & 0xF0) << 4) | (fr_data[(4 * i) + 2]));
			nY = (((fr_data[(4 * i) + 1] & 0x0F) << 8) | (fr_data[(4 * i) + 3]));

			pInfo->mtp[pInfo->key_count].x = nX * TOUCH_SCREEN_X_MAX / TPD_WIDTH;
			pInfo->mtp[pInfo->key_count].y = nY * TOUCH_SCREEN_Y_MAX / TPD_HEIGHT;
			pInfo->mtp[pInfo->key_count].pressure = fr_data[4 * (i + 1)];
			pInfo->mtp[pInfo->key_count].id = i;

			DBG("[x,y]=[%d,%d]", nX, nY);
			DBG("point[%d] : (%d,%d) = %d\n",
				pInfo->mtp[pInfo->key_count].id,
				pInfo->mtp[pInfo->key_count].x,
				pInfo->mtp[pInfo->key_count].y,
				pInfo->mtp[pInfo->key_count].pressure);

			pInfo->key_count++;

#ifdef USE_TYPE_B_PROTOCOL
			CurrentTouch[i] = 1;
#endif
		}
	}
	else if (fr_data[0] == P5_0_DEBUG_PACKET_ID)
	{
		DBG(" **** Parsing DEBUG packets ****");

		for (i = 0; i < MAX_TOUCH_NUM; i++)
		{
			if ((fr_data[(3 * i) + 5] == 0xFF) && (fr_data[(3 * i) + 6] && 0xFF) && (fr_data[(3 * i) + 7] == 0xFF))
			{
#ifdef USE_TYPE_B_PROTOCOL
				CurrentTouch[i] = 0;
#endif
				continue;
			}

			nX = (((fr_data[(3 * i) + 5] & 0xF0) << 4) | (fr_data[(3 * i) + 6]));
			nY = (((fr_data[(3 * i) + 5] & 0x0F) << 8) | (fr_data[(3 * i) + 7]));

			pInfo->mtp[pInfo->key_count].x = nX * TOUCH_SCREEN_X_MAX / TPD_WIDTH;
			pInfo->mtp[pInfo->key_count].y = nY * TOUCH_SCREEN_Y_MAX / TPD_HEIGHT;
			pInfo->mtp[pInfo->key_count].pressure = 1;
			pInfo->mtp[pInfo->key_count].id = i;

			DBG("[x,y]=[%d,%d]", nX, nY);
			DBG("point[%d] : (%d,%d) = %d\n",
				pInfo->mtp[pInfo->key_count].id,
				pInfo->mtp[pInfo->key_count].x,
				pInfo->mtp[pInfo->key_count].y,
				pInfo->mtp[pInfo->key_count].pressure);

			pInfo->key_count++;

#ifdef USE_TYPE_B_PROTOCOL
			CurrentTouch[i] = 1;
#endif
		}
	}

	return res;
}

static int finger_report_ver_3_2(uint8_t *fr_data, int length)
{
	DBG_INFO("Not implemented yet");
	parse_touch_package_v3_2(NULL, 0);
	return 0;
}

/*
 * The function is called by an interrupt and used to handle packet of finger 
 * touch from firmware. A differnece in the process of the data is acorrding to the protocol
 */
static int finger_report_ver_5_0(uint8_t *fr_data, int length)
{
	int i, res = 0;
	static int last_count = 0;

	// initialise struct of mutual toucn info
	memset(&mti, 0x0, sizeof(struct mutual_touch_info));

	//TODO: set packet length for gesture wake up

	// read finger touch packet when an interrupt occurs
	res = core_i2c_read(core_config->slave_i2c_addr, fr_data, length);
	if (res < 0)
	{
		DBG_ERR("Failed to read finger report packet");
		return res;
	}

	res = parse_touch_package_v5_0(fr_data, &mti, length);
	if (res < 0)
	{
		DBG_ERR("Failed to parse packet of finger touch");
		return -1;
	}

	DBG("tInfo.nCount = %d, nLastCount = %d\n", mti.key_count, last_count);

	// interpret parsed packat and send input events to uplayer
	if (mti.key_count > 0)
	{
#ifdef USE_TYPE_B_PROTOCOL
		for (i = 0; i < mti.key_count; i++)
		{
			input_report_key(core_fr->input_device, BTN_TOUCH, 1);
			core_fr_touch_press(mti.mtp[i].x, mti.mtp[i].y, mti.mtp[i].pressure, mti.mtp[i].id);

			input_report_key(core_fr->input_device, BTN_TOOL_FINGER, 1);
		}

		for (i = 0; i < MAX_TOUCH_NUM; i++)
		{
			DBG("PreviousTouch[%d]=%d, CurrentTouch[%d]=%d", i, PreviousTouch[i], i, CurrentTouch[i]);

			if (CurrentTouch[i] == 0 && PreviousTouch[i] == 1)
			{
				core_fr_touch_release(0, 0, i);
			}

			PreviousTouch[i] = CurrentTouch[i];
		}
#else // for A protocol
		for (i = 0; i < mti.key_count; i++)
		{
			core_fr_touch_press(mti.mtp[i].x, mti.mtp[i].y, mti.mtp[i].pressure, mti.mtp[i].id);
		}
#endif
		input_sync(core_fr->input_device);

		last_count = mti.key_count;
	}
	else
	{
		if (last_count > 0)
		{
#ifdef USE_TYPE_B_PROTOCOL
			for (i = 0; i < MAX_TOUCH_NUM; i++)
			{
				DBG("PreviousTouch[%d]=%d, CurrentTouch[%d]=%d", i, PreviousTouch[i], i, CurrentTouch[i]);

				if (CurrentTouch[i] == 0 && PreviousTouch[i] == 1)
				{
					core_fr_touch_release(0, 0, i);
				}
				PreviousTouch[i] = CurrentTouch[i];
			}

			input_report_key(core_fr->input_device, BTN_TOUCH, 0);
			input_report_key(core_fr->input_device, BTN_TOOL_FINGER, 0);
#else // for A protocol
			core_fr_touch_release(0, 0, 0);
#endif
			input_sync(core_fr->input_device);

			last_count = 0;
		}
	}
	return res;
}

// commands according to the procotol used on a chip.
extern uint8_t pcmd[10];

int core_fr_mode_control(uint8_t *from_user)
{
	int mode;
	int i, res = 0;
	uint8_t buf[3] = {0};

	uint8_t actual_mode[] =
		{
			P5_0_FIRMWARE_DEMO_MODE,
			P5_0_FIRMWARE_TEST_MODE,
			P5_0_FIRMWARE_DEBUG_MODE,
			P5_0_FIRMWARE_I2CUART_MODE,
		};

	ilitek_platform_disable_irq();

	if (from_user == NULL)
	{
		DBG_ERR("Arguments from user space are invaild");
		goto out;
	}

	DBG("size = %d, mode = %x, cmd_1 = %x, cmd_2 = %x ",
		ARRAY_SIZE(actual_mode), from_user[0], from_user[1], from_user[2]);

	mode = from_user[0];

	for (i = 0; i < ARRAY_SIZE(actual_mode); i++)
	{
		if (actual_mode[i] == mode)
		{
			if (mode == P5_0_FIRMWARE_I2CUART_MODE)
			{
				buf[0] = pcmd[6];
				buf[1] = *(from_user + 1);
				buf[2] = *(from_user + 2);

				DBG_INFO("Switch to I2CUART mode, cmd = 0x%x, byte 1 = 0x%x, byte 2 = 0x%x",
						 buf[0], buf[1], buf[2]);

				res = core_i2c_write(core_config->slave_i2c_addr, buf, 3);
				if (res < 0)
					goto out;
			}
			else if (mode == P5_0_FIRMWARE_TEST_MODE)
			{
				buf[0] = pcmd[5];
				buf[1] = mode;

				DBG_INFO("Switch to Test mode, cmd = 0x%x, byte 1 = 0x%x",
						 buf[0], buf[1]);

				res = core_i2c_write(core_config->slave_i2c_addr, buf, 2);
				if (res < 0)
					goto out;

				// doing sensor test
				core_mp_switch_mode();
			}
			else
			{
				buf[0] = pcmd[5];
				buf[1] = mode;

				DBG_INFO("Switch to Demo/Debug mode, cmd = 0x%x, byte 1 = 0x%x",
						 buf[0], buf[1]);

				res = core_i2c_write(core_config->slave_i2c_addr, buf, 2);
				if (res < 0)
					goto out;
			}
			core_fr->actual_fw_mode = actual_mode[i];
			break;
		}

		if (i == (ARRAY_SIZE(actual_mode) - 1))
		{
			DBG_ERR("Unknown Mode");
			res = -1;
			goto out;
		}
	}

	ilitek_platform_enable_irq();
	return res;

out:
	DBG_ERR("Failed to change mode, res = %d", res);
	ilitek_platform_enable_irq();
	return res;
}
EXPORT_SYMBOL(core_fr_mode_control);

/*
 * The table is used to handle calling functions that deal with packets of finger report.
 * The callback function might be different of what a protocol is used on a chip.
 *
 * It's possible to have the different protocol according to customer's requirement on the same
 * touch ic with customised firmware, so I don't have to identify which of the ic has been used; instead,
 * the version of protocol should match its parsing pattern.
 */
typedef struct
{
	uint16_t protocol;
	int (*finger_report)(uint8_t *packet, int length);
} fr_hashtable;

fr_hashtable fr_t[] = {
	{ILITEK_PROTOCOL_V3_2, finger_report_ver_3_2},
	{ILITEK_PROTOCOL_V5_0, finger_report_ver_5_0},
};

/*
 * The function is an entry for the work queue registered by ISR activates.
 *
 * Here will allocate the size of packet depending on what the current protocol
 * is used on its firmware.
 */
void core_fr_handler(void)
{
	int i, len = ARRAY_SIZE(fr_t);
	uint8_t *fr_data = NULL;
	uint16_t report_packet_length = -1;

	if (core_fr->isEnableFR)
	{
		for (i = 0; i < len; i++)
		{
			if (fr_t[i].protocol == core_config->use_protocol)
			{
				if (core_config->use_protocol == ILITEK_PROTOCOL_V5_0)
				{
					if (core_fr->actual_fw_mode == core_fr->fw_demo_mode)
					{
						report_packet_length = P5_0_DEMO_MODE_PACKET_LENGTH;
					}
					else if (core_fr->actual_fw_mode == core_fr->fw_test_mode)
					{
						report_packet_length = P5_0_TEST_MODE_PACKET_LENGTH;
					}
					else if (core_fr->actual_fw_mode == core_fr->fw_debug_mode)
					{
						report_packet_length = P5_0_DEBUG_MODE_PACKET_LENGTH;
					}
					else
					{
						DBG_ERR("Unknow firmware mode : %d", core_fr->actual_fw_mode);
						return;
					}
				}

				if (!report_packet_length)
				{
					DBG_ERR("Unknow packet length : %d", report_packet_length);
					return;
				}

				DBG("Length of report packet = %d", report_packet_length);

				fr_data = (uint8_t *)kmalloc(sizeof(uint8_t) * report_packet_length, GFP_KERNEL);
				memset(fr_data, 0xFF, sizeof(uint8_t) * report_packet_length);

				mutex_lock(&ipd->MUTEX);
				fr_t[i].finger_report(fr_data, report_packet_length);
				mutex_unlock(&ipd->MUTEX);

				break;
			}
		}

		if (core_fr->isEnableNetlink)
		{
			netlink_reply_msg(fr_data, report_packet_length);
		}
	}
	else
		DBG_ERR("The figner report was disabled");

	kfree(fr_data);
	return;
}
EXPORT_SYMBOL(core_fr_handler);

void core_fr_input_set_param(struct input_dev *input_device)
{
	int max_x = 0, max_y = 0, min_x = 0, min_y = 0;
	int max_tp = 0;

	core_fr->input_device = input_device;

	// set the supported event type for input device
	set_bit(EV_ABS, core_fr->input_device->evbit);
	set_bit(EV_SYN, core_fr->input_device->evbit);
	set_bit(EV_KEY, core_fr->input_device->evbit);
	set_bit(BTN_TOUCH, core_fr->input_device->keybit);
	set_bit(BTN_TOOL_FINGER, core_fr->input_device->keybit);
	set_bit(INPUT_PROP_DIRECT, core_fr->input_device->propbit);

	if (IS_ERR(core_config->tp_info))
	{
		max_x = TOUCH_SCREEN_X_MAX;
		max_y = TOUCH_SCREEN_Y_MAX;
		min_x = TOUCH_SCREEN_X_MIN;
		min_y = TOUCH_SCREEN_Y_MIN;
		max_tp = MAX_TOUCH_NUM;
	}
	else
	{
		max_x = core_config->tp_info->nMaxX;
		max_y = core_config->tp_info->nMaxY;
		min_x = core_config->tp_info->nMinX;
		min_y = core_config->tp_info->nMinY;
		max_tp = core_config->tp_info->nMaxTouchNum;
	}

	DBG("input resolution : max_x = %d, max_y = %d, min_x = %d, min_y = %d",
		max_x, max_y, min_x, min_y);
	DBG("input touch number: max_tp = %d", max_tp);

	input_set_abs_params(core_fr->input_device, ABS_MT_POSITION_X, min_x, max_x, 0, 0);
	input_set_abs_params(core_fr->input_device, ABS_MT_POSITION_Y, min_y, max_y, 0, 0);

	input_set_abs_params(core_fr->input_device, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
	input_set_abs_params(core_fr->input_device, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);

#ifdef FORCE_TOUCH
	input_set_abs_params(core_fr->input_device, ABS_MT_PRESSURE, 0, 255, 0, 0);
#endif

#ifdef USE_TYPE_B_PROTOCOL
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0)
	input_mt_init_slots(core_fr->input_device, max_tp, INPUT_MT_DIRECT);
#else
	input_mt_init_slots(core_fr->input_device, max_tp);
#endif
#else // for A protocol
	input_set_abs_params(core_fr->input_device, ABS_MT_TRACKING_ID, 0, max_tp, 0, 0);
#endif

//TODO: set virtual keys if tp has key count

#ifdef ENABLE_GESTURE_WAKEUP
	input_set_capability(core_fr->input_device, EV_KEY, KEY_POWER);
	input_set_capability(core_fr->input_device, EV_KEY, KEY_UP);
	input_set_capability(core_fr->input_device, EV_KEY, KEY_DOWN);
	input_set_capability(core_fr->input_device, EV_KEY, KEY_LEFT);
	input_set_capability(core_fr->input_device, EV_KEY, KEY_RIGHT);
	input_set_capability(core_fr->input_device, EV_KEY, KEY_W);
	input_set_capability(core_fr->input_device, EV_KEY, KEY_Z);
	input_set_capability(core_fr->input_device, EV_KEY, KEY_V);
	input_set_capability(core_fr->input_device, EV_KEY, KEY_O);
	input_set_capability(core_fr->input_device, EV_KEY, KEY_M);
	input_set_capability(core_fr->input_device, EV_KEY, KEY_C);
	input_set_capability(core_fr->input_device, EV_KEY, KEY_E);
	input_set_capability(core_fr->input_device, EV_KEY, KEY_S);
#endif
}
EXPORT_SYMBOL(core_fr_input_set_param);

int core_fr_init(struct i2c_client *pClient)
{
	int i = 0, res = 0;

	for (; i < nums_chip; i++)
	{
		if (SUP_CHIP_LIST[i] == ON_BOARD_IC)
		{
			core_fr = kzalloc(sizeof(*core_fr), GFP_KERNEL);

			core_fr->chip_id = SUP_CHIP_LIST[i];
			core_fr->isEnableFR = true;
			core_fr->isEnableNetlink = false;

			if (core_fr->chip_id == CHIP_TYPE_ILI2121)
			{
				if (core_config->use_protocol == ILITEK_PROTOCOL_V3_2)
				{
					core_fr->fw_unknow_mode = ILI2121_FIRMWARE_UNKNOWN_MODE;
					core_fr->fw_demo_mode = ILI2121_FIRMWARE_DEMO_MODE;
					core_fr->fw_debug_mode = ILI2121_FIRMWARE_DEBUG_MODE;
					core_fr->actual_fw_mode = ILI2121_FIRMWARE_DEMO_MODE;
				}
			}
			else if (core_fr->chip_id == CHIP_TYPE_ILI7807)
			{
				if (core_config->use_protocol == ILITEK_PROTOCOL_V5_0)
				{
					core_fr->fw_unknow_mode = P5_0_FIRMWARE_UNKNOWN_MODE;
					core_fr->fw_demo_mode = P5_0_FIRMWARE_DEMO_MODE;
					core_fr->fw_test_mode = P5_0_FIRMWARE_TEST_MODE;
					core_fr->fw_debug_mode = P5_0_FIRMWARE_DEBUG_MODE;
					core_fr->actual_fw_mode = P5_0_FIRMWARE_DEMO_MODE;
				}
			}
			else if (core_fr->chip_id == CHIP_TYPE_ILI9881)
			{
				if (core_config->use_protocol == ILITEK_PROTOCOL_V5_0)
				{
					core_fr->fw_unknow_mode = P5_0_FIRMWARE_UNKNOWN_MODE;
					core_fr->fw_demo_mode = P5_0_FIRMWARE_DEMO_MODE;
					core_fr->fw_test_mode = P5_0_FIRMWARE_TEST_MODE;
					core_fr->fw_debug_mode = P5_0_FIRMWARE_DEBUG_MODE;
					core_fr->actual_fw_mode = P5_0_FIRMWARE_DEMO_MODE;
				}
			}
		}
	}

	if (IS_ERR(core_fr))
	{
		DBG_ERR("Failed to init core_fr APIs");
		res = -ENOMEM;
		goto out;
	}

	return res;

out:
	core_fr_remove();
	return res;
}
EXPORT_SYMBOL(core_fr_init);

void core_fr_remove(void)
{
	DBG_INFO("Remove core-FingerReport members");
	kfree(core_fr);
}
EXPORT_SYMBOL(core_fr_remove);
