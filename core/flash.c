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

#include "../chip.h"
#include "flash.h"

/* 
 * The table contains fundamental data used to program our flash, which
 * would be different according to the vendors. 
 */
struct flash_table ft[] = {
	{0xEF, 0x6011, (128*1024), 256, (4*1024), (64*1024)},//W25Q10EW
    {0xC8, 0x6012, (256*1024), 256, (4*1024), (64*1024)},//GD25LQ20B
};

struct flash_table *flashtab = NULL;

void core_flash_init(uint16_t mid, uint16_t did)
{
    int i = 0;

    DBG_INFO("M_ID = %x, DEV_ID = %x", mid, did);

    for(; i < ARRAY_SIZE(ft); i++)
    {
        if(mid == ft[i].mid && did == ft[i].dev_id)
        {
            flashtab = kzalloc(sizeof(ft), GFP_KERNEL);
            flashtab->mid = mid;
            flashtab->dev_id = did;
            flashtab->mem_size = ft[i].mem_size;
            flashtab->program_page = ft[i].program_page;
            flashtab->sector = ft[i].sector;
            flashtab->block = ft[i].block;
            DBG_INFO("Max Memory size = %d", flashtab->mem_size);
            DBG_INFO("Per program page = %d", flashtab->program_page);
            DBG_INFO("Sector size = %d", flashtab->sector);
            DBG_INFO("Block size = %d", flashtab->block);
        }
    }
}

void core_flash_remove(void)
{
    DBG_INFO("Remove core-flash memebers");
    kfree(flashtab);
}


