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
#include <linux/i2c.h>
#include <linux/slab.h>

#include "../chip.h"
#include "config.h"
#include "i2c.h"

extern CORE_CONFIG *core_config;

CORE_I2C *core_i2c;

int core_i2c_write(uint8_t nSlaveId, uint8_t *pBuf, uint16_t nSize)
{
    int res = 0, i;

    struct i2c_msg msgs[] =
    {
        {
            .addr = nSlaveId,
            .flags = 0, // if read flag is undefined, then it means write flag.
            .len = nSize,
            .buf = pBuf,
        },
    };

	msgs[0].scl_rate = 400000;

	if(i2c_transfer(core_i2c->client->adapter, msgs, 1) < 0)
	{
		res = -EIO;
		DBG_ERR("I2C Write Error");
	}
	
	return res;
}
EXPORT_SYMBOL(core_i2c_write);

int core_i2c_read(uint8_t nSlaveId, uint8_t *pBuf, uint16_t nSize)
{
    int res = 0, i;

    struct i2c_msg msgs[] =
    {
        {
            .addr = nSlaveId,
            .flags = I2C_M_RD, // read flag
            .len = nSize,
            .buf = pBuf,
        },
    };

    msgs[0].scl_rate = 400000;

	if(i2c_transfer(core_i2c->client->adapter, msgs, 1) < 0)
	{
		res = -EIO;
		DBG_ERR("I2C Read Error");
	}

    return res;
}
EXPORT_SYMBOL(core_i2c_read);

int core_i2c_init(struct i2c_client *client)
{
	core_i2c = (CORE_I2C*)kmalloc(sizeof(*core_i2c), GFP_KERNEL);

	DBG_INFO();

	if(core_i2c == NULL) 
	{
		DBG_ERR("init core-i2c failed !");
		return -EINVAL;
	}

	core_i2c->client = client;

	return 0;
}
EXPORT_SYMBOL(core_i2c_init);

void core_i2c_remove(void)
{
	DBG_INFO();

	kfree(core_i2c);
}

