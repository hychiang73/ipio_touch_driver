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
#include "i2c.h"
#include "config.h"
#include "mp_test.h"

extern uint8_t pcmd[10];

void core_mp_switch_mode(void)
{
    int res = 0, timer = 500;
    uint8_t cmd[2] = {0};
    uint8_t buf[2] = {0};

    cmd[0] = pcmd[0];
    cmd[1] = pcmd[9];

    while (timer > 0)
    {
        core_i2c_write(core_config->slave_i2c_addr, cmd, 2);
        core_i2c_write(core_config->slave_i2c_addr, &cmd[1], 1);
        mdelay(10);
        core_i2c_read(core_config->slave_i2c_addr, &buf[0], 1);

        DBG_INFO("CDC busy state for AP = 0x%x", buf[0]);

        if (buf[0] == 0x41)
            break;
        timer--;
    }

    if (timer == 0)
    {
        DBG_ERR("CDC is busying, time out");
        res = -1;
        return;
    }

    res = core_config_ice_mode_enable();
    if (res < 0)
    {
        DBG_ERR("Failed to enter ICE mode, res = %d", res);
    //    goto out;
    }

    // DMA Trigger
    core_config_ice_mode_write(0x41010, 0xFF, 1);

    mdelay(30);

    // Code reset
    core_config_ice_mode_write(0x40040, 0xAE, 1);

    core_config_ice_mode_disable();

    timer = 500;

    while (timer > 0)
    {
        core_i2c_write(core_config->slave_i2c_addr, cmd, 2);
        core_i2c_write(core_config->slave_i2c_addr, &cmd[1], 1);
        mdelay(10);
        core_i2c_read(core_config->slave_i2c_addr, &buf[0], 1);

        DBG_INFO("CDC busy state for MP = 0x%x", buf[0]);

        if (buf[0] == 0x51 || buf[0] == 0x52)
            break;

        timer--;
    }

    if (timer == 0)
    {
        DBG_ERR("CDC is busying, time out");
        res = -1;
        return;
    }
}
EXPORT_SYMBOL(core_mp_switch_mode);

void core_mp_init(void)
{
    // TODO
}

void core_mp_remove(void)
{
    // TODO
}