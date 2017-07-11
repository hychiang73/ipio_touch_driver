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

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/proc_fs.h>

#include <linux/netlink.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <net/sock.h>

#include "chip.h"
#include "platform.h"

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

static ssize_t ilitek_proc_fw_process_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	int res = 0;
	uint32_t len = 0;

	// If file position is non-zero,  we assume the string has been read 
	// and indicates that there is no more data to be read.
	if(*pPos != 0)
	{
		return 0;
	}

	len = sprintf(buff, "%02d", core_firmware->update_status);

	DBG("update status = %d \n", core_firmware->update_status);

	res = copy_to_user((uint32_t*)buff, &core_firmware->update_status, len);
	if(res < 0)
	{
		DBG_ERR("Failed to copy data to user space");
	}

	*pPos = len;

	return len;
}

static ssize_t ilitek_proc_fw_status_read(struct file *filp, char __user *buff, size_t size, loff_t *pPos)
{
	int res = 0;
	uint32_t len = 0;

	// If file position is non-zero,  we assume the string has been read 
	// and indicates that there is no more data to be read.
	if(*pPos != 0)
	{
		return 0;
	}

	len = sprintf(buff, "%d", core_firmware->isUpgraded);

	DBG("isUpgraded = %d \n", core_firmware->isUpgraded);

	res = copy_to_user((uint32_t*)buff, &core_firmware->isUpgraded, len);
	if(res < 0)
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

	if(*pPos != 0)
	{
		return 0;
	}

	ilitek_platform_disable_irq();

	res = core_firmware_upgrade(UPDATE_FW_PATH, false);

	ilitek_platform_enable_irq();

	if(res < 0)
	{
		// return the status to user space even if any error occurs.
		core_firmware->update_status = res;
		DBG_ERR("Failed to upgrade firwmare, res = %d", res);
	}
	else
	{
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

	if(*pPos != 0)
	{
		return 0;
	}

	ilitek_platform_disable_irq();

	res = core_firmware_upgrade(UPDATE_FW_PATH, true);

	ilitek_platform_enable_irq();

	if(res < 0)
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

static long ilitek_proc_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	int res = 0, length = 0;
	uint8_t	 szBuf[512] = {0};
	static uint16_t i2c_rw_length = 0;
	uint32_t id_to_user = 0x0;

	DBG("cmd = %d", _IOC_NR(cmd));

	if(_IOC_TYPE(cmd) != ILITEK_IOCTL_MAGIC)
	{
		DBG_ERR("The Magic number doesn't match");
		return -ENOTTY;
	}

	if(_IOC_NR(cmd) > ILITEK_IOCTL_MAXNR)
	{
		DBG_ERR("The number of ioctl doesn't match");
		return -ENOTTY;
	}

	switch(cmd)
	{
		case ILITEK_IOCTL_I2C_WRITE_DATA:
			res = copy_from_user(szBuf, (uint8_t*)arg, i2c_rw_length);
			if(res < 0)
			{
				DBG_ERR("Failed to copy data from user space");
			}
			else
			{
				res = core_i2c_write(core_config->slave_i2c_addr, &szBuf[0], i2c_rw_length);
				if(res < 0)
				{
					DBG_ERR("Failed to write data via i2c");
				}
			}
			break;

		case ILITEK_IOCTL_I2C_READ_DATA:
			res = core_i2c_read(core_config->slave_i2c_addr, szBuf, i2c_rw_length);
			if(res < 0)
			{
				DBG_INFO("Failed to read data via i2c");
			}
			else
			{
				res = copy_to_user((uint8_t*)arg, szBuf, i2c_rw_length);
				if(res < 0)
				{
					DBG_INFO("Failed to copy data to user space");
				}
			}
			break;

		case ILITEK_IOCTL_I2C_SET_WRITE_LENGTH:
		case ILITEK_IOCTL_I2C_SET_READ_LENGTH:
			i2c_rw_length = arg;
			break;

		case ILITEK_IOCTL_TP_HW_RESET:
			ilitek_platform_tp_power_on(true);
			break;

		case ILITEK_IOCTL_TP_POWER_SWITCH:
			DBG_INFO("Not implemented yet");
			break;

		case ILITEK_IOCTL_TP_REPORT_SWITCH:
			res = copy_from_user(szBuf, (uint8_t*)arg, 1);
			if(res < 0)
			{
				DBG_ERR("Failed to copy data from user space");
			}
			else
			{
				if(szBuf[0])
				{
					core_fr->isEnableFR = true;
					DBG_INFO("Function of finger report was enabled");
				}
				else
				{
					core_fr->isEnableFR = false;
					DBG_INFO("Function of finger report was disabled");
				}
			}
			break;

		case ILITEK_IOCTL_TP_IRQ_SWITCH:
			res = copy_from_user(szBuf, (uint8_t*)arg, 1);
			if(res < 0)
			{
				DBG_ERR("Failed to copy data from user space");
			}
			else
			{
				if(szBuf[0])
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
			DBG_INFO("Not implemented yet");
			break;

		case ILITEK_IOCTL_TP_FUNC_MODE:
			DBG_INFO("Not implemented yet");
			break;

		case ILITEK_IOCTL_TP_FW_VER:
			res = core_config_get_fw_ver();
			if(res < 0)
			{
				DBG_ERR("Failed to get firmware version");
			}
			else
			{
				res = copy_to_user((uint8_t*)arg, core_config->firmware_ver, fw_cmd_len);
				if(res < 0)
				{
					DBG_ERR("Failed to copy firmware version to user space");
				}
			}
			break;

		case ILITEK_IOCTL_TP_PL_VER:
			res = core_config_get_protocol_ver();
			if(res < 0)
			{
				DBG_ERR("Failed to get protocol version");
			}
			else
			{
				res = copy_to_user((uint8_t*)arg, core_config->protocol_ver, protocol_cmd_len);
				if(res < 0)
				{
					DBG_ERR("Failed to copy protocol version to user space");
				}
			}
			break;

		case ILITEK_IOCTL_TP_CORE_VER:
			res = core_config_get_core_ver();
			if(res < 0)
			{
				DBG_ERR("Failed to get core version");
			}
			else
			{
				res = copy_to_user((uint8_t*)arg, core_config->core_ver, core_cmd_len);
				if(res < 0)
				{
					DBG_ERR("Failed to copy core version to user space");
				}
			}
			break;

		case ILITEK_IOCTL_TP_DRV_VER:
			length = sprintf(szBuf, "%s", DRIVER_VERSION);
			if(!length)
			{
				DBG_ERR("Failed to convert driver version from definiation");
			}
			else
			{
				res = copy_to_user((uint8_t*)arg, szBuf, length);
				if(res < 0)
				{
					DBG_INFO("Failed to copy driver ver to user space");
				}
			}
			break;

		case ILITEK_IOCTL_TP_CHIP_ID:
			res = core_config_get_chip_id();
			if(res < 0)
			{
				DBG_ERR("Failed to get chip id");
			}
			else
			{
				id_to_user = core_config->chip_id << 16 | core_config->chip_type;

				res = copy_to_user((uint32_t*)arg, &id_to_user, sizeof(uint32_t));
				if(res < 0)
				{
					DBG_ERR("Failed to copy chip id to user space");
				}
			}
			break;

		case ILITEK_IOCTL_TP_NETLINK_CTRL:
			res = copy_from_user(szBuf, (uint8_t*)arg, 1);
			if(res < 0)
			{
				DBG_ERR("Failed to copy data from user space");
			}
			else
			{
				if(szBuf[0])
				{
					core_fr->isEnableNetlink = true;
					DBG_INFO("Netlink has been enabled");
				}
				else
				{
					core_fr->isEnableNetlink = false;
					DBG_INFO("Netlink has been disabled");
				}
			}
			break;

		case ILITEK_IOCTL_TP_NETLINK_STATUS:
			DBG("Check if Netlink is enabled : %d", core_fr->isEnableNetlink);
			res = copy_to_user((int*)arg, &core_fr->isEnableNetlink, sizeof(int));
			if(res < 0)
			{
				DBG_ERR("Failed to copy chip id to user space");
			}
			break;

		case ILITEK_IOCTL_TP_MODE_CTRL:
			res = copy_from_user(szBuf, (uint8_t*)arg, 3);
			if(res < 0)
			{
				DBG_ERR("Failed to copy data from user space");
			}
			else
			{
				core_fr_mode_control(szBuf);
			}
			break;

		case ILITEK_IOCTL_TP_MODE_STATUS:
			DBG("Current firmware mode : %d", core_fr->actual_fw_mode);
			res = copy_to_user((int*)arg, &core_fr->actual_fw_mode, sizeof(int));
			if(res < 0)
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
struct proc_dir_entry *proc_fw_status;
struct proc_dir_entry *proc_fw_process;
struct proc_dir_entry *proc_fw_upgrade;
struct proc_dir_entry *proc_iram_upgrade;

struct file_operations proc_ioctl_fops = {
	.unlocked_ioctl = ilitek_proc_ioctl,
};

struct file_operations proc_fw_status_fops = {
	.read  = ilitek_proc_fw_status_read,
};

struct file_operations proc_fw_process_fops = {
	.read  = ilitek_proc_fw_process_read,
};

struct file_operations proc_fw_upgrade_fops = {
	.read = ilitek_proc_fw_upgrade_read,
};

struct file_operations proc_iram_upgrade_fops = {
	.read = ilitek_proc_iram_upgrade_read,
};

/*
 * This struct lists all file nodes will be created under /proc filesystem.
 *
 * Before creating a node that you want, declaring its file_operations structure
 * is necessary. After that, puts the structure into proc_table, defines its
 * node's name in the same row, and the init function lterates the table and
 * creates all nodes under /proc.
 *
 */
typedef struct {

	// node's name.
	char *name;

	// point to a node created by proc_create.
	struct proc_dir_entry *node;

	// point to a fops declard the above already.
	struct file_operations *fops;

	// indicate if a node is created.
	bool isCreated;
	
} proc_node_t;

proc_node_t proc_table[] = {
	{"ioctl",	 NULL,	&proc_ioctl_fops,	false},
	{"fw_status",NULL,	&proc_fw_status_fops,false},
	{"fw_process",NULL, &proc_fw_process_fops,false},
	{"fw_upgrade",NULL, &proc_fw_upgrade_fops,false},
	{"iram_upgrade",NULL, &proc_iram_upgrade_fops,false},
};


#define NETLINK_USER	31

struct sock *nl_sk;
struct nlmsghdr *nlh;
struct sk_buff *skb_out;

int pid;

void netlink_reply_msg(void *raw, int size)
{
	int res;
	int msg_size = size;
	uint8_t *data = (uint8_t *)raw;

	DBG_INFO("The size of data being sent to user = %d", msg_size);
	DBG_INFO("pid = %d", pid);
	DBG_INFO("Netlink is enable = %d", core_fr->isEnableNetlink);

	if(core_fr->isEnableNetlink == true)
	{
		if(skb_out != NULL)
			skb_out = nlmsg_new(msg_size, 0);

		if (!skb_out) 
		{
			DBG_INFO("Failed to allocate new skb");
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

	DBG_INFO("Netlink is enable = %d", core_fr->isEnableNetlink);

	if(core_fr->isEnableNetlink)
	{
		nlh = (struct nlmsghdr *)skb->data;

		DBG("Received a request from client: %s, %d",
		(char *)NLMSG_DATA(nlh), strlen((char *)NLMSG_DATA(nlh)));

		// pid of sending process
		pid = nlh->nlmsg_pid; 

		DBG_INFO("the pid of sending process = %d", pid);

		// TODO: may do something if there's not receiving msg from user.
		if(pid != 0)
		{
			DBG_ERR("The channel of Netlink has been established successfully !");
		}
		else
		{
			DBG_ERR("Failed to establish the channel between kernel and user space");
		}
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

	if(!nl_sk)
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

	for(; i < ARRAY_SIZE(proc_table); i++)
	{
		proc_table[i].node = proc_create(proc_table[i].name, 0666, proc_dir_ilitek, proc_table[i].fops);

		if(proc_table[i].node == NULL)
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

	for(; i < ARRAY_SIZE(proc_table); i++)
	{
		if(proc_table[i].isCreated == true)
		{
			DBG_INFO("Removed %s under /proc", proc_table[i].name);
			remove_proc_entry(proc_table[i].name, proc_dir_ilitek);
		}
	}

	remove_proc_entry("ilitek", NULL);
	netlink_kernel_release(nl_sk);
}
EXPORT_SYMBOL(ilitek_proc_remove);