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
#include <linux/fs.h>
#include <linux/fd.h>
#include <linux/file.h>
#include <linux/version.h>
#include <asm/uaccess.h>

#include "../chip.h"
#include "../platform.h"
#include "config.h"
#include "i2c.h"
#include "firmware.h"
#include "flash.h"

extern uint32_t SUP_CHIP_LIST[];
extern int nums_chip;

// the size of two arrays is different, depending on
// which of methods to upgrade firmware you choose for.
//uint8_t flash_fw[MAX_FLASH_FIRMWARE_SIZE] = {0};
uint8_t *flash_fw = NULL;
uint8_t iram_fw[MAX_IRAM_FIRMWARE_SIZE] = {0};

#ifdef FLASH_SECTOR
int Ssize = 0;
int sec_length = 0;
uint32_t tmp_ck = 0x0;

struct flash_sector
{
	uint32_t ss_addr;
	uint32_t se_addr;
	uint32_t checksum; 
	uint32_t crc32;
	uint32_t dlength;
	bool data_flag;
};

struct flash_sector *ffls;
#endif

#define CHECK(X,Y) ((X==Y) ? 0 : -1 )

struct core_firmware_data *core_firmware;

static uint32_t HexToDec(char *pHex, int32_t nLength)
{
	uint32_t nRetVal = 0, nTemp = 0, i;
	int32_t nShift = (nLength - 1) * 4;

	for (i = 0; i < nLength; nShift -= 4, i++)
	{
		if ((pHex[i] >= '0') && (pHex[i] <= '9'))
		{
			nTemp = pHex[i] - '0';
		}
		else if ((pHex[i] >= 'a') && (pHex[i] <= 'f'))
		{
			nTemp = (pHex[i] - 'a') + 10;
		}
		else if ((pHex[i] >= 'A') && (pHex[i] <= 'F'))
		{
			nTemp = (pHex[i] - 'A') + 10;
		}
		else
		{
			return -1;
		}

		nRetVal |= (nTemp << nShift);
	}

	return nRetVal;
}

static uint32_t calc_crc32(uint32_t start_addr, uint32_t end_addr, uint8_t *data)
{
	int i, j;
	uint32_t CRC_POLY = 0x04C11DB7;
	uint32_t ReturnCRC = 0xFFFFFFFF;

	for (i = start_addr; i < end_addr; i++)
	{
		ReturnCRC ^= (data[i] << 24);

		for (j = 0; j < 8; j++)
		{
			if ((ReturnCRC & 0x80000000) != 0)
			{
				ReturnCRC = ReturnCRC << 1 ^ CRC_POLY;
			}
			else
			{
				ReturnCRC = ReturnCRC << 1;
			}
		}
	}

	return ReturnCRC;
}

static int ili2121_checkcum(uint32_t nStartAddr, uint32_t nEndAddr)
{
	int i = 0, nInTimeCount = 100;
	uint8_t szBuf[64] = {0};

	core_config_ice_mode_write(0x4100B, 0x23, 1);
	core_config_ice_mode_write(0x41009, nEndAddr, 2);
	core_config_ice_mode_write(0x41000, 0x3B | (nStartAddr << 8), 4);
	core_config_ice_mode_write(0x041004, 0x66AA5500, 4);

	for (i = 0; i < nInTimeCount; i++)
	{
		szBuf[0] = core_config_read_write_onebyte(0x41011);

		if ((szBuf[0] & 0x01) == 0)
			break;

		mdelay(100);
	}

	return core_config_ice_mode_read(0x41018);
}

#ifdef FLASH_SECTOR
static uint32_t ili7807_check_data(uint32_t start_addr, uint32_t end_addr)
{
	int timer = 500;
	uint32_t write_len = 0;
	uint32_t iram_check = 0;

	write_len = end_addr;

	DBG_INFO("start = 0x%x , write_len = 0x%x, max_count = %x", 
				start_addr, end_addr, core_firmware->max_count);

	if (write_len > core_firmware->max_count)
	{
		DBG_ERR("The length (%x) written to firmware is greater than max count (%x)",
				write_len, core_firmware->max_count);
		goto out;
	}

	// CS low
	core_config_ice_mode_write(0x041000, 0x0, 1);
	core_config_ice_mode_write(0x041004, 0x66aa55, 3);
	core_config_ice_mode_write(0x041008, 0x3b, 1);

	// Set start address
	core_config_ice_mode_write(0x041008, start_addr, 3);
	// Enable Dio_Rx_dual
	core_config_ice_mode_write(0x041003, 0x01, 1);
	// Dummy
	core_config_ice_mode_write(0x041008, 0xFF, 1);

	// Set Receive count
	if(core_firmware->max_count == 0xFFFF)
		core_config_ice_mode_write(0x04100C, write_len, 2);
	else if(core_firmware->max_count == 0x1FFFF)
		core_config_ice_mode_write(0x04100C, write_len, 3);

	// Checksum enable
	core_config_ice_mode_write(0x041014, 0x10000, 3);
	// Start to receive
	core_config_ice_mode_write(0x041010, 0xFF, 1);

	mdelay(1);

	while (timer > 0)
	{
		mdelay(1);

		if (core_config_read_write_onebyte(0x041014) == 0x01)
			break;

		timer--;
	}

	// CS high
	core_config_ice_mode_write(0x041000, 0x1, 1);

	if (timer >= 0)
	{
		// Disable dio_Rx_dual
		core_config_ice_mode_write(0x041003, 0x0, 1);

		iram_check = core_firmware->isCRC ? core_config_ice_mode_read(0x4101C) : core_config_ice_mode_read(0x041018);
	}
	else
	{
		DBG_ERR("TIME OUT");
		goto out;
	}

	return iram_check;

out:
	DBG_ERR("Failed to read Checksum/CRC from IC");
	return -1;

}
#else
static int ili7807_check_data(void)
{
	int timer = 500, res = 0;
	uint32_t write_len = 0;
	uint32_t iram_check = 0;

	write_len = core_firmware->ap_end_addr + 1;

	if (write_len > core_firmware->max_count)
	{
		DBG_ERR("The length (%x) written to firmware is greater than max count (%x)",
				write_len, core_firmware->max_count);
		goto out;
	}

	// CS low
	core_config_ice_mode_write(0x041000, 0x0, 1);
	core_config_ice_mode_write(0x041004, 0x66aa55, 3);
	core_config_ice_mode_write(0x041008, 0x3b, 1);

	// Set start address
	core_config_ice_mode_write(0x041008, core_firmware->ap_start_addr, 3);
	// Enable Dio_Rx_dual
	core_config_ice_mode_write(0x041003, 0x01, 1);
	// Dummy
	core_config_ice_mode_write(0x041008, 0xFF, 1);
	// Set Receive count
	core_config_ice_mode_write(0x04100C, write_len, 2);
	// Checksum enable
	core_config_ice_mode_write(0x041014, 0x10000, 3);
	// Start to receive
	core_config_ice_mode_write(0x041010, 0xFF, 1);

	mdelay(1);

	while (timer > 0)
	{
		mdelay(1);

		if (core_config_read_write_onebyte(0x041014) == 0x01)
			break;

		timer--;
	}

	// CS high
	core_config_ice_mode_write(0x041000, 0x1, 1);

	if (timer != 0)
	{
		// Disable dio_Rx_dual
		core_config_ice_mode_write(0x041003, 0x0, 1);

		if (core_firmware->isCRC == true)
		{
			// read CRC
			iram_check = core_config_ice_mode_read(0x4101C);

			if (core_firmware->ap_crc != iram_check)
			{
				DBG_ERR("CRC Error: ap_crc = %x , iram_check = %x",
						core_firmware->ap_crc, iram_check);
				goto out;
			}
		}
		else
		{
			// read checksum
			iram_check = core_config_ice_mode_read(0x041018);

			if (core_firmware->ap_checksum != iram_check)
			{
				DBG_ERR("Checksum Error: ap_checksum = %x , iram_check = %x",
						core_firmware->ap_checksum, iram_check);
				goto out;
			}
		}

		DBG_INFO("The data written into iram is correct !");

		return res;
	}

out:
	DBG_ERR("Failed to get correct data");
	return -1;
}
#endif

#ifdef FLASH_SECTOR
static void calc_verify_data(uint32_t sa, uint32_t se, uint32_t *ck, uint32_t *crc)
{
	uint32_t i = 0;
	uint32_t tmp_ck = 0, tmp_crc = 0;

	if(ck != NULL)
	{
		for(i = sa; i < se; i++)
		{
			tmp_ck = tmp_ck + flash_fw[i];
		}
		*ck = tmp_ck;
	}

	if(crc != NULL) {
		tmp_crc = calc_crc32(sa, se, flash_fw);
		*crc = tmp_crc;
	}
}

static int verify_flash_data(void)
{
	int i = 0, res = 0;
	uint32_t ss = 0x0, se = 0x0;
	uint32_t vd = 0;
	uint32_t sum_check = 0, sum_crc = 0;
	int len = 0;
	int fps = flashtab->sector;

	for(i = 0; i < sec_length + 1; i++)
	{
		if(ffls[i].data_flag)
		{
			if(ss > ffls[i].ss_addr || len == 0)
				ss = ffls[i].ss_addr;

			len = len + ffls[i].dlength;
			se = ffls[i].se_addr + 1;

			calc_verify_data(ss, se, &sum_check, NULL);

			if(len >= (core_firmware->max_count - fps))
			{
				// commit data if its length is over than max count
				calc_verify_data(ss, se, NULL, &sum_crc);
				vd = ili7807_check_data(ss, len);
				if(core_firmware->isCRC)
				{
					res = CHECK(vd, sum_crc);
					if(res < 0)
					{
						DBG_ERR("Invalid ! (%x) : (%x)",vd,sum_crc);
						res = -1;
						break;						
					}			
				}
				else
				{
					res = CHECK(vd, sum_check);
					if(res < 0)
					{
						DBG_ERR("Invalid ! (%x) : (%x)",vd,sum_check);
						res = -1;
						break;						
					}
				}

				ss = ffls[i].ss_addr;
				sum_check = 0;
				sum_crc = 0;
				len = 0;
			}
		}
		else
		{
			// split block and commit last data to check
			if(len != 0)
			{
				calc_verify_data(ss, se, NULL, &sum_crc);
				vd = ili7807_check_data(ss, len);
				if(core_firmware->isCRC)
				{
					res = CHECK(vd, sum_crc);
					if(res < 0)
					{
						DBG_ERR("Invalid ! (%x) : (%x)",vd,sum_check);
						res = -1;
						break;						
					}								
				}
				else
				{
					res = CHECK(vd, sum_check);
					if(res < 0)
					{
						DBG_ERR("Invalid ! (%x) : (%x)",vd,sum_check);
						res = -1;
						break;						
					}
				}
			}

			ss = ffls[i].ss_addr;
			sum_check = 0;
			sum_crc = 0;
			len = 0;
		}
	}

	if(len != 0 && res != -1)
	{
		calc_verify_data(ss, core_firmware->end_addr - ss, NULL, &sum_crc);
		vd = ili7807_check_data(ss, core_firmware->end_addr - ss);
		if(core_firmware->isCRC)
		{
			res = CHECK(vd, sum_crc);
			if(res < 0)
			{
				DBG_ERR("Invalid ! (%x) : (%x)",vd,sum_crc);
				res = -1;
			}					
		}
		else
		{
			res = CHECK(vd, sum_check);
			if(res < 0)
			{
				DBG_ERR("Invalid ! (%x) : (%x)",vd,sum_check);
			}
		}
	}

	return res;
}
#endif

static int ili7807_polling_flash_busy(void)
{
	int timer = 500;

	core_config_ice_mode_write(0x041000, 0x0, 1);
	core_config_ice_mode_write(0x041004, 0x66aa55, 3);
	core_config_ice_mode_write(0x041008, 0x5, 1);

	while (timer > 0)
	{
		core_config_ice_mode_write(0x041008, 0xFF, 1);
		//core_config_ice_mode_write(0x041000, 0x1, 1);

		mdelay(1);

		if (core_config_read_write_onebyte(0x041010) == 0x00)
		{
			core_config_ice_mode_write(0x041000, 0x1, 1);
			return 0;
		}

		timer--;
	}

	DBG_ERR("Polling busy Time out !");
	return -1;
}

static int ili7807_write_enable(void)
{
	if (core_config_ice_mode_write(0x041000, 0x0, 1) < 0)
		goto out;
	if (core_config_ice_mode_write(0x041004, 0x66aa55, 3) < 0)
		goto out;
	if (core_config_ice_mode_write(0x041008, 0x6, 1) < 0)
		goto out;
	if (core_config_ice_mode_write(0x041000, 0x1, 1) < 0)
		goto out;

	return 0;

out:
	DBG_ERR("Write enable failed !");
	return -EIO;
}

static int iram_upgrade(void)
{
	int i, j, res = 0;
	uint8_t buf[512];
	int upl = flashtab->program_page;

	core_firmware->update_status = 0;

	// doing reset for erasing iram data before upgrade it.
	ilitek_platform_tp_power_on(1);

	mdelay(1);

	DBG_INFO("Upgrade firmware written data into IRAM directly");

	res = core_config_ice_mode_enable();
	if (res < 0)
	{
		DBG_ERR("Failed to enter ICE mode, res = %d", res);
		return res;
	}

	mdelay(20);

	core_config_reset_watch_dog();

	DBG("nStartAddr = 0x%06X, nEndAddr = 0x%06X, nChecksum = 0x%06X",
		core_firmware->start_addr, core_firmware->end_addr, core_firmware->checksum);

	// write hex to the addr of iram
	DBG_INFO("Writting data into IRAM ...");
	for (i = core_firmware->start_addr; i < core_firmware->end_addr; i += upl)
	{
		if ((i + 256) > core_firmware->end_addr)
		{
			upl = core_firmware->end_addr % upl;
		}

		buf[0] = 0x25;
		buf[3] = (char)((i & 0x00FF0000) >> 16);
		buf[2] = (char)((i & 0x0000FF00) >> 8);
		buf[1] = (char)((i & 0x000000FF));

		for (j = 0; j < upl; j++)
			buf[4 + j] = iram_fw[i + j];

		if (core_i2c_write(core_config->slave_i2c_addr, buf, upl + 4))
		{
			DBG_INFO("Failed to write data via i2c, address = 0x%X, start_addr = 0x%X, end_addr = 0x%X",
					 (int)i, (int)core_firmware->start_addr, (int)core_firmware->end_addr);
			res = -EIO;
			return res;
		}

		core_firmware->update_status = (i * 101) / core_firmware->ap_end_addr;
		printk("%cupgrade firmware(ap code), %02d%c", 0x0D, core_firmware->update_status, '%');

		mdelay(3);
	}

	// ice mode code reset
	DBG_INFO("Doing code reset ...");
	core_config_ice_mode_write(0x40040, 0xAE, 1);
	core_config_ice_mode_write(0x40040, 0x00, 1);

	mdelay(10);

	core_config_ice_mode_disable();

	//TODO: check iram status

	return res;
}

#ifdef FLASH_SECTOR
static int ili7807_firmware_upgrade(bool isIRAM)
{
	int i, j, res = 0;
	uint8_t buf[512] = {0};
	uint32_t temp_buf = 0, k;
	uint32_t start_addr = core_firmware->start_addr;
	uint32_t end_addr = core_firmware->end_addr;
	int upl = flashtab->program_page;

	core_firmware->update_status = 0;

	if (isIRAM)
	{
		res = iram_upgrade();
		return res;
	}

	ilitek_platform_tp_power_on(1);

	DBG_INFO("Enter to ICE Mode");

	res = core_config_ice_mode_enable();
	if (res < 0)
	{
		DBG_ERR("Failed to enable ICE mode");
		goto out;
	}

	mdelay(5);

	// This command is used to fix the bug of spi clk in 7807F-AB
	// while operating with flash.
	if (core_firmware->chip_id == CHIP_TYPE_ILI7807 
			&& core_config->chip_type == ILI7807_TYPE_F_AB)
	{
		res = core_config_ice_mode_write(0x4100C, 0x01, 1);
		if (res < 0)
			goto out;
	}

	mdelay(25);

	// there is no need to disable WTD if you're using 9881
	if (core_firmware->chip_id != CHIP_TYPE_ILI9881)
		core_config_reset_watch_dog();

	DBG_INFO("Erasing Flash data ...");
	for(i = 0; i < sec_length + 1; i++)
	{
		if(!ffls[i].data_flag)
			continue;

		res = ili7807_write_enable();
		if (res < 0)
		{
			DBG_ERR("Failed to config write enable");
			goto out;
		}

		// CS low
		core_config_ice_mode_write(0x041000, 0x0, 1);
		core_config_ice_mode_write(0x041004, 0x66aa55, 3);
		core_config_ice_mode_write(0x041008, 0x20, 1);

		core_config_ice_mode_write(0x041008, (ffls[i].ss_addr & 0xFF0000) >> 16, 1);//Addr_H
		core_config_ice_mode_write(0x041008, (ffls[i].ss_addr & 0x00FF00) >> 8, 1); //Addr_M
		core_config_ice_mode_write(0x041008, (ffls[i].ss_addr & 0x0000FF), 1); //Addr_L
		core_config_ice_mode_write(0x041000, 0x1, 1);

		mdelay(1);

		res = ili7807_polling_flash_busy();
		if (res < 0)
		{
			DBG_ERR("TIME OUT");
			goto out;
		}

		// CS low
		core_config_ice_mode_write(0x041000, 0x0, 1);
		core_config_ice_mode_write(0x041004, 0x66aa55, 3);
		core_config_ice_mode_write(0x041008, 0x3, 1);

		core_config_ice_mode_write(0x041008, (ffls[i].ss_addr & 0xFF0000) >> 16, 1);//Addr_H
		core_config_ice_mode_write(0x041008, (ffls[i].ss_addr & 0x00FF00) >> 8, 1); //Addr_M
		core_config_ice_mode_write(0x041008, (ffls[i].ss_addr & 0x0000FF), 1); //Addr_L
		core_config_ice_mode_write(0x041008, 0xFF, 1);

		temp_buf = core_config_read_write_onebyte(0x041010);

		if (temp_buf != 0xFF)
			DBG_ERR("Failed to read data at 0x%x ", i);

		// CS High
		core_config_ice_mode_write(0x041000, 0x1, 1);

		DBG_INFO("Earsing data at start addr: %x ", ffls[i].ss_addr );
	}

	mdelay(1);

	//write data into flash
	DBG_INFO("Writing data into flash ...");
	for(i = 0; i < sec_length + 1; i++)
	{
		if(!ffls[i].data_flag)
			continue;
	
		for(j = ffls[i].ss_addr; j < ffls[i].se_addr; j+= upl)
		{
			res = ili7807_write_enable();
			if (res < 0)
			{
				DBG_ERR("Failed to config write enable");
				goto out;
			}	

			// CS low
			core_config_ice_mode_write(0x041000, 0x0, 1);
			core_config_ice_mode_write(0x041004, 0x66aa55, 3);
			core_config_ice_mode_write(0x041008, 0x02, 1);

			core_config_ice_mode_write(0x041008, (j & 0xFF0000) >> 16, 1); //Addr_H
			core_config_ice_mode_write(0x041008, (j & 0x00FF00) >> 8, 1);  //Addr_M
			core_config_ice_mode_write(0x041008, (j & 0x0000FF), 1);	   //Addr_L

			buf[0] = 0x25;
			buf[3] = 0x04;
			buf[2] = 0x10;
			buf[1] = 0x08;

			for (k = 0; k < upl; k++)
			{
				if (j + k <= end_addr)
					buf[4 + k] = flash_fw[j + k];
			}

			if (core_i2c_write(core_config->slave_i2c_addr, buf, upl + 4) < 0)
			{
				DBG_ERR("Failed to write data at address = 0x%X, start_addr = 0x%X, end_addr = 0x%X", 
							(int)i, (int)start_addr, (int)end_addr);
				res = -EIO;
				goto out;
			}

			// CS high
			core_config_ice_mode_write(0x041000, 0x1, 1);

			res = ili7807_polling_flash_busy();
			if (res < 0)
			{
				DBG_ERR("TIME OUT");
				goto out;
			}

			core_firmware->update_status = (j * 101) / end_addr;
			printk("%cUpgrading firmware ... (0x%x...0x%x), %02d%c", 0x0D, ffls[i].ss_addr, ffls[i].se_addr, core_firmware->update_status, '%');
		}
	}

	// We do have to reset chip in order to move new code from flash to iram.
	DBG_INFO("Doing Soft Reset ..");
	core_config_ic_reset();

	// the delay time moving code depends on what the touch IC you're using.
	mdelay(core_firmware->delay_after_upgrade);

	// ensure that the chip has been updated
	DBG_INFO("Enter to ICE Mode again");
	res = core_config_ice_mode_enable();
	if (res < 0)
	{
		DBG_ERR("Failed to enable ICE mode");
		goto out;
	}

	mdelay(20);

	if (core_firmware->chip_id != CHIP_TYPE_ILI9881)
		core_config_reset_watch_dog();

	// check the data that we've just written into the iram.
	res = verify_flash_data();
	if(res == 0)
		DBG_INFO("Data Correct !");

out:
	core_config_ice_mode_disable();
	return res;
}
#else
static int ili7807_firmware_upgrade(bool isIRAM)
{
	int i, j, res = 0;
	uint32_t erase_start_addr = 0;
	uint8_t buf[512] = {0};
	uint32_t temp_buf = 0;
	uint32_t start_addr = core_firmware->start_addr;
	uint32_t end_addr = core_firmware->end_addr;

	core_firmware->update_status = 0;

	if (isIRAM)
	{
		res = iram_upgrade();
		return res;
	}

	ilitek_platform_tp_power_on(1);

	DBG_INFO("Enter to ICE Mode");

	res = core_config_ice_mode_enable();
	if (res < 0)
	{
		DBG_ERR("Failed to enable ICE mode");
		goto out;
	}

	mdelay(5);

	if (core_firmware->chip_id == CHIP_TYPE_ILI7807 
			&& core_confing->chip_type == ILI7807_TYPE_F_AB)
	{
		// This command is used to fixed the bug of spi clk in 7807F
		res = core_config_ice_mode_write(0x4100C, 0x01, 1);
		if (res < 0)
			goto out;
	}

	mdelay(25);

	// there is no need to disable WTD if you're using 9881
	if (core_firmware->chip_id != CHIP_TYPE_ILI9881)
		core_config_reset_watch_dog();

	DBG_INFO("Erasing Flash data ...");
	for (erase_start_addr = 0; erase_start_addr < 0xE0;)
	{
		res = ili7807_write_enable();
		if (res < 0)
		{
			DBG_ERR("Failed to config write enable");
			goto out;
		}

		// CS low
		core_config_ice_mode_write(0x041000, 0x0, 1);
		core_config_ice_mode_write(0x041004, 0x66aa55, 3);
		core_config_ice_mode_write(0x041008, 0x20, 1);

		core_config_ice_mode_write(0x041008, 0x0, 1);			   //Addr_H
		core_config_ice_mode_write(0x041008, erase_start_addr, 1); //Addr_M
		core_config_ice_mode_write(0x041008, 0x0, 1);			   //Addr_L
		core_config_ice_mode_write(0x041000, 0x1, 1);

		mdelay(1);

		res = ili7807_polling_flash_busy();
		if (res < 0)
		{
			DBG_ERR("TIME OUT");
			goto out;
		}

		// CS low
		core_config_ice_mode_write(0x041000, 0x0, 1);
		core_config_ice_mode_write(0x041004, 0x66aa55, 3);
		core_config_ice_mode_write(0x041008, 0x3, 1);

		core_config_ice_mode_write(0x041008, 0x0, 1);
		core_config_ice_mode_write(0x041008, erase_start_addr, 1);
		core_config_ice_mode_write(0x041008, 0x0, 1);
		core_config_ice_mode_write(0x041008, 0xFF, 1);

		temp_buf = core_config_read_write_onebyte(0x041010);

		if (temp_buf != 0xFF)
			DBG_ERR("Failed to read data at 0x%x ", erase_start_addr);

		erase_start_addr = erase_start_addr + 0x10;

		// CS High
		core_config_ice_mode_write(0x041000, 0x1, 1);
	}

	mdelay(1);

	//write data into flash
	DBG_INFO("Writing data into flash ...");
	for (i = start_addr; i < end_addr; i += UPDATE_FIRMWARE_PAGE_LENGTH)
	{
		res = ili7807_write_enable();
		if (res < 0)
		{
			DBG_ERR("Failed to config write enable");
			goto out;
		}

		// CS low
		core_config_ice_mode_write(0x041000, 0x0, 1);
		core_config_ice_mode_write(0x041004, 0x66aa55, 3);
		core_config_ice_mode_write(0x041008, 0x02, 1);

		core_config_ice_mode_write(0x041008, (i & 0xFF0000) >> 16, 1); //Addr_H
		core_config_ice_mode_write(0x041008, (i & 0x00FF00) >> 8, 1);  //Addr_M
		core_config_ice_mode_write(0x041008, (i & 0x0000FF), 1);	   //Addr_L

		buf[0] = 0x25;
		buf[3] = 0x04;
		buf[2] = 0x10;
		buf[1] = 0x08;

		for (j = 0; j < UPDATE_FIRMWARE_PAGE_LENGTH; j++)
		{
			if (i + j <= end_addr)
				buf[4 + j] = flash_fw[i + j];
			else
				buf[4 + j] = 0xFF;
		}

		if (core_i2c_write(core_config->slave_i2c_addr, buf, UPDATE_FIRMWARE_PAGE_LENGTH + 4) < 0)
		{
			DBG_ERR("Failed to write data at address = 0x%X, start_addr = 0x%X, end_addr = 0x%X", 
						(int)i, (int)start_addr, (int)end_addr);
			res = -EIO;
			goto out;
		}

		// CS high
		core_config_ice_mode_write(0x041000, 0x1, 1);

		res = ili7807_polling_flash_busy();
		if (res < 0)
		{
			DBG_ERR("TIME OUT");
			goto out;
		}

		core_firmware->update_status = (i * 101) / core_firmware->ap_end_addr;
		printk("%cupgrade firmware(ap code), %02d%c", 0x0D, core_firmware->update_status, '%');
	}

	// We do have to reset chip in order to move new code from flash to iram.
	DBG_INFO("Doing Soft Reset ..");
	core_config_ic_reset();

	// the delay time moving code depends on what the touch IC you're using.
	mdelay(core_firmware->delay_after_upgrade);

	// ensure that the chip has been updated
	DBG_INFO("Enter to ICE Mode again");
	res = core_config_ice_mode_enable();

	if (res < 0)
	{
		DBG_ERR("Failed to enable ICE mode");
		goto out;
	}

	mdelay(20);

	if (core_firmware->chip_id != CHIP_TYPE_ILI9881)
		core_config_reset_watch_dog();

	// check the data just written to firmware is valid.
	DBG_INFO("Checking the data in iram is valid ...");
	res = ili7807_check_data();
	if (res < 0)
	{
		DBG_ERR("The data written into iram isn't correct");
	}

out:
	core_config_ice_mode_disable();
	return res;
}
#endif

static int ili2121_firmware_upgrade(bool isIRAM)
{
	int32_t nUpgradeStatus = 0, nUpdateLength = 0;
	int32_t i = 0, j = 0, k = 0;
	uint8_t szBuf[512] = {0};
	uint8_t szCmd[2] = {0};
	uint32_t nApStartAddr = 0, nApEndAddr = 0;
	uint32_t nApChecksum = 0, nTemp = 0, nIcChecksum = 0;
	int res = 0;
	int upl = 256;

	nApStartAddr = core_firmware->ap_start_addr;
	nApEndAddr = core_firmware->ap_end_addr;
	nUpdateLength = core_firmware->ap_end_addr;
	nApChecksum = core_firmware->ap_checksum;

	DBG_INFO("AP_Start_Addr = 0x%06X, AP_End_Addr = 0x%06X, AP_checksum = 0x%06X",
			 core_firmware->ap_start_addr,
			 core_firmware->ap_end_addr,
			 core_firmware->ap_checksum);

	DBG_INFO("Enter to ICE Mode before updating firmware ... ");

	res = core_config_ice_mode_enable();

	core_config_reset_watch_dog();

	mdelay(5);

	for (i = 0; i <= 0xd000; i += 0x1000)
	{
		res = core_config_ice_mode_write(0x041000, 0x06, 1);
		if (res < 0)
			return res;

		mdelay(3);

		res = core_config_ice_mode_write(0x041004, 0x66aa5500, 4);
		if (res < 0)
			return res;

		mdelay(3);

		nTemp = (i << 8) + 0x20;

		res = core_config_ice_mode_write(0x041000, nTemp, 4);
		if (res < 0)
			return res;

		mdelay(3);

		res = core_config_ice_mode_write(0x041004, 0x66aa5500, 4);
		if (res < 0)
			return res;

		mdelay(20);

		for (j = 0; j < 50; j++)
		{
			res = core_config_ice_mode_write(0x041000, 0x05, 1);
			if (res < 0)
				return res;

			res = core_config_ice_mode_write(0x041004, 0x66aa5500, 4);
			if (res < 0)
				return res;

			mdelay(1);

			szBuf[0] = core_config_ice_mode_read(0x041013);
			if (szBuf[0] == 0)
				break;
			else
				mdelay(2);
		}
	}

	mdelay(100);

	DBG_INFO("Start to upgrade firmware from 0x%x to 0x%x in each size of %d",
			 nApStartAddr, nApEndAddr, upl);

	for (i = nApStartAddr; i < nApEndAddr; i += upl)
	{
		res = core_config_ice_mode_write(0x041000, 0x06, 1);
		if (res < 0)
			return res;

		res = core_config_ice_mode_write(0x041004, 0x66aa5500, 4);
		if (res < 0)
			return res;

		nTemp = (i << 8) + 0x02;

		res = core_config_ice_mode_write(0x041000, nTemp, 4);
		if (res < 0)
			return res;

		res = core_config_ice_mode_write(0x041004, 0x66aa5500 + upl - 1, 4);
		if (res < 0)
			return res;

		szBuf[0] = 0x25;
		szBuf[3] = (char)((0x041020 & 0x00FF0000) >> 16);
		szBuf[2] = (char)((0x041020 & 0x0000FF00) >> 8);
		szBuf[1] = (char)((0x041020 & 0x000000FF));

		for (k = 0; k < upl; k++)
		{
			szBuf[4 + k] = flash_fw[i + k];
		}

		if (core_i2c_write(core_config->slave_i2c_addr, szBuf, upl + 4) < 0)
		{
			DBG_INFO("Failed to write data via i2c, address = 0x%X, start_addr = 0x%X, end_addr = 0x%X",
					 (int)i, (int)nApStartAddr, (int)nApEndAddr);
			res = -EIO;
			return res;
		}

		nUpgradeStatus = (i * 100) / nUpdateLength;
		printk("%cupgrade firmware(ap code), %02d%c", 0x0D, nUpgradeStatus, '%');

		mdelay(3);
	}

	nIcChecksum = 0;
	nIcChecksum = ili2121_checkcum(nApStartAddr, nApEndAddr);

	DBG_INFO("nIcChecksum = 0x%X, nApChecksum = 0x%X\n", nIcChecksum, nApChecksum);

	if (nIcChecksum != nApChecksum)
	{
		//TODO: may add a retry func as protection.

		core_config_ic_reset();
		DBG_INFO("Both checksum didn't match");
		res = -1;
		return res;
	}

	core_config_ic_reset();

	szCmd[0] = ILI2121_TP_CMD_READ_DATA;
	res = core_i2c_write(core_config->slave_i2c_addr, &szCmd[0], 1);
	if (res < 0)
	{
		DBG_ERR("ILITEK_TP_CMD_READ_DATA failed, res = %d", res);
		return res;
	}

	mdelay(10);

	res = core_i2c_read(core_config->slave_i2c_addr, &szBuf[0], 3);
	if (res < 0)
	{
		DBG_ERR("ILITEK_TP_CMD_READ_DATA failed, res = %d\n", res);
		return res;
	}

	DBG_INFO("szBuf[0][1][2] = 0x%X, 0x%X, 0x%X",
			 szBuf[0], szBuf[1], szBuf[2]);

	if (szBuf[1] < 0x80)
	{
		//TODO: may add a retry fun as protection.

		DBG_ERR("Upgrade FW Failed");
		res = -1;
		return res;
	}

	DBG_INFO("Upgrade FW Success");
	mdelay(100);

	return res;
}

#ifdef FLASH_SECTOR
static int convert_hex_file(uint8_t *pBuf, uint32_t nSize, bool isIRAM)
{
	uint32_t i = 0, j = 0, k = 0;
	uint32_t nLength = 0, nAddr = 0, nType = 0;
	uint32_t nStartAddr = 0xFFF, nEndAddr = 0x0, nChecksum = 0x0,nExAddr = 0;
	uint32_t CRC32 = 0;

	int index = 0;
	uint32_t fpz = flashtab->sector;
	uint32_t max_flash_size = flashtab->mem_size;

	core_firmware->start_addr = 0;
	core_firmware->end_addr = 0;
	core_firmware->checksum = 0;
	core_firmware->crc32 = 0;

	for (; i < nSize;)
	{
		int32_t nOffset;
		nLength = HexToDec(&pBuf[i + 1], 2);
		nAddr = HexToDec(&pBuf[i + 3], 4);
		nType = HexToDec(&pBuf[i + 7], 2);

		// calculate checksum
		for (j = 8; j < (2 + 4 + 2 + (nLength * 2)); j += 2)
		{
			if (nType == 0x00)
			{	
				// for ice mode write method
				nChecksum = nChecksum + HexToDec(&pBuf[i + 1 + j], 2);
			}
		}
			
		if (nType == 0x04)
		{
			nExAddr = HexToDec(&pBuf[i + 9], 4);
		}

		if (nType == 0x02)
		{
			nExAddr = HexToDec(&pBuf[i + 9], 4);
			nExAddr = nExAddr >> 12;
		}

		nAddr = nAddr + (nExAddr << 16);
		if (pBuf[i + 1 + j + 2] == 0x0D)
		{
			nOffset = 2;
		}
		else
		{
			nOffset = 1;
		}

		if (nType == 0x00)
		{
			if (nAddr > MAX_HEX_FILE_SIZE)
			{
				DBG_ERR("Invalid hex format");
				goto out;
			}

			if (nAddr < nStartAddr)
			{
				nStartAddr = nAddr;
			}
			if ((nAddr + nLength) > nEndAddr)
			{
				nEndAddr = nAddr + nLength;
			}

			// fill data
			for (j = 0, k = 0; j < (nLength * 2); j += 2, k++)
			{
				if (isIRAM)
					iram_fw[nAddr + k] = HexToDec(&pBuf[i + 9 + j], 2);
				else
				{
					flash_fw[nAddr + k] = HexToDec(&pBuf[i + 9 + j], 2);

					if((nAddr + k) != 0)
					{
						index = ((nAddr + k) / fpz); 
						if(!ffls[index].data_flag)
						{
							ffls[index].ss_addr = index * fpz;
							ffls[index].se_addr = (index + 1) * fpz - 1;
							ffls[index].dlength = (ffls[index].se_addr - ffls[index].ss_addr) + 1;
							ffls[index].data_flag = true;
						}

					}

				}
			}
		}
		i += 1 + 2 + 4 + 2 + (nLength * 2) + 2 + nOffset;
	}

	sec_length = index;

	if(ffls[sec_length-1].se_addr > max_flash_size)
	{
		DBG_ERR("The size written to flash is larger than it required (%x) (%x)", 
					ffls[sec_length-1].se_addr, max_flash_size);
		goto out;
	}

	// for debug
	for(i = 0; i < sec_length + 1; i++)
	{
		DBG_INFO("ffls[%d]: ss_addr = 0x%x, se_addr = 0x%x, length = %x, data = %d", 
		i, ffls[i].ss_addr, ffls[i].se_addr, ffls[index].dlength, ffls[i].data_flag );
	}

	if (core_firmware->isCRC == true && isIRAM == false)
	{
		CRC32 = calc_crc32(nStartAddr, nEndAddr-1, flash_fw);
	}

	core_firmware->start_addr = nStartAddr;
	core_firmware->end_addr = nEndAddr;
	DBG_INFO("nStartAddr = 0x%06X, nEndAddr = 0x%06X", nStartAddr, nEndAddr);
	return 0;

out:
	DBG_ERR("Failed to convert HEX data");
	return -1;
}
#else
static int convert_hex_file(uint8_t *pBuf, uint32_t nSize, bool isIRAM)
{
	uint32_t CRC32 = 0;
	uint32_t i = 0, j = 0, k = 0;
	uint32_t nApStartAddr = 0xFFFF, nDfStartAddr = 0xFFFF, nExAddr = 0;
	uint32_t nApEndAddr = 0x0, nDfEndAddr = 0x0;
	uint32_t nApChecksum = 0x0, nDfChecksum = 0x0, nLength = 0, nAddr = 0, nType = 0;
	uint32_t nStartAddr = 0xFFF, nEndAddr = 0x0, nChecksum = 0x0;

	core_firmware->ap_start_addr = 0;
	core_firmware->ap_end_addr = 0;
	core_firmware->ap_checksum = 0;
	core_firmware->ap_crc = 0;

	if (nSize != 0)
	{
		for (; i < nSize;)
		{
			int32_t nOffset;

			nLength = HexToDec(&pBuf[i + 1], 2);
			nAddr = HexToDec(&pBuf[i + 3], 4);
			nType = HexToDec(&pBuf[i + 7], 2);

			// calculate checksum
			for (j = 8; j < (2 + 4 + 2 + (nLength * 2)); j += 2)
			{
				if (nType == 0x00)
				{
					// for ice mode write method
					nChecksum = nChecksum + HexToDec(&pBuf[i + 1 + j], 2);

					if (nAddr + (j - 8) / 2 < nDfStartAddr)
					{
						nApChecksum = nApChecksum + HexToDec(&pBuf[i + 1 + j], 2);
					}
					else
					{
						nDfChecksum = nDfChecksum + HexToDec(&pBuf[i + 1 + j], 2);
					}
				}
			}
			if (nType == 0x04)
			{
				nExAddr = HexToDec(&pBuf[i + 9], 4);
			}

			nAddr = nAddr + (nExAddr << 16);

			if (pBuf[i + 1 + j + 2] == 0x0D)
			{
				nOffset = 2;
			}
			else
			{
				nOffset = 1;
			}

			if (nType == 0x00)
			{
				if (nAddr > MAX_HEX_FILE_SIZE)
				{
					DBG_ERR("Invalid hex format");

					return -EINVAL;
				}

				if (nAddr < nStartAddr)
				{
					nStartAddr = nAddr;
				}
				if ((nAddr + nLength) > nEndAddr)
				{
					nEndAddr = nAddr + nLength;
				}

				// for Bl protocol 1.4+, nApStartAddr and nApEndAddr
				if (nAddr < nApStartAddr)
				{
					nApStartAddr = nAddr;
				}

				if ((nAddr + nLength) > nApEndAddr && (nAddr < nDfStartAddr))
				{
					nApEndAddr = nAddr + nLength - 1;

					if (nApEndAddr > nDfStartAddr)
					{
						nApEndAddr = nDfStartAddr - 1;
					}
				}

				// for Bl protocol 1.4+, bl_end_addr
				if ((nAddr + nLength) > nDfEndAddr && (nAddr >= nDfStartAddr))
				{
					nDfEndAddr = nAddr + nLength;
				}

				// fill data
				for (j = 0, k = 0; j < (nLength * 2); j += 2, k++)
				{
					if (isIRAM)
						iram_fw[nAddr + k] = HexToDec(&pBuf[i + 9 + j], 2);
					else
						flash_fw[nAddr + k] = HexToDec(&pBuf[i + 9 + j], 2);
				}
			}

			i += 1 + 2 + 4 + 2 + (nLength * 2) + 2 + nOffset;
		}

		if (core_firmware->isCRC == true && isIRAM == false)
		{
			CRC32 = calc_crc32(nApStartAddr, nApEndAddr, flash_fw);
		}

		core_firmware->ap_start_addr = nApStartAddr;
		core_firmware->ap_end_addr = nApEndAddr;
		core_firmware->ap_checksum = nApChecksum;
		core_firmware->ap_crc = CRC32;

		core_firmware->df_start_addr = nDfStartAddr;
		core_firmware->df_end_addr = nDfEndAddr;
		core_firmware->df_checksum = nDfChecksum;

		core_firmware->start_addr = nStartAddr;
		core_firmware->end_addr = nEndAddr;
		core_firmware->checksum = nChecksum;

		DBG_INFO("nApStartAddr = 0x%06X, nApEndAddr = 0x%06X, nApChecksum = 0x%06X, CRC = %x",
				 nApStartAddr, nApEndAddr, nApChecksum, CRC32);
		DBG_INFO("nDfStartAddr = 0x%06X, nDfEndAddr = 0x%06X, nDfChecksum = 0x%06X",
				 nDfStartAddr, nDfEndAddr, nDfChecksum);
		DBG_INFO("nStartAddr = 0x%06X, nEndAddr = 0x%06X, nChecksum = 0x%06X",
				 nStartAddr, nEndAddr, nChecksum);

		return 0;
	}

	return -1;
}
#endif

/*
 * It would basically be called by ioctl when users want to upgrade firmware.
 *
 * @pFilePath: pass a path where locates user's firmware file.
 *
 */
int core_firmware_upgrade(const char *pFilePath, bool isIRAM)
{
	int res = 0, i = 0, fsize;
	uint8_t *hex_buffer = NULL;

	struct file *pfile = NULL;
	struct inode *inode;
	mm_segment_t old_fs;
	loff_t pos = 0;

	core_firmware->isUpgraded = false;

	if(IS_ERR(flashtab))
	{
		DBG_ERR("Flash table isn't created");
		goto out;
	}

	//TODO: to compare old/new version if upgraded.

	pfile = filp_open(pFilePath, O_RDONLY, 0);
	if (IS_ERR(pfile))
	{
		DBG_ERR("Error occurred while opening file %s.", pFilePath);
		res = -ENOENT;
	}
	else
	{
#if LINUX_VERSION_CODE <= KERNEL_VERSION(3, 18, 0)
		inode = pfile->f_dentry->d_inode;
#else
		inode = pfile->f_path.dentry->d_inode;
#endif

		fsize = inode->i_size;

		DBG_INFO("fsize = %d", fsize);

		if (fsize <= 0)
		{
			DBG_ERR("The size of file is zero");
			goto out;
		}
		else
		{

			hex_buffer = kzalloc(sizeof(uint8_t) * fsize, GFP_KERNEL);
			flash_fw = kzalloc(sizeof(uint8_t) * fsize, GFP_KERNEL);
			memset(flash_fw, 0xff, sizeof(flash_fw));
#ifdef FLASH_SECTOR
			Ssize = fsize / flashtab->sector;
			ffls = kcalloc(Ssize, sizeof(uint32_t) * Ssize, GFP_KERNEL);
#endif
			// store current userspace mem segment.
			old_fs = get_fs();

			// set userspace mem segment equal to kernel's one.
			set_fs(get_ds());

			// read firmware data from userspace mem segment
			vfs_read(pfile, hex_buffer, fsize, &pos);

			// restore userspace mem segment after read.
			set_fs(old_fs);

			res = convert_hex_file(hex_buffer, fsize, isIRAM);

			if (res < 0)
			{
				DBG_ERR("Failed to covert firmware data, res = %d", res);
				goto out;
			}
			else
			{
				// calling that function defined at init depends on chips.
				res = core_firmware->upgrade_func(isIRAM);				
				if (res < 0)
				{
					DBG_ERR("Failed to upgrade firmware, res = %d", res);
					goto out;
				}

				core_firmware->isUpgraded = true;
			}
		}
	}

	if (core_firmware->isUpgraded)
	{
		DBG_INFO("Update firmware information...");
		core_config_get_fw_ver();
		core_config_get_protocol_ver();
		core_config_get_core_ver();
		core_config_get_tp_info();
		//core_config_get_key_info();

		//FIXME
		for (; i < ARRAY_SIZE(core_config->firmware_ver); i++)
		{
			core_firmware->new_fw_ver[i] = core_config->firmware_ver[i];
			DBG("new_fw_ver[%d] = %x : ", i, core_firmware->new_fw_ver[i])
		}
	}

out:
	filp_close(pfile, NULL);
	kfree(hex_buffer);
	kfree(flash_fw);
	return res;
}

int core_firmware_init(void)
{
	int i = 0, j = 0, res = 0;

	for (; i < nums_chip; i++)
	{
		if (SUP_CHIP_LIST[i] == ON_BOARD_IC)
		{
			core_firmware = kzalloc(sizeof(*core_firmware), GFP_KERNEL);

			core_firmware->chip_id = SUP_CHIP_LIST[i];

			for (j = 0; j < 4; j++)
			{
				core_firmware->old_fw_ver[i] = core_config->firmware_ver[i];
				core_firmware->new_fw_ver[i] = 0x0;
			}

			if (core_firmware->chip_id == CHIP_TYPE_ILI2121)
			{
				core_firmware->isCRC = false;
				core_firmware->upgrade_func = ili2121_firmware_upgrade;
			}
			else if (core_firmware->chip_id == CHIP_TYPE_ILI7807)
			{
				core_firmware->max_count = 0xFFFF;
				core_firmware->isCRC = false;
				core_firmware->upgrade_func = ili7807_firmware_upgrade;
				core_firmware->delay_after_upgrade = 100;
			}
			else if (core_firmware->chip_id == CHIP_TYPE_ILI9881)
			{
				core_firmware->max_count = 0x1FFFF;
				core_firmware->isCRC = true;
				core_firmware->upgrade_func = ili7807_firmware_upgrade;
				core_firmware->delay_after_upgrade = 200;
			}
		}
	}

	if (IS_ERR(core_firmware))
	{
		DBG_ERR("Can't find an id from the support list, init core_firmware failed");
		res = -ENOMEM;
		goto out;
	}

	return res;

out:
	core_firmware_remove();
	return res;
}

void core_firmware_remove(void)
{
	DBG_INFO("Remove core-firmware members");

	kfree(core_firmware);
}
