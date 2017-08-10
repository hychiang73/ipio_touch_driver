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

#define CHECK(X,Y) ((X==Y) ? 0 : -1 )

extern uint32_t SUP_CHIP_LIST[];
extern int nums_chip;

// the size of two arrays is different, depending on
// which of methods to upgrade firmware you choose for.
uint8_t *flash_fw = NULL;
uint8_t iram_fw[MAX_IRAM_FIRMWARE_SIZE] = {0};

// the length of array in each sector
int sec_length = 0;

struct flash_sector
{
	uint32_t ss_addr;
	uint32_t se_addr;
	uint32_t checksum; 
	uint32_t crc32;
	uint32_t dlength;
	bool data_flag;
};

struct flash_block_info
{
	char *block_name;
	uint32_t start_addr;
	uint32_t end_addr;
};

struct flash_sector *ffls;
struct flash_block_info fbi[4];
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
	uint32_t len = start_addr + end_addr;

	for (i = start_addr; i < len; i++)
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

static uint32_t ili7807_check_data(uint32_t start_addr, uint32_t end_addr)
{
	int timer = 500;
	uint32_t write_len = 0;
	uint32_t iram_check = 0;

	write_len = end_addr;

	DBG("start = 0x%x , write_len = 0x%x, max_count = %x", 
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

static void calc_verify_data(uint32_t sa, uint32_t se, uint32_t *check)
{
	uint32_t i = 0;
	uint32_t tmp_ck = 0, tmp_crc = 0;

	if(core_firmware->isCRC)
	{
		tmp_crc = calc_crc32(sa, se, flash_fw);
		*check = tmp_crc;
	}
	else
	{
		for(i = sa; i < (sa + se); i++)
			tmp_ck = tmp_ck + flash_fw[i];

		*check = tmp_ck;	
	}
}

static int do_check(uint32_t start, uint32_t len)
{
	int res = 0;
	uint32_t vd = 0, lc = 0;

	calc_verify_data(start, len, &lc);
	vd = ili7807_check_data(start, len);
	res = CHECK(vd, lc);

	DBG_INFO("%s (%x) : (%x)", (res < 0 ? "Invalid !" : "Correct !"), vd, lc );

	return res;	
}

static int verify_flash_data(void)
{
	int i = 0, res = 0, len = 0;
	int fps = flashtab->sector;
	uint32_t ss = 0x0;

	// update max count and check type if chip type is different
	if(core_config->chip_id == CHIP_TYPE_ILI7807 &&
		core_config->chip_type == ILI7807_TYPE_H)
	{
			core_firmware->max_count = 0x1FFFF;
			core_firmware->isCRC = true;
	}

	for(i = 0; i < sec_length + 1; i++)
	{
		if(ffls[i].data_flag)
		{
			if(ss > ffls[i].ss_addr || len == 0)
				ss = ffls[i].ss_addr;

			len = len + ffls[i].dlength;

			// if larger than max count, then committing data to check
			if(len >= (core_firmware->max_count - fps))
			{				
				res = do_check(ss, len);
				if(res < 0)
					goto out;

				ss = ffls[i].ss_addr;
				len = 0;
			}
		}
		else
		{
			// split sector and commit last data to check
			// if this sector doesn't have any data
			if(len != 0)
			{
				res = do_check(ss, len);
				if(res < 0)
					goto out;
				
				ss = ffls[i].ss_addr;
				len = 0;
			}
		}
	}

	// it might be lower than the size of sector if calc the last array.
	if(len != 0 && res != -1)
		res = do_check(ss, core_firmware->end_addr - ss);

out:		
	return res;
}

static int flash_polling_busy(void)
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

static int flash_write_enable(void)
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

static int flash_program_sector(void)
{
	int i, j, res = 0;
	uint32_t k;
	uint8_t buf[512] = {0};

	for(i = 0; i < sec_length + 1; i++)
	{
		if(!ffls[i].data_flag)
				continue;
	
		for(j = ffls[i].ss_addr; j < ffls[i].se_addr; j+= flashtab->program_page)
		{
			if(j > core_firmware->end_addr)
				goto out;

			res = flash_write_enable();
			if (res < 0)
				goto out;

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

			for (k = 0; k < flashtab->program_page; k++)
			{
				if (j + k <= core_firmware->end_addr)
					buf[4 + k] = flash_fw[j + k];
				else
					buf[4 + k] = 0xFF;
			}

			if (core_i2c_write(core_config->slave_i2c_addr, buf, flashtab->program_page + 4) < 0)
			{
				DBG_ERR("Failed to write data at j = 0x%X, k = 0x%X, addr = 0x%x", 
							j, k, j+k);
				res = -EIO;
				goto out;
			}

			// CS high
			core_config_ice_mode_write(0x041000, 0x1, 1);

			res = flash_polling_busy();
			if (res < 0)
				goto out;

			// holding the status until finish this upgrade.
			if(core_firmware->update_status > 90)
				continue;

			core_firmware->update_status = (j * 101) / core_firmware->end_addr;
			printk("%cUpgrading firmware ... (0x%x...0x%x), %02d%c", 0x0D, ffls[i].ss_addr, ffls[i].se_addr, core_firmware->update_status, '%');
		}
	}

out:
	return res;
}

static int flash_erase_sector(void)
{
	int i, res = 0;
	uint32_t temp_buf = 0;

	for(i = 0; i < sec_length + 1; i++)
	{
		if(!ffls[i].data_flag)
			continue;

		res = flash_write_enable();
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

		res = flash_polling_busy();
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

out:
	return res;
}

static int iram_upgrade(void)
{
	int i, j, res = 0;
	uint8_t buf[512];
	int upl = flashtab->program_page;

	core_firmware->update_status = 0;

	// doing reset for erasing iram data before upgrade it.
	ilitek_platform_tp_hw_reset(true);

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

		core_firmware->update_status = (i * 101) / core_firmware->end_addr;
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

static int tddi_fw_upgrade(bool isIRAM)
{
	int res = 0;

	if (isIRAM)
	{
		res = iram_upgrade();
		return res;
	}

	ilitek_platform_tp_hw_reset(true);

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
	if (core_config->chip_id == CHIP_TYPE_ILI7807 
			&& core_config->chip_type == ILI7807_TYPE_F_AB)
	{
		res = core_config_ice_mode_write(0x4100C, 0x01, 1);
		if (res < 0)
			goto out;
	}

	mdelay(25);

	// there is no need to disable WTD if you're using 9881
	if (core_config->chip_id != CHIP_TYPE_ILI9881)
		core_config_reset_watch_dog();

	res = flash_erase_sector();
	if(res < 0)
	{
		DBG_ERR("Failed to erase flash");
		goto out;
	}

	mdelay(1);

	res = flash_program_sector();
	if(res < 0)
	{
		DBG_ERR("Failed to program flash");
		goto out;
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

	if (core_config->chip_id != CHIP_TYPE_ILI9881)
		core_config_reset_watch_dog();

	// check the data that we've just written into the iram.
	res = verify_flash_data();
	if(res == 0)
		DBG_INFO("Data Correct !");

out:
	core_config_ice_mode_disable();
	return res;
}

static int convert_hex_file(uint8_t *pBuf, uint32_t nSize, bool isIRAM)
{
	uint32_t i = 0, j = 0, k = 0;
	uint32_t nLength = 0, nAddr = 0, nType = 0;
	uint32_t nStartAddr = 0xFFF, nEndAddr = 0x0, nChecksum = 0x0,nExAddr = 0;
	uint32_t CRC32 = 0;

	int index = 0, block = 0;
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

		if (nType == 0xAE)
		{
			/* insert block info extracted from hex */
			if(block < 4)
			{
				fbi[block].start_addr = HexToDec(&pBuf[i + 9], 6);
				fbi[block].end_addr = HexToDec(&pBuf[i + 9 + 6], 6);
				DBG_INFO("fbi[%d].name = %s, start_addr = %x, end = %x", 
					block, fbi[block].block_name, fbi[block].start_addr, fbi[block].end_addr);
			}
			block++;
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

/*
 * It would basically be called by ioctl when users want to upgrade firmware.
 *
 * @pFilePath: pass a path where locates user's firmware file.
 *
 */
int core_firmware_upgrade(const char *pFilePath, bool isIRAM)
{
	int res = 0, i = 0, fsize, Ssize;
	uint8_t *hex_buffer = NULL;

	struct file *pfile = NULL;
	struct inode *inode;
	mm_segment_t old_fs;
	loff_t pos = 0;

	core_firmware->isUpgraded = false;
	core_firmware->update_status = 0;

	//TODO: to compare old/new version if upgraded.

	pfile = filp_open(pFilePath, O_RDONLY, 0);
	if (IS_ERR(pfile))
	{
		DBG_ERR("Failed to open the file at %s.", pFilePath);
		res = -ENOENT;
		goto fail;
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
			res = -EINVAL;
			goto out;
		}
		else
		{
			if(flashtab == NULL)
			{
				DBG_ERR("Flash table isn't created");
				res = -ENOMEM;
				goto out;
			}

			hex_buffer = kzalloc(sizeof(uint8_t) * fsize, GFP_KERNEL);
			flash_fw = kzalloc(sizeof(uint8_t) * flashtab->mem_size, GFP_KERNEL);
			memset(flash_fw, 0xff, (int)sizeof(flash_fw));
			Ssize = flashtab->mem_size / flashtab->sector;
			ffls = kcalloc(Ssize, sizeof(uint32_t) * Ssize, GFP_KERNEL);

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
				core_firmware->update_status = 100;
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
fail:
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

			/* set default address in each block */
			fbi[0].block_name = "AP";
			fbi[0].start_addr = 0x0;
			fbi[0].end_addr = 0xFFFF;
			// the below are fakse addresses, just for test
			fbi[1].block_name = "MP";
			fbi[1].start_addr = 0x10000;
			fbi[1].end_addr = 0x12000;
			fbi[2].block_name = "Driver";
			fbi[2].start_addr = 0x12000;
			fbi[2].end_addr = 0x14000;
			fbi[3].block_name = "Gesture";
			fbi[3].start_addr = 0x14000;
			fbi[3].end_addr = 0x16000;

			for (j = 0; j < 4; j++)
			{
				core_firmware->old_fw_ver[i] = core_config->firmware_ver[i];
				core_firmware->new_fw_ver[i] = 0x0;
			}

			if (core_config->chip_id == CHIP_TYPE_ILI7807)
			{
				core_firmware->max_count = 0xFFFF;
				core_firmware->isCRC = false;
				core_firmware->upgrade_func = tddi_fw_upgrade;
				core_firmware->delay_after_upgrade = 100;
			}
			else if (core_config->chip_id == CHIP_TYPE_ILI9881)
			{
				core_firmware->max_count = 0x1FFFF;
				core_firmware->isCRC = true;
				core_firmware->upgrade_func = tddi_fw_upgrade;
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
