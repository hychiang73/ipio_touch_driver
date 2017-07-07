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


typedef struct {

	struct input_dev *input_device;

	int isEnableFR;

	/* able to send packet to user space */
	int isEnableNetlink;

	uint32_t chip_id;

	/* firmware mode */
	uint8_t fw_unknow_mode;
	uint8_t fw_demo_mode;
	uint8_t fw_test_mode;
	uint8_t fw_debug_mode;
	uint16_t actual_fw_mode;

	/* mutual firmware info */
	uint16_t log_packet_length;
	uint8_t log_packet_header;
	uint8_t type;
	uint8_t Mx;
	uint8_t My;
	uint8_t Sd;
	uint8_t Ss;

} CORE_FINGER_REPORT;

extern int core_fr_mode_control(uint8_t *from_user);
extern void core_fr_handler(void);
extern void core_fr_input_set_param(struct input_dev *input_device);
extern int core_fr_init(struct i2c_client *);
extern void core_fr_remove(void);

#endif
