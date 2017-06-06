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


#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/i2c.h>

#include "../chip.h"
#include "config.h"
#include "i2c.h"
#include "finger_report.h"

extern uint32_t SUP_CHIP_LIST[];
extern int nums_chip;
extern struct mutex MUTEX;
extern CORE_CONFIG *core_config;

/*
 * It represents an id and its position in each fingers
 */
struct mutual_touch_point {
	uint16_t id;
	uint16_t x;
	uint16_t y;
	uint16_t pressure;
};

/*
 * It represents keys and their code with each fingers
 */
struct mutual_touch_info {
	uint8_t key_count;
	uint8_t key_code;
	struct mutual_touch_point mtp[10];
};

struct mutual_touch_info mti;

// used to store packet of finger touch and its status
uint8_t *fr_data;
uint8_t CurrentTouch[MAX_TOUCH_NUM];
uint8_t PreviousTouch[MAX_TOUCH_NUM]; 

// store all necessary variables for the use of finger touch
CORE_FINGER_REPORT *core_fr;


/*
 * Create input device and config its settings
 *
 */
static int input_device_create(struct i2c_client *client)
{
	int res = 0;

	DBG_INFO();

	core_fr->input_device = input_allocate_device();

	if(IS_ERR(core_fr->input_device))
	{
		DBG_ERR("Failed to allocate touch input device");
		return -ENOMEM;
	}
	DBG_INFO("client->name = %s", client->name);	
	core_fr->input_device->name = client->name;
	core_fr->input_device->phys = "I2C";
	core_fr->input_device->dev.parent = &client->dev;
	core_fr->input_device->id.bustype = BUS_I2C;

    // set the supported event type for input device
    set_bit(EV_ABS, core_fr->input_device->evbit);
    set_bit(EV_SYN, core_fr->input_device->evbit);
    set_bit(EV_KEY, core_fr->input_device->evbit);
    set_bit(BTN_TOUCH, core_fr->input_device->keybit);
    set_bit(INPUT_PROP_DIRECT, core_fr->input_device->propbit);
    
	//TODO: set virtual keys
	
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

    input_set_abs_params(core_fr->input_device, ABS_MT_TRACKING_ID, 0, (MAX_TOUCH_NUM-1), 0, 0);

    input_set_abs_params(core_fr->input_device, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
    input_set_abs_params(core_fr->input_device, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);

#ifdef USE_TYPE_B_PROTOCOL
    input_set_abs_params(core_fr->input_device, ABS_MT_POSITION_X, TOUCH_SCREEN_X_MIN, TOUCH_SCREEN_X_MAX, 0, 0);
    input_set_abs_params(core_fr->input_device, ABS_MT_POSITION_Y, TOUCH_SCREEN_Y_MIN, TOUCH_SCREEN_Y_MAX, 0, 0);
	
#ifdef FORCE_TOUCH
    input_set_abs_params(core_fr->input_device, ABS_MT_PRESSURE, 0, 255, 0, 0);
#endif

    set_bit(BTN_TOOL_FINGER, core_fr->input_device->keybit);
    input_mt_init_slots(core_fr->input_device, MAX_TOUCH_NUM, 0);
#endif

    /* register the input device to input sub-system */
    res = input_register_device(core_fr->input_device);
    if (res < 0)
    {
        DBG_ERR("Failed to register touch input device, res = %d", res);
		input_free_device(core_fr->input_device);
        return res;
    }

	return res;
}

/*
 * Calculate the check sum of each packet reported by firmware 
 *
 * @pMsg: packet come from firmware
 * @nLength : the length of its packet
 */
static uint8_t CalculateCheckSum(uint8_t *pMsg, uint32_t nLength)
{
	int i;
	int32_t nCheckSum = 0;

	DBG_INFO();

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
 *
 */
void core_fr_touch_press(int32_t x, int32_t y, uint32_t pressure, int32_t id)
{
    DBG_INFO("point touch pressed"); 

    DBG_INFO("id = %d, x = %d, y = %d", id, x, y);

#ifdef USE_TYPE_B_PROTOCOL
    input_mt_slot(core_fr->input_device, id);
    input_mt_report_slot_state(core_fr->input_device, MT_TOOL_FINGER, true);
    input_report_abs(core_fr->input_device, ABS_MT_POSITION_X, x);
    input_report_abs(core_fr->input_device, ABS_MT_POSITION_Y, y);
#ifdef FORCE_TOUCH
    input_report_abs(core_fr->input_device, ABS_MT_PRESSURE, pressure);
#endif
#else
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
EXPORT_SYMBOL(core_fr_touch_press);

/*
 * It'd be called when a finger's touched up from a screen. It'll notify
 * the event to the uplayer from input device.
 *
 * @x: the axis of X
 * @y: the axis of Y
 * @id: an id represents a finger leaving from a screen.
 *
 */
void core_fr_touch_release(int32_t x, int32_t y, int32_t id)
{
    DBG_INFO("point touch released"); 

    DBG_INFO("id = %d, x = %d, y = %d", id, x, y);

#ifdef USE_TYPE_B_PROTOCOL
    input_mt_slot(core_fr->input_device, id);
    input_mt_report_slot_state(core_fr->input_device, MT_TOOL_FINGER, false);
#else 
    input_report_key(core_fr->input_device, BTN_TOUCH, 0);
    input_mt_sync(core_fr->input_device);
#endif
}
EXPORT_SYMBOL(core_fr_touch_release);


//TODO
/*
 * It mainly parses the packet assembled by protocol v3.2
 *
 */
static int parse_touch_package_v3_2(uint8_t *fr_packet, int mode)
{
	DBG_INFO();
}

/*
 * It mainly parses the packet assembled by protocol v5.0
 *
 * @fr_data: the packet of finger report come from firmware.
 * @mode: a mode on the current firmware. (demo or debug)
 * @pInfo: a struct of mutual touch information.
 * @rpl: the lenght of packet of finger report.
 *
 */
static int parse_touch_package_v5_0(uint8_t *fr_data, int mode, struct mutual_touch_info *pInfo, uint16_t rpl)
{
	int i, res = 0;
	uint8_t check_sum = 0;
	uint32_t nX = 0, nY = 0;

#if 0
    for(i = 0; i < 9; i++)
	{
		DBG_INFO("fr_data[%d] = %x", i, fr_data[i]);
	}
#endif

	//TODO: calculate report rate

	check_sum = CalculateCheckSum(&fr_data[0], (rpl-1));	
    DBG_INFO("fr_data = %x  ;  check_sum : %x ", fr_data[rpl-1], check_sum);

    if (fr_data[rpl-1] != check_sum)
    {
        DBG_ERR("WRONG CHECKSUM");
        return -1;
    }

	//TODO: parse pakcet for gesture if enabled

	// 0 : debug, 1 : demo
	if(mode)
	{
		if(fr_data[0] == 0x5A)
		{
			DBG_INFO("Mode & Header are correct in demo mode");
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

				DBG_INFO("[x,y]=[%d,%d]", nX, nY);
				DBG_INFO("point[%d] : (%d,%d) = %d\n",
						 pInfo->mtp[pInfo->key_count].id,
						 pInfo->mtp[pInfo->key_count].x,
						 pInfo->mtp[pInfo->key_count].y,
						 pInfo->mtp[pInfo->key_count].pressure);

				pInfo->key_count++;

			#ifdef USE_TYPE_B_PROTOCOL
				CurrentTouch[i] = 1;
			#endif
			}
			//TODO: sending fr packets up to app layer if log enabled
		}
	}
	else
	{
		if(fr_data[0] == 0xA7)
		{
			DBG_INFO("Mode & Header are correct in debug mode");
			
			//TODO: implement parsing packet in debug mode
		}
	}

	return res;
}

static int finger_report_ili2121(void)
{
	DBG_INFO();
}

/*
 * The function is called by an interrupt and used to handle packet of finger 
 * touch from firmware. A differnece in the process of the data is acorrding to the protocol
 *
 */
static int finger_report_ili7807(void)
{
	int i, res = 0;
	uint16_t report_packet_length = 0;
	static int last_count = 0;
	
	//DBG_INFO();

	// initialise struct of mutual toucn info
	memset(&mti, 0x0, sizeof(struct mutual_touch_info));

	if(core_fr->actual_fw_mode ==  core_fr->fw_demo_mode)
	{
		// allocate length and size of finger touch packet
		report_packet_length = ILI7807_DEMO_MODE_PACKET_LENGTH;
		fr_data = (uint8_t*)kmalloc(sizeof(uint8_t) * ILI7807_DEMO_MODE_PACKET_LENGTH, GFP_KERNEL);
		memset(fr_data, 0xFF, sizeof(uint8_t) * ILI7807_DEMO_MODE_PACKET_LENGTH);

		//TODO: set packet length for gesture wake up

		// read finger touch packet when an interrupt occurs
		res = core_i2c_read(core_config->slave_i2c_addr, fr_data, report_packet_length);
		if(res < 0)
		{
			DBG_ERR("Failed to read finger report packet");
			return res;
		}

		// parsing package of finger touch by protocol
		if(core_config->use_protocol == ILITEK_PROTOCOL_V3_2)
		{
			//TODO: interpret parsed packat for protocol 3.2
			//res = parse_touch_package_v3_2(fr_data, 1);
			//if(res < 0)
			//{
			//	DBG_ERR("Failed to parse packet of finger touch");
			//	return -1;
			//}
		}
		else if(core_config->use_protocol == ILITEK_PROTOCOL_V5_0)
		{
			res = parse_touch_package_v5_0(fr_data, 1, &mti, report_packet_length);
			if(res < 0)
			{
				DBG_ERR("Failed to parse packet of finger touch");
				return -1;
			}

    		//DBG_INFO("tInfo.nCount = %d, nLastCount = %d\n", mti.key_count, last_count);

			// interpret parsed packat and send input events to uplayer
			if(mti.key_count > 0)
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
					//DBG_INFO("PreviousTouch[%d]=%d, CurrentTouch[%d]=%d", i, PreviousTouch[i], i, CurrentTouch[i]);

					if (CurrentTouch[i] == 0 && PreviousTouch[i] == 1)
					{
						core_fr_touch_release(0, 0, i);
					}

					PreviousTouch[i] = CurrentTouch[i];
				}
#else
				for (i = 0; i < mti.key_count; i++)
				{
					core_fr_touch_press(mti.mtp[i].x, mti.mtp[i].y, mti.mtp[i].pressure, mti.mtp[i].id);
				}
#endif
				input_sync(core_fr->input_device);

				last_count = mti.key_count;
			}
			else // key_count < 0
			{
				if (last_count > 0)
				{
#ifdef USE_TYPE_B_PROTOCOL
					for (i = 0; i < MAX_TOUCH_NUM; i++)
					{
						//DBG_INFO("PreviousTouch[%d]=%d, CurrentTouch[%d]=%d", i, PreviousTouch[i], i, CurrentTouch[i]);

						if (CurrentTouch[i] == 0 && PreviousTouch[i] == 1)
						{
							core_fr_touch_release(0, 0, i);
						}
						PreviousTouch[i] = CurrentTouch[i];
					}

					input_report_key(core_fr->input_device, BTN_TOUCH, 0);
					input_report_key(core_fr->input_device, BTN_TOOL_FINGER, 0);
#else
					core_fr_touch_release(0, 0, 0);
#endif
					input_sync(core_fr->input_device);

					last_count = 0;
				}
			}
		}
		else
		{
			DBG_ERR("Can't detect which of protocols are used in the packet");
			return -1;
		}
	}
	else if(core_fr->actual_fw_mode == core_fr->fw_debug_mode)
	{
		report_packet_length = ILI7807_DEBUG_MODE_PACKET_LENGTH;
		fr_data = (uint8_t*)kmalloc(sizeof(uint8_t) * ILI7807_DEBUG_MODE_PACKET_LENGTH, GFP_KERNEL);

		//DBG_INFO("DBG MODE: report packet length = %d", report_packet_length);

		// read data of finger touch
		res = core_i2c_read(core_config->slave_i2c_addr, fr_data, report_packet_length);
		if(res < 0)
		{
			DBG_ERR("Failed to read finger report packet");
			return res;
		}

		// parsing package of finger touch by protocol
		if(core_config->use_protocol == ILITEK_PROTOCOL_V3_2)
		{
			//TODO:
			//parse_touch_package_v3_2(fr_data, 0);
		}
		else if(core_config->use_protocol == ILITEK_PROTOCOL_V5_0)
		{
			parse_touch_package_v5_0(fr_data, 0, &mti, report_packet_length);
			if(res < 0)
			{
				DBG_ERR("Failed to parse packet of finger touch");
				return -1;
			}
		}
		else
		{
			DBG_ERR("Can't detect which of protocols are used in the packet");
			return -1;
		}
	}
	else
	{
		DBG_ERR("Unknow firmware mode : %x", core_fr->actual_fw_mode);
		return -1;
	}

	kfree(fr_data);
	return res;
}

/*
 * The hash table is used to handle calling functions that deal with packets of finger report.
 * The function might be different based on differnet types of chip.
 *
 */
typedef struct {
	uint32_t chip_id;
	int (*finger_report)(void);
} fr_hashtable;

fr_hashtable fr_t[] = {
	{CHIP_TYPE_ILI2121, finger_report_ili2121},
	{CHIP_TYPE_ILI7807, finger_report_ili7807},
	{CHIP_TYPE_ILI9881, finger_report_ili7807},
};

/*
 * The function is an entry when the work queue registered by IRS activates.
 *
 */
void core_fr_handler(void)
{
	int i, len = sizeof(fr_t)/sizeof(fr_t[0]);

	if(core_fr->isEnableFR)
	{
		for(i = 0; i < len; i++)
		{
			if(fr_t[i].chip_id == core_fr->chip_id)
			{
				mutex_lock(&MUTEX);
				fr_t[i].finger_report();
				mutex_unlock(&MUTEX);
				break;
			}
		}
	}
	else
	{
		DBG_INFO("The figner report was disabled");
	}
}
EXPORT_SYMBOL(core_fr_handler);

int core_fr_init(struct i2c_client *pClient)
{
	int i = 0, res = 0;

	for(; i < nums_chip; i++)
	{
		if(SUP_CHIP_LIST[i] == ON_BOARD_IC)
		{
			core_fr = (CORE_FINGER_REPORT*)kmalloc(sizeof(*core_fr), GFP_KERNEL);

			core_fr->chip_id = SUP_CHIP_LIST[i];
			core_fr->isEnableFR = true;

			core_fr->log_packet_length = 0x0;
			core_fr->log_packet_header = 0x0;
			core_fr->type = 0x0;
			core_fr->Mx = 0x0;
			core_fr->My = 0x0;
			core_fr->Sd = 0x0;
			core_fr->Ss = 0x0;

			if(core_fr->chip_id == CHIP_TYPE_ILI2121)
			{
				core_fr->fw_unknow_mode = ILI2121_FIRMWARE_UNKNOWN_MODE;
				core_fr->fw_demo_mode =	  ILI2121_FIRMWARE_DEMO_MODE;
				core_fr->fw_debug_mode =  ILI2121_FIRMWARE_DEBUG_MODE;
				core_fr->actual_fw_mode = ILI2121_FIRMWARE_DEMO_MODE;

			}
			else if(core_fr->chip_id == CHIP_TYPE_ILI7807)
			{
				core_fr->fw_unknow_mode = ILI7807_FIRMWARE_UNKNOWN_MODE;
				core_fr->fw_demo_mode =	  ILI7807_FIRMWARE_DEMO_MODE;
				core_fr->fw_debug_mode =  ILI7807_FIRMWARE_DEBUG_MODE;
				core_fr->actual_fw_mode = ILI7807_FIRMWARE_DEMO_MODE;
			}
			else if(core_fr->chip_id == CHIP_TYPE_ILI9881)
			{
				core_fr->fw_unknow_mode = ILI9881_FIRMWARE_UNKNOWN_MODE;
				core_fr->fw_demo_mode =	  ILI9881_FIRMWARE_DEMO_MODE;
				core_fr->fw_debug_mode =  ILI9881_FIRMWARE_DEBUG_MODE;
				core_fr->actual_fw_mode = ILI9881_FIRMWARE_DEMO_MODE;
			}
		}
	}

	if(IS_ERR(core_fr))
	{
		DBG_ERR("Failed to init core_fr APIs");
		res = -ENOMEM;
		goto Err;
	} 
	else
	{
		res = input_device_create(pClient);
		if(res < 0)
		{
			DBG_ERR("Failed to create input device");
			goto Err;
		}
	}

	return res;

Err:
	core_fr_remove();
	return res;
}
EXPORT_SYMBOL(core_fr_init);

void core_fr_remove(void)
{
	DBG_INFO();

	input_unregister_device(core_fr->input_device);
	input_free_device(core_fr->input_device);
	kfree(core_fr);
}
EXPORT_SYMBOL(core_fr_remove);
