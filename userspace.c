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
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/proc_fs.h>
#include <linux/string.h>
#include <linux/ctype.h>

#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <net/sock.h>

#include "common.h"
#include "platform.h"
#include "core/config.h"
#include "core/firmware.h"
#include "core/finger_report.h"
#include "core/flash.h"
#include "core/i2c.h"
#include "core/protocol.h"
#include "core/mp_test.h"
#include "core/parser.h"

#define ILITEK_IOCTL_MAGIC	100 
#define ILITEK_IOCTL_MAXNR	18

#define ILITEK_IOCTL_I2C_WRITE_DATA			_IOWR(ILITEK_IOCTL_MAGIC, 0, uint8_t*)
#define ILITEK_IOCTL_I2C_SET_WRITE_LENGTH	_IOWR(ILITEK_IOCTL_MAGIC, 1, int)
#define ILITEK_IOCTL_I2C_READ_DATA			_IOWR(ILITEK_IOCTL_MAGIC, 2, uint8_t*)
#define ILITEK_IOCTL_I2C_SET_READ_LENGTH	_IOWR(ILITEK_IOCTL_MAGIC, 3, int)

#define ILITEK_IOCTL_TP_HW_RESET			_IOWR(ILITEK_IOCTL_MAGIC, 4, int)
#define ILITEK_IOCTL_TP_POWER_SWITCH		_IOWR(ILITEK_IOCTL_MAGIC, 5, int)
#define ILITEK_IOCTL_TP_REPORT_SWITCH		_IOWR(ILITEK_IOCTL_MAGIC, 6, int)
#define ILITEK_IOCTL_TP_IRQ_SWITCH			_IOWR(ILITEK_IOCTL_MAGIC, 7, int)

#define ILITEK_IOCTL_TP_DEBUG_LEVEL			_IOWR(ILITEK_IOCTL_MAGIC, 8, int)
#define ILITEK_IOCTL_TP_FUNC_MODE			_IOWR(ILITEK_IOCTL_MAGIC, 9, int)

#define ILITEK_IOCTL_TP_FW_VER				_IOWR(ILITEK_IOCTL_MAGIC, 10, uint8_t*)
#define ILITEK_IOCTL_TP_PL_VER				_IOWR(ILITEK_IOCTL_MAGIC, 11, uint8_t*)
#define ILITEK_IOCTL_TP_CORE_VER			_IOWR(ILITEK_IOCTL_MAGIC, 12, uint8_t*)
#define ILITEK_IOCTL_TP_DRV_VER				_IOWR(ILITEK_IOCTL_MAGIC, 13, uint8_t*)
#define ILITEK_IOCTL_TP_CHIP_ID				_IOWR(ILITEK_IOCTL_MAGIC, 14, uint32_t*)

#define ILITEK_IOCTL_TP_NETLINK_CTRL		_IOWR(ILITEK_IOCTL_MAGIC, 15, int*)
#define ILITEK_IOCTL_TP_NETLINK_STATUS		_IOWR(ILITEK_IOCTL_MAGIC, 16, int*)

#define ILITEK_IOCTL_TP_MODE_CTRL			_IOWR(ILITEK_IOCTL_MAGIC, 17, uint8_t*)
#define ILITEK_IOCTL_TP_MODE_STATUS			_IOWR(ILITEK_IOCTL_MAGIC, 18, int*)

#define UPDATE_FW_PATH		"/mnt/sdcard/ILITEK_FW"

static int katoi(char *string)
{
	int result = 0;
    unsigned int digit;
	int sign;

	if (*string == '-')
	{
		sign = 1;
		string += 1;
	} 
	else
	{
		sign = 0;
		if (*string == '+') 
		{
			string += 1;
		}
	}
	
	for ( ; ; string += 1)
	{
		digit = *string - '0';
		if (digit > 9)
			break;
		result = (10*result) + digit;
	}
	
	if (sign) 
	{
		return -result;
	}
		return result;
}

static int str2hex(char *str) 
{
	int strlen,result,intermed,intermedtop;
	char *s;
	s=str;
	while(*s != 0x0) {s++;}
	strlen=(int)(s-str);
	s=str;
	if(*s != 0x30) {
	  return -1;
	} else {
	  s++;
	  if(*s != 0x78 && *s != 0x58){
		  return -1;
	  }
	  s++;
	}
	strlen = strlen-3;
	result = 0;
	while(*s != 0x0) {
	  intermed = *s & 0x0f;
	  intermedtop = *s & 0xf0;
	  if(intermedtop == 0x60 || intermedtop == 0x40) {
		  intermed += 0x09;
	  }
	  intermed = intermed << (strlen << 2);
	  result = result | intermed;
	  strlen -= 1;
	  s++;
	}
	return result;
}

static ssize_t ilitek_proc_report_data_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	uint32_t len = 0;

	if (*pPos != 0)
		return 0;

	if(core_fr->isPrint)
		core_fr->isPrint = false;
	else
		core_fr->isPrint = true;

	len = sprintf(buff, "%d", core_fr->isPrint);

	DBG_INFO("Print report data = %d\n", core_fr->isPrint);

	*pPos = len;

	return len;
}

static ssize_t ilitek_proc_mp_test_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	int i, j, mp_num = 12;
	uint32_t len = 0;
	char str[512]={0};
	char **mp_ini = NULL;
	uint8_t test_cmd[2] = {0};

	if (*pPos != 0)
		return 0;

	DBG_INFO();

	mp_ini = (char **)kmalloc(mp_num, GFP_KERNEL);

	for(i = 0; i < mp_num; i++)
		mp_ini[i] = (char *)kmalloc(256 * sizeof(char), GFP_KERNEL);

	/* listing test items which are all corrensponding with INI section name */
	//sprintf(mp_ini[0], "FW Ver. Check");
	sprintf(mp_ini[0], "Untouch Calibration Data(DAC) - Mutual");
	sprintf(mp_ini[1], "Untouch Signal Data(BG-Raw-4096) - Mutual");
	sprintf(mp_ini[2], "Untouch Raw Data(Have BK) - Mutual");
	sprintf(mp_ini[3], "Untouch Raw Data(No BK) - Mutual");
	sprintf(mp_ini[4], "Open Test(integration)");
	sprintf(mp_ini[5], "Open Test(Cap)");
	sprintf(mp_ini[6], "Untouch Cm Data");
	sprintf(mp_ini[7], "Short Test (Rx)");
	sprintf(mp_ini[8], "Pixel Raw (No BK)");
	sprintf(mp_ini[9], "Pixel Raw (Have BK)");
	sprintf(mp_ini[10], "Untouch Peak to Peak");	
	sprintf(mp_ini[11], "Tx/Rx Delta");
	// sprintf(mp_ini[12], "Key Raw Open Test");
	// sprintf(mp_ini[13], "Key Raw Short Test");
	// sprintf(mp_ini[14], "Key Raw Data");
	// sprintf(mp_ini[15], "Key Raw BK DAC");
	// sprintf(mp_ini[16], "Key Baseline Data");

	if(core_parser_path(INI_NAME_PATH) < 0)
	{
		DBG_ERR("Failed to parsing INI file \n");
		goto out;
	}

	test_cmd[0] = 0x1;
	core_fr_mode_control(test_cmd);

	ilitek_platform_disable_irq();

	for(i = 0; i < mp_num; i++)
	{
		for(j = 0; j < core_mp->mp_items; j++)
		{
			if(mp_ini[i] == NULL)
				continue;

			if(strcmp(tItems[j].desp, mp_ini[i]) == 0)
			{
				core_parser_get_int_data(mp_ini[i], "Enable", str);
				tItems[j].run = katoi(str);

				/* Get threshold from ini structure in parser */
				if(strcmp("Tx/Rx Delta", mp_ini[i]) == 0)
				{
					core_parser_get_int_data(mp_ini[i], "Tx Max", str);
					core_mp->TxDeltaMax = katoi(str);
					core_parser_get_int_data(mp_ini[i], "Tx Min", str);
					core_mp->TxDeltaMin = katoi(str);
					core_parser_get_int_data(mp_ini[i], "Rx Max", str);
					core_mp->RxDeltaMax = katoi(str);
					core_parser_get_int_data(mp_ini[i], "Rx Min", str);
					core_mp->RxDeltaMin = katoi(str);
					DBG_INFO("%s: Tx Max = %d, Tx Min = %d, Rx Max = %d,  Rx Min = %d\n"
					,tItems[j].desp, core_mp->TxDeltaMax,core_mp->TxDeltaMin, core_mp->RxDeltaMax,core_mp->RxDeltaMin);
				}
				else
				{
					core_parser_get_int_data(mp_ini[i], "Max", str);
					tItems[j].max = katoi(str);
					core_parser_get_int_data(mp_ini[i], "Min", str);
					tItems[j].min = katoi(str);
				}

				core_parser_get_int_data(mp_ini[i], "Frame Count", str);
				tItems[j].frame_count = katoi(str);


				DBG_INFO("%s: run = %d, max = %d, min = %d, frame_count = %d\n"
					,tItems[j].desp, tItems[j].run,tItems[j].max,tItems[j].min,tItems[j].frame_count);
			}			
		}
	}

	core_mp_run_test();
	core_mp_show_result();
	core_mp_test_free();

out:
	for(i = 0; i < mp_num; i++)
		kfree(mp_ini[i]);
	kfree(mp_ini);
	ilitek_platform_enable_irq();	
	*pPos = len;
	return len;
}

static ssize_t ilitek_proc_mp_test_write(struct file *filp, const char *buff, size_t size, loff_t *pPos)
{
	int res = 0, count = 0;
	char cmd[64] = {0};
	char *token = NULL, *cur = NULL;	
	uint8_t *va = NULL;

	if(buff != NULL)
	{
		res = copy_from_user(cmd, buff, size - 1);
		if(res < 0)
		{
			DBG_INFO("copy data from user space, failed\n");
			return -1;
		}
	}

	DBG_INFO("size = %d, cmd = %s\n", (int)size, cmd);

	if(size > 64)
	{
		DBG_ERR("The size of string is too long\n");
		return size;
	}

	token = cur = cmd;

	va = kmalloc(64 * sizeof(uint8_t), GFP_KERNEL);
	memset(va, 0, 64);

	while((token = strsep(&cur, ",")) != NULL)
	{
		va[count] = str2hex(token);
		//DBG_INFO("data[%d] = %x",count, va[count]);	
		count++;

		if(count > 2)
			break;
	}

	//core_mp_run_test(cmd);

	return size;
}

static ssize_t ilitek_proc_debug_level_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	int res = 0;
	uint32_t len = 0;

	if (*pPos != 0)
		return 0;

	len = sprintf(buff, "%d", ipio_debug_level);

	DBG_INFO("Current DEBUG Level = %d\n", ipio_debug_level);
	DBG_INFO("You can set one of levels for debug as below:\n");
	DBG_INFO("DEBUG_NONE = %d\n", DEBUG_NONE);
	DBG_INFO("DEBUG_IRQ = %d\n", DEBUG_IRQ);
	DBG_INFO("DEBUG_FINGER_REPORT = %d\n", DEBUG_FINGER_REPORT);
	DBG_INFO("DEBUG_FIRMWARE = %d\n", DEBUG_FIRMWARE);
	DBG_INFO("DEBUG_CONFIG = %d\n", DEBUG_CONFIG);
	DBG_INFO("DEBUG_I2C = %d\n", DEBUG_I2C);
	DBG_INFO("DEBUG_BATTERY = %d\n", DEBUG_BATTERY);
	DBG_INFO("DEBUG_MP_TEST = %d\n", DEBUG_MP_TEST);
	DBG_INFO("DEBUG_IOCTL = %d\n", DEBUG_IOCTL);
	DBG_INFO("DEBUG_NETLINK = %d\n", DEBUG_NETLINK);
	DBG_INFO("DEBUG_ALL = %d\n", DEBUG_ALL);

	res = copy_to_user((uint32_t *)buff, &ipio_debug_level, len);
	if (res < 0)
	{
		DBG_ERR("Failed to copy data to user space\n");
	}

	*pPos = len;

	return len;
}

static ssize_t ilitek_proc_debug_level_write(struct file *filp, const char *buff, size_t size, loff_t *pPos)
{
	int res = 0;
	char cmd[10] = {0};

	if(buff != NULL)
	{
		res = copy_from_user(cmd, buff, size - 1);
		if(res < 0)
		{
			DBG_INFO("copy data from user space, failed\n");
			return -1;
		}
	}

	ipio_debug_level = katoi(cmd);

	DBG_INFO("ipio_debug_level = %d\n", ipio_debug_level);

	return size;
}

static ssize_t ilitek_proc_gesture_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	int res = 0;
	uint32_t len = 0;

	if (*pPos != 0)
		return 0;

	len = sprintf(buff, "%d", core_config->isEnableGesture );

	DBG_INFO("isEnableGesture = %d\n", core_config->isEnableGesture);

	res = copy_to_user((uint32_t *)buff, &core_config->isEnableGesture, len);
	if (res < 0)
	{
		DBG_ERR("Failed to copy data to user space\n");
	}

	*pPos = len;

	return len;
}

static ssize_t ilitek_proc_gesture_write(struct file *filp, const char *buff, size_t size, loff_t *pPos)
{
	int res = 0;
	char cmd[10] = {0};

	if(buff != NULL)
	{
		res = copy_from_user(cmd, buff, size - 1);
		if(res < 0)
		{
			DBG_INFO("copy data from user space, failed\n");
			return -1;
		}
	}

	DBG_INFO("size = %d, cmd = %s\n", (int)size, cmd);

	if(strcmp(cmd, "on") == 0)
	{
		DBG_INFO("enable gesture mode\n");
		core_config->isEnableGesture = true;		
	}
	else if(strcmp(cmd, "off") == 0)
	{
		DBG_INFO("disable gesture mode\n");
		core_config->isEnableGesture = false;
	}
	else
		DBG_ERR("Unknown command\n");

	return size;
}

static ssize_t ilitek_proc_check_battery_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	int res = 0;
	uint32_t len = 0;

	if (*pPos != 0)
		return 0;

	len = sprintf(buff, "%d", ipd->isEnablePollCheckPower );

	DBG_INFO("isEnablePollCheckPower = %d\n", ipd->isEnablePollCheckPower);

	res = copy_to_user((uint32_t *)buff, &ipd->isEnablePollCheckPower, len);
	if (res < 0)
	{
		DBG_ERR("Failed to copy data to user space\n");
	}

	*pPos = len;

	return len;
}

static ssize_t ilitek_proc_check_battery_write(struct file *filp, const char *buff, size_t size, loff_t *pPos)
{
	int res = 0;
	char cmd[10] = {0};

	if(buff != NULL)
	{
		res = copy_from_user(cmd, buff, size - 1);
		if(res < 0)
		{
			DBG_INFO("copy data from user space, failed\n");
			return -1;
		}
	}

	DBG_INFO("size = %d, cmd = %s\n", (int)size, cmd);

	if(strcmp(cmd, "on") == 0)
	{
		DBG_INFO("Start the thread of check power status\n");
		queue_delayed_work(ipd->check_power_status_queue, &ipd->check_power_status_work, ipd->work_delay);
		ipd->isEnablePollCheckPower = true;
	}
	else if(strcmp(cmd, "off") == 0)
	{
		DBG_INFO("Cancel the thread of check power status\n");
		cancel_delayed_work_sync(&ipd->check_power_status_work);
		ipd->isEnablePollCheckPower = false;
	}
	else
		DBG_ERR("Unknown command\n");

	return size;
}

static ssize_t ilitek_proc_fw_process_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	int res = 0;
	uint32_t len = 0;

	// If file position is non-zero,  we assume the string has been read
	// and indicates that there is no more data to be read.
	if (*pPos != 0)
		return 0;

	len = sprintf(buff, "%02d", core_firmware->update_status);

	DBG_INFO("update status = %d\n", core_firmware->update_status);

	res = copy_to_user((uint32_t *)buff, &core_firmware->update_status, len);
	if (res < 0)
	{
		DBG_ERR("Failed to copy data to user space");
	}

	*pPos = len;

	return len;
}

/*
 * To avoid the restriction of selinux, we assigned a fixed path where locates firmware file,
 * reading (cat) this node to notify driver running the upgrade process from user space.
 */
static ssize_t ilitek_proc_fw_upgrade_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	int res = 0;
	uint32_t len = 0;

	DBG_INFO("Preparing to upgarde firmware\n");

	if (*pPos != 0)
		return 0;

	ilitek_platform_disable_irq();

	if(ipd->isEnablePollCheckPower)
		cancel_delayed_work_sync(&ipd->check_power_status_work);

	res = core_firmware_upgrade(UPDATE_FW_PATH, false);

	ilitek_platform_enable_irq();

	if(ipd->isEnablePollCheckPower)
		queue_delayed_work(ipd->check_power_status_queue, &ipd->check_power_status_work, ipd->work_delay);

	if (res < 0)
	{
		core_firmware->update_status = res;		
		DBG_ERR("Failed to upgrade firwmare\n");		
	}
	else
	{
		core_firmware->update_status = 100;
		DBG_INFO("Succeed to upgrade firmware\n");		
	}

	*pPos = len;

	return len;
}

static ssize_t ilitek_proc_iram_upgrade_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	int res = 0;
	uint32_t len = 0;

	DBG_INFO("Preparing to upgarde firmware by IRAM\n");

	if (*pPos != 0)
		return 0;

	ilitek_platform_disable_irq();

	res = core_firmware_upgrade(UPDATE_FW_PATH, true);

	ilitek_platform_enable_irq();

	if (res < 0)
	{
		// return the status to user space even if any error occurs.
		core_firmware->update_status = res;
		DBG_ERR("Failed to upgrade firwmare by IRAM, res = %d\n", res);
	}
	else
	{
		DBG_INFO("Succeed to upgrade firmware by IRAM\n");
	}

	*pPos = len;

	return len;
}

// for debug
static ssize_t ilitek_proc_ioctl_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	int res = 0;
	uint32_t len = 0;
	uint8_t cmd[2] = {0};

	if (*pPos != 0)
		return 0;

	if(size < 4095)
	{
		res = copy_from_user(cmd, buff, size - 1);
		if(res < 0)
		{
			DBG_INFO("copy data from user space, failed\n");
			return -1;
		}
	}

	DBG_INFO("size = %d, cmd = %d", (int)size, cmd[0]);

	// test
	if(cmd[0] == 0x1)
	{
		DBG_INFO("HW Reset\n");
		ilitek_platform_tp_hw_reset(true);
	}
	else if(cmd[0] == 0x02)
	{
		DBG_INFO("Disable IRQ\n");
		ilitek_platform_disable_irq();
	}
	else if(cmd[0] == 0x03)
	{
		DBG_INFO("Enable IRQ\n");
		ilitek_platform_enable_irq();
	}
	else if(cmd[0] == 0x04)
	{
		DBG_INFO("Get Chip id\n");
		core_config_get_chip_id();
	}

	*pPos = len;

	return len;
}

// for debug
static ssize_t ilitek_proc_ioctl_write(struct file *filp, const char *buff, size_t size, loff_t *pPos)
{
	int res = 0, count = 0, i;
	int w_len = 0, r_len = 0, i2c_delay = 0;
	char cmd[512] = {0};
	char *token = NULL, *cur = NULL;	
	uint8_t i2c[256] = {0};	
	uint8_t *data = NULL;

	if(buff != NULL)
	{
		res = copy_from_user(cmd, buff, size - 1);
		if(res < 0)
		{
			DBG_INFO("copy data from user space, failed\n");
			return -1;
		}
	}

	DBG_INFO("size = %d, cmd = %s\n", (int)size, cmd);

	token = cur = cmd;

	data = kmalloc(512 * sizeof(uint8_t), GFP_KERNEL);
	memset(data, 0, 512);

	while((token = strsep(&cur, ",")) != NULL)
	{
		data[count] = str2hex(token);
		//DBG_INFO("data[%d] = %x",count, data[count]);	
		count++;
	}

	DBG_INFO("cmd = %s\n", cmd);

	if(strcmp(cmd, "reset") == 0)
	{
		DBG_INFO("HW Reset\n");
		ilitek_platform_tp_hw_reset(true);
	}
	else if(strcmp(cmd, "disirq") == 0)
	{
		DBG_INFO("Disable IRQ\n");
		ilitek_platform_disable_irq();
	}
	else if(strcmp(cmd, "enairq") == 0)
	{
		DBG_INFO("Enable IRQ\n");
		ilitek_platform_enable_irq();
	}
	else if(strcmp(cmd, "getchip") == 0)
	{
		DBG_INFO("Get Chip id\n");
		core_config_get_chip_id();
	}
	else if(strcmp(cmd, "dispcc") == 0)
	{
		DBG_INFO("disable phone cover\n");
		core_config_phone_cover_ctrl(false);
	}
	else if(strcmp(cmd, "enapcc") == 0)
	{
		DBG_INFO("enable phone cover\n");
		core_config_phone_cover_ctrl(true);
	}
	else if(strcmp(cmd, "disfsc") == 0)
	{
		DBG_INFO("disable finger sense\n")
		core_config_finger_sense_ctrl(false);
	}
	else if(strcmp(cmd, "enafsc") == 0)
	{
		DBG_INFO("enable finger sense\n");
		core_config_finger_sense_ctrl(true);
	}
	else if(strcmp(cmd, "disprox") == 0)
	{
		DBG_INFO("disable proximity\n");
		core_config_proximity_ctrl(false);
	}
	else if(strcmp(cmd, "enaprox") == 0)
	{
		DBG_INFO("enable proximity\n");
		core_config_proximity_ctrl(true);
	}
	else if(strcmp(cmd, "disglove") == 0)
	{
		DBG_INFO("disable glove function\n");
		core_config_glove_ctrl(false, false);
	}
	else if(strcmp(cmd, "enaglove") == 0)
	{
		DBG_INFO("enable glove function\n");
		core_config_glove_ctrl(true, false);
	}
	else if(strcmp(cmd, "glovesl") == 0)
	{
		DBG_INFO("set glove as seamless\n");
		core_config_glove_ctrl(true, true);
	}
	else if(strcmp(cmd, "enastylus") == 0)
	{
		DBG_INFO("enable stylus\n");
		core_config_stylus_ctrl(true, false);
	}
	else if(strcmp(cmd, "disstylus") == 0)
	{
		DBG_INFO("disable stylus\n");
		core_config_stylus_ctrl(false, false);
	}
	else if(strcmp(cmd, "stylussl") == 0)
	{
		DBG_INFO("set stylus as seamless\n");
		core_config_stylus_ctrl(true, true);
	}
	else if(strcmp(cmd, "tpscan_ab") == 0)
	{
		DBG_INFO("set TP scan as mode AB\n");
		core_config_tp_scan_mode(true);
	}
	else if(strcmp(cmd, "tpscan_b") == 0)
	{
		DBG_INFO("set TP scan as mode B\n");
		core_config_tp_scan_mode(false);
	}
	else if(strcmp(cmd, "i2c_w") == 0)
	{
		w_len = data[1];
		DBG_INFO("w_len = %d\n", w_len);

		for(i = 0; i < w_len; i++)
		{
			i2c[i] = data[2+i];
			DBG_INFO("i2c[%d] = %x\n",i , i2c[i]);
		}
		
		core_i2c_write(core_config->slave_i2c_addr, i2c, w_len);
	}
	else if(strcmp(cmd, "i2c_r") == 0)
	{
		r_len = data[1];
		DBG_INFO("r_len = %d\n", r_len);
	
		core_i2c_read(core_config->slave_i2c_addr, &i2c[0], r_len);

		for(i = 0; i < r_len; i++)
			DBG_INFO("i2c[%d] = %x\n",i , i2c[i]);
	}
	else if(strcmp(cmd, "i2c_w_r") == 0)
	{
		w_len = data[1];
		r_len = data[2];
		i2c_delay = data[3];
		DBG_INFO("w_len = %d, r_len = %d, delay = %d\n", w_len, r_len, i2c_delay);

		for(i = 0; i < w_len; i++)
		{
			i2c[i] = data[4+i];
			DBG_INFO("i2c[%d] = %x\n",i , i2c[i]);
		}
		
		core_i2c_write(core_config->slave_i2c_addr, i2c, w_len);

		memset(i2c, 0, sizeof(i2c));
		mdelay(i2c_delay);

		core_i2c_read(core_config->slave_i2c_addr, &i2c[0], r_len);

		for(i = 0; i < r_len; i++)
			DBG_INFO("i2c[%d] = %x\n",i , i2c[i]);
	}
	else
	{
		DBG_ERR("Unknown command\n");
	}

	kfree(data);
	return size;	
}

static long ilitek_proc_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int res = 0, length = 0;
	uint8_t szBuf[512] = {0};
	static uint16_t i2c_rw_length = 0;
	uint32_t id_to_user = 0x0;
	char dbg[10] = {0};

	DBG(DEBUG_IOCTL, "cmd = %d\n", _IOC_NR(cmd));

	if (_IOC_TYPE(cmd) != ILITEK_IOCTL_MAGIC)
	{
		DBG_ERR("The Magic number doesn't match\n");
		return -ENOTTY;
	}

	if (_IOC_NR(cmd) > ILITEK_IOCTL_MAXNR)
	{
		DBG_ERR("The number of ioctl doesn't match\n");
		return -ENOTTY;
	}

	switch (cmd)
	{
	case ILITEK_IOCTL_I2C_WRITE_DATA:
		res = copy_from_user(szBuf, (uint8_t *)arg, i2c_rw_length);
		if (res < 0)
		{
			DBG_ERR("Failed to copy data from user space\n");
		}
		else
		{
			res = core_i2c_write(core_config->slave_i2c_addr, &szBuf[0], i2c_rw_length);
			if (res < 0)
			{
				DBG_ERR("Failed to write data via i2c\n");
			}
		}
		break;

	case ILITEK_IOCTL_I2C_READ_DATA:
		res = core_i2c_read(core_config->slave_i2c_addr, szBuf, i2c_rw_length);
		if (res < 0)
		{
			DBG_ERR("Failed to read data via i2c\n");
		}
		else
		{
			res = copy_to_user((uint8_t *)arg, szBuf, i2c_rw_length);
			if (res < 0)
			{
				DBG_ERR("Failed to copy data to user space\n");
			}
		}
		break;

	case ILITEK_IOCTL_I2C_SET_WRITE_LENGTH:
	case ILITEK_IOCTL_I2C_SET_READ_LENGTH:
		i2c_rw_length = arg;
		break;

	case ILITEK_IOCTL_TP_HW_RESET:
		ilitek_platform_tp_hw_reset(true);
		break;

	case ILITEK_IOCTL_TP_POWER_SWITCH:
		DBG_INFO("Not implemented yet\n");
		break;

	case ILITEK_IOCTL_TP_REPORT_SWITCH:
		res = copy_from_user(szBuf, (uint8_t *)arg, 1);
		if (res < 0)
		{
			DBG_ERR("Failed to copy data from user space\n");
		}
		else
		{
			if (szBuf[0])
			{
				core_fr->isEnableFR = true;
				DBG(DEBUG_IOCTL, "Function of finger report was enabled\n");
			}
			else
			{
				core_fr->isEnableFR = false;
				DBG(DEBUG_IOCTL, "Function of finger report was disabled\n");
			}
		}
		break;

	case ILITEK_IOCTL_TP_IRQ_SWITCH:
		res = copy_from_user(szBuf, (uint8_t *)arg, 1);
		if (res < 0)
		{
			DBG_ERR("Failed to copy data from user space\n");
		}
		else
		{
			if (szBuf[0])
			{
				ilitek_platform_enable_irq();
			}
			else
			{
				ilitek_platform_disable_irq();
			}
		}
		break;

	case ILITEK_IOCTL_TP_DEBUG_LEVEL:
		res = copy_from_user(dbg, (uint32_t *)arg, sizeof(uint32_t));
		if (res < 0)
		{
			DBG_ERR("Failed to copy data from user space\n");
		}
		else
		{
			ipio_debug_level = katoi(dbg);
			DBG_INFO("ipio_debug_level = %d", ipio_debug_level);
		}
		break;

	case ILITEK_IOCTL_TP_FUNC_MODE:
		res = copy_from_user(szBuf, (uint8_t *)arg, 3);
		if (res < 0)
		{
			DBG_ERR("Failed to copy data from user space\n");
		}
		else
		{
			core_i2c_write(core_config->slave_i2c_addr, &szBuf[0], 3);
		}
		break;

	case ILITEK_IOCTL_TP_FW_VER:
		res = core_config_get_fw_ver();
		if (res < 0)
		{
			DBG_ERR("Failed to get firmware version\n");
		}
		else
		{
			res = copy_to_user((uint8_t *)arg, core_config->firmware_ver, protocol->fw_ver_len);
			if (res < 0)
			{
				DBG_ERR("Failed to copy firmware version to user space\n");
			}
		}
		break;

	case ILITEK_IOCTL_TP_PL_VER:
		res = core_config_get_protocol_ver();
		if (res < 0)
		{
			DBG_ERR("Failed to get protocol version\n");
		}
		else
		{
			res = copy_to_user((uint8_t *)arg, core_config->protocol_ver, protocol->pro_ver_len);
			if (res < 0)
			{
				DBG_ERR("Failed to copy protocol version to user space\n");
			}
		}
		break;

	case ILITEK_IOCTL_TP_CORE_VER:
		res = core_config_get_core_ver();
		if (res < 0)
		{
			DBG_ERR("Failed to get core version\n");
		}
		else
		{
			res = copy_to_user((uint8_t *)arg, core_config->core_ver, protocol->core_ver_len);
			if (res < 0)
			{
				DBG_ERR("Failed to copy core version to user space\n");
			}
		}
		break;

	case ILITEK_IOCTL_TP_DRV_VER:
		length = sprintf(szBuf, "%s", DRIVER_VERSION);
		if (!length)
		{
			DBG_ERR("Failed to convert driver version from definiation\n");
		}
		else
		{
			res = copy_to_user((uint8_t *)arg, szBuf, length);
			if (res < 0)
			{
				DBG_ERR("Failed to copy driver ver to user space\n");
			}
		}
		break;

	case ILITEK_IOCTL_TP_CHIP_ID:
		res = core_config_get_chip_id();
		if (res < 0)
		{
			DBG_ERR("Failed to get chip id\n");
		}
		else
		{
			id_to_user = core_config->chip_id << 16 | core_config->chip_type;

			res = copy_to_user((uint32_t *)arg, &id_to_user, sizeof(uint32_t));
			if (res < 0)
			{
				DBG_ERR("Failed to copy chip id to user space\n");
			}
		}
		break;

	case ILITEK_IOCTL_TP_NETLINK_CTRL:
		res = copy_from_user(szBuf, (uint8_t *)arg, 1);
		if (res < 0)
		{
			DBG_ERR("Failed to copy data from user space\n");
		}
		else
		{
			if (szBuf[0])
			{
				core_fr->isEnableNetlink = true;
				DBG(DEBUG_IOCTL, "Netlink has been enabled\n");
			}
			else
			{
				core_fr->isEnableNetlink = false;
				DBG(DEBUG_IOCTL, "Netlink has been disabled\n");
			}
		}
		break;

	case ILITEK_IOCTL_TP_NETLINK_STATUS:
		DBG(DEBUG_IOCTL, "Netlink is enabled : %d\n", core_fr->isEnableNetlink);
		res = copy_to_user((int *)arg, &core_fr->isEnableNetlink, sizeof(int));
		if (res < 0)
		{
			DBG_ERR("Failed to copy chip id to user space\n");
		}
		break;

	case ILITEK_IOCTL_TP_MODE_CTRL:
		res = copy_from_user(szBuf, (uint8_t *)arg, 4);
		if (res < 0)
		{
			DBG_ERR("Failed to copy data from user space\n");
		}
		else
		{
			core_fr_mode_control(szBuf);
		}
		break;

	case ILITEK_IOCTL_TP_MODE_STATUS:
		DBG(DEBUG_IOCTL, "Current firmware mode : %d", core_fr->actual_fw_mode);
		res = copy_to_user((int *)arg, &core_fr->actual_fw_mode, sizeof(int));
		if (res < 0)
		{
			DBG_ERR("Failed to copy chip id to user space\n");
		}
		break;

	default:
		res = -ENOTTY;
		break;
	}

	return res;
}

struct proc_dir_entry *proc_dir_ilitek;
struct proc_dir_entry *proc_ioctl;
struct proc_dir_entry *proc_fw_process;
struct proc_dir_entry *proc_fw_upgrade;
struct proc_dir_entry *proc_iram_upgrade;
struct proc_dir_entry *proc_gesture;
struct proc_dir_entry *proc_debug_level;
struct proc_dir_entry *proc_report_data;
struct proc_dir_entry *proc_mp_test;

struct file_operations proc_ioctl_fops = {
	.unlocked_ioctl = ilitek_proc_ioctl,
	.read = ilitek_proc_ioctl_read,
	.write = ilitek_proc_ioctl_write,
};

struct file_operations proc_fw_process_fops = {
	.read = ilitek_proc_fw_process_read,
};

struct file_operations proc_fw_upgrade_fops = {
	.read = ilitek_proc_fw_upgrade_read,
};

struct file_operations proc_iram_upgrade_fops = {
	.read = ilitek_proc_iram_upgrade_read,
};

struct file_operations proc_gesture_fops = {
	.write = ilitek_proc_gesture_write,
	.read = ilitek_proc_gesture_read,
};

struct file_operations proc_check_battery_fops = {
	.write = ilitek_proc_check_battery_write,
	.read = ilitek_proc_check_battery_read,
};

struct file_operations proc_debug_level_fops = {
	.write = ilitek_proc_debug_level_write,
	.read = ilitek_proc_debug_level_read,
};

struct file_operations proc_report_data_fops = {
	.read = ilitek_proc_report_data_read,
};

struct file_operations proc_mp_test_fops = {
	.write = ilitek_proc_mp_test_write,
	.read = ilitek_proc_mp_test_read,
};

/**
 * This struct lists all file nodes will be created under /proc filesystem.
 *
 * Before creating a node that you want, declaring its file_operations structure
 * is necessary. After that, puts the structure into proc_table, defines its
 * node's name in the same row, and the init function lterates the table and
 * creates all nodes under /proc.
 *
 */
typedef struct
{
	char *name;
	struct proc_dir_entry *node;
	struct file_operations *fops;
	bool isCreated;
} proc_node_t;

proc_node_t proc_table[] = {
	{"ioctl", NULL, &proc_ioctl_fops, false},
	{"fw_process", NULL, &proc_fw_process_fops, false},
	{"fw_upgrade", NULL, &proc_fw_upgrade_fops, false},
	{"iram_upgrade", NULL, &proc_iram_upgrade_fops, false},
	{"gesture", NULL, &proc_gesture_fops, false},
	{"check_battery", NULL, &proc_check_battery_fops, false},
	{"debug_level", NULL, &proc_debug_level_fops, false},
	{"report_data", NULL, &proc_report_data_fops, false},
	{"mp_test", NULL, &proc_mp_test_fops, false},
};

#define NETLINK_USER 21

struct sock *nl_sk;
struct nlmsghdr *nlh;
struct sk_buff *skb_out;

int pid;

void netlink_reply_msg(void *raw, int size)
{
	int res;
	int msg_size = size;
	uint8_t *data = (uint8_t *)raw;

	DBG(DEBUG_NETLINK, "The size of data being sent to user = %d\n", msg_size);
	DBG(DEBUG_NETLINK, "pid = %d\n", pid);
	DBG(DEBUG_NETLINK, "Netlink is enable = %d\n", core_fr->isEnableNetlink);

	if (core_fr->isEnableNetlink)
	{
		skb_out = nlmsg_new(msg_size, 0);

		if (!skb_out)
		{
			DBG_ERR("Failed to allocate new skb\n");
			return;
		}

		nlh = nlmsg_put(skb_out, 0, 0, NLMSG_DONE, msg_size, 0);
		NETLINK_CB(skb_out).dst_group = 0; /* not in mcast group */

		//strncpy(NLMSG_DATA(nlh), data, msg_size);
		memcpy(nlmsg_data(nlh), data, msg_size);

		res = nlmsg_unicast(nl_sk, skb_out, pid);
		if (res < 0)
			DBG_ERR("Failed to send data back to user\n");
	}
}
EXPORT_SYMBOL(netlink_reply_msg);

static void netlink_recv_msg(struct sk_buff *skb)
{
	pid = 0;

	DBG(DEBUG_NETLINK, "Netlink is enable = %d\n", core_fr->isEnableNetlink);

	nlh = (struct nlmsghdr *)skb->data;

	DBG(DEBUG_NETLINK, "Received a request from client: %s, %d\n",
		(char *)NLMSG_DATA(nlh), (int)strlen((char *)NLMSG_DATA(nlh)));

	/* pid of sending process */
	pid = nlh->nlmsg_pid;

	DBG(DEBUG_NETLINK, "the pid of sending process = %d\n", pid);

	/* TODO: may do something if there's not receiving msg from user. */
	if (pid != 0)
	{
		DBG_ERR("The channel of Netlink has been established successfully !\n");
		core_fr->isEnableNetlink = true;
	}
	else
	{
		DBG_ERR("Failed to establish the channel between kernel and user space\n");
		core_fr->isEnableNetlink = false;
	}
}

static int netlink_init(void)
{
	int res = 0;

#if LINUX_VERSION_CODE < KERNEL_VERSION(3, 4, 0)
	nl_sk = netlink_kernel_create(&init_net, NETLINK_USER, netlink_recv_msg, NULL, THIS_MODULE);
#else
	struct netlink_kernel_cfg cfg = {
		.input = netlink_recv_msg,
	};

	nl_sk = netlink_kernel_create(&init_net, NETLINK_USER, &cfg);
#endif

	DBG_INFO("Initialise Netlink and create its socket\n");

	if (!nl_sk)
	{
		DBG_ERR("Failed to create nelink socket\n");
		res = -EFAULT;
	}

	return res;
}

int ilitek_proc_init(void)
{
	int i = 0, res = 0;

	proc_dir_ilitek = proc_mkdir("ilitek", NULL);

	for (; i < ARRAY_SIZE(proc_table); i++)
	{
		proc_table[i].node = proc_create(proc_table[i].name, 0666, proc_dir_ilitek, proc_table[i].fops);

		if (proc_table[i].node == NULL)
		{
			proc_table[i].isCreated = false;
			DBG_ERR("Failed to create %s under /proc\n", proc_table[i].name);
			res = -ENODEV;
		}
		else
		{
			proc_table[i].isCreated = true;
			DBG_INFO("Succeed to create %s under /proc\n", proc_table[i].name);
		}
	}

	netlink_init();

	return res;
}
EXPORT_SYMBOL(ilitek_proc_init);

void ilitek_proc_remove(void)
{
	int i = 0;

	for (; i < ARRAY_SIZE(proc_table); i++)
	{
		if (proc_table[i].isCreated == true)
		{
			DBG_INFO("Removed %s under /proc\n", proc_table[i].name);
			remove_proc_entry(proc_table[i].name, proc_dir_ilitek);
		}
	}

	remove_proc_entry("ilitek", NULL);
	netlink_kernel_release(nl_sk);
}
EXPORT_SYMBOL(ilitek_proc_remove);
