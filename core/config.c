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
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/i2c.h>

#ifdef CONFIG_OF
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#endif

#include "../common.h"
#include "../platform.h"
#include "config.h"
#include "i2c.h"
#include "flash.h"

extern uint32_t SUP_CHIP_LIST[];
extern int nums_chip;

// the length returned from touch ic after command.
int fw_cmd_len = 0;
int protocol_cmd_len = 0;
int tp_info_len = 0;
int key_info_len = 0;
int core_cmd_len = 0;

// protocol commands defined on chip.h
// it's able to store 10 commands as default.
uint8_t pcmd[10] = {0};

struct core_config_data *core_config = NULL;

/*
 * assign i2c commands according to the version of protocol.
 *
 * @protocol_ver: the version is currently using on firmware.
 */
static void set_protocol_cmd(uint32_t protocol_ver)
{
	if (protocol_ver == ILITEK_PROTOCOL_V3_2)
	{
		fw_cmd_len = 4;
		protocol_cmd_len = 4;
		tp_info_len = 10;
		key_info_len = 29;

		pcmd[0] = PCMD_3_2_GET_TP_INFORMATION;
		pcmd[1] = PCMD_3_2_GET_FIRMWARE_VERSION;
		pcmd[2] = PCMD_3_2_GET_PROTOCOL_VERSION;
		pcmd[3] = PCMD_3_2_GET_KEY_INFORMATION;
	}
	else if (protocol_ver == ILITEK_PROTOCOL_V5_0)
	{
		fw_cmd_len = 4;
		protocol_cmd_len = 3;
		tp_info_len = 14;
		key_info_len = 30;
		core_cmd_len = 5;

		pcmd[0] = PCMD_5_0_READ_DATA_CTRL;
		pcmd[1] = PCMD_5_0_GET_TP_INFORMATION;
		pcmd[2] = PCMD_5_0_GET_FIRMWARE_VERSION;
		pcmd[3] = PCMD_5_0_GET_PROTOCOL_VERSION;
		pcmd[4] = PCMD_5_0_GET_KEY_INFORMATION;
		pcmd[5] = PCMD_5_0_GET_CORE_VERSION;
		pcmd[6] = PCMD_5_0_MODE_CONTROL;
		pcmd[7] = PCMD_5_0_I2C_UART;
		pcmd[8] = PCMD_5_0_SLEEP_CONTROL;
		pcmd[9] = PCMD_5_0_CDC_BUSY_STATE;
	}
}

static void read_flash_info(uint8_t cmd, int len)
{
	int i;
	uint16_t flash_id = 0, flash_mid = 0;
	uint8_t buf[4] = {0};

	// This command is used to fix the bug of spi clk in 7807F-AB
	// when operating with flash.
	if (core_config->chip_id == CHIP_TYPE_ILI7807 
			&& core_config->chip_type == ILI7807_TYPE_F_AB)
	{
		core_config_ice_mode_write(0x4100C, 0x01, 1);
		mdelay(25);
	}

	core_config_ice_mode_write(0x41000, 0x0, 1);// CS LOW
	core_config_ice_mode_write(0x41004, 0x66aa55, 3);
	core_config_ice_mode_write(0x41008, cmd, 1);

	for(i = 0; i < len; i++)
	{
		core_config_ice_mode_write(0x041008, 0xFF, 1);
		buf[i] = core_config_ice_mode_read(0x41010);
	}

	core_config_ice_mode_write(0x041000, 0x1, 1);// CS High

	// look up flash info and init its struct after obtained flash id.
	flash_mid = buf[0];
	flash_id = buf[1] << 8 | buf[2];
	core_flash_init(flash_mid, flash_id);
}

/*
 * It checks chip id shifting sepcific bits based on chip's requirement.
 *
 * @pid_data: 4 bytes, reading from firmware.
 *
 */
static uint32_t check_chip_id(uint32_t pid_data)
{
	uint32_t id = 0;

	if (core_config->chip_id == CHIP_TYPE_ILI7807)
	{
		id = pid_data >> 16;
		core_config->chip_type = pid_data & 0x0000FFFF;

		if (core_config->chip_type == ILI7807_TYPE_F_AB)
		{
			core_config->ic_reset_addr = 0x04004C;
		}
		else if (core_config->chip_type == ILI7807_TYPE_H)
		{
			core_config->ic_reset_addr = 0x040050;
		}
	}
	else if (core_config->chip_id == CHIP_TYPE_ILI9881)
	{
		id = pid_data >> 16;
		core_config->ic_reset_addr = 0x040050;
	}
	else
	{
		DBG_ERR("The Chip isn't supported by the driver");
	}

	return id;
}

/*
 * Read & Write one byte in ICE Mode.
 */
uint32_t core_config_read_write_onebyte(uint32_t addr)
{
	int res = 0;
	uint32_t data = 0;
	uint8_t szOutBuf[64] = {0};

	szOutBuf[0] = 0x25;
	szOutBuf[1] = (char)((addr & 0x000000FF) >> 0);
	szOutBuf[2] = (char)((addr & 0x0000FF00) >> 8);
	szOutBuf[3] = (char)((addr & 0x00FF0000) >> 16);

	res = core_i2c_write(core_config->slave_i2c_addr, szOutBuf, 4);
	if (res < 0)
		goto out;

	mdelay(1);

	res = core_i2c_read(core_config->slave_i2c_addr, szOutBuf, 1);
	if (res < 0)
		goto out;

	data = (szOutBuf[0]);

	return data;

out:
	DBG_ERR("Failed to read/write data in ICE mode, res = %d", res);
	return res;
}
EXPORT_SYMBOL(core_config_read_write_onebyte);

uint32_t core_config_ice_mode_read(uint32_t addr)
{
	int res = 0;
	uint8_t szOutBuf[64] = {0};
	uint32_t data = 0;

	szOutBuf[0] = 0x25;
	szOutBuf[1] = (char)((addr & 0x000000FF) >> 0);
	szOutBuf[2] = (char)((addr & 0x0000FF00) >> 8);
	szOutBuf[3] = (char)((addr & 0x00FF0000) >> 16);

	res = core_i2c_write(core_config->slave_i2c_addr, szOutBuf, 4);
	if (res < 0)
		goto out;

	mdelay(10);

	res = core_i2c_read(core_config->slave_i2c_addr, szOutBuf, 4);
	if (res < 0)
		goto out;

	data = (szOutBuf[0] + szOutBuf[1] * 256 + szOutBuf[2] * 256 * 256 + szOutBuf[3] * 256 * 256 * 256);

	return data;

out:
	DBG_ERR("Failed to read data in ICE mode, res = %d", res);
	return res;
}
EXPORT_SYMBOL(core_config_ice_mode_read);

/*
 * Write commands into firmware in ICE Mode.
 *
 */
int core_config_ice_mode_write(uint32_t addr, uint32_t data, uint32_t size)
{
	int res = 0, i;
	uint8_t szOutBuf[64] = {0};

	szOutBuf[0] = 0x25;
	szOutBuf[1] = (char)((addr & 0x000000FF) >> 0);
	szOutBuf[2] = (char)((addr & 0x0000FF00) >> 8);
	szOutBuf[3] = (char)((addr & 0x00FF0000) >> 16);

	for (i = 0; i < size; i++)
	{
		szOutBuf[i + 4] = (char)(data >> (8 * i));
	}

	res = core_i2c_write(core_config->slave_i2c_addr, szOutBuf, size + 4);

	if (res < 0)
		DBG_ERR("Failed to write data in ICE mode, res = %d", res);

	return res;
}
EXPORT_SYMBOL(core_config_ice_mode_write);

uint32_t vfIceRegRead(uint32_t addr)
{
	int i, nInTimeCount = 100;
	uint8_t szBuf[4] = {0};

	core_config_ice_mode_write(0x41000, 0x3B | (addr << 8), 4);
	core_config_ice_mode_write(0x041004, 0x66AA5500, 4);

	// Check Flag
	// Check Busy Flag
	for (i = 0; i < nInTimeCount; i++)
	{
		szBuf[0] = core_config_read_write_onebyte(0x41011);

		if ((szBuf[0] & 0x01) == 0)
		{
			break;
		}
		mdelay(5);
	}

	return core_config_read_write_onebyte(0x41012);
}
EXPORT_SYMBOL(vfIceRegRead);

/*
 * Doing soft reset on ic.
 *
 * It resets ic's status, moves code and leave ice mode automatically if in
 * that mode.
 */
void core_config_ic_reset(void)
{
	uint32_t key = 0;

	if (core_config->chip_id == CHIP_TYPE_ILI7807)
	{
		if(core_config->chip_type == ILI7807_TYPE_H)
			key = 0x00117807;
		else
			key = 0x00017807;
	}
	if (core_config->chip_id == CHIP_TYPE_ILI9881)
	{
		key = 0x00019881;
	}

	DBG_INFO("key = 0x%x", key);
	if(key != 0)
	{
		core_config->do_ic_reset = true;
		core_config_ice_mode_write(core_config->ic_reset_addr, key, 4);
		core_config->do_ic_reset = false;
	}
}
EXPORT_SYMBOL(core_config_ic_reset);

void core_config_sense_ctrl(bool start)
{
	uint8_t sense_start[3] = {0x1, 0x1, 0x1};
	uint8_t sense_stop[3] = {0x1, 0x1, 0x0};

	DBG_INFO("sense start = %d", start);

	if(start)
	{
		/* sense start for TP */
		core_i2c_write(core_config->slave_i2c_addr, sense_start, 3);
	}
	else
	{
		/* sense stop for TP */
		core_i2c_write(core_config->slave_i2c_addr, sense_stop, 3);
	}

	/* check system busy */
	if(core_config_check_cdc_busy() < 0)
		DBG_ERR("Check busy is timout !");
}
EXPORT_SYMBOL(core_config_sense_ctrl);

void core_config_ic_suspend(void)
{
	uint8_t cmd[2] = {0x02, 0x0};

	DBG_INFO("Tell IC to suspend");
	
	core_config_func_ctrl(cmd);
}
EXPORT_SYMBOL(core_config_ic_suspend);

void core_config_ic_resume(void)
{
	uint8_t cmd[2] = {0x02, 0x01};

	DBG_INFO("Tell IC to resume");

	core_config_func_ctrl(cmd);

	// it's better to do reset after resuem.
	core_config_ice_mode_enable();
	mdelay(10);
	core_config_ic_reset();
}
EXPORT_SYMBOL(core_config_ic_resume);

int core_config_ice_mode_disable(void)
{
	uint8_t cmd[4];

	cmd[0] = 0x1b;
	cmd[1] = 0x62;
	cmd[2] = 0x10;
	cmd[3] = 0x18;

	DBG("ICE Mode disabled")

	return core_i2c_write(core_config->slave_i2c_addr, cmd, 4);
}
EXPORT_SYMBOL(core_config_ice_mode_disable);

int core_config_ice_mode_enable(void)
{
	DBG("ICE Mode enabled");
	return core_config_ice_mode_write(0x181062, 0x0, 0);
}
EXPORT_SYMBOL(core_config_ice_mode_enable);

int core_config_reset_watch_dog(void)
{
	 if (core_config->chip_id == CHIP_TYPE_ILI7807)
	{
		core_config_ice_mode_write(0x5100C, 0x7, 1);
		core_config_ice_mode_write(0x5100C, 0x78, 1);
	}

	return 0;
}
EXPORT_SYMBOL(core_config_reset_watch_dog);

int core_config_check_cdc_busy(void)
{
	int timer = 50, res = -1;
    uint8_t cmd[2] = {0};
	uint8_t busy = 0;

    cmd[0] = pcmd[0];
    cmd[1] = pcmd[9];

	while(timer > 0)
	{
		core_i2c_write(core_config->slave_i2c_addr, cmd, 2);
		mdelay(1);
		core_i2c_write(core_config->slave_i2c_addr, &cmd[1], 1);
		mdelay(10);
		core_i2c_read(core_config->slave_i2c_addr, &busy, 1);
		DBG("CDC busy state = 0x%x", busy);
		if(busy == 0x41)
		{
			res = 0;
			break;
		}
		timer--;
	}

	return res;
}
EXPORT_SYMBOL(core_config_check_cdc_busy);

void core_config_func_ctrl(uint8_t *buf)
{
	int len = 3;
	uint8_t cmd[3] = {0};

	cmd[0] = 0x1;
	cmd[1] = buf[0];
	cmd[2] = buf[1];

	DBG_INFO("func = %x , ctrl = %x", cmd[1], cmd[2]);

	switch(cmd[1])
	{
		case 0x2:
			if(cmd[2] == 0x0)
			{
				DBG_INFO("Sleep IN ... Gesture = %d", core_config->isEnableGesture);
				if(core_config->isEnableGesture)
				{
					/* LPWG Ctrl */
					cmd[1] = 0x0A;
					cmd[2] = 0x01;
					DBG_INFO("cmd = 0x%x, 0x%x, 0x%x", cmd[0], cmd[1], cmd[2]);
					core_i2c_write(core_config->slave_i2c_addr, cmd, len);
				}
				else
				{
					/* sense stop */
					core_config_sense_ctrl(false);

					/* sleep in */
					core_i2c_write(core_config->slave_i2c_addr, cmd, len);
				}
			}
			else
			{
				DBG_INFO("Sleep OUT ...");
			
				/* sleep out */
				core_i2c_write(core_config->slave_i2c_addr, cmd, len);
	
				/* sense start for TP */
				core_config_sense_ctrl(true);
			}
			break;

		default:
			core_i2c_write(core_config->slave_i2c_addr, cmd, len);
			mdelay(1);
			break;
	}
}
EXPORT_SYMBOL(core_config_func_ctrl);

int core_config_get_key_info(void)
{
	int res = 0, i;
	uint8_t cmd[2] = {0};
	uint8_t szReadBuf[key_info_len];

	memset(szReadBuf, 0, sizeof(szReadBuf));

	if(core_config->use_protocol == ILITEK_PROTOCOL_V5_0)
	{
		cmd[0] = pcmd[0];
		cmd[1] = pcmd[4];

		res = core_i2c_write(core_config->slave_i2c_addr, cmd, 2);
		if (res < 0)
		{
			DBG_ERR("Failed to write data via I2C, %d", res);
			goto out;
		}

		mdelay(1);

		res = core_i2c_write(core_config->slave_i2c_addr, &cmd[1], 1);
		if (res < 0)
		{
			DBG_ERR("Failed to write data via I2C, %d", res);
			goto out;
		}

		mdelay(1);

		res = core_i2c_read(core_config->slave_i2c_addr, &szReadBuf[0], key_info_len);
		if (res < 0)
		{
			DBG_ERR("Failed to read data via I2C, %d", res);
			goto out;
		}

		for (i = 0; i < key_info_len; i++)
			DBG("key_info[%d] = %x", i, szReadBuf[i]);

		if (core_config->tp_info->nKeyCount)
		{
			//TODO: Firmware not ready yet
			#if 0
			core_config->tp_info->nKeyAreaXLength = (szReadBuf[0] << 8) + szReadBuf[1];
			core_config->tp_info->nKeyAreaYLength = (szReadBuf[2] << 8) + szReadBuf[3];

			for (i = 0; i < core_config->tp_info->nKeyCount; i ++)
			{
				core_config->tp_info->virtual_key[i].nId = szReadBuf[i*5+4];
				core_config->tp_info->virtual_key[i].nX = (szReadBuf[i*5+5] << 8) + szReadBuf[i*5+6];
				core_config->tp_info->virtual_key[i].nY = (szReadBuf[i*5+7] << 8) + szReadBuf[i*5+8];
				core_config->tp_info->virtual_key[i].nStatus = 0;
			}
			#endif
		}
	}

	return res;

out:
	return res;
}
EXPORT_SYMBOL(core_config_get_key_info);

int core_config_get_tp_info(void)
{
	int res = 0, i = 0;
	uint8_t cmd[2] = {0};
	uint8_t szReadBuf[tp_info_len];

	memset(szReadBuf, 0, sizeof(szReadBuf));

	if(core_config->use_protocol == ILITEK_PROTOCOL_V5_0)
	{
		cmd[0] = pcmd[0];
		cmd[1] = pcmd[1];
	
		res = core_i2c_write(core_config->slave_i2c_addr, cmd, 2);
		if (res < 0)
		{
			DBG_ERR("Failed to write data via I2C, %d", res);
			goto out;
		}

		mdelay(1);

		res = core_i2c_write(core_config->slave_i2c_addr, &cmd[1], 1);
		if (res < 0)
		{
			DBG_ERR("Failed to write data via I2C, %d", res);
			goto out;
		}

		mdelay(1);

		res = core_i2c_read(core_config->slave_i2c_addr, &szReadBuf[0], tp_info_len);
		if (res < 0)
		{
			DBG_ERR("Failed to read data via I2C, %d", res);
			goto out;
		}

		for (; i < tp_info_len; i++)
			DBG("tp_info[%d] = %x", i, szReadBuf[i]);

		// in protocol v5, ignore the first btye because of a header.
		core_config->tp_info->nMinX = szReadBuf[1];
		core_config->tp_info->nMinY = szReadBuf[2];
		core_config->tp_info->nMaxX = (szReadBuf[4] << 8) + szReadBuf[3];
		core_config->tp_info->nMaxY = (szReadBuf[6] << 8) + szReadBuf[5];
		core_config->tp_info->nXChannelNum = szReadBuf[7];
		core_config->tp_info->nYChannelNum = szReadBuf[8];
		core_config->tp_info->self_tx_channel_num = szReadBuf[11];
		core_config->tp_info->self_rx_channel_num = szReadBuf[12];
		core_config->tp_info->side_touch_type = szReadBuf[13];
		core_config->tp_info->nMaxTouchNum = szReadBuf[9];
		core_config->tp_info->nKeyCount = szReadBuf[10];

		core_config->tp_info->nMaxKeyButtonNum = 5;

		DBG_INFO("minX = %d, minY = %d, maxX = %d, maxY = %d",
				 core_config->tp_info->nMinX, core_config->tp_info->nMinY,
				 core_config->tp_info->nMaxX, core_config->tp_info->nMaxY);
		DBG_INFO("xchannel = %d, ychannel = %d, self_tx = %d, self_rx = %d",
				 core_config->tp_info->nXChannelNum, core_config->tp_info->nYChannelNum,
				 core_config->tp_info->self_tx_channel_num, core_config->tp_info->self_rx_channel_num);
		DBG_INFO("side_touch_type = %d, max_touch_num= %d, touch_key_num = %d, max_key_num = %d",
				 core_config->tp_info->side_touch_type, core_config->tp_info->nMaxTouchNum,
				 core_config->tp_info->nKeyCount, core_config->tp_info->nMaxKeyButtonNum);
	}

	return res;

out:
	return res;
}
EXPORT_SYMBOL(core_config_get_tp_info);

int core_config_get_protocol_ver(void)
{
	int res = 0, i = 0;
	uint8_t cmd[2] = {0};
	uint8_t szReadBuf[protocol_cmd_len];

	memset(szReadBuf, 0, sizeof(szReadBuf));

	if(core_config->use_protocol == ILITEK_PROTOCOL_V5_0)
	{
		cmd[0] = pcmd[0];
		cmd[1] = pcmd[3];
	
		res = core_i2c_write(core_config->slave_i2c_addr, cmd, 2);
		if (res < 0)
		{
			DBG_ERR("Failed to write data via I2C, %d", res);
			goto out;
		}

		mdelay(1);

		res = core_i2c_write(core_config->slave_i2c_addr, &cmd[1], 1);
		if (res < 0)
		{
			DBG_ERR("Failed to write data via I2C, %d", res);
			goto out;
		}

		mdelay(1);

		res = core_i2c_read(core_config->slave_i2c_addr, &szReadBuf[0], protocol_cmd_len);
		if (res < 0)
		{
			DBG_ERR("Failed to read data via I2C, %d", res);
			goto out;
		}

		for (; i < protocol_cmd_len; i++)
		{
			core_config->protocol_ver[i] = szReadBuf[i];
			DBG("protocol_ver[%d] = %d", i, szReadBuf[i]);
		}

		// in protocol v5, ignore the first btye because of a header.
		DBG_INFO("Procotol Version = %d.%d",
				core_config->protocol_ver[1],
				core_config->protocol_ver[2]);
	}

	return res;

out:
	return res;
}
EXPORT_SYMBOL(core_config_get_protocol_ver);

int core_config_get_core_ver(void)
{
	int res = 0, i = 0;
	uint8_t cmd[2] = {0};
	uint8_t szReadBuf[core_cmd_len];

	memset(szReadBuf, 0, sizeof(szReadBuf));

	if (core_config->use_protocol == ILITEK_PROTOCOL_V5_0)
	{
		cmd[0] = pcmd[0];
		cmd[1] = pcmd[5];
 
		res = core_i2c_write(core_config->slave_i2c_addr, cmd, 2);
		if (res < 0)
		{
			DBG_ERR("Failed to write data via I2C, %d", res);
			goto out;
		}

		mdelay(1);

		res = core_i2c_write(core_config->slave_i2c_addr, &cmd[1], 1);
		if (res < 0)
		{
			DBG_ERR("Failed to write data via I2C, %d", res);
			goto out;
		}

		mdelay(1);

		res = core_i2c_read(core_config->slave_i2c_addr, &szReadBuf[0], core_cmd_len);
		if (res < 0)
		{
			DBG_ERR("Failed to read data via I2C, %d", res);
			goto out;
		}

		for (; i < core_cmd_len; i++)
		{
			core_config->core_ver[i] = szReadBuf[i];
			DBG("core_ver[%d] = %d", i, szReadBuf[i]);
		}

		// in protocol v5, ignore the first btye because of a header.
		DBG_INFO("Core Version = %d.%d.%d.%d",
				core_config->core_ver[1],
				core_config->core_ver[2],
				core_config->core_ver[3],
				core_config->core_ver[4]);
	}

	return res;

out:
	return res;
}
EXPORT_SYMBOL(core_config_get_core_ver);

/*
 * Getting the version of firmware used on the current one.
 *
 */
int core_config_get_fw_ver(void)
{
	int res = 0, i = 0;
	uint8_t cmd[2] = {0};
	uint8_t szReadBuf[fw_cmd_len];

	memset(szReadBuf, 0, sizeof(szReadBuf));

	if(core_config->use_protocol == ILITEK_PROTOCOL_V5_0)
	{
		cmd[0] = pcmd[0];
		cmd[1] = pcmd[2];

		res = core_i2c_write(core_config->slave_i2c_addr, cmd, 2);
		if (res < 0)
		{
			DBG_ERR("Failed to write data via I2C, %d", res);
			goto out;
		}

		mdelay(1);

		res = core_i2c_write(core_config->slave_i2c_addr, &cmd[1], 1);
		if (res < 0)
		{
			DBG_ERR("Failed to write data via I2C, %d", res);
			goto out;
		}

		mdelay(1);

		res = core_i2c_read(core_config->slave_i2c_addr, &szReadBuf[0], fw_cmd_len);
		if (res < 0)
		{
			DBG_ERR("Failed to read fw version %d", res);
			goto out;
		}

		for (; i < fw_cmd_len; i++)
		{
			core_config->firmware_ver[i] = szReadBuf[i];
			DBG("firmware_ver[%d] = %d", i, szReadBuf[i]);
		}
		// in protocol v5, ignore the first btye because of a header.
		DBG_INFO("Firmware Version = %d.%d.%d",
				 core_config->firmware_ver[1],
				 core_config->firmware_ver[2],
				 core_config->firmware_ver[3]);
	}

	return res;

out:
	return res;
}
EXPORT_SYMBOL(core_config_get_fw_ver);

int core_config_get_chip_id(void)
{
	int res = 0;
	static int do_once = 0;
	uint32_t RealID = 0, PIDData = 0;

	res = core_config_ice_mode_enable();
	if (res < 0)
	{
		DBG_ERR("Failed to enter ICE mode, res = %d", res);
		goto out;
	}

	mdelay(20);

	PIDData = core_config_ice_mode_read(core_config->pid_addr);

	if (PIDData)
	{
		RealID = check_chip_id(PIDData);

		DBG_INFO("CHIP ID = 0x%x, CHIP TYPE = %04x", RealID, core_config->chip_type);

		if (RealID != core_config->chip_id)
		{
			DBG_ERR("CHIP ID ERROR: 0x%x, ON_BOARD_IC = 0x%x", 
						RealID, ON_BOARD_IC);
			res = -ENODEV;
			goto out;
		}
	}
	else
	{
		DBG_ERR("PID DATA error : 0x%x", PIDData);
		res = -EINVAL;
		goto out;
	}

	if(do_once == 0)
	{
		// reading flash id needs to let ic entry to ICE mode.
		read_flash_info(0x9F, 4);
		do_once = 1;
	}

	core_config_ic_reset();
	mdelay(150);
	return res;

out:
	core_config_ic_reset();
	mdelay(150);
	return res;
}
EXPORT_SYMBOL(core_config_get_chip_id);

int core_config_init(void)
{
	int i = 0, res = -1;
	int alloca_size = 0;

	for (; i < nums_chip; i++)
	{
		if (SUP_CHIP_LIST[i] == ON_BOARD_IC)
		{
			alloca_size = sizeof(*core_config) * sizeof(uint8_t) * 6;
			core_config = kzalloc(alloca_size, GFP_KERNEL);
			if(ERR_ALLOC_MEM(core_config))
			{
				DBG_ERR("Failed to allocate core_config memory, %ld", PTR_ERR(core_config));
				res = -ENOMEM;
				goto out;
			}

			alloca_size = sizeof(*core_config->tp_info);
			core_config->tp_info = kzalloc(alloca_size, GFP_KERNEL);
			if(ERR_ALLOC_MEM(core_config->tp_info))
			{
				DBG_ERR("Failed to allocate core_config->tp_info memory, %ld", PTR_ERR(core_config->tp_info));
				res = -ENOMEM;
				goto out;
			}

			core_config->chip_id = SUP_CHIP_LIST[i];
			core_config->chip_type = 0x0000;
			core_config->do_ic_reset = false;
			core_config->isEnableGesture = false;

			if (core_config->chip_id == CHIP_TYPE_ILI7807)
			{
				core_config->use_protocol = ILITEK_PROTOCOL_V5_0;
				core_config->slave_i2c_addr = ILI7807_SLAVE_ADDR;
				core_config->ice_mode_addr = ILI7807_ICE_MODE_ADDR;
				core_config->pid_addr = ILI7807_PID_ADDR;
			}
			else if (core_config->chip_id == CHIP_TYPE_ILI9881)
			{
				core_config->use_protocol = ILITEK_PROTOCOL_V5_0;
				core_config->slave_i2c_addr = ILI9881_SLAVE_ADDR;
				core_config->ice_mode_addr = ILI9881_ICE_MODE_ADDR;
				core_config->pid_addr = ILI9881_PID_ADDR;
			}

			set_protocol_cmd(core_config->use_protocol);
			res = 0;
			return res;
		}
	}

	DBG_ERR("Can't find this chip in support list");
out:
	core_config_remove();
	return res;
}
EXPORT_SYMBOL(core_config_init);

void core_config_remove(void)
{
	DBG_INFO("Remove core-config memebers");

	if(core_config != NULL)
	{
		if(core_config->tp_info != NULL)
			kfree(core_config->tp_info);
		
		kfree(core_config);
	}
}
EXPORT_SYMBOL(core_config_remove);
