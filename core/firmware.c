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

extern CORE_CONFIG *core_config;
extern uint32_t SUP_CHIP_LIST[];
extern int nums_chip;

CORE_FIRMWARE *core_firmware;

uint8_t flash_fw[MAX_FLASH_FIRMWARE_SIZE];

// IRAM test 
//#define IRAM_TEST

#ifdef IRAM_TEST
uint8_t iram_fw[MAX_IRAM_FIRMWARE_SIZE];
#endif

static uint32_t HexToDec(char *pHex, int32_t nLength)
{
    uint32_t nRetVal = 0, nTemp = 0, i;
    int32_t nShift = (nLength - 1) * 4;

    for (i = 0; i < nLength; nShift -= 4, i ++)
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

static int CheckSum(uint32_t nStartAddr, uint32_t nEndAddr)
{
	u16 i = 0, nInTimeCount = 100;
	u8 szBuf[64] = {0};

	core_config_ice_mode_write(0x4100B, 0x23, 1);
	core_config_ice_mode_write(0x41009, nEndAddr, 2);
	core_config_ice_mode_write(0x41000, 0x3B | (nStartAddr << 8), 4);
	core_config_ice_mode_write(0x041004, 0x66AA5500, 4);

	for (i = 0; i < nInTimeCount; i++)
	{
		szBuf[0] = core_config_read_write_onebyte(0x41011);

		if ((szBuf[0] & 0x01) == 0)
		{
			break;
		}
		mdelay(100);
	} 		

	return core_config_ice_mode_read(0x41018);
}

#ifdef IRAM_TEST
static int iram_upgrade(void)
{
	int i, j, k, res = 0;
	int update_page_len = UPDATE_FIRMWARE_PAGE_LENGTH;
	uint8_t buf[512];

	DBG_INFO("Upgrade firmware written data into IRAM directly");

	ilitek_platform_ic_reset();

	mdelay(1);

	res = core_config_ice_mode_enable();
	if(res < 0)
	{
		DBG_ERR("Failed to enter ICE mode, res = %d", res);
		return res;
	}

	core_config_ice_mode_write(0x4100C, 0x01, 0);

	mdelay(20);

	core_config_reset_watch_dog();

	DBG_INFO("nStartAddr = 0x%06X, nEndAddr = 0x%06X, nChecksum = 0x%06X",
				core_firmware->start_addr, core_firmware->end_addr, core_firmware->checksum);

	// write hex to the addr of iram
	for(i = core_firmware->start_addr; i < core_firmware->end_addr; i += UPDATE_FIRMWARE_PAGE_LENGTH)
	{
		if((i + 256) > core_firmware->end_addr)
		{
			update_page_len = core_firmware->end_addr % UPDATE_FIRMWARE_PAGE_LENGTH;
		}

		buf[0] = 0x25;
		buf[3] = (char)((i & 0x00FF0000) >> 16);
		buf[2] = (char)((i & 0x0000FF00) >> 8);
		buf[1] = (char)((i & 0x000000FF));

		for(j = 0; j < update_page_len; j++)
		{
			buf[4 + j] = iram_fw[i + j];
		//	DBG_INFO("write --- buf[4 + %d] = %x", j, buf[4+j])
		}

		if(core_i2c_write(core_config->slave_i2c_addr, buf, update_page_len + 4))
		{
            DBG_INFO("Failed to write data via i2c, address = 0x%X, start_addr = 0x%X, end_addr = 0x%X", 
					(int)i, (int)core_firmware->start_addr, (int)core_firmware->end_addr);
			res = -EIO;
            return res;
		}

        printk("%cupgrade firmware(ap code), %02d%c", 0x0D, (i * 100) / core_firmware->end_addr, '%');

		mdelay(3);
	}

	// ice mode code reset
	DBG_INFO("ice mode code reset");
	core_config_ice_mode_write(0x40040, 0xAE, 1);
//	mdelay(1);
	core_config_ice_mode_write(0x40040, 0x00, 1);

	mdelay(10);

	core_config_ice_mode_disable();
	//core_config_ice_mode_reset();

	//TODO: check iram status

	return res;
}
#endif //IRAM_TEST

static int32_t convert_firmware(uint8_t *pBuf, uint32_t nSize)
{
    uint32_t i = 0, j = 0, k = 0;
	uint32_t nApStartAddr = 0xFFFF, nDfStartAddr = 0xFFFF, nExAddr = 0;
    uint32_t nApEndAddr = 0x0, nDfEndAddr = 0x0;
    uint32_t nApChecksum = 0x0, nDfChecksum = 0x0, nLength = 0, nAddr = 0, nType = 0;
	uint32_t nStartAddr = 0xFFF, nEndAddr = 0x0, nChecksum = 0x0;

	DBG_INFO("size = %d", nSize);

	core_firmware->ap_start_addr = 0;
	core_firmware->ap_end_addr = 0;
	core_firmware->ap_checksum = 0;

	if(nSize != 0)
	{
		for(; i < nSize ; )
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

			if (pBuf[i+1+j+2] == 0x0D)
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
				for (j = 0, k = 0; j < (nLength * 2); j += 2, k ++)
				{
#ifdef IRAM_TEST 
					iram_fw[nAddr + k] = HexToDec(&pBuf[i + 9 + j], 2);
#endif
					flash_fw[nAddr + k] = HexToDec(&pBuf[i + 9 + j], 2);
				}
			}

			i += 1 + 2 + 4 + 2 + (nLength * 2) + 2 + nOffset;        
		}

		core_firmware->ap_start_addr	= nApStartAddr;
		core_firmware->ap_end_addr		= nApEndAddr;
		core_firmware->ap_checksum		= nApChecksum;

		core_firmware->df_start_addr	= nDfStartAddr;
		core_firmware->df_end_addr		= nDfEndAddr;
		core_firmware->df_checksum		= nDfChecksum;

		core_firmware->start_addr		= nStartAddr;
		core_firmware->end_addr			= nEndAddr;
		core_firmware->checksum			= nChecksum;

		DBG_INFO("nApStartAddr = 0x%06X, nApEndAddr = 0x%06X, nApChecksum = 0x%06X",
				nApStartAddr, nApEndAddr, nApChecksum);
		DBG_INFO("nDfStartAddr = 0x%06X, nDfEndAddr = 0x%06X, nDfChecksum = 0x%06X",
				nDfStartAddr, nDfEndAddr, nDfChecksum);
		DBG_INFO("nStartAddr = 0x%06X, nEndAddr = 0x%06X, nChecksum = 0x%06X",
				nStartAddr, nEndAddr, nChecksum);

		return 0;
	}

	return -1;
}

static int ili7807_check_data(void)
{
	int i, res = 0;
	uint32_t write_len = 0;
	uint32_t iram_check = 0;

	if(core_config->chip_type == ILI7807_TYPE_F)
	{
		write_len = core_firmware->ap_end_addr + 1;

		core_config_ice_mode_write(0x041000, 0x0, 1);
		core_config_ice_mode_write(0x041004, 0x66aa55, 3);
		core_config_ice_mode_write(0x041008, 0x3b, 1);
		core_config_ice_mode_write(0x041008, core_firmware->ap_start_addr, 3);
		core_config_ice_mode_write(0x041003, 0x01, 1);
		core_config_ice_mode_write(0x041008, 0xFF, 1);
		core_config_ice_mode_write(0x04100C, write_len, 2);
		core_config_ice_mode_write(0x041014, 0x10000, 3);
		core_config_ice_mode_write(0x041010, 0xFF, 1);

		mdelay(1);

		for(i = 0; i < 500; i++)
		{
			if(core_config_read_write_onebyte(0x041014) == 0x01)
			{
				break;
			}

			mdelay(1);

			if(i == 500)
			{
				DBG_ERR("Time out");
				goto out;
			}
		}

		core_config_ice_mode_write(0x041000, 0x1, 1);
		core_config_ice_mode_write(0x041003, 0x0, 1);

		iram_check = core_config_ice_mode_read(0x041018);
	}
	else if(core_config->chip_type == ILI7807_TYPE_H)
	{
		//TODO: implement the way to get checksum
	}

	if(core_firmware->ap_checksum != iram_check)
	{
		DBG_ERR("Checksum Error: %x , %x", core_firmware->ap_checksum, iram_check);
		goto out;
	}

	return res;

out:
	DBG_ERR("Failed to get correct data");
	return -1;
}

static int ili7807_polling_flash_busy(void)
{
	int i, res = 0;

	core_config_ice_mode_write(0x041000, 0x0, 1);
	core_config_ice_mode_write(0x041004, 0x66aa55, 3);
	core_config_ice_mode_write(0x041008, 0x5, 1);

	for(i = 0; i < 500; i++)
	{
		core_config_ice_mode_write(0x041008, 0xFF, 1);
		core_config_ice_mode_write(0x041000, 0x1, 1);

		mdelay(10);

		if(core_config_read_write_onebyte(0x041010) == 0x0)
		{
			break;
		}

		if(i == 500)
			return -EIO;
	}
		
	res = core_config_ice_mode_write(0x041000, 0x1, 1);

	return 0;
}

static int ili7807_write_enable(void)
{
	int res = 0;

	if(core_config_ice_mode_write(0x041000, 0x0, 1) < 0)
		goto out;
	if(core_config_ice_mode_write(0x041004, 0x66aa55, 3) < 0)
		goto out;
	if(core_config_ice_mode_write(0x041008, 0x6, 1) < 0)
		goto out;
	if(core_config_ice_mode_write(0x041000, 0x1, 1) < 0)
		goto out;

	return 0;

out:
	return -EIO;
}

static int ili7807_firmware_upgrade(void)
{
	int i, j, k, res = 0;
	uint32_t erase_start_addr = 0;
    uint8_t buf[512] = {0};
	uint32_t temp_buf = 0;
	uint32_t iram_check = 0;

	core_firmware->update_status = 0;

#ifdef IRAM_TEST
	res = iram_upgrade();
	return res;
#else
	
	DBG_INFO("Enter to ICE Mode before updating firmware ... ");

	res = core_config_ice_mode_enable();
	if(res < 0)
	{
		DBG_ERR("Failed to enable ICE mode");
		goto out;
	}

	// set CS low
	if(core_config->chip_type == ILI7807_TYPE_F)
	{
		res =core_config_ice_mode_write(0x4100C, 0x01, 0);
		if(res < 0)
		{
			DBG_ERR("Failed to set CS as Low");
			goto out;
		}
	}

	mdelay(20);

	core_config_reset_watch_dog();

	DBG_INFO("Erase Flash ...");
	for(erase_start_addr = 0; erase_start_addr < 0xE0; )
	{
		res = ili7807_write_enable();
		if(res < 0)
		{
			DBG_ERR("Failed to config write enable");
			goto out;
		}

		//sector erase
		core_config_ice_mode_write(0x041000, 0x0, 1);
		core_config_ice_mode_write(0x041004, 0x66aa55, 3);
		core_config_ice_mode_write(0x041008, 0x20, 1);
		core_config_ice_mode_write(0x041008, 0x0, 1);//Addr_H
		core_config_ice_mode_write(0x041008, erase_start_addr, 1);//Addr_M
		core_config_ice_mode_write(0x041008, 0x0, 1);//Addr_L	
		core_config_ice_mode_write(0x041000, 0x1, 1);

		mdelay(1);

		res = ili7807_polling_flash_busy();
		if(res < 0)
		{
			DBG_ERR("TIME OUT");
			goto out;
		}

		// read flash data
		core_config_ice_mode_write(0x041000, 0x0, 1);
		core_config_ice_mode_write(0x041004, 0x66aa55, 3);
		core_config_ice_mode_write(0x041008, 0x3, 1);
		core_config_ice_mode_write(0x041008, 0x0, 1);
		core_config_ice_mode_write(0x041008, erase_start_addr, 1);
		core_config_ice_mode_write(0x041008, 0x0, 1);
		core_config_ice_mode_write(0x041008, 0xFF, 1);

		temp_buf = core_config_read_write_onebyte(0x041010);
		if(temp_buf != 0xFF)
		{
			DBG_ERR("Failed to read data at 0x%x ", erase_start_addr);
		}

		erase_start_addr = erase_start_addr + 0x10;
		core_config_ice_mode_write(0x041000, 0x1, 1);
	}

	mdelay(1);

	//write data into flash
	DBG_INFO("write data info flash ...");
    for (i = core_firmware->ap_start_addr; i < core_firmware->ap_end_addr; i += UPDATE_FIRMWARE_PAGE_LENGTH)
	{
		res = ili7807_write_enable();
		if(res < 0)
		{
			DBG_ERR("Failed to config write enable");
			goto out;
		}

		core_config_ice_mode_write(0x041000, 0x0, 1);
		core_config_ice_mode_write(0x041004, 0x66aa55, 3);
		core_config_ice_mode_write(0x041008, 0x02, 1);
		core_config_ice_mode_write(0x041008, (i & 0xFF0000) >> 16, 1);//Addr_H
		core_config_ice_mode_write(0x041008, (i & 0x00FF00) >> 8, 1);//Addr_M
		core_config_ice_mode_write(0x041008, (i & 0x0000FF), 1);//Addr_L	

		buf[0] = 0x25;
		buf[3] = 0x04;
		buf[2] = 0x10;
		buf[1] = 0x08;

		for(j = 0; j < UPDATE_FIRMWARE_PAGE_LENGTH; j++)
		{
			if(i + j <= core_firmware->ap_end_addr)
				buf[4 + j] = flash_fw[i + j];
			else
				buf[4 + j] = 0xFF;
		}

		if (core_i2c_write(core_config->slave_i2c_addr, buf, UPDATE_FIRMWARE_PAGE_LENGTH + 4) < 0)
		{
			DBG_INFO("Failed to write data at address = 0x%X, start_addr = 0x%X, end_addr = 0x%X"
					,(int)i, (int)core_firmware->ap_start_addr, (int)core_firmware->ap_end_addr);
			res = -EIO;
			goto out;
		}

		core_config_ice_mode_write(0x041000, 0x1, 1);

		res = ili7807_polling_flash_busy();
		if(res < 0)
		{
			DBG_ERR("TIME OUT");
			goto out;
		}

        core_firmware->update_status = (i * 101) / core_firmware->ap_end_addr;
        printk("%cupgrade firmware(ap code), %02d%c", 0x0D, core_firmware->update_status, '%');
	}

	// it's fine if got an error in reset ice mode at second time.
	core_config_ic_reset(core_firmware->chip_id);

	mdelay(50);

	res = core_config_ice_mode_enable();
	mdelay(20);
	if(res < 0)
	{
		DBG_ERR("Failed to enable ICE mode");
		goto out;
	}

	core_config_reset_watch_dog();

	res = ili7807_check_data();
	if(res < 0)
	{
		DBG_ERR("The data written into flash isn't correct");
	}

out:
	core_config_ice_mode_disable();
	core_config_ic_reset(core_firmware->chip_id);
	return res;

#endif
}

static int ili2121_firmware_upgrade(void)
{
    int32_t nUpdateRetryCount = 0, nUpgradeStatus = 0, nUpdateLength = 0;
	int32_t	nCheckFwFlag = 0, nChecksum = 0, i = 0, j = 0, k = 0;
    uint8_t szFwVersion[4] = {0};
    uint8_t szBuf[512] = {0};
	uint8_t szCmd[2] = {0};
    uint32_t nApStartAddr = 0, nDfStartAddr = 0, nApEndAddr = 0, nDfEndAddr = 0;
	uint32_t nApChecksum = 0, nDfChecksum = 0, nTemp = 0, nIcChecksum = 0;
	int res = 0;

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
		if(res < 0)
			return res;

        mdelay(3);

		res = core_config_ice_mode_write(0x041004, 0x66aa5500, 4); 
		if(res < 0)
			return res;

        mdelay(3);
        
        nTemp = (i << 8) + 0x20;

		res = core_config_ice_mode_write(0x041000, nTemp, 4); 
		if(res < 0)
			return res;

        mdelay(3);
        
		res = core_config_ice_mode_write(0x041004, 0x66aa5500, 4); 
		if(res < 0)
			return res;

        mdelay(20);
        
        for (j = 0; j < 50; j ++)
        {
			res = core_config_ice_mode_write(0x041000, 0x05, 1); 
			if(res < 0)
				return res;

			res = core_config_ice_mode_write(0x041004, 0x66aa5500, 4); 
			if(res < 0)
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
			nApStartAddr, nApEndAddr, UPDATE_FIRMWARE_PAGE_LENGTH);

    for (i = nApStartAddr; i < nApEndAddr; i += UPDATE_FIRMWARE_PAGE_LENGTH)
    {
		res = core_config_ice_mode_write(0x041000, 0x06, 1); 
		if(res < 0)
			return res;

		res = core_config_ice_mode_write(0x041004, 0x66aa5500, 4); 
		if(res < 0)
			return res;

        nTemp = (i << 8) + 0x02;

		res = core_config_ice_mode_write(0x041000, nTemp, 4); 
		if(res < 0)
			return res;

        res = core_config_ice_mode_write(0x041004, 0x66aa5500 + UPDATE_FIRMWARE_PAGE_LENGTH - 1, 4);
		if(res < 0)
			return res;

        szBuf[0] = 0x25;
        szBuf[3] = (char)((0x041020 & 0x00FF0000) >> 16);
        szBuf[2] = (char)((0x041020 & 0x0000FF00) >> 8);
        szBuf[1] = (char)((0x041020 & 0x000000FF));
        
        for (k = 0; k < UPDATE_FIRMWARE_PAGE_LENGTH; k ++)
        {
            szBuf[4 + k] = flash_fw[i + k];
        }

        if (core_i2c_write(core_config->slave_i2c_addr, szBuf, UPDATE_FIRMWARE_PAGE_LENGTH + 4) < 0) {
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
    nIcChecksum = CheckSum(nApStartAddr, nApEndAddr);

    DBG_INFO("nIcChecksum = 0x%X, nApChecksum = 0x%X\n", nIcChecksum, nApChecksum);

	if (nIcChecksum != nApChecksum)
	{
		//TODO: may add a retry func as protection.
		
		core_config_ic_reset(core_firmware->chip_id);
		DBG_INFO("Both checksum didn't match");
		res = -1;
		return res;
	}

	core_config_ic_reset(core_firmware->chip_id);

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

#if 0
int core_firmware_iram_upgrade(const char *pf)
{
	int i, j, k, res = 0;
	int iram_end_addr = 0x0, update_page_len = 256;
	uint8_t buf[512];
	uint8_t cmd[4];
    struct file *pfile = NULL;
    struct inode *inode;
    uint32_t fsize = 0;
	int32_t nUpgradeStatus = 0;
    mm_segment_t old_fs;
    loff_t pos = 0;

    pfile = filp_open(pf, O_RDONLY, 0);
    if (IS_ERR(pfile))
    {
        DBG_ERR("Error occurred while opening file %s.", pf);
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
			res = -1;
		}
		else
		{

			iram_fw = kmalloc(sizeof(uint8_t) * fsize, GFP_KERNEL);

			// store current userspace mem segment.
			old_fs = get_fs();

			// set userspace mem segment equal to kernel's one.
			set_fs(KERNEL_DS);

			// read firmware data from userspace mem segment
			vfs_read(pfile, iram_fw, fsize, &pos);
			//vfs_read(pfile, fwdata_buffer, fsize, &pos);

			// restore userspace mem segment after read.
			set_fs(old_fs);

			res == convert_firmware(iram_fw, fsize);
			if( res < 0)
			{
				DBG_ERR("Failed to covert firmware data, res = %d", res);
				return res;
			}

		}
	}

	//core_firmware->end_addr = 0xCFFF;

	//iram_end_addr = fsize;

	//iram_end_addr = sizeof(iram_fw);

	//DBG_INFO("IRAM fw size = %d", iram_end_addr);
	// soft reset
	//core_config_ic_reset(core_firmware->chip_id);

	ilitek_platform_ic_power_on();

	//core_config_ice_mode_reset();

	udelay(1000);

	res = core_config_ice_mode_enable();
	if(res < 0)
	{
		DBG_ERR("Failed to enter ICE mode, res = %d", res);
		return res;
	}

	//mdelay(1);
	core_config_ice_mode_write(0x4100C, 0x01, 1);

	mdelay(20);

	// disable watch dog
	core_config_reset_watch_dog();

	DBG_INFO("core_firmware->start_addr = 0x%06X, nEndAddr = 0x%06X, nChecksum = 0x%06X",
				core_firmware->start_addr, core_firmware->end_addr, core_firmware->checksum);

	// write hex to the addr of iram
	for(i = core_firmware->start_addr; i < core_firmware->end_addr; i += 256)
	{
		if((i + 256) > core_firmware->end_addr)
		{
			update_page_len = core_firmware->end_addr % 256;
		}

		buf[0] = 0x25;
		buf[3] = (char)((i & 0x00FF0000) >> 16);
		buf[2] = (char)((i & 0x0000FF00) >> 8);
		buf[1] = (char)((i & 0x000000FF));

		for(j = 0; j < update_page_len; j++)
		{
			buf[4 + j] = iram_data[i + j];
		//	DBG_INFO("write --- buf[4 + %d] = %x", j, buf[4+j])
		}

		if(core_i2c_write(core_config->slave_i2c_addr, buf, update_page_len + 4))
		{
            DBG_INFO("Failed to write data via i2c, address = 0x%X, start_addr = 0x%X, end_addr = 0x%X", 
					(int)i, (int)core_firmware->start_addr, (int)core_firmware->end_addr);
			res = -EIO;
            return res;
		}

        nUpgradeStatus = (i * 100) / core_firmware->end_addr;
        printk("%cupgrade firmware(ap code), %02d%c", 0x0D, nUpgradeStatus, '%');

		mdelay(3);
	}

#if 0 //read iram data
	memset(buf, 0xFF, 512);
	update_page_len = 256;

	for(i = core_firmware->start_addr; i < core_firmware->end_addr; i += 256)
	{
		if((i + 256) > core_firmware->end_addr)
		{
			update_page_len = core_firmware->end_addr % 256;
		}

		buf[0] = 0x25;
		buf[3] = (char)((i & 0x00FF0000) >> 16);
		buf[2] = (char)((i & 0x0000FF00) >> 8);
		buf[1] = (char)((i & 0x000000FF));

		core_i2c_write(core_config->slave_i2c_addr, buf, 4);

		if(core_i2c_read(core_config->slave_i2c_addr, buf, update_page_len ))
		{
			DBG_INFO("Failed to read data via i2c, address = 0x%X, start_addr = 0x%X, end_addr = 0x%X", 
					(int)i, (int)core_firmware->start_addr, (int)core_firmware->end_addr);
			res = -EIO;
			return res;
		}

		for(j = 0; j < 3; j++)
		{
			if(i+j*16 > core_firmware->end_addr)
				break;

			printk("0x%06X: ", i+j*16);

			for(k = 0; k < 16; k++)
			{
				printk(" %02X", buf[k+j*16]);
			}
			printk("\n");
		}
	}
#endif

	// ice mode code reset
	DBG_INFO("ice mode code reset");
	core_config_ice_mode_write(0x40040, 0xAE, 1);

	mdelay(10);

	// leave ice mode without reset chip.
	core_config_ice_mode_disable();

	//TODO: check iram status

	filp_close(pfile, NULL);
	kfree(iram_fw);
	kfree(iram_data);
	return res;
}
#endif

/*
 * It would basically be called by ioctl when users want to upgrade firmware.
 *
 * @pFilePath: pass a path where locates user's firmware file.
 *
 */
int core_firmware_upgrade(const char *pFilePath)
{
	int res = 0, i = 0, fsize;
	uint8_t *hex_buffer;

    struct file *pfile = NULL;
    struct inode *inode;
    mm_segment_t old_fs;
    loff_t pos = 0;

	DBG_INFO("file path = %s", pFilePath);

	core_firmware->isUpgraded = false;

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
			goto Err;
		}
		else
		{

			hex_buffer = kmalloc(sizeof(uint8_t) * fsize, GFP_KERNEL);
			memset(hex_buffer, 0x0, sizeof(uint8_t) * fsize);

			// store current userspace mem segment.
			old_fs = get_fs();

			// set userspace mem segment equal to kernel's one.
			set_fs(KERNEL_DS);

			// read firmware data from userspace mem segment
			vfs_read(pfile, hex_buffer, fsize, &pos);

			// restore userspace mem segment after read.
			set_fs(old_fs);

			res == convert_firmware(hex_buffer, fsize);

			if( res < 0)
			{
				DBG_ERR("Failed to covert firmware data, res = %d", res);
				goto Err;
			}
			else
			{
				// calling that function defined at init depends on chips.
				res = core_firmware->upgrade_func();
				if(res < 0)
				{
					DBG_ERR("Failed to upgrade firmware, res = %d", res);
					goto Err;
				}

				core_firmware->isUpgraded = true;
			}
		}
	}

	// update firmware version if upgraded
	if(core_firmware->isUpgraded)
	{
		core_config_get_fw_ver();
		for(; i < 4; i++)
		{
			core_firmware->new_fw_ver[i] = core_config->firmware_ver[i];
		}
	}

Err:
	filp_close(pfile, NULL);
	kfree(hex_buffer);
	return res;
}

int core_firmware_init(void)
{
	int i = 0, j = 0, res = 0; 

	DBG_INFO();

	for(; i < nums_chip; i++)
	{
		if(SUP_CHIP_LIST[i] == ON_BOARD_IC)
		{
			core_firmware = (CORE_FIRMWARE*)kmalloc(sizeof(*core_firmware), GFP_KERNEL);

			core_firmware->chip_id = SUP_CHIP_LIST[i];

			for(j = 0; j < 4; j++)
			{
				core_firmware->old_fw_ver[i] = core_config->firmware_ver[i];
				core_firmware->new_fw_ver[i] = 0x0;
			}

			core_firmware->ap_start_addr	= 0x0;
			core_firmware->ap_end_addr		= 0x0;
			core_firmware->df_start_addr	= 0x0;
			core_firmware->df_end_addr		= 0x0;
			core_firmware->ap_checksum		= 0x0;
			core_firmware->ap_crc			= 0x0;
			core_firmware->df_checksum		= 0x0;
			core_firmware->df_crc			= 0x0;
			core_firmware->start_addr		= 0x0;
			core_firmware->end_addr			= 0x0;

			core_firmware->isUpgraded		= false;
			core_firmware->update_status	= 0;
			core_firmware->isCRC			= false;

			if(core_firmware->chip_id == CHIP_TYPE_ILI2121)
			{
				core_firmware->upgrade_func		= ili2121_firmware_upgrade;
			}
			else if(core_firmware->chip_id == CHIP_TYPE_ILI7807)
			{
				core_firmware->upgrade_func		= ili7807_firmware_upgrade;
			}
			else if(core_firmware->chip_id == CHIP_TYPE_ILI9881)
			{
				core_firmware->upgrade_func		= ili7807_firmware_upgrade;
			}
		}
	}

	if(IS_ERR(core_firmware)) 
	{
		DBG_ERR("Can't find an id from the support list, init core_firmware failed");
		res = -ENOMEM;
		goto Err;
	}

	return res;

Err:
	core_firmware_remove();
	return res;
}

void core_firmware_remove(void)
{
	DBG_INFO();

	kfree(core_firmware);
}
