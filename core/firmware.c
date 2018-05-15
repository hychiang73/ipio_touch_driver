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

#include "../common.h"
#include "../platform.h"
#include "config.h"
#include "i2c.h"
#include "firmware.h"
#include "flash.h"
#include "protocol.h"
#include "finger_report.h"

#ifdef BOOT_FW_UPGRADE
#include "ilitek_fw.h"
#endif
#ifdef HOST_DOWNLOAD
#include "ilitek_fw.h"
#endif
extern uint32_t SUP_CHIP_LIST[];
extern int nums_chip;
extern struct core_fr_data *core_fr;
/*
 * the size of two arrays is different depending on
 * which of methods to upgrade firmware you choose for.
 */
uint8_t *flash_fw = NULL;

//#ifdef HOST_DOWNLOAD
uint8_t ap_fw[MAX_AP_FIRMWARE_SIZE] = { 0 };
uint8_t dlm_fw[MAX_DLM_FIRMWARE_SIZE] = { 0 };
uint8_t mp_fw[MAX_MP_FIRMWARE_SIZE] = { 0 };
//#else
uint8_t iram_fw[MAX_IRAM_FIRMWARE_SIZE] = { 0 };
//#endif
/* the length of array in each sector */
int g_section_len = 0;
int g_total_sector = 0;

#ifdef BOOT_FW_UPGRADE
/* The addr of block reserved for customers */
int g_start_resrv = 0x1C000;
int g_end_resrv = 0x1CFFF;
#endif

struct flash_sector {
	uint32_t ss_addr;
	uint32_t se_addr;
	uint32_t checksum;
	uint32_t crc32;
	uint32_t dlength;
	bool data_flag;
	bool inside_block;
};

struct flash_block_info {
	uint32_t start_addr;
	uint32_t end_addr;
};

struct flash_sector *g_flash_sector = NULL;
struct flash_block_info g_flash_block_info[4];
struct core_firmware_data *core_firmware = NULL;

static uint32_t HexToDec(char *pHex, int32_t nLength)
{
	uint32_t nRetVal = 0, nTemp = 0, i;
	int32_t nShift = (nLength - 1) * 4;

	for (i = 0; i < nLength; nShift -= 4, i++) {
		if ((pHex[i] >= '0') && (pHex[i] <= '9')) {
			nTemp = pHex[i] - '0';
		} else if ((pHex[i] >= 'a') && (pHex[i] <= 'f')) {
			nTemp = (pHex[i] - 'a') + 10;
		} else if ((pHex[i] >= 'A') && (pHex[i] <= 'F')) {
			nTemp = (pHex[i] - 'A') + 10;
		} else {
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

	for (i = start_addr; i < len; i++) {
		ReturnCRC ^= (data[i] << 24);

		for (j = 0; j < 8; j++) {
			if ((ReturnCRC & 0x80000000) != 0) {
				ReturnCRC = ReturnCRC << 1 ^ CRC_POLY;
			} else {
				ReturnCRC = ReturnCRC << 1;
			}
		}
	}

	return ReturnCRC;
}

static uint32_t tddi_check_data(uint32_t start_addr, uint32_t end_addr)
{
	int timer = 500;
	uint32_t busy = 0;
	uint32_t write_len = 0;
	uint32_t iram_check = 0;
	uint32_t id = core_config->chip_id;
	uint32_t type = core_config->chip_type;

	write_len = end_addr;

	ipio_debug(DEBUG_FIRMWARE, "start = 0x%x , write_len = 0x%x, max_count = %x\n",
	    start_addr, end_addr, core_firmware->max_count);

	if (write_len > core_firmware->max_count) {
		ipio_err("The length (%x) written to firmware is greater than max count (%x)\n",
			write_len, core_firmware->max_count);
		goto out;
	}

	core_config_ice_mode_write(0x041000, 0x0, 1);	/* CS low */
	core_config_ice_mode_write(0x041004, 0x66aa55, 3);	/* Key */

	core_config_ice_mode_write(0x041008, 0x3b, 1);
	core_config_ice_mode_write(0x041008, (start_addr & 0xFF0000) >> 16, 1);
	core_config_ice_mode_write(0x041008, (start_addr & 0x00FF00) >> 8, 1);
	core_config_ice_mode_write(0x041008, (start_addr & 0x0000FF), 1);

	core_config_ice_mode_write(0x041003, 0x01, 1);	/* Enable Dio_Rx_dual */
	core_config_ice_mode_write(0x041008, 0xFF, 1);	/* Dummy */

	/* Set Receive count */
	if (core_firmware->max_count == 0xFFFF)
		core_config_ice_mode_write(0x04100C, write_len, 2);
	else if (core_firmware->max_count == 0x1FFFF)
		core_config_ice_mode_write(0x04100C, write_len, 3);

	if (id == CHIP_TYPE_ILI9881 && type == ILI9881_TYPE_F) {
		/* Checksum_En */
		core_config_ice_mode_write(0x041014, 0x10000, 3);
	} else if (id == CHIP_TYPE_ILI9881 && type == ILI9881_TYPE_H) {
		/* Clear Int Flag */
		core_config_ice_mode_write(0x048007, 0x02, 1);

		/* Checksum_En */
		core_config_ice_mode_write(0x041016, 0x00, 1);
		core_config_ice_mode_write(0x041016, 0x01, 1);
	}

	/* Start to receive */
	core_config_ice_mode_write(0x041010, 0xFF, 1);

	while (timer > 0) {

		mdelay(1);

		if (id == CHIP_TYPE_ILI9881 && type == ILI9881_TYPE_F)
			busy = core_config_read_write_onebyte(0x041014);
		else if (id == CHIP_TYPE_ILI9881 && type == ILI9881_TYPE_H) {
			busy = core_config_read_write_onebyte(0x041014);
			busy = busy >> 1;
		} else {
			ipio_err("Unknow chip type\n");
			break;
		}

		if ((busy & 0x01) == 0x01)
			break;

		timer--;
	}

	core_config_ice_mode_write(0x041000, 0x1, 1);	/* CS high */

	if (timer >= 0) {
		/* Disable dio_Rx_dual */
		core_config_ice_mode_write(0x041003, 0x0, 1);
		iram_check =  core_firmware->isCRC ? core_config_ice_mode_read(0x4101C) : core_config_ice_mode_read(0x041018);
	} else {
		ipio_err("TIME OUT\n");
		goto out;
	}

	return iram_check;

out:
	ipio_err("Failed to read Checksum/CRC from IC\n");
	return -1;

}

static void calc_verify_data(uint32_t sa, uint32_t se, uint32_t *check)
{
	uint32_t i = 0;
	uint32_t tmp_ck = 0, tmp_crc = 0;

	if (core_firmware->isCRC) {
		tmp_crc = calc_crc32(sa, se, flash_fw);
		*check = tmp_crc;
	} else {
		for (i = sa; i < (sa + se); i++)
			tmp_ck = tmp_ck + flash_fw[i];

		*check = tmp_ck;
	}
}

static int do_check(uint32_t start, uint32_t len)
{
	int res = 0;
	uint32_t vd = 0, lc = 0;

	calc_verify_data(start, len, &lc);
	vd = tddi_check_data(start, len);
	res = CHECK_EQUAL(vd, lc);

	ipio_info("%s (%x) : (%x)\n", (res < 0 ? "Invalid !" : "Correct !"), vd, lc);

	return res;
}

static int verify_flash_data(void)
{
	int i = 0, res = 0, len = 0;
	int fps = flashtab->sector;
	uint32_t ss = 0x0;

	/* check chip type with its max count */
	if (core_config->chip_id == CHIP_TYPE_ILI7807 && core_config->chip_type == ILI7807_TYPE_H) {
		core_firmware->max_count = 0x1FFFF;
		core_firmware->isCRC = true;
	}

	for (i = 0; i < g_section_len + 1; i++) {
		if (g_flash_sector[i].data_flag) {
			if (ss > g_flash_sector[i].ss_addr || len == 0)
				ss = g_flash_sector[i].ss_addr;

			len = len + g_flash_sector[i].dlength;

			/* if larger than max count, then committing data to check */
			if (len >= (core_firmware->max_count - fps)) {
				res = do_check(ss, len);
				if (res < 0)
					goto out;

				ss = g_flash_sector[i].ss_addr;
				len = 0;
			}
		} else {
			/* split flash sector and commit the last data to fw */
			if (len != 0) {
				res = do_check(ss, len);
				if (res < 0)
					goto out;

				ss = g_flash_sector[i].ss_addr;
				len = 0;
			}
		}
	}

	/* it might be lower than the size of sector if calc the last array. */
	if (len != 0 && res != -1)
		res = do_check(ss, core_firmware->end_addr - ss);

out:
	return res;
}

static int do_program_flash(uint32_t start_addr)
{
	int res = 0;
	uint32_t k;
	uint8_t buf[512] = { 0 };

	res = core_flash_write_enable();
	if (res < 0)
		goto out;

	core_config_ice_mode_write(0x041000, 0x0, 1);	/* CS low */
	core_config_ice_mode_write(0x041004, 0x66aa55, 3);	/* Key */

	core_config_ice_mode_write(0x041008, 0x02, 1);
	core_config_ice_mode_write(0x041008, (start_addr & 0xFF0000) >> 16, 1);
	core_config_ice_mode_write(0x041008, (start_addr & 0x00FF00) >> 8, 1);
	core_config_ice_mode_write(0x041008, (start_addr & 0x0000FF), 1);

	buf[0] = 0x25;
	buf[3] = 0x04;
	buf[2] = 0x10;
	buf[1] = 0x08;

	for (k = 0; k < flashtab->program_page; k++) {
		if (start_addr + k <= core_firmware->end_addr)
			buf[4 + k] = flash_fw[start_addr + k];
		else
			buf[4 + k] = 0xFF;
	}

	if (core_write(core_config->slave_i2c_addr, buf, flashtab->program_page + 4) < 0) {
		ipio_err("Failed to write data at start_addr = 0x%X, k = 0x%X, addr = 0x%x\n",
			start_addr, k, start_addr + k);
		res = -EIO;
		goto out;
	}

	core_config_ice_mode_write(0x041000, 0x1, 1);	/* CS high */

	res = core_flash_poll_busy();
	if (res < 0)
		goto out;

	core_firmware->update_status = (start_addr * 101) / core_firmware->end_addr;

	/* holding the status until finish this upgrade. */
	if (core_firmware->update_status > 90)
		core_firmware->update_status = 90;

	/* Don't use ipio_info to print log because it needs to be kpet in the same line */
	printk("%cUpgrading firmware ... start_addr = 0x%x, %02d%c", 0x0D, start_addr, core_firmware->update_status,
	       '%');

out:
	return res;
}

static int flash_program_sector(void)
{
	int i, j, res = 0;

	for (i = 0; i < g_section_len + 1; i++) {
		/*
		 * If running the boot stage, fw will only be upgrade data with the flag of block,
		 * otherwise data with the flag itself will be programed.
		 */
		if (core_firmware->isboot) {
			if (!g_flash_sector[i].inside_block)
				continue;
		} else {
			if (!g_flash_sector[i].data_flag)
				continue;
		}

		/* programming flash by its page size */
		for (j = g_flash_sector[i].ss_addr; j < g_flash_sector[i].se_addr; j += flashtab->program_page) {
			if (j > core_firmware->end_addr)
				goto out;

			res = do_program_flash(j);
			if (res < 0)
				goto out;
		}
	}

out:
	return res;
}

static int do_erase_flash(uint32_t start_addr)
{
	int res = 0;
	uint32_t temp_buf = 0;

	res = core_flash_write_enable();
	if (res < 0) {
		ipio_err("Failed to config write enable\n");
		goto out;
	}

	core_config_ice_mode_write(0x041000, 0x0, 1);	/* CS low */
	core_config_ice_mode_write(0x041004, 0x66aa55, 3);	/* Key */

	core_config_ice_mode_write(0x041008, 0x20, 1);
	core_config_ice_mode_write(0x041008, (start_addr & 0xFF0000) >> 16, 1);
	core_config_ice_mode_write(0x041008, (start_addr & 0x00FF00) >> 8, 1);
	core_config_ice_mode_write(0x041008, (start_addr & 0x0000FF), 1);

	core_config_ice_mode_write(0x041000, 0x1, 1);	/* CS high */

	mdelay(1);

	res = core_flash_poll_busy();
	if (res < 0)
		goto out;

	core_config_ice_mode_write(0x041000, 0x0, 1);	/* CS low */
	core_config_ice_mode_write(0x041004, 0x66aa55, 3);	/* Key */

	core_config_ice_mode_write(0x041008, 0x3, 1);
	core_config_ice_mode_write(0x041008, (start_addr & 0xFF0000) >> 16, 1);
	core_config_ice_mode_write(0x041008, (start_addr & 0x00FF00) >> 8, 1);
	core_config_ice_mode_write(0x041008, (start_addr & 0x0000FF), 1);
	core_config_ice_mode_write(0x041008, 0xFF, 1);

	temp_buf = core_config_read_write_onebyte(0x041010);
	if (temp_buf != 0xFF) {
		ipio_err("Failed to erase data(0x%x) at 0x%x\n", temp_buf, start_addr);
		res = -EINVAL;
		goto out;
	}

	core_config_ice_mode_write(0x041000, 0x1, 1);	/* CS high */

	ipio_debug(DEBUG_FIRMWARE, "Earsing data at start addr: %x\n", start_addr);

out:
	return res;
}

static int flash_erase_sector(void)
{
	int i, res = 0;

	for (i = 0; i < g_total_sector; i++) {
		if (core_firmware->isboot) {
			if (!g_flash_sector[i].inside_block)
				continue;
		} else {
			if (!g_flash_sector[i].data_flag && !g_flash_sector[i].inside_block)
				continue;
		}

		res = do_erase_flash(g_flash_sector[i].ss_addr);
		if (res < 0)
			goto out;
	}

out:
	return res;
}

static int iram_upgrade(void)
{
	int i, j, res = 0;
	uint8_t buf[512];
	int upl = flashtab->program_page;

	/* doing reset for erasing iram data before upgrade it. */
	ilitek_platform_tp_hw_reset(true);

	mdelay(1);

	ipio_info("Upgrade firmware written data into IRAM directly\n");

	res = core_config_ice_mode_enable();
	if (res < 0) {
		ipio_err("Failed to enter ICE mode, res = %d\n", res);
		return res;
	}

	mdelay(20);

	core_config_reset_watch_dog();

	ipio_debug(DEBUG_FIRMWARE, "nStartAddr = 0x%06X, nEndAddr = 0x%06X, nChecksum = 0x%06X\n",
	    core_firmware->start_addr, core_firmware->end_addr, core_firmware->checksum);

	/* write hex to the addr of iram */
	ipio_info("Writing data into IRAM ...\n");
	for (i = core_firmware->start_addr; i < core_firmware->end_addr; i += upl) {
		if ((i + 256) > core_firmware->end_addr) {
			upl = core_firmware->end_addr % upl;
		}

		buf[0] = 0x25;
		buf[3] = (char)((i & 0x00FF0000) >> 16);
		buf[2] = (char)((i & 0x0000FF00) >> 8);
		buf[1] = (char)((i & 0x000000FF));

		for (j = 0; j < upl; j++)
			buf[4 + j] = iram_fw[i + j];

		if (core_write(core_config->slave_i2c_addr, buf, upl + 4)) {
			ipio_err("Failed to write data via i2c, address = 0x%X, start_addr = 0x%X, end_addr = 0x%X\n",
				(int)i, (int)core_firmware->start_addr, (int)core_firmware->end_addr);
			res = -EIO;
			return res;
		}

		core_firmware->update_status = (i * 101) / core_firmware->end_addr;
		printk("%cupgrade firmware(ap code), %02d%c", 0x0D, core_firmware->update_status, '%');

		mdelay(3);
	}

	/* ice mode code reset */
	ipio_info("Doing code reset ...\n");
	core_config_ice_mode_write(0x40040, 0xAE, 1);
	core_config_ice_mode_write(0x40040, 0x00, 1);

	mdelay(10);

	core_config_ice_mode_disable();

	/*TODO: check iram status */

	return res;
}

int host_download(bool isIRAM)
{
	int i, j, res = 0, update_status = 0;
	uint8_t *buf, *read_ap_buf, *read_dlm_buf, *read_mp_buf;
	int upl = SPI_UPGRADE_LEN;

    read_ap_buf = (uint8_t*)kmalloc(MAX_AP_FIRMWARE_SIZE, GFP_KERNEL);   
	memset(read_ap_buf, 0xFF, MAX_AP_FIRMWARE_SIZE);
    read_dlm_buf = (uint8_t*)kmalloc(MAX_DLM_FIRMWARE_SIZE, GFP_KERNEL);   
	memset(read_dlm_buf, 0xFF, MAX_DLM_FIRMWARE_SIZE);
    read_mp_buf = (uint8_t*)kmalloc(MAX_MP_FIRMWARE_SIZE, GFP_KERNEL);   
	memset(read_mp_buf, 0xFF, MAX_MP_FIRMWARE_SIZE);
    buf = (uint8_t*)kmalloc(sizeof(uint8_t)*0x10000+4, GFP_KERNEL);   
	memset(buf, 0xFF, (int)sizeof(uint8_t) * 0x10000+4);

	ipio_info("Upgrade firmware written data into AP code directly\n");
	res = core_config_ice_mode_enable();
	if (res < 0) {
		ipio_err("Failed to enter ICE mode, res = %d\n", res);
		return res;
	}
	
	mdelay(20);
	core_config_ice_mode_read(core_config->pid_addr);
	ipio_debug(DEBUG_FIRMWARE, "nStartAddr = 0x%06X, nEndAddr = 0x%06X, nChecksum = 0x%06X\n",
	    core_firmware->start_addr, core_firmware->end_addr, core_firmware->checksum);
	core_config_ice_mode_write(0x5100C, 0x81, 1);
	core_config_ice_mode_write(0x5100C, 0x98, 1);
	if(core_fr->actual_fw_mode == P5_0_FIRMWARE_TEST_MODE)
	{
		/* write hex to the addr of MP code */
		ipio_info("Writing data into MP code ...\n");
		for (i = 0; i < MAX_MP_FIRMWARE_SIZE; i += upl) {

			buf[0] = 0x25;
			buf[3] = (char)((i & 0x00FF0000) >> 16);
			buf[2] = (char)((i & 0x0000FF00) >> 8);
			buf[1] = (char)((i & 0x000000FF));

			for (j = 0; j < upl; j++)
				buf[4 + j] = mp_fw[i + j];

			if (core_write(core_config->slave_i2c_addr, buf, upl + 4)) {
				ipio_err("Failed to write data via SPI, address = 0x%X, start_addr = 0x%X, end_addr = 0x%X\n",
					(int)i, 0, MAX_MP_FIRMWARE_SIZE);
				res = -EIO;
				return res;
			}

			update_status = (i * 101) / MAX_MP_FIRMWARE_SIZE;
			printk("%cupgrade firmware(mp code), %02d%c", 0x0D, update_status, '%');
		}
		/* Check AP mode Buffer data */
		// inital upgrade lenght
		upl = SPI_UPGRADE_LEN;
		for (i = 0; i < MAX_MP_FIRMWARE_SIZE; i += upl) {
			if ((i + SPI_UPGRADE_LEN) > MAX_MP_FIRMWARE_SIZE) {
				upl = MAX_MP_FIRMWARE_SIZE % upl;
			}
			
			buf[0] = 0x25;
			buf[3] = (char)((i & 0x00FF0000) >> 16);
			buf[2] = (char)((i & 0x0000FF00) >> 8);
			buf[1] = (char)((i & 0x000000FF));
			if (core_write(core_config->slave_i2c_addr, buf, 4)) {
				ipio_err("Failed to write data via SPI\n");
				res = -EIO;
				return res;
			}
			res = core_read(core_config->slave_i2c_addr, buf, upl); 
			memcpy(read_mp_buf + i, buf, upl);
		}
		if(memcmp(mp_fw, read_mp_buf, MAX_MP_FIRMWARE_SIZE) == 0)
		{
			ipio_info("Check MP Mode upgrade: PASS\n");	
		}
		else
		{
			ipio_info("Check MP Mode upgrade: FAIL\n");
		}

	}
	else
	{
		/* write hex to the addr of AP code */
		ipio_info("Writing data into AP code ...\n");
		for (i = 0; i < MAX_AP_FIRMWARE_SIZE; i += upl) {

			buf[0] = 0x25;
			buf[3] = (char)((i & 0x00FF0000) >> 16);
			buf[2] = (char)((i & 0x0000FF00) >> 8);
			buf[1] = (char)((i & 0x000000FF));

			for (j = 0; j < upl; j++)
				buf[4 + j] = ap_fw[i + j];

			if (core_write(core_config->slave_i2c_addr, buf, upl + 4)) {
				ipio_err("Failed to write data via SPI, address = 0x%X, start_addr = 0x%X, end_addr = 0x%X\n",
					(int)i, 0, MAX_AP_FIRMWARE_SIZE);
				res = -EIO;
				return res;
			}

			update_status = (i * 101) / MAX_AP_FIRMWARE_SIZE;
			printk("%cupgrade firmware(ap code), %02d%c", 0x0D, update_status, '%');
		}
		/* write hex to the addr of DLM code */
		ipio_info("Writing data into DLM code ...\n");
		// inital upgrade lenght
		upl = SPI_UPGRADE_LEN;
		for (i = 0; i < MAX_DLM_FIRMWARE_SIZE; i += upl) {
			if ((i + SPI_UPGRADE_LEN) > MAX_DLM_FIRMWARE_SIZE) {
				upl = MAX_DLM_FIRMWARE_SIZE % upl;
			}
			
			buf[0] = 0x25;
			buf[3] = (char)(((i+DLM_START_ADDRESS) & 0x00FF0000) >> 16);
			buf[2] = (char)(((i+DLM_START_ADDRESS) & 0x0000FF00) >> 8);
			buf[1] = (char)((i+DLM_START_ADDRESS) & 0x000000FF);
			for (j = 0; j < upl; j++)
				buf[4 + j] = dlm_fw[i + j];
			if (core_write(core_config->slave_i2c_addr, buf, upl + 4)) {
				ipio_err("Failed to write data via SPI\n");
				res = -EIO;
				return res;
			}
		}
		/* Check AP mode Buffer data */
		// inital upgrade lenght
		upl = SPI_UPGRADE_LEN;
		for (i = 0; i < MAX_AP_FIRMWARE_SIZE; i += upl) {
			if ((i + SPI_UPGRADE_LEN) > MAX_AP_FIRMWARE_SIZE) {
				upl = MAX_AP_FIRMWARE_SIZE % upl;
			}
			
			buf[0] = 0x25;
			buf[3] = (char)((i & 0x00FF0000) >> 16);
			buf[2] = (char)((i & 0x0000FF00) >> 8);
			buf[1] = (char)(i & 0x000000FF);
			if (core_write(core_config->slave_i2c_addr, buf, 4)) {
				ipio_err("Failed to write data via SPI\n");
				res = -EIO;
				return res;
			}
			res = core_read(core_config->slave_i2c_addr, buf, upl); 
			memcpy(read_ap_buf + i, buf, upl);
		}
		if(memcmp(ap_fw, read_ap_buf, MAX_AP_FIRMWARE_SIZE) == 0)
		{
			ipio_info("Check AP Mode upgrade: PASS\n");	
		}
		else
		{
			ipio_info("Check AP Mode upgrade: FAIL\n");
		}
		/* Check DLM mode Buffer data */
		buf[0] = 0x25;
		buf[3] = (char)(((DLM_START_ADDRESS) & 0x00FF0000) >> 16);
		buf[2] = (char)(((DLM_START_ADDRESS) & 0x0000FF00) >> 8);
		buf[1] = (char)(((DLM_START_ADDRESS) & 0x000000FF));
		if (core_write(core_config->slave_i2c_addr, buf, 4)) {
			ipio_err("Failed to write data via SPI, address = 0x%X, start_addr = 0x%X, end_addr = 0x%X\n",
				(int)i, 0, MAX_DLM_FIRMWARE_SIZE);
			res = -EIO;
			return res;
		}
		res = core_read(core_config->slave_i2c_addr, read_dlm_buf, MAX_DLM_FIRMWARE_SIZE); 
		if(memcmp(dlm_fw, read_dlm_buf, MAX_DLM_FIRMWARE_SIZE) == 0)
		{
			ipio_info("Check DLM Mode upgrade: PASS\n");
		}
		else
		{
			ipio_info("Check DLM Mode upgrade: FAIL\n");
		}
	}
	/* ice mode code reset */
	ipio_info("Doing code reset ...\n");
	core_config_ice_mode_write(0x40040, 0xAE, 1);

	mdelay(10);
	core_config_ice_mode_disable();
	mdelay(1000);
	kfree(buf);
	kfree(read_ap_buf);
	kfree(read_dlm_buf);
	kfree(read_mp_buf);
	return res;
}
EXPORT_SYMBOL(host_download);

static int tddi_fw_upgrade(bool isIRAM)
{
	int res = 0;

	if (isIRAM) {
		res = iram_upgrade();
		return res;
	}

	ilitek_platform_tp_hw_reset(true);

	ipio_info("Enter to ICE Mode\n");

	res = core_config_ice_mode_enable();
	if (res < 0) {
		ipio_err("Failed to enable ICE mode\n");
		goto out;
	}

	mdelay(5);

	/*
	 * This command is used to fix the bug of spi clk in 7807F-AB
	 * while operating with flash.
	 */
	if (core_config->chip_id == CHIP_TYPE_ILI7807 && core_config->chip_type == ILI7807_TYPE_F_AB) {
		res = core_config_ice_mode_write(0x4100C, 0x01, 1);
		if (res < 0)
			goto out;
	}

	mdelay(25);

	/* It's not necessary to disable WTD if you're using 9881 */
	if (core_config->chip_id != CHIP_TYPE_ILI9881)
		core_config_reset_watch_dog();

	/* Disable flash protection from being written */
	core_flash_enable_protect(false);

	res = flash_erase_sector();
	if (res < 0) {
		ipio_err("Failed to erase flash\n");
		goto out;
	}

	mdelay(1);

	res = flash_program_sector();
	if (res < 0) {
		ipio_err("Failed to program flash\n");
		goto out;
	}

	/* We do have to reset chip in order to move new code from flash to iram. */
	ipio_info("Doing Soft Reset ..\n");
	core_config_ic_reset();

	/* the delay time moving code depends on what the touch IC you're using. */
	mdelay(core_firmware->delay_after_upgrade);

	/* ensure that the chip has been updated */
	ipio_info("Enter to ICE Mode again\n");
	res = core_config_ice_mode_enable();
	if (res < 0) {
		ipio_err("Failed to enable ICE mode\n");
		goto out;
	}

	mdelay(20);

	if (core_config->chip_id != CHIP_TYPE_ILI9881)
		core_config_reset_watch_dog();

	/* check the data that we've just written into the iram. */
	res = verify_flash_data();
	if (res == 0)
		ipio_info("Data Correct !\n");

out:
	core_config_ice_mode_disable();
	return res;
}

#ifdef BOOT_FW_UPGRADE  
static int convert_hex_array(void)
{
	int i, j, index = 0;
	int block = 0, blen = 0, bindex = 0;
	uint32_t tmp_addr = 0x0;

	core_firmware->start_addr = 0;
	core_firmware->end_addr = 0;
	core_firmware->checksum = 0;
	core_firmware->crc32 = 0;
	core_firmware->hasBlockInfo = false;

	ipio_info("CTPM_FW = %d\n", (int)ARRAY_SIZE(CTPM_FW));

	if (ARRAY_SIZE(CTPM_FW) <= 0) {
		ipio_err("The size of CTPM_FW is invaild (%d)\n", (int)ARRAY_SIZE(CTPM_FW));
		goto out;
	}

	/* Get new version from ILI array */
	core_firmware->new_fw_ver[0] = CTPM_FW[19];
	core_firmware->new_fw_ver[1] = CTPM_FW[20];
	core_firmware->new_fw_ver[2] = CTPM_FW[21];

	/* The process will be executed if the comparison is different with origin ver */
	for (i = 0; i < ARRAY_SIZE(core_firmware->old_fw_ver); i++) {
		if (core_firmware->old_fw_ver[i] != core_firmware->new_fw_ver[i]) {
			ipio_info("FW version is different, preparing to upgrade FW\n");
			break;
		}
	}

	if (i == ARRAY_SIZE(core_firmware->old_fw_ver)) {
		ipio_err("FW version is the same as previous version\n");
		goto out;
	}

	/* Extract block info */
	block = CTPM_FW[33];

	if (block > 0) {
		core_firmware->hasBlockInfo = true;

		/* Initialize block's index and length */
		blen = 6;
		bindex = 34;

		for (i = 0; i < block; i++) {
			for (j = 0; j < blen; j++) {
				if (j < 3)
					g_flash_block_info[i].start_addr =
					    (g_flash_block_info[i].start_addr << 8) | CTPM_FW[bindex + j];
				else
					g_flash_block_info[i].end_addr =
					    (g_flash_block_info[i].end_addr << 8) | CTPM_FW[bindex + j];
			}

			bindex += blen;
		}
	}

	/* Fill data into buffer */
	for (i = 0; i < ARRAY_SIZE(CTPM_FW) - 64; i++) {
		flash_fw[i] = CTPM_FW[i + 64];
		index = i / flashtab->sector;
		if (!g_flash_sector[index].data_flag) {
			g_flash_sector[index].ss_addr = index * flashtab->sector;
			g_flash_sector[index].se_addr = (index + 1) * flashtab->sector - 1;
			g_flash_sector[index].dlength =
			    (g_flash_sector[index].se_addr - g_flash_sector[index].ss_addr) + 1;
			g_flash_sector[index].data_flag = true;
		}
	}

	g_section_len = index;

	if (g_flash_sector[g_section_len].se_addr > flashtab->mem_size) {
		ipio_err("The size written to flash is larger than it required (%x) (%x)\n",
			g_flash_sector[g_section_len].se_addr, flashtab->mem_size);
		goto out;
	}

	for (i = 0; i < g_total_sector; i++) {
		/* fill meaing address in an array where is empty */
		if (g_flash_sector[i].ss_addr == 0x0 && g_flash_sector[i].se_addr == 0x0) {
			g_flash_sector[i].ss_addr = tmp_addr;
			g_flash_sector[i].se_addr = (i + 1) * flashtab->sector - 1;
		}

		tmp_addr += flashtab->sector;

		/* set erase flag in the block if the addr of sectors is between them. */
		if (core_firmware->hasBlockInfo) {
			for (j = 0; j < ARRAY_SIZE(g_flash_block_info); j++) {
				if (g_flash_sector[i].ss_addr >= g_flash_block_info[j].start_addr
				    && g_flash_sector[i].se_addr <= g_flash_block_info[j].end_addr) {
					g_flash_sector[i].inside_block = true;
					break;
				}
			}
		}

		/*
		 * protects the reserved address been written and erased.
		 * This feature only applies on the boot upgrade. The addr is progrmmable in normal case.
		 */
		if (g_flash_sector[i].ss_addr == g_start_resrv && g_flash_sector[i].se_addr == g_end_resrv) {
			g_flash_sector[i].inside_block = false;
		}
	}

	/* DEBUG: for showing data with address that will write into fw or be erased */
	for (i = 0; i < g_total_sector; i++) {
		ipio_info
		    ("g_flash_sector[%d]: ss_addr = 0x%x, se_addr = 0x%x, length = %x, data = %d, inside_block = %d\n",
		     i, g_flash_sector[i].ss_addr, g_flash_sector[i].se_addr, g_flash_sector[index].dlength,
		     g_flash_sector[i].data_flag, g_flash_sector[i].inside_block);
	}

	core_firmware->start_addr = 0x0;
	core_firmware->end_addr = g_flash_sector[g_section_len].se_addr;
	ipio_info("start_addr = 0x%06X, end_addr = 0x%06X\n", core_firmware->start_addr, core_firmware->end_addr);
	return 0;

out:
	ipio_err("Failed to convert ILI FW array\n");
	return -1;
}

int core_firmware_boot_upgrade(void)
{
	int res = 0;
	bool power = false;

	ipio_info("BOOT: Starting to upgrade firmware ...\n");

	core_firmware->isUpgrading = true;
	core_firmware->update_status = 0;

	if (ipd->isEnablePollCheckPower) {
		ipd->isEnablePollCheckPower = false;
		cancel_delayed_work_sync(&ipd->check_power_status_work);
		power = true;
	}

	/* store old version before upgrade fw */
	core_firmware->old_fw_ver[0] = core_config->firmware_ver[1];
	core_firmware->old_fw_ver[1] = core_config->firmware_ver[2];
	core_firmware->old_fw_ver[2] = core_config->firmware_ver[3];

	if (flashtab == NULL) {
		ipio_err("Flash table isn't created\n");
		res = -ENOMEM;
		goto out;
	}

	flash_fw = kcalloc(flashtab->mem_size, sizeof(uint8_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(flash_fw)) {
		ipio_err("Failed to allocate flash_fw memory, %ld\n", PTR_ERR(flash_fw));
		res = -ENOMEM;
		goto out;
	}

	memset(flash_fw, 0xff, (int)sizeof(uint8_t) * flashtab->mem_size);

	g_total_sector = flashtab->mem_size / flashtab->sector;
	if (g_total_sector <= 0) {
		ipio_err("Flash configure is wrong\n");
		res = -1;
		goto out;
	}

	g_flash_sector = kcalloc(g_total_sector, sizeof(struct flash_sector), GFP_KERNEL);
	if (ERR_ALLOC_MEM(g_flash_sector)) {
		ipio_err("Failed to allocate g_flash_sector memory, %ld\n", PTR_ERR(g_flash_sector));
		res = -ENOMEM;
		goto out;
	}

	res = convert_hex_array();
	if (res < 0) {
		ipio_err("Failed to covert firmware data, res = %d\n", res);
		goto out;
	}
	/* calling that function defined at init depends on chips. */
	res = core_firmware->upgrade_func(false);
	if (res < 0) {
		core_firmware->update_status = res;
		ipio_err("Failed to upgrade firmware, res = %d\n", res);
		goto out;
	}
	
	core_firmware->update_status = 100;
	ipio_info("Update firmware information...\n");
	core_config_get_fw_ver();
	core_config_get_protocol_ver();
	core_config_get_core_ver();
	core_config_get_tp_info();
	core_config_get_key_info();

out:
	if (power) {
		ipd->isEnablePollCheckPower = true;
		queue_delayed_work(ipd->check_power_status_queue, &ipd->check_power_status_work, ipd->work_delay);
	}

	ipio_kfree((void **)&flash_fw);
	ipio_kfree((void **)&g_flash_sector);
	core_firmware->isUpgrading = false;
	return res;
}
#endif /* BOOT_FW_UPGRADE */

int core_firmware_boot_host_download(void)
{
	int res = 0, i = 0;
	bool power = false;
	flash_fw = kcalloc(MAX_AP_FIRMWARE_SIZE + MAX_DLM_FIRMWARE_SIZE + MAX_MP_FIRMWARE_SIZE, sizeof(uint8_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(flash_fw)) {
		ipio_err("Failed to allocate flash_fw memory, %ld\n", PTR_ERR(flash_fw));
		res = -ENOMEM;
		goto out;
	}

	memset(flash_fw, 0xff, (int)sizeof(uint8_t) * MAX_AP_FIRMWARE_SIZE + MAX_DLM_FIRMWARE_SIZE + MAX_MP_FIRMWARE_SIZE);

	ipio_info("BOOT: Starting to upgrade firmware ...\n");

	core_firmware->isUpgrading = true;
	core_firmware->update_status = 0;

	if (ipd->isEnablePollCheckPower) {
		ipd->isEnablePollCheckPower = false;
		cancel_delayed_work_sync(&ipd->check_power_status_work);
		power = true;
	}

	/* Fill data into buffer */
	for (i = 0; i < ARRAY_SIZE(CTPM_FW) - 64; i++) {
		flash_fw[i] = CTPM_FW[i + 64];
	}
	memcpy(ap_fw, flash_fw, MAX_AP_FIRMWARE_SIZE);
	memcpy(dlm_fw, flash_fw + DLM_HEX_ADDRESS, MAX_DLM_FIRMWARE_SIZE);
	gpio_direction_output(ipd->reset_gpio, 1);
	mdelay(ipd->delay_time_high);
	gpio_set_value(ipd->reset_gpio, 0);
	mdelay(ipd->delay_time_low);
	gpio_set_value(ipd->reset_gpio, 1);
	mdelay(ipd->edge_delay);
	res = core_firmware->upgrade_func(true);
	if (res < 0) {
		core_firmware->update_status = res;
		ipio_err("Failed to upgrade firmware, res = %d\n", res);
		goto out;
	}
	core_firmware->update_status = 100;
	ipio_info("Update firmware information...\n");
	core_config_get_fw_ver();
	core_config_get_protocol_ver();
	core_config_get_core_ver();
	core_config_get_tp_info();
	core_config_get_key_info();

out:
	if (power) {
		ipd->isEnablePollCheckPower = true;
		queue_delayed_work(ipd->check_power_status_queue, &ipd->check_power_status_work, ipd->work_delay);
	}

	ipio_kfree((void **)&flash_fw);
	ipio_kfree((void **)&g_flash_sector);
	core_firmware->isUpgrading = false;
	return res;
}
EXPORT_SYMBOL(core_firmware_boot_host_download);
static int convert_hex_file(uint8_t *pBuf, uint32_t nSize, bool isIRAM)
{
	uint32_t i = 0, j = 0, k = 0;
	uint32_t nLength = 0, nAddr = 0, nType = 0;
	uint32_t nStartAddr = 0x0, nEndAddr = 0x0, nChecksum = 0x0, nExAddr = 0;
	uint32_t tmp_addr = 0x0;
	int index = 0, block = 0;
	ipio_info("\n");
	core_firmware->start_addr = 0;
	ipio_info("\n");
	core_firmware->end_addr = 0;
	core_firmware->checksum = 0;
	core_firmware->crc32 = 0;
	core_firmware->hasBlockInfo = false;
	ipio_info("\n");
	memset(g_flash_block_info, 0x0, sizeof(g_flash_block_info));
#ifdef HOST_DOWNLOAD
	memset(ap_fw, 0xFF, sizeof(ap_fw));
	memset(dlm_fw, 0xFF, sizeof(dlm_fw));
	memset(mp_fw, 0xFF, sizeof(mp_fw));
#endif
	/* Parsing HEX file */
	for (; i < nSize;) {
		int32_t nOffset;

		nLength = HexToDec(&pBuf[i + 1], 2);
		nAddr = HexToDec(&pBuf[i + 3], 4);
		nType = HexToDec(&pBuf[i + 7], 2);

		/* calculate checksum */
		for (j = 8; j < (2 + 4 + 2 + (nLength * 2)); j += 2) {
			if (nType == 0x00) {
				/* for ice mode write method */
				nChecksum = nChecksum + HexToDec(&pBuf[i + 1 + j], 2);
			}
		}

		if (nType == 0x04) {
			nExAddr = HexToDec(&pBuf[i + 9], 4);
		}

		if (nType == 0x02) {
			nExAddr = HexToDec(&pBuf[i + 9], 4);
			nExAddr = nExAddr >> 12;
		}

		if (nType == 0xAE) {
			core_firmware->hasBlockInfo = true;
			/* insert block info extracted from hex */
			if (block < 4) {
				g_flash_block_info[block].start_addr = HexToDec(&pBuf[i + 9], 6);
				g_flash_block_info[block].end_addr = HexToDec(&pBuf[i + 9 + 6], 6);
				ipio_debug(DEBUG_FIRMWARE, "Block[%d]: start_addr = %x, end = %x\n",
				    block, g_flash_block_info[block].start_addr, g_flash_block_info[block].end_addr);
			}
			block++;
		}

		nAddr = nAddr + (nExAddr << 16);
		if (pBuf[i + 1 + j + 2] == 0x0D) {
			nOffset = 2;
		} else {
			nOffset = 1;
		}

		if (nType == 0x00) {
			if (nAddr > MAX_HEX_FILE_SIZE) {
				ipio_err("Invalid hex format\n");
				goto out;
			}

			if (nAddr < nStartAddr) {
				nStartAddr = nAddr;
			}
			if ((nAddr + nLength) > nEndAddr) {
				nEndAddr = nAddr + nLength;
			}
			
			/* fill data */
			for (j = 0, k = 0; j < (nLength * 2); j += 2, k++) {
				if (isIRAM)
				{
					#ifdef HOST_DOWNLOAD
					if(nAddr < 0x10000)
					{
						ap_fw[nAddr + k] = HexToDec(&pBuf[i + 9 + j], 2);
					}
					else if(nAddr >= DLM_HEX_ADDRESS && nAddr < 0x10C00)
					{
						dlm_fw[nAddr - DLM_HEX_ADDRESS + k] = HexToDec(&pBuf[i + 9 + j], 2);
					}
					else if(nAddr >= MP_HEX_ADDRESS)
					{
						mp_fw[nAddr - MP_HEX_ADDRESS + k] = HexToDec(&pBuf[i + 9 + j], 2);
					}
					#else	
					iram_fw[nAddr + k] = HexToDec(&pBuf[i + 9 + j], 2);
					#endif
				}				
				else {
					flash_fw[nAddr + k] = HexToDec(&pBuf[i + 9 + j], 2);

					if ((nAddr + k) != 0) {
						index = ((nAddr + k) / flashtab->sector);
						if (!g_flash_sector[index].data_flag) {
							g_flash_sector[index].ss_addr = index * flashtab->sector;
							g_flash_sector[index].se_addr =
							    (index + 1) * flashtab->sector - 1;
							g_flash_sector[index].dlength =
							    (g_flash_sector[index].se_addr -
							     g_flash_sector[index].ss_addr) + 1;
							g_flash_sector[index].data_flag = true;
						}
					}
				}
			}
		}
		i += 1 + 2 + 4 + 2 + (nLength * 2) + 2 + nOffset;
	}
	// for (i = 0; i < MAX_AP_FIRMWARE_SIZE;) {
	// 	if(i%16 == 0)
	// 		printk("0x%4x:", i);
	// 	printk("0x%2x,", ap_fw[i]);
	// 	i++;
	// 	if( i%16 == 0)
	// 		printk("\n");
	// }
	// for (i = 0; i < MAX_DLM_FIRMWARE_SIZE;) {
	// 	if(i%16 == 0)
	// 		printk("0x%4x:", i);
	// 	printk("0x%2x,", dlm_fw[i]);
	// 	i++;
	// 	if( i%16 == 0)
	// 		printk("\n");
	// }
	#ifdef HOST_DOWNLOAD
		return 0;
	#endif
	ipio_info("\n");
	/* Update the length of section */
	g_section_len = index;

	if (g_flash_sector[g_section_len - 1].se_addr > flashtab->mem_size) {
		ipio_err("The size written to flash is larger than it required (%x) (%x)\n",
			g_flash_sector[g_section_len - 1].se_addr, flashtab->mem_size);
		goto out;
	}

	for (i = 0; i < g_total_sector; i++) {
		/* fill meaing address in an array where is empty */
		if (g_flash_sector[i].ss_addr == 0x0 && g_flash_sector[i].se_addr == 0x0) {
			g_flash_sector[i].ss_addr = tmp_addr;
			g_flash_sector[i].se_addr = (i + 1) * flashtab->sector - 1;
		}

		tmp_addr += flashtab->sector;

		/* set erase flag in the block if the addr of sectors is between them. */
		if (core_firmware->hasBlockInfo) {
			for (j = 0; j < ARRAY_SIZE(g_flash_block_info); j++) {
				if (g_flash_sector[i].ss_addr >= g_flash_block_info[j].start_addr
				    && g_flash_sector[i].se_addr <= g_flash_block_info[j].end_addr) {
					g_flash_sector[i].inside_block = true;
					break;
				}
			}
		}
	}
	ipio_info("\n");
	/* DEBUG: for showing data with address that will write into fw or be erased */
	for (i = 0; i < g_total_sector; i++) {
		ipio_debug(DEBUG_FIRMWARE,
		    "g_flash_sector[%d]: ss_addr = 0x%x, se_addr = 0x%x, length = %x, data = %d, inside_block = %d\n", i,
		    g_flash_sector[i].ss_addr, g_flash_sector[i].se_addr, g_flash_sector[index].dlength,
		    g_flash_sector[i].data_flag, g_flash_sector[i].inside_block);
	}

	core_firmware->start_addr = nStartAddr;
	core_firmware->end_addr = nEndAddr;
	ipio_info("nStartAddr = 0x%06X, nEndAddr = 0x%06X\n", nStartAddr, nEndAddr);
	return 0;

out:
	ipio_err("Failed to convert HEX data\n");
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
	int res = 0, fsize;
	uint8_t *hex_buffer = NULL;
	bool power = false;

	struct file *pfile = NULL;
	mm_segment_t old_fs;
	loff_t pos = 0;

	core_firmware->isUpgrading = true;
	core_firmware->update_status = 0;

	if (ipd->isEnablePollCheckPower) {
		ipd->isEnablePollCheckPower = false;
		cancel_delayed_work_sync(&ipd->check_power_status_work);
		power = true;
	}

	pfile = filp_open(pFilePath, O_RDONLY, 0);
	if (ERR_ALLOC_MEM(pfile)) {
		ipio_err("Failed to open the file at %s.\n", pFilePath);
		res = -ENOENT;
		return res;
	}

	fsize = pfile->f_inode->i_size;

	ipio_info("fsize = %d\n", fsize);

	if (fsize <= 0) {
		ipio_err("The size of file is zero\n");
		res = -EINVAL;
		goto out;
	}
	ipio_info("\n");
#ifndef HOST_DOWNLOAD
	if (flashtab == NULL) {
		ipio_err("Flash table isn't created\n");
		res = -ENOMEM;
		goto out;
	}

	flash_fw = kcalloc(flashtab->mem_size, sizeof(uint8_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(flash_fw)) {
		ipio_err("Failed to allocate flash_fw memory, %ld\n", PTR_ERR(flash_fw));
		res = -ENOMEM;
		goto out;
	}

	memset(flash_fw, 0xff, sizeof(uint8_t) * flashtab->mem_size);

	g_total_sector = flashtab->mem_size / flashtab->sector;
	if (g_total_sector <= 0) {
		ipio_err("Flash configure is wrong\n");
		res = -1;
		goto out;
	}

	g_flash_sector = kcalloc(g_total_sector, sizeof(*g_flash_sector), GFP_KERNEL);
	if (ERR_ALLOC_MEM(g_flash_sector)) {
		ipio_err("Failed to allocate g_flash_sector memory, %ld\n", PTR_ERR(g_flash_sector));
		res = -ENOMEM;
		goto out;
	}
#endif
	ipio_info("\n");
	hex_buffer = kcalloc(fsize, sizeof(uint8_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(hex_buffer)) {
		ipio_err("Failed to allocate hex_buffer memory, %ld\n", PTR_ERR(hex_buffer));
		res = -ENOMEM;
		goto out;
	}
	ipio_info("\n");
	/* store current userspace mem segment. */
	old_fs = get_fs();

	/* set userspace mem segment equal to kernel's one. */
	set_fs(get_ds());

	/* read firmware data from userspace mem segment */
	vfs_read(pfile, hex_buffer, fsize, &pos);

	/* restore userspace mem segment after read. */
	set_fs(old_fs);
	ipio_info("\n");
	res = convert_hex_file(hex_buffer, fsize, isIRAM);
	if (res < 0) {
		ipio_err("Failed to covert firmware data, res = %d\n", res);
		goto out;
	}

	/* calling that function defined at init depends on chips. */
	res = core_firmware->upgrade_func(isIRAM);
	if (res < 0) {
		ipio_err("Failed to upgrade firmware, res = %d\n", res);
		goto out;
	}

	ipio_info("Update TP/Firmware information...\n");
	core_config_get_fw_ver();
	core_config_get_protocol_ver();
	core_config_get_core_ver();
	core_config_get_tp_info();
	core_config_get_key_info();

out:
	if (power) {
		ipd->isEnablePollCheckPower = true;
		queue_delayed_work(ipd->check_power_status_queue, &ipd->check_power_status_work, ipd->work_delay);
	}

	filp_close(pfile, NULL);
	ipio_kfree((void **)&hex_buffer);
	ipio_kfree((void **)&flash_fw);
	ipio_kfree((void **)&g_flash_sector);
	core_firmware->isUpgrading = false;
	return res;
}

int core_firmware_init(void)
{
	int i = 0, j = 0;

	core_firmware = kzalloc(sizeof(*core_firmware), GFP_KERNEL);
	if (ERR_ALLOC_MEM(core_firmware)) {
		ipio_err("Failed to allocate core_firmware mem, %ld\n", PTR_ERR(core_firmware));
		core_firmware_remove();
		return -ENOMEM;
	}

	core_firmware->hasBlockInfo = false;
	core_firmware->isboot = false;

	for (; i < ARRAY_SIZE(ipio_chip_list); i++) {
		if (ipio_chip_list[i] == TP_TOUCH_IC) {
			for (j = 0; j < 4; j++) {
				core_firmware->old_fw_ver[i] = core_config->firmware_ver[i];
				core_firmware->new_fw_ver[i] = 0x0;
			}

			if (ipio_chip_list[i] == CHIP_TYPE_ILI7807) {
				core_firmware->max_count = 0xFFFF;
				core_firmware->isCRC = false;
				core_firmware->upgrade_func = tddi_fw_upgrade;
				core_firmware->delay_after_upgrade = 100;
			} else if (ipio_chip_list[i] == CHIP_TYPE_ILI9881) {
				core_firmware->max_count = 0x1FFFF;
				core_firmware->isCRC = true;	
			#ifdef HOST_DOWNLOAD
				core_firmware->upgrade_func = host_download;
			#else
				core_firmware->upgrade_func = tddi_fw_upgrade;
			#endif
				core_firmware->delay_after_upgrade = 200;
			}
			return 0;
		}
	}

	ipio_err("Can't find this chip in support list\n");
	return 0;
}

void core_firmware_remove(void)
{
	ipio_info("Remove core-firmware members\n");
	ipio_kfree((void **)&core_firmware);
}
