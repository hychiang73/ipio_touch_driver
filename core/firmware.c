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
#include <linux/firmware.h>

#include "../common.h"
#include "../platform.h"
#include "config.h"
#include "i2c.h"
#include "firmware.h"
#include "flash.h"
#include "protocol.h"
#include "finger_report.h"
#include "gesture.h"
#include "mp_test.h"

#if defined(BOOT_FW_UPGRADE) || defined(HOST_DOWNLOAD)
#ifndef BOOT_FW_UPGRADE_READ_HEX
#include "ilitek_fw.h"
#endif
#endif /* BOOT_FW_UPGRADE */

#define CHECK_FW_FAIL	-1
#define UPDATE_FAIL		-1
#define NEED_UPDATE		 1
#define NO_NEED_UPDATE 	 0
#define UPDATE_OK		 0
#define FW_VER_ADDR	   0xFFE0
#define CRC_ONESET(X, Y)	({Y = (*(X+0) << 24) | (*(X+1) << 16) | (*(X+2) << 8) | (*(X+3));})

/*
 * the size of two arrays is different depending on
 * which of methods to upgrade firmware you choose for.
 */
uint8_t *flash_fw = NULL;

/* TODO: may use a better way to declair them for upgrade fw */
uint8_t ap_fw[MAX_AP_FIRMWARE_SIZE] = { 0 };
uint8_t dlm_fw[MAX_DLM_FIRMWARE_SIZE] = { 0 };
uint8_t mp_fw[MAX_MP_FIRMWARE_SIZE] = { 0 };
uint8_t gesture_fw[MAX_GESTURE_FIRMWARE_SIZE] = { 0 };
uint8_t tuning_fw[MAX_TUNING_FIRMWARE_SIZE] = { 0 };
uint8_t ddi_fw[MAX_DDI_FIRMWARE_SIZE] = { 0 };

/* the length of array in each sector */
int g_section_len = 0;
int g_total_sector = 0;

#ifdef BOOT_FW_UPGRADE
/* The addr of block reserved for customers */
int g_start_resrv = 0x1D000;
int g_end_resrv = 0x1DFFF;
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
	uint32_t hex_crc;
	uint32_t block_crc;
	uint8_t  number;
};

struct flash_sector *g_flash_sector = NULL;
struct flash_block_info fbi[FW_BLOCK_INFO_NUM];
struct core_firmware_data *core_firmware = NULL;

static int convert_hex_file(uint8_t *pBuf, uint32_t nSize, bool isIRAM);

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

#ifndef HOST_DOWNLOAD
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

	if (id == CHIP_TYPE_ILI9881) {
		if (type == TYPE_F) {
			/* Checksum_En */
			core_config_ice_mode_write(0x041014, 0x10000, 3);
		} else {
			/* Clear Int Flag */
			core_config_ice_mode_write(0x048007, 0x02, 1);

			/* Checksum_En */
			core_config_ice_mode_write(0x041016, 0x00, 1);
			core_config_ice_mode_write(0x041016, 0x01, 1);
		}
	} else if (id == CHIP_TYPE_ILI7807) {
			/* Clear Int Flag */
			core_config_ice_mode_write(0x048007, 0x02, 1);

			/* Checksum_En */
			core_config_ice_mode_write(0x041016, 0x00, 1);
			core_config_ice_mode_write(0x041016, 0x01, 1);
	} else {
		ipio_err("Unknown CHIP\n");
		return -ENODEV;
	}

	/* Start to receive */
	core_config_ice_mode_write(0x041010, 0xFF, 1);

	while (timer > 0) {

		mdelay(1);

		if (id == CHIP_TYPE_ILI9881) {
			if (type == TYPE_F) {
				busy = core_config_read_write_onebyte(0x041014);
			} else {
				busy = core_config_read_write_onebyte(0x048007);
				busy = busy >> 1;
			}
		} else if (id == CHIP_TYPE_ILI7807) {
			busy = core_config_read_write_onebyte(0x048007);
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

void tddi_clear_dma_flash(void)
{
	core_config_ice_mode_bit_mask(FLASH0_ADDR, FLASH0_reg_preclk_sel, (2 << 16));
	core_config_ice_mode_write(FLASH0_reg_flash_csb, 0x01, 1);	/* CS high */

	core_config_ice_mode_bit_mask(FLASH4_ADDR, FLASH4_reg_flash_dma_trigger_en, (0 << 24));
	core_config_ice_mode_bit_mask(FLASH0_ADDR, FLASH0_reg_rx_dual, (0 << 24));

	core_config_ice_mode_write(FLASH3_reg_rcv_cnt, 0x00, 1);
	core_config_ice_mode_write(FLASH4_reg_rcv_data, 0xFF, 1);
}

void tddi_write_dma_flash(uint32_t start, uint32_t end, uint32_t len)
{
	core_config_ice_mode_bit_mask(FLASH0_ADDR, FLASH0_reg_preclk_sel, 1 << 16);

	core_config_ice_mode_write(FLASH0_reg_flash_csb, 0x00, 1);	/* CS low */
	core_config_ice_mode_write(FLASH1_reg_flash_key1, 0x66aa55, 3);	/* Key */

	core_config_ice_mode_write(FLASH2_reg_tx_data, 0x0b, 1);
	while(!(core_config_ice_mode_read(INTR1_ADDR) & BIT(25)));
	core_config_ice_mode_bit_mask(INTR1_ADDR, INTR1_reg_flash_int_flag, (1 << 25));

	core_config_ice_mode_write(FLASH2_reg_tx_data, (start & 0xFF0000) >> 16, 1);
	while(!(core_config_ice_mode_read(INTR1_ADDR) & BIT(25)));
	core_config_ice_mode_bit_mask(INTR1_ADDR, INTR1_reg_flash_int_flag, (1 << 25));

	core_config_ice_mode_write(FLASH2_reg_tx_data, (start & 0x00FF00) >> 8, 1);
	while(!(core_config_ice_mode_read(INTR1_ADDR) & BIT(25)));
	core_config_ice_mode_bit_mask(INTR1_ADDR, INTR1_reg_flash_int_flag, (1 << 25));

	core_config_ice_mode_write(FLASH2_reg_tx_data, (start & 0x0000FF), 1);
	while(!(core_config_ice_mode_read(INTR1_ADDR) & BIT(25)));
	core_config_ice_mode_bit_mask(INTR1_ADDR, INTR1_reg_flash_int_flag, (1 << 25));

	core_config_ice_mode_bit_mask(FLASH0_ADDR, FLASH0_reg_rx_dual, 0 << 24);

	core_config_ice_mode_write(FLASH2_reg_tx_data, 0x00, 1);	/* Dummy */
	while(!(core_config_ice_mode_read(INTR1_ADDR) & BIT(25)));
	core_config_ice_mode_bit_mask(INTR1_ADDR, INTR1_reg_flash_int_flag, (1 << 25));

	core_config_ice_mode_write(FLASH3_reg_rcv_cnt, len, 4);	/* Write Length */
}

static int tddi_read_flash(uint32_t start, uint32_t end, uint8_t *data, int dlen)
{
	uint32_t i, cont = 0;

	if (data == NULL) {
		ipio_err("data is null, read failed\n");
		return -1;
	}

	if (end - start > dlen) {
		ipio_err("the length (%d) reading crc is over than dlen(%d)\n", end - start, dlen);
		return -1;
	}

	core_config_ice_mode_write(0x041000, 0x0, 1);	/* CS low */
	core_config_ice_mode_write(0x041004, 0x66aa55, 3);	/* Key */
	core_config_ice_mode_write(0x041008, 0x03, 1);

	core_config_ice_mode_write(0x041008, (start & 0xFF0000) >> 16, 1);
	core_config_ice_mode_write(0x041008, (start & 0x00FF00) >> 8, 1);
	core_config_ice_mode_write(0x041008, (start & 0x0000FF), 1);

	for (i = start; i <= end; i++) {
		core_config_ice_mode_write(0x041008, 0xFF, 1);	/* Dummy */

		data[cont] = core_config_read_write_onebyte(0x41010);
		ipio_info("data[%d] = %x\n", cont, data[cont]);
		cont++;
	}

	core_config_ice_mode_write(0x041000, 0x1, 1);	/* CS high */
	return 0;
}

static int tddi_check_fw_upgrade(void)
{
	int ret = NO_NEED_UPDATE;
	int i, crc_byte_len = 4;
	uint8_t flash_crc[4] = {0};
	uint32_t start_addr = 0, end_addr = 0, flash_crc_cb;

	if (flash_fw == NULL) {
		ipio_err("Flash data is null, ignore upgrade\n");
		return CHECK_FW_FAIL;
	}

	/* No need to check fw ver/crc if we upgrade it by manual */
	if (!core_firmware->isboot) {
		ipio_info("Upgrad FW forcly by manual\n");
		return NEED_UPDATE;
	}

	/* Check FW version */
	ipio_info("New FW ver = 0x%x, Old FW ver = 0x%x\n", core_firmware->new_fw_cb, core_firmware->old_fw_cb);
	if (core_firmware->new_fw_cb >= core_firmware->old_fw_cb) {
		ipio_info("New FW ver is greater or equial to old, need to check hw crc if it's correct\n");
		goto check_hw_crc;
	} else {
		ipio_info("New FW ver is smaller than old, need to check flash crc if it's correct\n");
		goto check_flash_crc;
	}

check_hw_crc:
	/* Check HW and Hex CRC */
	for(i = 0; i < ARRAY_SIZE(fbi); i++) {
		start_addr = fbi[i].start_addr;
		end_addr = fbi[i].end_addr;

		/* Invaild end address */
		if (end_addr == 0)
			continue;

		fbi[i].hex_crc = (flash_fw[end_addr - 3] << 24) + (flash_fw[end_addr - 2] << 16) + (flash_fw[end_addr - 1] << 8) + flash_fw[end_addr];

		/* Get HW CRC for each block */
		fbi[i].block_crc = tddi_check_data(start_addr, end_addr - start_addr - crc_byte_len + 1);

		ipio_info("HW CRC = 0x%06x, HEX CRC = 0x%06x\n", fbi[i].hex_crc, fbi[i].block_crc);

		/* Compare HW CRC with HEX CRC */
		if(fbi[i].hex_crc != fbi[i].block_crc)
			ret = NEED_UPDATE;
	}

	return ret;

check_flash_crc:
	/* Check Flash CRC and HW CRC */
	for(i = 0; i < ARRAY_SIZE(fbi); i++) {
		start_addr = fbi[i].start_addr;
		end_addr = fbi[i].end_addr;

		/* Invaild end address */
		if (end_addr == 0)
			continue;

		ret = tddi_read_flash(end_addr - crc_byte_len + 1, end_addr, flash_crc, sizeof(flash_crc));
		if (ret < 0) {
			ipio_err("Read Flash failed\n");
			return CHECK_FW_FAIL;
		}

		fbi[i].block_crc = tddi_check_data(start_addr, end_addr - start_addr - crc_byte_len + 1);

		CRC_ONESET(flash_crc, flash_crc_cb);

		ipio_info("HW CRC = 0x%06x, Flash CRC = 0x%06x\n", fbi[i].block_crc, flash_crc_cb);

		/* Compare Flash CRC with HW CRC */
		if(flash_crc_cb != fbi[i].block_crc)
			ret = NEED_UPDATE;

		memset(flash_crc, 0, sizeof(flash_crc));
	}

	return ret;
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
	int ret = 0;
	uint32_t vd = 0, lc = 0;

	calc_verify_data(start, len, &lc);
	vd = tddi_check_data(start, len);
	ret = CHECK_EQUAL(vd, lc);

	ipio_info("%s (%x) : (%x)\n", (ret < 0 ? "Invalid !" : "Correct !"), vd, lc);

	return ret;
}

static int verify_flash_data(void)
{
	int i = 0, ret = 0, len = 0;
	int fps = flashtab->sector;
	uint32_t ss = 0x0;

	for (i = 0; i < g_section_len + 1; i++) {
		if (g_flash_sector[i].data_flag) {
			if (core_firmware->isboot && !g_flash_sector[i].inside_block) {
				if (len != 0) {
					ret = do_check(ss, len);
					if (ret < 0)
						goto out;

					ss = g_flash_sector[i].ss_addr;
					len = 0;
				}
				continue;
			}

			if (ss > g_flash_sector[i].ss_addr || len == 0)
				ss = g_flash_sector[i].ss_addr;

			len = len + g_flash_sector[i].dlength;

			/* if larger than max count, then committing data to check */
			if (len >= (core_firmware->max_count - fps)) {
				ret = do_check(ss, len);
				if (ret < 0)
					goto out;

				ss = g_flash_sector[i].ss_addr;
				len = 0;
			}
		} else {
			/* split flash sector and commit the last data to fw */
			if (len != 0) {
				ret = do_check(ss, len);
				if (ret < 0)
					goto out;

				ss = g_flash_sector[i].ss_addr;
				len = 0;
			}
		}
	}

	/* it might be lower than the size of sector if calc the last array. */
	if (len != 0 && ret != -1)
		ret = do_check(ss, core_firmware->end_addr - ss);

out:
	return ret;
}

static int do_program_flash(uint32_t start_addr)
{
	int ret = 0;
	uint32_t k;
	uint8_t buf[512] = { 0 };

	ret = core_flash_write_enable();
	if (ret < 0)
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
		ret = -EIO;
		goto out;
	}

	core_config_ice_mode_write(0x041000, 0x1, 1);	/* CS high */

	ret = core_flash_poll_busy();
	if (ret < 0)
		goto out;

	core_firmware->update_status = (start_addr * 101) / core_firmware->end_addr;

	/* holding the status until finish this upgrade. */
	if (core_firmware->update_status > 90)
		core_firmware->update_status = 90;

	/* Don't use ipio_info to print log because it needs to be kpet in the same line */
	printk("%cUpgrading firmware ... start_addr = 0x%x, %02d%c", 0x0D,
			start_addr, core_firmware->update_status,'%');

out:
	return ret;
}

static int flash_program_sector(void)
{
	int i, j, ret = 0;

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

			ret = do_program_flash(j);
			if (ret < 0)
				goto out;
		}
	}

out:
	return ret;
}

static int do_erase_flash(uint32_t start_addr)
{
	int ret = 0;
	uint32_t temp_buf = 0;

	ret = core_flash_write_enable();
	if (ret < 0) {
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

	ret = core_flash_poll_busy();
	if (ret < 0)
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
		ret = -EINVAL;
		goto out;
	}

	core_config_ice_mode_write(0x041000, 0x1, 1);	/* CS high */

	ipio_debug(DEBUG_FIRMWARE, "Earsing data at start addr: %x\n", start_addr);

out:
	return ret;
}

static int flash_erase_sector(void)
{
	int i, ret = 0;

	for (i = 0; i < g_total_sector; i++) {
		if (core_firmware->isboot) {
			if (!g_flash_sector[i].inside_block)
				continue;
		} else {
			if (!g_flash_sector[i].data_flag && !g_flash_sector[i].inside_block)
				continue;
		}

		ret = do_erase_flash(g_flash_sector[i].ss_addr);
		if (ret < 0)
			goto out;
	}

out:
	return ret;
}

int tddi_fw_upgrade(bool isIRAM)
{
	int ret = 0;

	ilitek_platform_reset_ctrl(true, HW_RST);

	ilitek_platform_disable_irq();

	ipio_info("Enter to ICE Mode\n");

	ret = core_config_ice_mode_enable();
	if (ret < 0) {
		ipio_err("Failed to enable ICE mode\n");
		goto out;
	}

	mdelay(25);

	if (core_config_set_watch_dog(false) < 0) {
		ipio_err("Failed to disable watch dog\n");
		ret = -EINVAL;
		goto out;
	}

	/* Check if need to upgrade fw */
	ret = tddi_check_fw_upgrade();
	if (ret == NEED_UPDATE) {
		ipio_info("FW CRC is different, doing upgrade\n");
	} else if (ret == NO_NEED_UPDATE) {
		ipio_info("FW CRC is the same, doing nothing\n");
		goto out;
	} else {
		ipio_err("FW check is incorrect, unexpected errors\n");
		goto out;
	}

	/* Disable flash protection from being written */
	core_flash_enable_protect(false);

	ret = flash_erase_sector();
	if (ret < 0) {
		ipio_err("Failed to erase flash\n");
		goto out;
	}

	mdelay(1);

	ret = flash_program_sector();
	if (ret < 0) {
		ipio_err("Failed to program flash\n");
		goto out;
	}

	/* We do have to reset chip in order to move new code from flash to iram. */
	ilitek_platform_reset_ctrl(true, HW_RST);

	/* the delay time moving code depends on what the touch IC you're using. */
	mdelay(core_firmware->delay_after_upgrade);

	/* ensure that the chip has been updated */
	ipio_info("Enter to ICE Mode again\n");
	ret = core_config_ice_mode_enable();
	if (ret < 0) {
		ipio_err("Failed to enable ICE mode\n");
		goto out;
	}

	mdelay(20);

	/* check the data that we've just written into the iram. */
	ret = verify_flash_data();
	if (ret == 0)
		ipio_info("Data Correct !\n");

out:
	if (core_config_set_watch_dog(true) < 0) {
		ipio_err("Failed to enable watch dog\n");
		ret = -EINVAL;
	}

	core_config_ice_mode_disable();
	return ret;
}
#else /* HOST_DOWNLOAD */
static int read_download(uint32_t start, uint32_t size, uint8_t *r_buf, uint32_t r_len)
{
	int ret = 0, addr = 0, i = 0;
	int safe_len = r_len;
	uint32_t end = start + size;
	uint8_t *buf = NULL;

    buf = kmalloc(sizeof(uint8_t) * size + 4, GFP_KERNEL);
	if (ERR_ALLOC_MEM(buf)) {
		ipio_err("Failed to allocate a buffer to be read, %ld\n", PTR_ERR(buf));
		return -ENOMEM;
	}

	memset(buf, 0xFF, (int)sizeof(uint8_t) * size + 4);

	for (addr = start, i = 0; addr < end; i += r_len, addr += r_len) {
		if ((addr + r_len) > end) {
			r_len = end % r_len;
		}

		buf[0] = 0x25;
		buf[3] = (char)((addr & 0x00FF0000) >> 16);
		buf[2] = (char)((addr & 0x0000FF00) >> 8);
		buf[1] = (char)((addr & 0x000000FF));

		if (core_write(core_config->slave_i2c_addr, buf, 4)) {
			ipio_err("Failed to write data via SPI in host download\n");
			ret = -EIO;
			goto out;
		}

		if (core_read(core_config->slave_i2c_addr, buf, r_len)) {
			ipio_err("Failed to Read data via SPI in host download\n");
			ret = -EIO;
			goto out;
		}

		ipio_memcpy(r_buf + i, buf, r_len, safe_len);
	}

out:
	ipio_kfree((void **)&buf);
	return ret;
}

static int write_download(uint32_t start, uint32_t size, uint8_t *w_buf, uint32_t w_len)
{
	int addr = 0, i = 0, update_status = 0, j = 0;
	uint32_t end = start + size;
	uint8_t *buf = NULL;

    buf = (uint8_t*)kmalloc(sizeof(uint8_t) * size + 4, GFP_KERNEL);
	if (ERR_ALLOC_MEM(buf)) {
		ipio_err("Failed to allocate a buffer to be read, %ld\n", PTR_ERR(buf));
		return -ENOMEM;
	}

	memset(buf, 0xFF, (int)sizeof(uint8_t) * size + 4);

	for (addr = start, i = 0; addr < end; addr += w_len, i += w_len) {
		if ((addr + w_len) > end) {
			w_len = end % w_len;
		}

		buf[0] = 0x25;
		buf[3] = (char)((addr & 0x00FF0000) >> 16);
		buf[2] = (char)((addr & 0x0000FF00) >> 8);
		buf[1] = (char)((addr & 0x000000FF));

		for (j = 0; j < w_len; j++)
			buf[4 + j] = w_buf[i + j];

		if (core_write(core_config->slave_i2c_addr, buf, w_len + 4)) {
			ipio_err("Failed to write data via SPI in host download (%x)\n", w_len);
			ipio_kfree((void **)&buf);
			return -EIO;
		}

		update_status = (i * 101) / end;
		printk("%cupgrade firmware(mp code), %02d%c", 0x0D, update_status, '%');
	}

	ipio_kfree((void **)&buf);
	return 0;
}

static int host_download_dma_check(int block)
{
	int i = 0;
	int count = 50;
	uint32_t start_addr = 0, block_size = 0;
	uint32_t busy = 0;
	uint32_t data_firmware_size = 8 * 1024;

	if (core_firmware->hex_tag == 0xAE) {
		if (block == AP_BLOCK_NUM) {
			start_addr = 0;
			block_size = MAX_AP_FIRMWARE_SIZE - 0x4;
		} else if (block == DATA_BLOCK_NUM) {
			start_addr = DLM_START_ADDRESS;
			block_size = MAX_DLM_FIRMWARE_SIZE;
		}
	} else {
		if (block == AP_BLOCK_NUM) {
			start_addr = 0;
		} else if (block == DATA_BLOCK_NUM) {
			start_addr = DLM_START_ADDRESS;
		} else if (block == TUNING_BLOCK_NUM) {
			for (i = 0; i < core_firmware->block_number; i++) {
				if (fbi[i].number == DATA_BLOCK_NUM) {
					data_firmware_size = (fbi[i].end_addr - fbi[i].start_addr + 1);
					break;
				}
			}
			start_addr = DLM_START_ADDRESS + data_firmware_size;
		}

		for (i = 0; i < core_firmware->block_number; i++) {
			if (fbi[i].number == block) {
				block_size = (fbi[i].end_addr - fbi[i].start_addr + 1) - 0x4;
				break;
			}
		}
	}

	/* dma1 src1 adress */
	core_config_ice_mode_write(0x072104, start_addr, 4);
	/* dma1 src1 format */
	core_config_ice_mode_write(0x072108, 0x80000001, 4);
	/* dma1 dest address */
	core_config_ice_mode_write(0x072114, 0x00030000, 4);
	/* dma1 dest format */
	core_config_ice_mode_write(0x072118, 0x80000000, 4);
	/* Block size*/
	core_config_ice_mode_write(0x07211C, block_size, 4);

	if (core_config->chip_id == CHIP_TYPE_ILI7807) {
		/* crc off */
		core_config_ice_mode_write(0x041016, 0x00, 1);
		/* dma crc */
		 core_config_ice_mode_write(0x041017, 0x03, 1);
	} else if (core_config->chip_id == CHIP_TYPE_ILI9881) {
		/* crc off */
		core_config_ice_mode_write(0x041014, 0x00000000, 4);
		/* dma crc */
		core_config_ice_mode_write(0x041048, 0x00000001, 4);
	}

	/* crc on */
	core_config_ice_mode_write(0x041016, 0x01, 1);
	/* Dma1 stop */
	core_config_ice_mode_write(0x072100, 0x00000000, 4);
	/* clr int */
	core_config_ice_mode_write(0x048006, 0x1, 1);
	/* Dma1 start */
	core_config_ice_mode_write(0x072100, 0x01000000, 4);

	/* Polling BIT0 */
	while (count > 0) {
		mdelay(1);
		busy = core_config_read_write_onebyte(0x048006);

		if ((busy & 0x01) == 1)
			break;

		count--;
	}

	if (count <= 0) {
		ipio_err("BIT0 is busy\n");
		return -1;
	}

	return core_config_ice_mode_read(0x04101C);
}

int tddi_host_download(bool mode)
{
	int i = 0, ret = 0;
	uint8_t *buf = NULL, *read_ap_buf = NULL;
	uint8_t *read_dlm_buf = NULL, *read_mp_buf = NULL;
	uint8_t *read_gesture_buf = NULL, *gesture_ap_buf = NULL;
	uint32_t data_firmware_size = 8 * 1024;
	uint32_t ap_crc = 0, ap_dma = 0, dlm_crc = 0;
	uint32_t dlm_dma = 0, tuning_crc = 0, tuning_dma = 0;

	ret = core_config_ice_mode_enable();
	if (ret < 0) {
		ipio_err("Failed to enter ICE mode, ret = %d\n", ret);
		return ret;
	}

	/* Allocate buffers for host download */
	read_ap_buf = kcalloc(MAX_AP_FIRMWARE_SIZE, sizeof(uint8_t), GFP_KERNEL);
	read_mp_buf = kcalloc(MAX_MP_FIRMWARE_SIZE, sizeof(uint8_t), GFP_KERNEL);
	read_dlm_buf = kcalloc(MAX_DLM_FIRMWARE_SIZE, sizeof(uint8_t), GFP_KERNEL);
	buf = kcalloc(MAX_AP_FIRMWARE_SIZE + 4, sizeof(uint8_t), GFP_KERNEL);

	if (ERR_ALLOC_MEM(read_ap_buf) || ERR_ALLOC_MEM(read_mp_buf) ||
		ERR_ALLOC_MEM(read_dlm_buf) || ERR_ALLOC_MEM(buf)) {
		ipio_err("Failed to allocate buffers for host download\n");
		return -ENOMEM;
	}

	memset(read_ap_buf, 0xFF, MAX_AP_FIRMWARE_SIZE);
	memset(read_dlm_buf, 0xFF, MAX_DLM_FIRMWARE_SIZE);
	memset(read_mp_buf, 0xFF, MAX_MP_FIRMWARE_SIZE);
	memset(buf, 0xFF, MAX_AP_FIRMWARE_SIZE + 4);

	/* Allocate buffers for gesture to load code */
	ipio_info("core_gesture->entry = %d\n", core_gesture->entry);
	if (core_gesture->entry) {
		read_gesture_buf = kmalloc(core_gesture->ap_length, GFP_KERNEL);
		gesture_ap_buf = kmalloc(core_gesture->ap_length, GFP_KERNEL);

		if (ERR_ALLOC_MEM(read_gesture_buf) || ERR_ALLOC_MEM(gesture_ap_buf)) {
			ipio_err("Failed to allocate buffers for gesture\n");
			return -ENOMEM;
		}

		memset(gesture_ap_buf, 0xFF, core_gesture->ap_length);
		memset(read_gesture_buf, 0xFF, core_gesture->ap_length);
	}

	if (core_config_set_watch_dog(false) < 0) {
		ipio_err("Failed to disable watch dog\n");
		ret = -EINVAL;
		goto out;
	}

	if (core_fr->actual_fw_mode == protocol->test_mode) {
		if (core_firmware->hex_tag == 0xAE) {
			if (write_download(0, MAX_MP_FIRMWARE_SIZE, mp_fw, SPI_UPGRADE_LEN) < 0) {
				ipio_err("SPI Write MP code data error\n");
			}

			if (read_download(0, MAX_MP_FIRMWARE_SIZE, read_mp_buf, SPI_UPGRADE_LEN)) {
				ipio_err("SPI Read MP code data error\n");
			}

			if (memcmp(mp_fw, read_mp_buf, MAX_MP_FIRMWARE_SIZE) == 0) {
				ipio_info("Check MP Mode upgrade: PASS\n");
			} else {
				ipio_info("Check MP Mode upgrade: FAIL\n");
				ret = UPDATE_FAIL;
				goto out;
			}
		} else {
			for (i = 0; i < core_firmware->block_number; i++) {
				if (fbi[i].number == MP_BLOCK_NUM) {
					/* write hex to the addr of MP code */
					ipio_info("Writing data into MP code ... len = 0x%X\n", (fbi[i].end_addr - fbi[i].start_addr + 1));
					if (write_download(0, (fbi[i].end_addr - fbi[i].start_addr + 1), mp_fw, SPI_UPGRADE_LEN) < 0) {
						ipio_err("SPI Write MP code data error\n");
					}
					if (read_download(0, (fbi[i].end_addr - fbi[i].start_addr + 1), read_mp_buf, SPI_UPGRADE_LEN)) {
						ipio_err("SPI Read MP code data error\n");
					}
					if (memcmp(mp_fw, read_mp_buf, (fbi[i].end_addr - fbi[i].start_addr + 1)) == 0) {
						ipio_info("Check MP Mode upgrade: PASS\n");
					} else {
						ipio_info("Check MP Mode upgrade: FAIL\n");
						ret = UPDATE_FAIL;
						goto out;
					}
					break;
				}
			}
		}
	} else if (core_gesture->entry) {
		if (mode) {
			/* write hex to the addr of Gesture code */
			ipio_info("Writing data into Gesture code ...\n");
			if (write_download(core_gesture->ap_start_addr, core_gesture->length, gesture_fw, core_gesture->length) < 0) {
				ipio_err("SPI Write Gesture code data error\n");
			}

			if (read_download(core_gesture->ap_start_addr, core_gesture->length, read_gesture_buf, core_gesture->length)) {
				ipio_err("SPI Read Gesture code data error\n");
			}

			if (memcmp(gesture_fw, read_gesture_buf, core_gesture->length) == 0) {
				ipio_info("Check Gesture Mode upgrade: PASS\n");
			} else {
				ipio_info("Check Gesture Mode upgrade: FAIL\n");
				ret = UPDATE_FAIL;
				goto out;
			}
		} else {
			/* write hex to the addr of AP code */
			ipio_memcpy(gesture_ap_buf, ap_fw + core_gesture->ap_start_addr, core_gesture->ap_length, MAX_GESTURE_FIRMWARE_SIZE);
			ipio_info("Writing data into AP code ...\n");
			if (write_download(core_gesture->ap_start_addr, core_gesture->ap_length, gesture_ap_buf, core_gesture->ap_length) < 0) {
				ipio_err("SPI Write AP code data error\n");
			}
			if (read_download(core_gesture->ap_start_addr, core_gesture->ap_length, read_ap_buf, core_gesture->ap_length)) {
				ipio_err("SPI Read AP code data error\n");
			}

			if (memcmp(gesture_ap_buf, read_ap_buf, core_gesture->ap_length) == 0) {
				ipio_info("Check AP Mode upgrade: PASS\n");
			} else {
				ipio_info("Check AP Mode upgrade: FAIL\n");
				ret = UPDATE_FAIL;
				goto out;
			}
		}
	} else {
		if (core_firmware->hex_tag == 0xAE) {
			ipio_info("Writing data into AP code ...\n");
			if (write_download(0, MAX_AP_FIRMWARE_SIZE, ap_fw, SPI_UPGRADE_LEN) < 0)
				ipio_err("SPI Write AP code data error\n");

			ipio_info("Writing data into DLM code ...\n");
			if (write_download(DLM_START_ADDRESS, MAX_DLM_FIRMWARE_SIZE, dlm_fw, SPI_UPGRADE_LEN) < 0)
				ipio_err("SPI Write DLM code data error\n");
		} else {
			for (i = 0; i < core_firmware->block_number; i++) {
				if (fbi[i].number == AP_BLOCK_NUM) {
					ipio_info("Writing data into AP code ... len = 0x%X\n", (fbi[i].end_addr - fbi[i].start_addr + 1));
					if (write_download(0, (fbi[i].end_addr - fbi[i].start_addr + 1), ap_fw, SPI_UPGRADE_LEN) < 0)
						ipio_err("SPI Write AP code data error\n");
				} else if (fbi[i].number == DATA_BLOCK_NUM) {
					data_firmware_size = (fbi[i].end_addr - fbi[i].start_addr + 1);
					ipio_info("Writing data into DLM code ...len = 0x%X\n", data_firmware_size);
					if (write_download(DLM_START_ADDRESS, data_firmware_size, dlm_fw, SPI_UPGRADE_LEN) < 0) {
						ipio_err("SPI Write DLM code data error\n");
					}
				} else if (fbi[i].number == TUNING_BLOCK_NUM) {
					ipio_info("Writing data into Tuning code ...len = 0x%X\n", (fbi[i].end_addr - fbi[i].start_addr + 1));
					if (write_download(DLM_START_ADDRESS + data_firmware_size, (fbi[i].end_addr - fbi[i].start_addr + 1), tuning_fw, SPI_UPGRADE_LEN) < 0) {
						ipio_err("SPI Write TUNING code data error\n");
					}
				}
			}
		}

		/* Check AP/DLM/Tunning mode Buffer data */
		if (((core_config->core_type >= CORE_TYPE_E) && (core_config->chip_id == CHIP_TYPE_ILI9881)) || ((core_config->chip_id == CHIP_TYPE_ILI7807))) {
			if (core_firmware->hex_tag == 0xAE) {
				ap_crc = calc_crc32(0, MAX_AP_FIRMWARE_SIZE - 4, ap_fw);
				ap_dma = host_download_dma_check(AP_BLOCK_NUM);

				dlm_crc = calc_crc32(0, MAX_DLM_FIRMWARE_SIZE, dlm_fw);
				dlm_dma = host_download_dma_check(DATA_BLOCK_NUM);
			} else {
				for (i = 0; i < core_firmware->block_number; i++) {
					if (fbi[i].number == AP_BLOCK_NUM) {
						ap_crc = calc_crc32(0, (fbi[i].end_addr - fbi[i].start_addr + 1) - 4, ap_fw);
						ap_dma = host_download_dma_check(AP_BLOCK_NUM);
					} else if (fbi[i].number == DATA_BLOCK_NUM) {
						dlm_crc = calc_crc32(0, (fbi[i].end_addr - fbi[i].start_addr + 1) - 4, dlm_fw);
						dlm_dma = host_download_dma_check(DATA_BLOCK_NUM);
					} else if (fbi[i].number == TUNING_BLOCK_NUM) {
						tuning_crc = calc_crc32(0, (fbi[i].end_addr - fbi[i].start_addr + 1) - 4, tuning_fw);
						tuning_dma = host_download_dma_check(TUNING_BLOCK_NUM);
					}
				}
			}

			ipio_info("AP CRC %s (%x) : (%x)\n",
				(ap_crc != ap_dma ? "Invalid !" : "Correct !"), ap_crc, ap_dma);

			ipio_info("DLM CRC %s (%x) : (%x)\n",
				(dlm_crc != dlm_dma ? "Invalid !" : "Correct !"), dlm_crc, dlm_dma);

			ipio_info("TUNING CRC %s (%x) : (%x)\n",
				(tuning_crc != tuning_dma ? "Invalid !" : "Correct !"), tuning_crc, tuning_dma);

			if (CHECK_EQUAL(ap_crc, ap_dma) == UPDATE_FAIL ||
				CHECK_EQUAL(dlm_crc, dlm_dma) == UPDATE_FAIL ||
				CHECK_EQUAL(tuning_crc, tuning_dma) == UPDATE_FAIL) {
				ipio_info("Check AP/DLM/TUNING Mode upgrade: FAIL\n");
				ret = UPDATE_FAIL;
				goto out;
			}
		} else {
			read_download(0, MAX_AP_FIRMWARE_SIZE, read_ap_buf, SPI_UPGRADE_LEN);
			read_download(DLM_START_ADDRESS, MAX_DLM_FIRMWARE_SIZE, read_dlm_buf, SPI_UPGRADE_LEN);

			if (memcmp(ap_fw, read_ap_buf, MAX_AP_FIRMWARE_SIZE) != 0 ||
				memcmp(dlm_fw, read_dlm_buf, MAX_DLM_FIRMWARE_SIZE) != 0) {
				ipio_info("Check AP/DLM Mode upgrade: FAIL\n");
				ret = UPDATE_FAIL;
				goto out;
			} else {
				ipio_info("Check AP/DLM Mode upgrade: SUCCESS\n");
			}
		}
	}

out:
	if (!core_gesture->entry) {
		/* ice mode code reset */
		ipio_info("Doing code reset ...\n");
		core_config_ice_mode_write(0x40040, 0xAE, 1);
	}

	core_config_ice_mode_disable();

	ipio_kfree((void **)&buf);
	ipio_kfree((void **)&read_ap_buf);
	ipio_kfree((void **)&read_dlm_buf);
	ipio_kfree((void **)&read_mp_buf);
	ipio_kfree((void **)&read_gesture_buf);
	ipio_kfree((void **)&gesture_ap_buf);
	return ret;
}
EXPORT_SYMBOL(tddi_host_download);
#endif /* HOST_DOWNLOAD */


#ifdef HOST_DOWNLOAD
static void convert_host_download_ili_file(int type)
{
	int i = 0, block = 0, ges_info;

	if (type == 0) {
		/* It's equal to the tag as 0xAE */
		memcpy(ap_fw, CTPM_FW + ILI_FILE_HEADER, MAX_AP_FIRMWARE_SIZE);
		memcpy(dlm_fw, CTPM_FW + ILI_FILE_HEADER + DLM_HEX_ADDRESS, MAX_DLM_FIRMWARE_SIZE);

		core_gesture->ap_start_addr = (ap_fw[0xFFD3] << 24) + (ap_fw[0xFFD2] << 16) + (ap_fw[0xFFD1] << 8) + ap_fw[0xFFD0];
		core_gesture->ap_length = MAX_GESTURE_FIRMWARE_SIZE;
		core_gesture->start_addr = (ap_fw[0xFFDB] << 24) + (ap_fw[0xFFDA] << 16) + (ap_fw[0xFFD9] << 8) + ap_fw[0xFFD8];
		core_gesture->length = MAX_GESTURE_FIRMWARE_SIZE;
		core_gesture->area_section = (ap_fw[0xFFCF] << 24) + (ap_fw[0xFFCE] << 16) + (ap_fw[0xFFCD] << 8) + ap_fw[0xFFCC];

		ipio_info("gesture_start_addr = 0x%x, length = 0x%x\n", core_gesture->start_addr, core_gesture->length);
		ipio_info("area = %d, ap_start_addr = 0x%x, ap_length = 0x%x\n",
					core_gesture->area_section, core_gesture->ap_start_addr, core_gesture->ap_length);

		memcpy(mp_fw, CTPM_FW + ILI_FILE_HEADER + MP_HEX_ADDRESS, MAX_MP_FIRMWARE_SIZE);
		ipio_memcpy(gesture_fw, CTPM_FW + ILI_FILE_HEADER + core_gesture->start_addr, core_gesture->length, MAX_GESTURE_FIRMWARE_SIZE);
	} else {
		for (i = 0; i < FW_BLOCK_INFO_NUM; i++) {
			if (((type >> i) & 0x01) == 0x01) {
				fbi[block].number = i + 1;
				if (fbi[block].number == 6) {
					fbi[block].start_addr = (CTPM_FW[0] << 16) + (CTPM_FW[1] << 8) + (CTPM_FW[2]);
					fbi[block].end_addr = (CTPM_FW[3] << 16) + (CTPM_FW[4] << 8) + (CTPM_FW[5]);
				} else {
					fbi[block].start_addr = (CTPM_FW[34 + i * 6] << 16) + (CTPM_FW[35 + i * 6] << 8) + (CTPM_FW[36 + i * 6]);
					fbi[block].end_addr = (CTPM_FW[37 + i * 6] << 16) + (CTPM_FW[38 + i * 6] << 8) + (CTPM_FW[39 + i * 6]);
				}

				ipio_info("Block[%d]: start_addr = %x, end = %x number = 0x%X\n",
						block, fbi[block].start_addr, fbi[block].end_addr, fbi[block].number);

				switch (fbi[block].number) {
					case AP_BLOCK_NUM:
						ges_info = (fbi[block].end_addr + 1 - 60);
						ipio_info("ges_info = 0x%X\n", ges_info);

						/* Parsing gesture info inside AP code */
						ipio_memcpy(ap_fw, CTPM_FW + ILI_FILE_HEADER + fbi[block].start_addr, fbi[block].end_addr - fbi[block].start_addr + 1, sizeof(ap_fw));
						core_gesture->area_section = (ap_fw[ges_info + 3] << 24) + (ap_fw[ges_info + 2] << 16) + (ap_fw[ges_info + 1] << 8) + ap_fw[ges_info];
						core_gesture->ap_start_addr = (ap_fw[ges_info + 7] << 24) + (ap_fw[ges_info + 6] << 16) + (ap_fw[ges_info + 5] << 8) + ap_fw[ges_info + 4];
						core_gesture->start_addr = (ap_fw[ges_info + 15] << 24) + (ap_fw[ges_info + 14] << 16) + (ap_fw[ges_info + 13] << 8) + ap_fw[ges_info + 12];
						core_gesture->ap_length = MAX_GESTURE_FIRMWARE_SIZE;
						core_gesture->length = MAX_GESTURE_FIRMWARE_SIZE;
						break;
					case DATA_BLOCK_NUM:
						ipio_memcpy(dlm_fw, CTPM_FW + ILI_FILE_HEADER + fbi[block].start_addr, fbi[block].end_addr - fbi[block].start_addr + 1, sizeof(dlm_fw));
						break;
					case MP_BLOCK_NUM:
						ipio_memcpy(mp_fw, CTPM_FW + ILI_FILE_HEADER + fbi[block].start_addr, fbi[block].end_addr - fbi[block].start_addr + 1, sizeof(mp_fw));
						break;
					case GESTURE_BLOCK_NUM:
						ipio_memcpy(gesture_fw, CTPM_FW + ILI_FILE_HEADER + fbi[block].start_addr, fbi[block].end_addr - fbi[block].start_addr + 1, sizeof(gesture_fw));
						break;
					case TUNING_BLOCK_NUM:
						ipio_memcpy(tuning_fw, CTPM_FW + ILI_FILE_HEADER + fbi[block].start_addr, fbi[block].end_addr - fbi[block].start_addr + 1, sizeof(tuning_fw));
						break;
					case DDI_BLOCK_NUM:
						ipio_memcpy(ddi_fw, CTPM_FW + ILI_FILE_HEADER + fbi[block].start_addr, fbi[block].end_addr - fbi[block].start_addr + 1, sizeof(ddi_fw));
						break;
					default:
						break;
				}
				block++;
			}
		}
	}
}

int core_firmware_boot_host_download(void)
{
	int ret = 0;
	int type = 0;
	bool power = false, esd = false;

	ipio_info("BOOT: Starting to upgrade firmware ...\n");

	core_firmware->isUpgrading = true;
	core_firmware->update_status = 0;

	if (ipd->isEnablePollCheckPower) {
		ipd->isEnablePollCheckPower = false;
		cancel_delayed_work_sync(&ipd->check_power_status_work);
		power = true;
	}
	if (ipd->isEnablePollCheckEsd) {
		ipd->isEnablePollCheckEsd = false;
		cancel_delayed_work_sync(&ipd->check_esd_status_work);
		esd = true;
	}

	type = CTPM_FW[32];
	ipio_info("type = %d\n", type);

	convert_host_download_ili_file(type);

	ret = core_firmware->upgrade_func(true);
	if (ret < 0) {
		core_firmware->update_status = ret;
		ipio_err("Failed to upgrade firmware, ret = %d\n", ret);
		goto out;
	}

	core_firmware->update_status = 100;

out:
	if (power) {
		ipd->isEnablePollCheckPower = true;
		queue_delayed_work(ipd->check_power_status_queue,
			&ipd->check_power_status_work, ipd->work_delay);
	}
	if (esd) {
		ipd->isEnablePollCheckEsd = true;
		queue_delayed_work(ipd->check_esd_status_queue,
			&ipd->check_esd_status_work, ipd->esd_check_time);
	}

	core_firmware->isUpgrading = false;
	return ret;
}
EXPORT_SYMBOL(core_firmware_boot_host_download);
#endif

#ifdef BOOT_FW_UPGRADE
#ifndef BOOT_FW_UPGRADE_READ_HEX
static int convert_hex_array(void)
{
	int i, j, index = 0;
	int block = 0, blen = 0, bindex = 0;
	uint32_t tmp_addr = 0x0;

	core_firmware->start_addr = 0;
	core_firmware->end_addr = 0;
	core_firmware->checksum = 0;
	core_firmware->crc32 = 0;
	core_firmware->hex_tag = false;

	ipio_info("CTPM_FW = %d\n", (int)ARRAY_SIZE(CTPM_FW));

	if (ARRAY_SIZE(CTPM_FW) <= 0) {
		ipio_err("The size of CTPM_FW is invaild (%d)\n", (int)ARRAY_SIZE(CTPM_FW));
		goto out;
	}

	/* Extract block info */
	block = CTPM_FW[33];

	if (block > 0) {
		core_firmware->hex_tag = true;

		/* Initialize block's index and length */
		blen = 6;
		bindex = 34;

		for (i = 0; i < block; i++) {
			for (j = 0; j < blen; j++) {
				if (j < 3)
					fbi[i].start_addr =
					    (fbi[i].start_addr << 8) | CTPM_FW[bindex + j];
				else
					fbi[i].end_addr =
					    (fbi[i].end_addr << 8) | CTPM_FW[bindex + j];
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

	/* Get hex fw vers */
	core_firmware->new_fw_cb = (flash_fw[FW_VER_ADDR] << 24) | (flash_fw[FW_VER_ADDR + 1] << 16) |
			(flash_fw[FW_VER_ADDR + 2] << 8) | (flash_fw[FW_VER_ADDR + 3]);

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
		if (core_firmware->hex_tag) {
			for (j = 0; j < ARRAY_SIZE(fbi); j++) {
				if (g_flash_sector[i].ss_addr >= fbi[j].start_addr
				    && g_flash_sector[i].se_addr <= fbi[j].end_addr) {
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
#endif /* BOOT_FW_UPGRADE_READ_HEX */

int core_firmware_boot_upgrade(void)
{
	int ret = 0;
	bool power = false, esd = false;
	uint8_t *hex_buffer = NULL;
#ifdef BOOT_FW_UPGRADE_READ_HEX
	const struct firmware *fw;
	int fsize = 0;
#endif

	ipio_info("BOOT: Starting to upgrade firmware ...\n");

	core_firmware->isUpgrading = true;
	core_firmware->update_status = 0;

	if (ipd->isEnablePollCheckPower) {
		ipd->isEnablePollCheckPower = false;
		cancel_delayed_work_sync(&ipd->check_power_status_work);
		power = true;
	}
	if (ipd->isEnablePollCheckEsd) {
		ipd->isEnablePollCheckEsd = false;
		cancel_delayed_work_sync(&ipd->check_esd_status_work);
		esd = true;
	}

	/* store old version before upgrade fw */
	if(protocol->mid >= 0x3) {
		core_firmware->old_fw_ver[0] = core_config->firmware_ver[1];
		core_firmware->old_fw_ver[1] = core_config->firmware_ver[2];
		core_firmware->old_fw_ver[2] = core_config->firmware_ver[3];
		core_firmware->old_fw_ver[3] = core_config->firmware_ver[4];
		core_firmware->old_fw_cb = (core_firmware->old_fw_ver[0] << 24) |
			(core_firmware->old_fw_ver[1] << 16)| (core_firmware->old_fw_ver[2] << 8) | core_firmware->old_fw_ver[3];
	} else {
		core_firmware->old_fw_ver[0] = core_config->firmware_ver[1];
		core_firmware->old_fw_ver[1] = core_config->firmware_ver[2];
		core_firmware->old_fw_ver[2] = core_config->firmware_ver[3];
		core_firmware->old_fw_cb = (core_firmware->old_fw_ver[0] << 16) |
				(core_firmware->old_fw_ver[1] << 8) | core_firmware->old_fw_ver[2];
	}

	if (flashtab == NULL) {
		ipio_err("Flash table isn't created\n");
		ret = -ENOMEM;
		goto out;
	}

	flash_fw = kcalloc(flashtab->mem_size, sizeof(uint8_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(flash_fw)) {
		ipio_err("Failed to allocate flash_fw memory, %ld\n", PTR_ERR(flash_fw));
		ret = -ENOMEM;
		goto out;
	}

	memset(flash_fw, 0xff, (int)sizeof(uint8_t) * flashtab->mem_size);

	g_total_sector = flashtab->mem_size / flashtab->sector;
	if (g_total_sector <= 0) {
		ipio_err("Flash configure is wrong\n");
		ret = -1;
		goto out;
	}

	g_flash_sector = kcalloc(g_total_sector, sizeof(struct flash_sector), GFP_KERNEL);
	if (ERR_ALLOC_MEM(g_flash_sector)) {
		ipio_err("Failed to allocate g_flash_sector memory, %ld\n", PTR_ERR(g_flash_sector));
		ret = -ENOMEM;
		goto out;
	}

#ifdef BOOT_FW_UPGRADE_READ_HEX
	msleep(BOOT_UPDATE_FW_DELAY_TIME);
	ret = request_firmware(&fw, BOOT_FW_HEX_NAME, &ipd->client->dev);

	if (ret < 0) {
		ipio_err("Failed to open the file Name %s.\n", BOOT_FW_HEX_NAME);
		ret = -ENOENT;
		return ret;
	}

	fsize = fw->size;

	ipio_info("fsize = %d\n", fsize);

	if (fsize <= 0) {
		ipio_err("The size of file is zero\n");
		ret = -EINVAL;
		goto out;
	}

	hex_buffer = kcalloc(fsize, sizeof(uint8_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(hex_buffer)) {
		ipio_err("Failed to allocate hex_buffer memory, %ld\n", PTR_ERR(hex_buffer));
		ret = -ENOMEM;
		goto out;
	}

	ipio_memcpy(hex_buffer, fw->data, fsize * sizeof(*fw->data), fsize);

	ret = convert_hex_file(hex_buffer, fsize, false);
#else
	ret = convert_hex_array();
	if (ret < 0) {
		ipio_err("Failed to covert firmware data, ret = %d\n", ret);
		goto out;
	}
#endif

	/* calling that function defined at init depends on chips. */
	ret = core_firmware->upgrade_func(false);
	if (ret < 0) {
		core_firmware->update_status = ret;
		ipio_err("Failed to upgrade firmware, ret = %d\n", ret);
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
		queue_delayed_work(ipd->check_power_status_queue,
			&ipd->check_power_status_work, ipd->work_delay);
	}
	if (esd) {
		ipd->isEnablePollCheckEsd = true;
		queue_delayed_work(ipd->check_esd_status_queue,
			&ipd->check_esd_status_work, ipd->esd_check_time);
	}

	ipio_kfree((void **)&flash_fw);
	ipio_kfree((void **)&g_flash_sector);
	ipio_kfree((void **)&hex_buffer);
	core_firmware->isUpgrading = false;
	return ret;
}
#endif /* BOOT_FW_UPGRADE */

static void convert_hex_tag(void *hex_data, uint32_t addr, int addr_offset, int hex_index, int hex_offset, int block)
{
	int i = 0;
	uint8_t *pBuf = (uint8_t *)hex_data;
	static int do_once = 0;
	uint32_t ges_info_addr = 0x0;

	if (core_firmware->hex_tag == 0xAF) {
		for (i = 0; i < block; i++) {
			if (addr >= fbi[i].start_addr && addr < fbi[i].end_addr) {
				switch (fbi[i].number) {
					case AP_BLOCK_NUM:
						ap_fw[addr + addr_offset] = HexToDec(&pBuf[hex_index + 9 + hex_offset], 2);
						ges_info_addr = (fbi[i].end_addr + 1 - 60);

						/* Parsing gesture info inside AP code */
						ipio_info("ges_info_addr = 0x%X\n", ges_info_addr);
						core_gesture->area_section = (ap_fw[ges_info_addr + 3] << 24) + (ap_fw[ges_info_addr + 2] << 16) + (ap_fw[ges_info_addr + 1] << 8) + ap_fw[ges_info_addr];
						core_gesture->ap_start_addr = (ap_fw[ges_info_addr + 7] << 24) + (ap_fw[ges_info_addr + 6] << 16) + (ap_fw[ges_info_addr + 5] << 8) + ap_fw[ges_info_addr + 4];
						core_gesture->start_addr = (ap_fw[ges_info_addr + 15] << 24) + (ap_fw[ges_info_addr + 14] << 16) + (ap_fw[ges_info_addr + 13] << 8) + ap_fw[ges_info_addr + 12];
						core_gesture->ap_length = MAX_GESTURE_FIRMWARE_SIZE;
						core_gesture->length = MAX_GESTURE_FIRMWARE_SIZE;
						break;
					case DATA_BLOCK_NUM:
						dlm_fw[addr + addr_offset - fbi[i].start_addr] = HexToDec(&pBuf[hex_index + 9 + hex_offset], 2);
						break;
					case MP_BLOCK_NUM:
						mp_fw[addr + addr_offset - fbi[i].start_addr] = HexToDec(&pBuf[hex_index + 9 + hex_offset], 2);
						break;
					case GESTURE_BLOCK_NUM:
						gesture_fw[addr + addr_offset - fbi[i].start_addr] = HexToDec(&pBuf[hex_index + 9 + hex_offset], 2);
						break;
					case TUNING_BLOCK_NUM:
						tuning_fw[addr + addr_offset - fbi[i].start_addr] = HexToDec(&pBuf[hex_index + 9 + hex_offset], 2);
						break;
					case DDI_BLOCK_NUM:
						ddi_fw[addr + addr_offset - fbi[i].start_addr] = HexToDec(&pBuf[hex_index + 9 + hex_offset], 2);
					default:
						break;
				}
			}
		}
	} else if (core_firmware->hex_tag == 0xAE) {
		if (addr < 0x10000) {
			ap_fw[addr + addr_offset] = HexToDec(&pBuf[hex_index + 9 + hex_offset], 2);
		} else if ((addr >= DLM_HEX_ADDRESS) && (addr < MP_HEX_ADDRESS)) {
			if (addr < DLM_HEX_ADDRESS + MAX_DLM_FIRMWARE_SIZE)
				dlm_fw[addr - DLM_HEX_ADDRESS + addr_offset] = HexToDec(&pBuf[hex_index + 9 + hex_offset], 2);
		} else if (addr >= MP_HEX_ADDRESS) {
			mp_fw[addr - MP_HEX_ADDRESS + addr_offset] = HexToDec(&pBuf[hex_index + 9 + hex_offset], 2);
		}
		if (addr > MAX_AP_FIRMWARE_SIZE && do_once == 0) {
			do_once = 1;
			core_gesture->ap_start_addr = (ap_fw[0xFFD3] << 24) + (ap_fw[0xFFD2] << 16) + (ap_fw[0xFFD1] << 8) + ap_fw[0xFFD0];
			core_gesture->ap_length = MAX_GESTURE_FIRMWARE_SIZE;
			core_gesture->start_addr = (ap_fw[0xFFDB] << 24) + (ap_fw[0xFFDA] << 16) + (ap_fw[0xFFD9] << 8) + ap_fw[0xFFD8];
			core_gesture->end_addr = (ap_fw[0xFFDF] << 24) + (ap_fw[0xFFDE] << 16) + (ap_fw[0xFFDD] << 8) + ap_fw[0xFFDC];
			core_gesture->length = MAX_GESTURE_FIRMWARE_SIZE;
			core_gesture->area_section = (ap_fw[0xFFCF] << 24) + (ap_fw[0xFFCE] << 16) + (ap_fw[0xFFCD] << 8) + ap_fw[0xFFCC];
			ipio_info("gesture_start_addr = 0x%x, gesture_end_addr = 0x%x, gesture_length = 0x%x\n",
						core_gesture->start_addr, core_gesture->end_addr, core_gesture->length);
			ipio_info("area = %d, ap_start_addr = 0x%x, ap_end_addr = 0x%x\n",
						core_gesture->area_section, core_gesture->ap_start_addr, core_gesture->ap_length);
		}
		if (addr >= core_gesture->start_addr && addr < core_gesture->start_addr + MAX_GESTURE_FIRMWARE_SIZE)
			gesture_fw[addr - core_gesture->start_addr + addr_offset] = HexToDec(&pBuf[hex_index + 9 + hex_offset], 2);
	} else {
		ipio_err("Warning ! Tag (0x%x) is invalid !\n", core_firmware->hex_tag);
	}
}

static int convert_hex_file(uint8_t *pBuf, uint32_t nSize, bool host_download)
{
	int index = 0, block = 0;
	uint32_t i = 0, j = 0, k = 0;
	uint32_t nLength = 0, nAddr = 0, nType = 0;
	uint32_t nStartAddr = 0x0, nEndAddr = 0x0, nChecksum = 0x0, nExAddr = 0;
	uint32_t tmp_addr = 0x0;

	ipio_info("host_download = %d\n", host_download);

	core_firmware->start_addr = 0;
	core_firmware->end_addr = 0;
	core_firmware->checksum = 0;
	core_firmware->crc32 = 0;
<<<<<<< HEAD
	core_firmware->hasBlockInfo = false;

	memset(g_flash_block_info, 0x0, sizeof(g_flash_block_info));
=======
	core_firmware->hex_tag = 0;
>>>>>>> master

	memset(fbi, 0x0, sizeof(fbi));

	if (host_download) {
		memset(ap_fw, 0xFF, sizeof(ap_fw));
		memset(dlm_fw, 0xFF, sizeof(dlm_fw));
		memset(mp_fw, 0xFF, sizeof(mp_fw));
		memset(gesture_fw, 0xFF, sizeof(gesture_fw));
	}

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

		if (nType == 0xAE || nType == 0xAF) {
			core_firmware->hex_tag = nType;
			/* insert block info extracted from hex */
			if (block < FW_BLOCK_INFO_NUM) {
				fbi[block].start_addr = HexToDec(&pBuf[i + 9], 6);
				fbi[block].end_addr = HexToDec(&pBuf[i + 9 + 6], 6);

				if(nType == 0xAF)
				    fbi[block].number = HexToDec(&pBuf[i + 9 + 6 + 6], 2);
			}
			ipio_info("Block[%d]: start_addr = %x, end = %x, number = %d\n",
				block, fbi[block].start_addr, fbi[block].end_addr, fbi[block].number);
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
				if (host_download) {
					convert_hex_tag(pBuf, nAddr, k, i, j, block);
				} else {
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

	ipio_info("Hex Tag = 0x%x\n", core_firmware->hex_tag);

	if (host_download)
		goto hd_out;

	/* Get hex fw vers */
	core_firmware->new_fw_cb = (flash_fw[FW_VER_ADDR] << 24) | (flash_fw[FW_VER_ADDR + 1] << 16) |
			(flash_fw[FW_VER_ADDR + 2] << 8) | (flash_fw[FW_VER_ADDR + 3]);

	ipio_info("new_fw_cb = 0x%x\n", core_firmware->new_fw_cb);

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
		if (core_firmware->hex_tag) {
			for (j = 0; j < ARRAY_SIZE(fbi); j++) {
				if (g_flash_sector[i].ss_addr >= fbi[j].start_addr
				    && g_flash_sector[i].se_addr <= fbi[j].end_addr) {
					g_flash_sector[i].inside_block = true;
					break;
				}
			}
		}
	}

	/* DEBUG: for showing data with address that will write into fw or be erased */
	for (i = 0; i < g_total_sector; i++) {
		ipio_debug(DEBUG_FIRMWARE,
		    "g_flash_sector[%d]: ss_addr = 0x%x, se_addr = 0x%x, length = %x, data = %d, inside_block = %d\n", i,
		    g_flash_sector[i].ss_addr, g_flash_sector[i].se_addr, g_flash_sector[index].dlength,
		    g_flash_sector[i].data_flag, g_flash_sector[i].inside_block);
	}

hd_out:
	core_firmware->start_addr = nStartAddr;
	core_firmware->end_addr = nEndAddr;
	core_firmware->block_number = block;
	ipio_info("nStartAddr = 0x%06X, nEndAddr = 0x%06X, block_number(AF) = %d\n", nStartAddr, nEndAddr, block);
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
int core_firmware_upgrade(const char *pFilePath, bool host_download)
{
	int ret = 0, fsize;
	uint8_t *hex_buffer = NULL;
	bool power = false, esd = false;
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
	if (ipd->isEnablePollCheckEsd) {
		ipd->isEnablePollCheckEsd = false;
		cancel_delayed_work_sync(&ipd->check_esd_status_work);
		esd = true;
	}

	if(protocol->mid >= 0x3) {
		core_firmware->old_fw_ver[0] = core_config->firmware_ver[1];
		core_firmware->old_fw_ver[1] = core_config->firmware_ver[2];
		core_firmware->old_fw_ver[2] = core_config->firmware_ver[3];
		core_firmware->old_fw_ver[3] = core_config->firmware_ver[4];
		core_firmware->old_fw_cb = (core_firmware->old_fw_ver[0] << 24) |
			(core_firmware->old_fw_ver[1] << 16)| (core_firmware->old_fw_ver[2] << 8) | core_firmware->old_fw_ver[3];
	} else {
		core_firmware->old_fw_ver[0] = core_config->firmware_ver[1];
		core_firmware->old_fw_ver[1] = core_config->firmware_ver[2];
		core_firmware->old_fw_ver[2] = core_config->firmware_ver[3];
		core_firmware->old_fw_cb = (core_firmware->old_fw_ver[0] << 16) |
				(core_firmware->old_fw_ver[1] << 8) | core_firmware->old_fw_ver[2];
	}

	pfile = filp_open(pFilePath, O_RDONLY, 0);
	if (ERR_ALLOC_MEM(pfile)) {
		ipio_err("Failed to open the file at %s.\n", pFilePath);
#ifdef HOST_DOWNLOAD
		ipio_info("Feed ili file to host download instead of hex\n");
		ret = core_firmware->upgrade_func(host_download);
		if (ret < 0)
			ipio_err("host download failed, ret = %d\n", ret);
		goto out_hd;
#endif
		ret = -ENOENT;
		return ret;
	}

	fsize = pfile->f_inode->i_size;

	ipio_info("fsize = %d\n", fsize);

	if (fsize <= 0) {
		ipio_err("The size of file is zero\n");
		ret = -EINVAL;
		goto out;
	}

#ifdef HOST_DOWNLOAD
	hex_buffer = kcalloc(fsize, sizeof(uint8_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(hex_buffer)) {
		ipio_err("Failed to allocate hex_buffer memory, %ld\n", PTR_ERR(hex_buffer));
		ret = -ENOMEM;
		goto out;
	}
	goto host_load;
#endif

	if (flashtab == NULL) {
		ipio_err("Flash table isn't created\n");
		ret = -ENOMEM;
		goto out;
	}

	flash_fw = kcalloc(flashtab->mem_size, sizeof(uint8_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(flash_fw)) {
		ipio_err("Failed to allocate flash_fw memory, %ld\n", PTR_ERR(flash_fw));
		ret = -ENOMEM;
		goto out;
	}

	memset(flash_fw, 0xff, sizeof(uint8_t) * flashtab->mem_size);

	g_total_sector = flashtab->mem_size / flashtab->sector;
	if (g_total_sector <= 0) {
		ipio_err("Flash configure is wrong\n");
		ret = -1;
		goto out;
	}

	g_flash_sector = kcalloc(g_total_sector, sizeof(struct flash_sector), GFP_KERNEL);
	if (ERR_ALLOC_MEM(g_flash_sector)) {
		ipio_err("Failed to allocate g_flash_sector memory, %ld\n", PTR_ERR(g_flash_sector));
		ret = -ENOMEM;
		goto out;
	}

#ifdef HOST_DOWNLOAD
host_load:
#endif
	hex_buffer = kcalloc(fsize, sizeof(uint8_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(hex_buffer)) {
		ipio_err("Failed to allocate hex_buffer memory, %ld\n", PTR_ERR(hex_buffer));
		ret = -ENOMEM;
		goto out;
	}

	/* store current userspace mem segment. */
	old_fs = get_fs();

	/* set userspace mem segment equal to kernel's one. */
	set_fs(get_ds());

	/* read firmware data from userspace mem segment */
	vfs_read(pfile, hex_buffer, fsize, &pos);

	/* restore userspace mem segment after read. */
	set_fs(old_fs);

	ret = convert_hex_file(hex_buffer, fsize, host_download);
	if (ret < 0) {
		ipio_err("Failed to covert firmware data, ret = %d\n", ret);
		goto out;
	}

	/* calling that function defined at init depends on chips. */
	ret = core_firmware->upgrade_func(host_download);
	if (ret < 0) {
		ipio_err("Failed to upgrade firmware, ret = %d\n", ret);
		goto out;
	}

	ipio_info("Update TP/Firmware information...\n");

#ifdef HOST_DOWNLOAD
	/* Waiting for fw load code finished in order to get TP info */
	mdelay(10);
#endif

	core_config_get_fw_ver();
	core_config_get_protocol_ver();
	core_config_get_core_ver();
	core_config_get_tp_info();
	core_config_get_key_info();

out:
	filp_close(pfile, NULL);
#ifdef HOST_DOWNLOAD
out_hd:
#endif
	if (power) {
		ipd->isEnablePollCheckPower = true;
		queue_delayed_work(ipd->check_power_status_queue,
			&ipd->check_power_status_work, ipd->work_delay);
	}
	if (esd) {
		ipd->isEnablePollCheckEsd = true;
		queue_delayed_work(ipd->check_esd_status_queue,
			&ipd->check_esd_status_work, ipd->esd_check_time);
	}

	core_firmware->isUpgrading = false;
	ipio_kfree((void **)&g_flash_sector);
	ipio_kfree((void **)&hex_buffer);
	ipio_kfree((void **)&flash_fw);
	ipio_kfree((void **)&g_flash_sector);
	return ret;
}

int core_firmware_init(void)
{
	int i = 0, j = 0;

	core_firmware = devm_kzalloc(ipd->dev, sizeof(struct core_firmware_data), GFP_KERNEL);
	if (ERR_ALLOC_MEM(core_firmware)) {
		ipio_err("Failed to allocate core_firmware mem, %ld\n", PTR_ERR(core_firmware));
		return -ENOMEM;
	}

	core_firmware->hex_tag = 0;
	core_firmware->isboot = false;

	for (j = 0; j < 4; j++) {
		core_firmware->old_fw_ver[i] = core_config->firmware_ver[i];
		core_firmware->new_fw_ver[i] = 0x0;
	}

	for (i = 0; i < ARRAY_SIZE(ipio_chip_list); i++) {
<<<<<<< HEAD
		switch (ipio_chip_list[i]) {
			case CHIP_TYPE_ILI7807:
			case CHIP_TYPE_ILI9881:
				core_firmware->max_count = 0x1FFFF;
				core_firmware->isCRC = true;
=======
		if (ipio_chip_list[i] == TP_TOUCH_IC) {
			switch (ipio_chip_list[i]) {
				case CHIP_TYPE_ILI7807:
				case CHIP_TYPE_ILI9881:
					core_firmware->max_count = 0x1FFFF;
					core_firmware->isCRC = true;
>>>>>>> master
#ifdef HOST_DOWNLOAD
					core_firmware->upgrade_func = tddi_host_download;
#else
					core_firmware->upgrade_func = tddi_fw_upgrade;
#endif
<<<<<<< HEAD
				core_firmware->delay_after_upgrade = 200;
				break;
			default:
				ipio_err("Can't find this chip (%x) in support list\n", ipio_chip_list[i]);
				return -ENODEV;
=======
					core_firmware->delay_after_upgrade = 200;
					break;
				default:
					break;
			}
>>>>>>> master
		}
	}
	return 0;
}