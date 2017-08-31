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

static ssize_t ilitek_proc_debug_level_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	int res = 0;
	uint32_t len = 0;

	if (*pPos != 0)
		return 0;

	len = sprintf(buff, "%d", ipio_debug_level);

	DBG_INFO("Current DEBUG Level = %d\n", ipio_debug_level);
	DBG_INFO("You can set one of levels for debug as below:");
	DBG_INFO("DEBUG_NONE = %d", DEBUG_NONE);
	DBG_INFO("DEBUG_IRQ = %d", DEBUG_IRQ);
	DBG_INFO("DEBUG_FINGER_REPORT = %d", DEBUG_FINGER_REPORT);
	DBG_INFO("DEBUG_FIRMWARE = %d", DEBUG_FIRMWARE);
	DBG_INFO("DEBUG_CONFIG = %d", DEBUG_CONFIG);
	DBG_INFO("DEBUG_I2C = %d", DEBUG_I2C);
	DBG_INFO("DEBUG_BATTERY = %d", DEBUG_BATTERY);
	DBG_INFO("DEBUG_MP_TEST = %d", DEBUG_MP_TEST);
	DBG_INFO("DEBUG_IOCTL = %d", DEBUG_IOCTL);
	DBG_INFO("DEBUG_NETLINK = %d", DEBUG_NETLINK);
	DBG_INFO("DEBUG_ALL = %d", DEBUG_ALL);

	res = copy_to_user((uint32_t *)buff, &ipio_debug_level, len);
	if (res < 0)
	{
		DBG_ERR("Failed to copy data to user space");
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
			DBG_INFO("copy data from user space, failed");
			return -1;
		}
	}

	ipio_debug_level = katoi(cmd);

	DBG_INFO("ipio_debug_level = %d", ipio_debug_level);

	return size;
}

static ssize_t ilitek_proc_gesture_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	int res = 0;
	uint32_t len = 0;

	if (*pPos != 0)
		return 0;

	len = sprintf(buff, "%d", core_config->isEnableGesture );

	DBG_INFO("isEnableGesture = %d", core_config->isEnableGesture);

	res = copy_to_user((uint32_t *)buff, &core_config->isEnableGesture, len);
	if (res < 0)
	{
		DBG_ERR("Failed to copy data to user space");
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
			DBG_INFO("copy data from user space, failed");
			return -1;
		}
	}

	DBG_INFO("size = %d, cmd = %s", (int)size, cmd);

	if(strcmp(cmd, "on") == 0)
	{
		DBG_INFO("enable gesture mode");
		core_config->isEnableGesture = true;		
	}
	else if(strcmp(cmd, "off") == 0)
	{
		DBG_INFO("disable gesture mode");
		core_config->isEnableGesture = false;
	}
	else
		DBG_ERR("Unknown command");

	return size;
}

static ssize_t ilitek_proc_check_battery_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	int res = 0;
	uint32_t len = 0;

	if (*pPos != 0)
		return 0;

	len = sprintf(buff, "%d", ipd->isEnablePollCheckPower );

	DBG_INFO("isEnablePollCheckPower = %d", ipd->isEnablePollCheckPower);

	res = copy_to_user((uint32_t *)buff, &ipd->isEnablePollCheckPower, len);
	if (res < 0)
	{
		DBG_ERR("Failed to copy data to user space");
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
			DBG_INFO("copy data from user space, failed");
			return -1;
		}
	}

	DBG_INFO("size = %d, cmd = %s", (int)size, cmd);

	if(strcmp(cmd, "on") == 0)
	{
		DBG_INFO("Start the thread of check power status");
		queue_delayed_work(ipd->check_power_status_queue, &ipd->check_power_status_work, ipd->work_delay);
		ipd->isEnablePollCheckPower = true;
	}
	else if(strcmp(cmd, "off") == 0)
	{
		DBG_INFO("Cancel the thread of check power status");
		cancel_delayed_work_sync(&ipd->check_power_status_work);
		ipd->isEnablePollCheckPower = false;
	}
	else
		DBG_ERR("Unknown command");

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

	DBG_INFO("update status = %d", core_firmware->update_status);

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

	DBG_INFO("Preparing to upgarde firmware");

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
		DBG_ERR("Failed to upgrade firwmare");		
	}
	else
	{
		core_firmware->update_status = 100;
		DBG_INFO("Succeed to upgrade firmware");		
	}

	*pPos = len;

	return len;
}

static ssize_t ilitek_proc_iram_upgrade_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	int res = 0;
	uint32_t len = 0;

	DBG_INFO("Preparing to upgarde firmware by IRAM");

	if (*pPos != 0)
		return 0;

	ilitek_platform_disable_irq();

	res = core_firmware_upgrade(UPDATE_FW_PATH, true);

	ilitek_platform_enable_irq();

	if (res < 0)
	{
		// return the status to user space even if any error occurs.
		core_firmware->update_status = res;
		DBG_ERR("Failed to upgrade firwmare by IRAM, res = %d", res);
	}
	else
	{
		DBG_INFO("Succeed to upgrade firmware by IRAM");
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
			DBG_INFO("copy data from user space, failed");
			return -1;
		}
	}

	DBG_INFO("size = %d, cmd = %d", (int)size, cmd[0]);

	// test
	if(cmd[0] == 0x1)
	{
		DBG_INFO("HW Reset");
		ilitek_platform_tp_hw_reset(true);
	}
	else if(cmd[0] == 0x02)
	{
		DBG_INFO("Disable IRQ");
		ilitek_platform_disable_irq();
	}
	else if(cmd[0] == 0x03)
	{
		DBG_INFO("Enable IRQ");
		ilitek_platform_enable_irq();
	}
	else if(cmd[0] == 0x04)
	{
		DBG_INFO("Get Chip id");
		core_config_get_chip_id();
	}

	*pPos = len;

	return len;
}

// for debug
static ssize_t ilitek_proc_ioctl_write(struct file *filp, const char *buff, size_t size, loff_t *pPos)
{
	int res = 0;
	uint8_t cmd[10] = {0};
	uint8_t func[2] = {0};

	if(buff != NULL)
	{
		res = copy_from_user(cmd, buff, size - 1);
		if(res < 0)
		{
			DBG_INFO("copy data from user space, failed");
			return -1;
		}
	}

	DBG_INFO("size = %d, cmd = %s", (int)size, cmd);

	if(strcmp(cmd, "reset") == 0)
	{
		DBG_INFO("HW Reset");
		ilitek_platform_tp_hw_reset(true);
	}
	else if(strcmp(cmd, "disirq") == 0)
	{
		DBG_INFO("Disable IRQ");
		ilitek_platform_disable_irq();
	}
	else if(strcmp(cmd, "enairq") == 0)
	{
		DBG_INFO("Enable IRQ");
		ilitek_platform_enable_irq();
	}
	else if(strcmp(cmd, "getchip") == 0)
	{
		DBG_INFO("Get Chip id");
		core_config_get_chip_id();
	}
	else if(strcmp(cmd, "dispcc") == 0)
	{
		DBG_INFO("disable phone cover control");
		func[0] = 0x0C;
		func[1] = 0x00;
		core_config_func_ctrl(func);
	}
	else if(strcmp(cmd, "enapcc") == 0)
	{
		DBG_INFO("enable phone cover control");
		func[0] = 0x0C;
		func[1] = 0x01;
		core_config_func_ctrl(func);
	}
	else if(strcmp(cmd, "disfsc") == 0)
	{
		DBG_INFO("disable finger sense control");
		func[0] = 0x0F;
		func[1] = 0x00;
		core_config_func_ctrl(func);
	}
	else if(strcmp(cmd, "enafsc") == 0)
	{
		DBG_INFO("enable finger sense control");
		func[0] = 0x0F;
		func[1] = 0x01;
		core_config_func_ctrl(func);
	}
	else if(strcmp(cmd, "disprox") == 0)
	{
		DBG_INFO("disable proximity function");
		func[0] = 0x10;
		func[1] = 0x00;
		core_config_func_ctrl(func);
	}
	else if(strcmp(cmd, "enaprox") == 0)
	{
		DBG_INFO("enable proximity function");
		func[0] = 0x10;
		func[1] = 0x01;
		core_config_func_ctrl(func);
	}
	else if(strcmp(cmd, "disglove") == 0)
	{
		DBG_INFO("disable glove function");
		func[0] = 0x06;
		func[1] = 0x00;
		core_config_func_ctrl(func);
	}
	else if(strcmp(cmd, "enaglove") == 0)
	{
		DBG_INFO("enable glove function");
		func[0] = 0x06;
		func[1] = 0x01;
		core_config_func_ctrl(func);
	}
	else if(strcmp(cmd, "glovesl") == 0)
	{
		DBG_INFO("set glove as seamless");
		func[0] = 0x06;
		func[1] = 0x02;
		core_config_func_ctrl(func);
	}
	else
	{
		DBG_ERR("Unknown command");
	}
	
	return size;	
}

static long ilitek_proc_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int res = 0, length = 0;
	uint8_t szBuf[512] = {0};
	static uint16_t i2c_rw_length = 0;
	uint32_t id_to_user = 0x0;
	char dbg[10] = {0};

	DBG(DEBUG_IOCTL, "cmd = %d", _IOC_NR(cmd));

	if (_IOC_TYPE(cmd) != ILITEK_IOCTL_MAGIC)
	{
		DBG_ERR("The Magic number doesn't match");
		return -ENOTTY;
	}

	if (_IOC_NR(cmd) > ILITEK_IOCTL_MAXNR)
	{
		DBG_ERR("The number of ioctl doesn't match");
		return -ENOTTY;
	}

	switch (cmd)
	{
	case ILITEK_IOCTL_I2C_WRITE_DATA:
		res = copy_from_user(szBuf, (uint8_t *)arg, i2c_rw_length);
		if (res < 0)
		{
			DBG_ERR("Failed to copy data from user space");
		}
		else
		{
			res = core_i2c_write(core_config->slave_i2c_addr, &szBuf[0], i2c_rw_length);
			if (res < 0)
			{
				DBG_ERR("Failed to write data via i2c");
			}
		}
		break;

	case ILITEK_IOCTL_I2C_READ_DATA:
		res = core_i2c_read(core_config->slave_i2c_addr, szBuf, i2c_rw_length);
		if (res < 0)
		{
			DBG_ERR("Failed to read data via i2c");
		}
		else
		{
			res = copy_to_user((uint8_t *)arg, szBuf, i2c_rw_length);
			if (res < 0)
			{
				DBG_ERR("Failed to copy data to user space");
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
		DBG_INFO("Not implemented yet");
		break;

	case ILITEK_IOCTL_TP_REPORT_SWITCH:
		res = copy_from_user(szBuf, (uint8_t *)arg, 1);
		if (res < 0)
		{
			DBG_ERR("Failed to copy data from user space");
		}
		else
		{
			if (szBuf[0])
			{
				core_fr->isEnableFR = true;
				DBG(DEBUG_IOCTL, "Function of finger report was enabled");
			}
			else
			{
				core_fr->isEnableFR = false;
				DBG(DEBUG_IOCTL, "Function of finger report was disabled");
			}
		}
		break;

	case ILITEK_IOCTL_TP_IRQ_SWITCH:
		res = copy_from_user(szBuf, (uint8_t *)arg, 1);
		if (res < 0)
		{
			DBG_ERR("Failed to copy data from user space");
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
			DBG_ERR("Failed to copy data from user space");
		}
		else
		{
			ipio_debug_level = katoi(dbg);
			DBG_INFO("ipio_debug_level = %d", ipio_debug_level);
		}
		break;

	case ILITEK_IOCTL_TP_FUNC_MODE:
		res = copy_from_user(szBuf, (uint8_t *)arg, 2);
		if (res < 0)
		{
			DBG_ERR("Failed to copy data from user space");
		}
		else
		{
			core_config_func_ctrl(szBuf);
		}
		break;

	case ILITEK_IOCTL_TP_FW_VER:
		res = core_config_get_fw_ver();
		if (res < 0)
		{
			DBG_ERR("Failed to get firmware version");
		}
		else
		{
			res = copy_to_user((uint8_t *)arg, core_config->firmware_ver, fw_cmd_len);
			if (res < 0)
			{
				DBG_ERR("Failed to copy firmware version to user space");
			}
		}
		break;

	case ILITEK_IOCTL_TP_PL_VER:
		res = core_config_get_protocol_ver();
		if (res < 0)
		{
			DBG_ERR("Failed to get protocol version");
		}
		else
		{
			res = copy_to_user((uint8_t *)arg, core_config->protocol_ver, protocol_cmd_len);
			if (res < 0)
			{
				DBG_ERR("Failed to copy protocol version to user space");
			}
		}
		break;

	case ILITEK_IOCTL_TP_CORE_VER:
		res = core_config_get_core_ver();
		if (res < 0)
		{
			DBG_ERR("Failed to get core version");
		}
		else
		{
			res = copy_to_user((uint8_t *)arg, core_config->core_ver, core_cmd_len);
			if (res < 0)
			{
				DBG_ERR("Failed to copy core version to user space");
			}
		}
		break;

	case ILITEK_IOCTL_TP_DRV_VER:
		length = sprintf(szBuf, "%s", DRIVER_VERSION);
		if (!length)
		{
			DBG_ERR("Failed to convert driver version from definiation");
		}
		else
		{
			res = copy_to_user((uint8_t *)arg, szBuf, length);
			if (res < 0)
			{
				DBG_ERR("Failed to copy driver ver to user space");
			}
		}
		break;

	case ILITEK_IOCTL_TP_CHIP_ID:
		res = core_config_get_chip_id();
		if (res < 0)
		{
			DBG_ERR("Failed to get chip id");
		}
		else
		{
			id_to_user = core_config->chip_id << 16 | core_config->chip_type;

			res = copy_to_user((uint32_t *)arg, &id_to_user, sizeof(uint32_t));
			if (res < 0)
			{
				DBG_ERR("Failed to copy chip id to user space");
			}
		}
		break;

	case ILITEK_IOCTL_TP_NETLINK_CTRL:
		res = copy_from_user(szBuf, (uint8_t *)arg, 1);
		if (res < 0)
		{
			DBG_ERR("Failed to copy data from user space");
		}
		else
		{
			if (szBuf[0])
			{
				core_fr->isEnableNetlink = true;
				DBG(DEBUG_IOCTL, "Netlink has been enabled");
			}
			else
			{
				core_fr->isEnableNetlink = false;
				DBG(DEBUG_IOCTL, "Netlink has been disabled");
			}
		}
		break;

	case ILITEK_IOCTL_TP_NETLINK_STATUS:
		DBG(DEBUG_IOCTL, "Netlink is enabled : %d", core_fr->isEnableNetlink);
		res = copy_to_user((int *)arg, &core_fr->isEnableNetlink, sizeof(int));
		if (res < 0)
		{
			DBG_ERR("Failed to copy chip id to user space");
		}
		break;

	case ILITEK_IOCTL_TP_MODE_CTRL:
		res = copy_from_user(szBuf, (uint8_t *)arg, 4);
		if (res < 0)
		{
			DBG_ERR("Failed to copy data from user space");
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
			DBG_ERR("Failed to copy chip id to user space");
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

	DBG(DEBUG_NETLINK, "The size of data being sent to user = %d", msg_size);
	DBG(DEBUG_NETLINK, "pid = %d", pid);
	DBG(DEBUG_NETLINK, "Netlink is enable = %d", core_fr->isEnableNetlink);

	if (core_fr->isEnableNetlink)
	{
		skb_out = nlmsg_new(msg_size, 0);

		if (!skb_out)
		{
			DBG_ERR("Failed to allocate new skb");
			return;
		}

		nlh = nlmsg_put(skb_out, 0, 0, NLMSG_DONE, msg_size, 0);
		NETLINK_CB(skb_out).dst_group = 0; /* not in mcast group */

		//strncpy(NLMSG_DATA(nlh), data, msg_size);
		memcpy(nlmsg_data(nlh), data, msg_size);

		res = nlmsg_unicast(nl_sk, skb_out, pid);
		if (res < 0)
			DBG_ERR("Failed to send data back to user");
	}
}
EXPORT_SYMBOL(netlink_reply_msg);

static void netlink_recv_msg(struct sk_buff *skb)
{
	pid = 0;

	DBG(DEBUG_NETLINK, "Netlink is enable = %d", core_fr->isEnableNetlink);

	nlh = (struct nlmsghdr *)skb->data;

	DBG(DEBUG_NETLINK, "Received a request from client: %s, %d",
		(char *)NLMSG_DATA(nlh), (int)strlen((char *)NLMSG_DATA(nlh)));

	/* pid of sending process */
	pid = nlh->nlmsg_pid;

	DBG(DEBUG_NETLINK, "the pid of sending process = %d", pid);

	/* TODO: may do something if there's not receiving msg from user. */
	if (pid != 0)
	{
		DBG_ERR("The channel of Netlink has been established successfully !");
		core_fr->isEnableNetlink = true;
	}
	else
	{
		DBG_ERR("Failed to establish the channel between kernel and user space");
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

	DBG_INFO("Initialise Netlink and create its socket");

	if (!nl_sk)
	{
		DBG_ERR("Failed to create nelink socket");
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
			DBG_ERR("Failed to create %s under /proc", proc_table[i].name);
			res = -ENODEV;
		}
		else
		{
			proc_table[i].isCreated = true;
			DBG_INFO("Succeed to create %s under /proc", proc_table[i].name);
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
			DBG_INFO("Removed %s under /proc", proc_table[i].name);
			remove_proc_entry(proc_table[i].name, proc_dir_ilitek);
		}
	}

	remove_proc_entry("ilitek", NULL);
	netlink_kernel_release(nl_sk);
}
EXPORT_SYMBOL(ilitek_proc_remove);