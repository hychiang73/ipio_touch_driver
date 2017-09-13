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
#include "i2c.h"
#include "config.h"
#include "mp_test.h"
#include "protocol.h"

#define EXEC_WRITE 1
#define EXEC_READ  0

#define SET_KEY    0
#define SET_MUTUAL 1
#define SET_SELF   2

#define FAIL       -1

struct mp_test_items
{
	char *name;
	int (*do_test)(uint8_t value);
};

struct mp_test_items single_test[] = 
{
	{"cm_short",	core_mp_cm_test},
	{"tx_short",	core_mp_tx_short_test},

	{"rx_open",		core_mp_rx_open_test},
	{"rx_short",	core_mp_rx_short_test},

	{"key_open",	core_mp_key_open_test},
	{"key_short",	core_mp_key_short_test},
	{"key_has_bg",	core_mp_key_has_bg_test},
	{"key_no_bk",	core_mp_key_no_bk_test},
	{"key_has_bk",	core_mp_key_has_bk_test},
	{"key_dac",		core_mp_key_dac_test},

	{"self_signal",	core_mp_self_signal_test},
	{"self_no_bk",	core_mp_self_no_bk_test},
	{"self_has_bk",	core_mp_self_has_bk_test},
	{"self_dac",	core_mp_self_dac_test},

	{"mutual_signal",	core_mp_mutual_signal_test},
	{"mutual_no_bk",	core_mp_mutual_no_bk_test},
	{"mutual_has_bk",	core_mp_mutual_has_bk_test},
	{"mutual_dac",		core_mp_mutual_dac_test},

};

static void cdc_free(void *d)
{
    kfree(d);
    d = NULL;
    return;
}

static void *cdc_alloc(int *return_len , bool type)
{
    int xch = 0, ych = 0;
    void *cdc = NULL;

    if(type == SET_KEY)
    {
        /* Key */
        *return_len = core_config->tp_info->nKeyCount;
    }
    else if(type == SET_MUTUAL || type == SET_SELF)
    {
        /* Mutual & Self */
        xch = core_config->tp_info->nXChannelNum;
        ych = core_config->tp_info->nYChannelNum;
        
        *return_len = xch * ych * 2;
    }
    else
    {
        DBG_INFO("Unknow types , %d", type);
        return NULL;
    }

    if(cdc != NULL)
    {
        cdc_free(cdc);
        cdc = NULL;
    }

    DBG_INFO("return length = %d", *return_len);

    cdc = kzalloc(*return_len, GFP_KERNEL);
    if(ERR_ALLOC_MEM(cdc))
    {
        DBG_ERR("Failed to allocate CDC buffer");
        cdc = NULL;
    }

    return cdc;
}

static int exec_cdc_command(bool write, uint8_t *item, int length, uint8_t *buf)
{
    int res = 0;
    uint8_t cmd[3] = {0};

    if(write)
    {
        cmd[0] = protocol->cmd_cdc;
        cmd[1] = item[0];
		cmd[2] = item[1];
		DBG_INFO("cmd[0] = %x, cmd[1] = %x, cmd[2] = %x",cmd[0],cmd[1],cmd[2]);
        res = core_i2c_write(core_config->slave_i2c_addr, cmd, 1 + length);
    }
    else
    {
        if(ERR_ALLOC_MEM(buf))
        {
            DBG_ERR("Invalid buffer");
            return -ENOMEM;
        }

        cmd[0] = protocol->cmd_read_ctrl;
        cmd[1] = protocol->cmd_get_cdc;
        res = core_i2c_write(core_config->slave_i2c_addr, cmd, 2);
        
        mdelay(1);

        res = core_i2c_read(core_config->slave_i2c_addr, buf, length);

        {
            int i = 0;
            for(i = 0; i < 10; i++)
                printk("0x%x , ", buf[i]);
            
            printk("\n");
        }
    }

    return res;
}

int core_mp_tx_short_test(uint8_t value)
{
    int res = 0, len;
	uint8_t cmd[2] = {0};
    uint8_t *tx_short;

	cmd[0] = protocol->tx_short;
	cmd[1] = value;

    res = exec_cdc_command(EXEC_WRITE, cmd, 2, NULL);
    if(res < 0)
    {
        return res;        
    }
    else
    {
        tx_short = cdc_alloc(&len, SET_MUTUAL);
        if(ERR_ALLOC_MEM(tx_short))
        {
            res = FAIL;
            goto out;
        }

        exec_cdc_command(EXEC_READ, 0, len, tx_short);

        cdc_free(tx_short);
    }

out:
    return res;
}
EXPORT_SYMBOL(core_mp_tx_short_test);

int core_mp_rx_open_test(uint8_t value)
{
    int res = 0, len;
    uint8_t cmd[2] = {0};
    uint8_t *rx_open;

    cmd[0] = protocol->rx_open;
	cmd[1] = value;

    res = exec_cdc_command(EXEC_WRITE, cmd, 2, NULL);
    if(res < 0)
    {
        return res;        
    }
    else
    {
        rx_open = cdc_alloc(&len, SET_MUTUAL);
        if(ERR_ALLOC_MEM(rx_open))
        {
            res = FAIL;
            goto out;
        }

        exec_cdc_command(EXEC_READ, 0, len, rx_open);

        cdc_free(rx_open);
    }

out:
    return res;
}
EXPORT_SYMBOL(core_mp_rx_open_test);

int core_mp_rx_short_test(uint8_t value)
{
    int res = 0, len;
	uint8_t cmd[2] = {0};
    uint8_t *rx_short;

    cmd[0] = protocol->rx_short;
	cmd[1] = value;

    res = exec_cdc_command(EXEC_WRITE, cmd, 2, NULL);
    if(res < 0)
    {
        return res;        
    }
    else
    {
        rx_short = cdc_alloc(&len, SET_MUTUAL);
        if(ERR_ALLOC_MEM(rx_short))
        {
            res = FAIL;
            goto out;
        }

        exec_cdc_command(EXEC_READ, 0, len, rx_short);

        cdc_free(rx_short);
    }

out:
    return res;
}
EXPORT_SYMBOL(core_mp_rx_short_test);

int core_mp_key_open_test(uint8_t value)
{
    int res = 0, len;
	uint8_t cmd[2] = {0};
    uint8_t *key_open;

    cmd[0] = protocol->key_open;
	cmd[1] = value;

    res = exec_cdc_command(EXEC_WRITE, cmd, 2, NULL);
    if(res < 0)
    {
        return res;        
    }
    else
    {
        key_open = cdc_alloc(&len, SET_KEY);
        if(ERR_ALLOC_MEM(key_open))
        {
            res = FAIL;
            goto out;
        }

        exec_cdc_command(EXEC_READ, 0, len, key_open);

        cdc_free(key_open);
    }

out:
    return res;
}
EXPORT_SYMBOL(core_mp_key_open_test);

int core_mp_key_short_test(uint8_t value)
{
    int res = 0, len;
	uint8_t cmd[2] = {0};
    uint8_t *key_short;

    cmd[0] = protocol->key_short;
	cmd[1] = value;

    res = exec_cdc_command(EXEC_WRITE, cmd, 2, NULL);
    if(res < 0)
    {
        return res;        
    }
    else
    {
        key_short = cdc_alloc(&len, SET_KEY);
        if(ERR_ALLOC_MEM(key_short))
        {
            res = FAIL;
            goto out;
        }

        exec_cdc_command(EXEC_READ, 0, len, key_short);

        cdc_free(key_short);
    }

out:
    return res;
}
EXPORT_SYMBOL(core_mp_key_short_test);

int core_mp_key_has_bg_test(uint8_t value)
{
    int res = 0, len;
	uint8_t cmd[2] = {0};
    uint8_t *key_bg;

    cmd[0] = protocol->key_bg;
	cmd[1] = value;

    res = exec_cdc_command(EXEC_WRITE, cmd, 2, NULL);
    if(res < 0)
    {
        return res;        
    }
    else
    {
        key_bg = cdc_alloc(&len, SET_KEY);
        if(ERR_ALLOC_MEM(key_bg))
        {
            res = FAIL;
            goto out;
        }

        exec_cdc_command(EXEC_READ, 0, len, key_bg);

        cdc_free(key_bg);
    }

out:
    return res;
}
EXPORT_SYMBOL(core_mp_key_has_bg_test);

int core_mp_key_no_bk_test(uint8_t value)
{
    int res = 0, len;
    uint8_t cmd[2] = {0};
    uint8_t *key_no_bk;

    cmd[0] = protocol->key_no_bk;
	cmd[1] = value;

    res = exec_cdc_command(EXEC_WRITE, cmd, 2, NULL);
    if(res < 0)
    {
        return res;        
    }
    else
    {
        key_no_bk = cdc_alloc(&len, SET_KEY);
        if(ERR_ALLOC_MEM(key_no_bk))
        {
            res = FAIL;
            goto out;
        }

        exec_cdc_command(EXEC_READ, 0, len, key_no_bk);

        cdc_free(key_no_bk);
    }

out:
    return res;
}
EXPORT_SYMBOL(core_mp_key_no_bk_test);

int core_mp_key_has_bk_test(uint8_t value)
{
    int res = 0, len;
    uint8_t cmd[2] = {0};
    uint8_t *key_has_bk;

    cmd[0] = protocol->key_no_bk;
	cmd[1] = value;

    res = exec_cdc_command(EXEC_WRITE, cmd, 2, NULL);
    if(res < 0)
    {
        return res;        
    }
    else
    {
        key_has_bk = cdc_alloc(&len, SET_KEY);
        if(ERR_ALLOC_MEM(key_has_bk))
        {
            res = FAIL;
            goto out;
        }

        exec_cdc_command(EXEC_READ, 0, len, key_has_bk);

        cdc_free(key_has_bk);
    }

out:
    return res;
}
EXPORT_SYMBOL(core_mp_key_has_bk_test);

int core_mp_key_dac_test(uint8_t value)
{
    int res = 0, len;
    uint8_t cmd[2] = {0};
    uint8_t *key_dac;

    cmd[0] = protocol->key_dac;
	cmd[1] = value;

    res = exec_cdc_command(EXEC_WRITE, cmd, 2, NULL);
    if(res < 0)
    {
        return res;        
    }
    else
    {
        key_dac = cdc_alloc(&len, SET_KEY);
        if(ERR_ALLOC_MEM(key_dac))
        {
            res = FAIL;
            goto out;
        }

        exec_cdc_command(EXEC_READ, 0, len, key_dac);

        cdc_free(key_dac);
    }

out:
    return res;
}
EXPORT_SYMBOL(core_mp_key_dac_test);

int core_mp_self_signal_test(uint8_t value)
{
    int res = 0, len;
    uint8_t cmd[2] = {0};
    uint8_t *self_signal;

    cmd[0] = protocol->self_signal;
	cmd[1] = value;

    res = exec_cdc_command(EXEC_WRITE, cmd, 2, NULL);
    if(res < 0)
    {
        return res;        
    }
    else
    {
        self_signal = cdc_alloc(&len, SET_SELF);
        if(ERR_ALLOC_MEM(self_signal))
        {
            res = FAIL;
            goto out;
        }

        exec_cdc_command(EXEC_READ, 0, len, self_signal);

        cdc_free(self_signal);
    }

out:
    return res;
}
EXPORT_SYMBOL(core_mp_self_signal_test);

int core_mp_self_no_bk_test(uint8_t value)
{
    int res = 0, len;
    uint8_t cmd[2] = {0};
    uint8_t *self_no_bk;

    cmd[0] = protocol->self_no_bk;
	cmd[1] = value;

    res = exec_cdc_command(EXEC_WRITE, cmd, 2, NULL);
    if(res < 0)
    {
        return res;        
    }
    else
    {
        self_no_bk = cdc_alloc(&len, SET_SELF);
        if(ERR_ALLOC_MEM(self_no_bk))
        {
            res = FAIL;
            goto out;
        }

        exec_cdc_command(EXEC_READ, 0, len, self_no_bk);

        cdc_free(self_no_bk);
    }

out:
    return res;
}
EXPORT_SYMBOL(core_mp_self_no_bk_test);

int core_mp_self_has_bk_test(uint8_t value)
{
    int res = 0, len;
    uint8_t cmd[2] = {0};
    uint8_t *self_has_bk;

    cmd[0] = protocol->self_has_bk;
	cmd[1] = value;

    res = exec_cdc_command(EXEC_WRITE, cmd, 2, NULL);
    if(res < 0)
    {
        return res;        
    }
    else
    {
        self_has_bk = cdc_alloc(&len, SET_SELF);
        if(ERR_ALLOC_MEM(self_has_bk))
        {
            res = FAIL;
            goto out;
        }

        exec_cdc_command(EXEC_READ, 0, len, self_has_bk);

        cdc_free(self_has_bk);
    }

out:
    return res;
}
EXPORT_SYMBOL(core_mp_self_has_bk_test);

int core_mp_self_dac_test(uint8_t value)
{
    int res = 0, len;
    uint8_t cmd[2] = {0};
    uint8_t *self_dac;

    cmd[0] = protocol->self_dac;
	cmd[1] = value;

    res = exec_cdc_command(EXEC_WRITE, cmd, 2, NULL);
    if(res < 0)
    {
        return res;        
    }
    else
    {
        self_dac = cdc_alloc(&len, SET_SELF);
        if(ERR_ALLOC_MEM(self_dac))
        {
            res = FAIL;
            goto out;
        }

        exec_cdc_command(EXEC_READ, 0, len, self_dac);

        cdc_free(self_dac);
    }

out:
    return res;
}
EXPORT_SYMBOL(core_mp_self_dac_test);

int core_mp_cm_test(uint8_t value)
{
    int res = 0, len;
    uint8_t cmd[2] = {0};
    uint8_t *cm_data;

    cmd[0] = protocol->cm_data;
	cmd[1] = value;

    res = exec_cdc_command(EXEC_WRITE, cmd, 2, NULL);
    if(res < 0)
    {
        return res;        
    }
    else
    {
        cm_data = cdc_alloc(&len, SET_MUTUAL);
        if(ERR_ALLOC_MEM(cm_data))
        {
            res = FAIL;
            goto out;
        }

        exec_cdc_command(EXEC_READ, 0, len, cm_data);

        cdc_free(cm_data);
    }

out:
    return res;
}
EXPORT_SYMBOL(core_mp_cm_test);

int core_mp_mutual_signal_test(uint8_t value)
{
    int res = 0, len;
    uint8_t cmd[2] = {0};
    uint8_t *mutual_signal;

    cmd[0] = protocol->mutual_signal;
	cmd[1] = value;

    res = exec_cdc_command(EXEC_WRITE, cmd, 2, NULL);
    if(res < 0)
    {
        return res;        
    }
    else
    {
        mutual_signal = cdc_alloc(&len, SET_MUTUAL);
        if(ERR_ALLOC_MEM(mutual_signal))
        {
            res = FAIL;
            goto out;
        }

        exec_cdc_command(EXEC_READ, 0, len, mutual_signal);

        cdc_free(mutual_signal);
    }

out:
    return res;
}
EXPORT_SYMBOL(core_mp_mutual_signal_test);

int core_mp_mutual_no_bk_test(uint8_t value)
{
    int res = 0, len;
    uint8_t cmd[2] = {0};
    uint8_t *mutual_no_bk;

    cmd[0] = protocol->mutual_no_bk;
	cmd[1] = value;

    res = exec_cdc_command(EXEC_WRITE, cmd, 2, NULL);
    if(res < 0)
    {
        return res;        
    }
    else
    {
        mutual_no_bk = cdc_alloc(&len, SET_MUTUAL);
        if(ERR_ALLOC_MEM(mutual_no_bk))
        {
            res = FAIL;
            goto out;
        }

        exec_cdc_command(EXEC_READ, 0, len, mutual_no_bk);

        cdc_free(mutual_no_bk);
    }

out:
    return res;
}
EXPORT_SYMBOL(core_mp_mutual_no_bk_test);

int core_mp_mutual_has_bk_test(uint8_t value)
{
    int res = 0, len;
    uint8_t cmd[2] = {0};
    uint8_t *mutual_has_bk;

    cmd[0] = protocol->mutual_has_bk;
	cmd[1] = value;

    res = exec_cdc_command(EXEC_WRITE, cmd, 2, NULL);
    if(res < 0)
    {
        return res;        
    }
    else
    {
        mutual_has_bk = cdc_alloc(&len, SET_MUTUAL);
        if(ERR_ALLOC_MEM(mutual_has_bk))
        {
            res = FAIL;
            goto out;
        }

        exec_cdc_command(EXEC_READ, 0, len, mutual_has_bk);

        cdc_free(mutual_has_bk);
    }

out:
    return res;
}
EXPORT_SYMBOL(core_mp_mutual_has_bk_test);

int core_mp_mutual_dac_test(uint8_t value)
{
    int res = 0, len;
    uint8_t cmd[2] = {0};
    uint8_t *mutual_dac;

    cmd[0] = protocol->mutual_dac;
	cmd[1] = value;

    res = exec_cdc_command(EXEC_WRITE, cmd, 2, NULL);
    if(res < 0)
    {
        return res;        
    }
    else
    {
        mutual_dac = cdc_alloc(&len, SET_MUTUAL);
        if(ERR_ALLOC_MEM(mutual_dac))
        {
            res = FAIL;
            goto out;
        }

        exec_cdc_command(EXEC_READ, 0, len, mutual_dac);

        cdc_free(mutual_dac);
    }

out:
    return res;
}
EXPORT_SYMBOL(core_mp_mutual_dac_test);

void core_mp_move_code(void)
{
    if(core_config_check_cdc_busy() < 0)
        DBG_ERR("Check busy is timout !");

    if(core_config_ice_mode_enable() < 0)
    {
        DBG_ERR("Failed to enter ICE mode");
        return;
    }

    /* DMA Trigger */
    core_config_ice_mode_write(0x41010, 0xFF, 1);

    mdelay(30);

    /* Code reset */
    core_config_ice_mode_write(0x40040, 0xAE, 1);

    core_config_ice_mode_disable();

    if(core_config_check_cdc_busy() < 0)
        DBG_ERR("Check busy is timout !");
}
EXPORT_SYMBOL(core_mp_move_code);

int core_mp_run_test(const char *name, uint8_t value)
{
	int i = 0, res = 0;

	DBG_INFO("Test name = %s", name);

	for(i = 0; i < ARRAY_SIZE(single_test); i++)
	{
		if(strcmp(name, single_test[i].name) == 0)
		{
			res = single_test[i].do_test(value);
			DBG_INFO("Result = %d", res);
			return res;
		}
	}

    DBG_ERR("The name can't be found in the list");
    return FAIL;
}

void core_mp_init(void)
{
    
}

void core_mp_remove(void)
{

}
