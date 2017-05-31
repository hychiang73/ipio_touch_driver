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

#ifndef __FINGER_REPORT_H
#define __FINGER_REPORT_H

// Either B TYPE or A Type in MTP
#define USE_TYPE_B_PROTOCOL 

//#define ENABLE_GESTURE_WAKEUP

// Whether to detect the value of pressure in finger touch
//#define FORCE_TOUCH

// set up width and heigth of a screen
#define TOUCH_SCREEN_X_MIN	0
#define TOUCH_SCREEN_Y_MIN	0
#define TOUCH_SCREEN_X_MAX	1080
#define TOUCH_SCREEN_Y_MAX	1920

#define TPD_HEIGHT 2048
#define TPD_WIDTH  2048

typedef struct {

	struct input_dev *input_device;

	int isDisableFR;

	uint32_t chip_id;

	/* mutual firmware info */
	uint8_t fw_unknow_mode;
	uint8_t fw_demo_mode;
	uint8_t fw_debug_mode;
	uint16_t actual_fw_mode;
	uint16_t log_packet_length;
	uint8_t log_packet_header;
	uint8_t type;
	uint8_t Mx;
	uint8_t My;
	uint8_t Sd;
	uint8_t Ss;

} CORE_FINGER_REPORT;

extern void core_fr_handler(void);
extern int core_fr_init(struct i2c_client *);
extern void core_fr_remove(void);

#endif
