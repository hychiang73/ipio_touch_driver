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

/*
 * Relative Driver with Touch IC
 */

// This macro defines what types of chip supported by the driver.
//#define ON_BOARD_IC		0x2121
//#define ON_BOARD_IC		0x7807
#define ON_BOARD_IC		0x9881

// Shows the version of driver
#define DRIVER_VERSION	"1.0.0.4"

// In kernel pr_debug is disabled as default, typeing "echo 8 4 1 7 > /proc/sys/kernel/printk"
// in terminal to enable it if you'd like to see more debug details. 
#define DBG_INFO(fmt, arg...) \
			pr_info("ILITEK: (%s, %d): " fmt "\n", __func__, __LINE__, ##arg);

#define DBG_ERR(fmt, arg...) \
			pr_err("ILITEK: (%s, %d): " fmt "\n", __func__, __LINE__, ##arg);

#define DBG(fmt, arg...) \
			pr_debug( "ILITEK: (%s, %d): " fmt "\n", __func__, __LINE__, ##arg);

/*
 * Relative Firmware Upgrade
 */

#define MAX_HEX_FILE_SIZE			(160*1024)
#define MAX_FLASH_FIRMWARE_SIZE		(256*1024)
#define MAX_IRAM_FIRMWARE_SIZE		(60*1024)

#define UPDATE_FIRMWARE_PAGE_LENGTH		256
#define FLASH_PROGRAM_SIZE			(4*1024)

/*
 * Protocol commands 
 */
/* V3.2 */
#define ILITEK_PROTOCOL_V3_2			0x302
#define PCMD_3_2_GET_TP_INFORMATION		0x20
#define PCMD_3_2_GET_KEY_INFORMATION	0x22
#define PCMD_3_2_GET_FIRMWARE_VERSION	0x40
#define PCMD_3_2_GET_PROTOCOL_VERSION	0x42

/* V5.0 */
#define ILITEK_PROTOCOL_V5_0			0x50
#define PCMD_5_0_READ_DATA_CTRL			0xF6
#define PCMD_5_0_GET_TP_INFORMATION		0x20
#define PCMD_5_0_GET_KEY_INFORMATION	0x27
#define PCMD_5_0_GET_FIRMWARE_VERSION	0x21
#define PCMD_5_0_GET_PROTOCOL_VERSION	0x22
#define PCMD_5_0_GET_CORE_VERSION		0x23
#define PCMD_5_0_MODE_CONTROL			0xF0
#define PCMD_5_0_I2C_UART				0x40
#define PCMD_5_0_SLEEP_CONTROL			0x02
#define PCMD_5_0_CDC_BUSY_STATE			0xF3

// firmware mode
#define P5_0_FIRMWARE_UNKNOWN_MODE		0xFF
#define P5_0_FIRMWARE_DEMO_MODE			0x00
#define P5_0_FIRMWARE_TEST_MODE			0x01
#define P5_0_FIRMWARE_DEBUG_MODE		0x02
#define P5_0_FIRMWARE_I2CUART_MODE		0x03 //defined by ourself 

// Packet ID at the first byte of each finger touch packet
#define P5_0_DEMO_PACKET_ID		0x5A
#define P5_0_DEBUG_PACKET_ID	0xA7
#define P5_0_TEST_PACKET_ID		0xF2
#define P5_0_GESTURE_PACKET_ID	0xAA
#define P5_0_I2CUART_PACKET_ID	0x7A

// length of finger touch packet
#define P5_0_DEMO_MODE_PACKET_LENGTH  	43
#define P5_0_DEBUG_MODE_PACKET_LENGTH  	1280
#define P5_0_TEST_MODE_PACKET_LENGTH  	1180

/*
 *  ILI2121
 */
#define CHIP_TYPE_ILI2121		0x2121

#define ILI2121_SLAVE_ADDR		0x41
#define ILI2121_ICE_MODE_ADDR	0x181062
#define ILI2121_PID_ADDR		0x4009C

// firmware mode
#define ILI2121_FIRMWARE_UNKNOWN_MODE		0xFF
#define ILI2121_FIRMWARE_DEMO_MODE			0x00
#define ILI2121_FIRMWARE_DEBUG_MODE			0x01

// length of finger touch packet
#define ILI2121_DEMO_MODE_PACKET_LENGTH		53

// i2c command
#define ILI2121_TP_CMD_READ_DATA			0x10
#define ILI2121_TP_CMD_READ_SUB_DATA		0x11

/*
 * ILI7807 Series
 */
#define CHIP_TYPE_ILI7807		0x7807
#define ILI7807_TYPE_F			0x0001
#define ILI7807_TYPE_H			0x1100

#define ILI7807_SLAVE_ADDR		0x41
#define ILI7807_ICE_MODE_ADDR	0x181062
#define ILI7807_PID_ADDR		0x4009C

/*
 * ILI9881 Series
 */
#define CHIP_TYPE_ILI9881		0x9881

#define ILI9881_SLAVE_ADDR		0x41
#define ILI9881_ICE_MODE_ADDR	0x181062
#define ILI9881_PID_ADDR		0x4009C

/*
 * Other settings
 */
#define MAX_TOUCH_NUM	10
