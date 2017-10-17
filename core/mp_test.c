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

#define EXEC_READ  0
#define EXEC_WRITE 1

#define SET_KEY    0
#define SET_MUTUAL 1
#define SET_SELF   2

#define FAIL       -1

#define Mathabs(x) ({					\
    long ret;					        \
    if (sizeof(x) == sizeof(long)) {	\
        long __x = (x);				    \
        ret = (__x < 0) ? -__x : __x;	\
    } else {					        \
        int __x = (x);				    \
        ret = (__x < 0) ? -__x : __x;	\
    }						            \
    ret;						        \
})

#define CSV_PATH    "/sdcard/ilitek_mp_test.csv"

/* Handle its own test item */
static int mutual_test(int index, uint8_t val);
static int self_test(int index, uint8_t val);
static int key_test(int index, uint8_t val);
static int st_test(int index, uint8_t val);
static int tx_rx_delta_test(int index, uint8_t val);
static int untouch_p2p_test(int index, uint8_t val);

static void mp_test_free(void);
static int exec_cdc_command(bool write, uint8_t *item, int length, uint8_t *buf);

struct core_mp_test_data *core_mp = NULL;

static void mp_test_free(void)
{
    int i;
     
    for(i = 0; i < ARRAY_SIZE(core_mp->tItems); i++)
    {
        core_mp->tItems[i].run = false;
        
        if(core_mp->tItems[i].catalog == TX_RX_DELTA)
        {
            kfree(core_mp->rx_delta_buf);
            core_mp->rx_delta_buf = NULL;
            kfree(core_mp->tx_delta_buf);
            core_mp->tx_delta_buf = NULL;
        }
        else
        {
            kfree(core_mp->tItems[i].buf);
            core_mp->tItems[i].buf = NULL;
        }
    }

    core_mp->m_signal = false;
	core_mp->m_dac = false;
	core_mp->s_signal = false;
	core_mp->s_dac = false;
	core_mp->key_dac = false;
    core_mp->st_dac = false;
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
        if(res < 0)
            goto out;
    }
    else
    {
        if(ERR_ALLOC_MEM(buf))
        {
            DBG_ERR("Invalid buffer");
            res = -ENOMEM;
            goto out;
        }

        cmd[0] = protocol->cmd_read_ctrl;
        cmd[1] = protocol->cmd_get_cdc;
        res = core_i2c_write(core_config->slave_i2c_addr, cmd, 2);
        if(res < 0)
            goto out;
        
        mdelay(1);

        res = core_i2c_read(core_config->slave_i2c_addr, buf, length);
        if(res < 0)
            goto out;
    }

out:
    return res;
}

static int mutual_test(int index, uint8_t val)
{
    int i, res = 0, len = 0;
    int inDACp = 0, inDACn = 0;
    int inCountX = 0, inCountY = 0;
    int FrameCount = 0;
    uint8_t cmd[2] = {0};
    uint8_t *mutual = NULL;
    int32_t *raw = NULL, *raw_sin = NULL;

    cmd[0] = core_mp->tItems[index].cmd;
    cmd[1] = val;

    /* update X/Y channel length if they're changed */
	core_mp->xch_len = core_config->tp_info->nXChannelNum;
	core_mp->ych_len = core_config->tp_info->nYChannelNum;
    
    len = core_mp->xch_len * core_mp->ych_len * 2;
    FrameCount = core_mp->xch_len * core_mp->ych_len;

    DBG_INFO("Read X/Y Channel length = %d", len);
    DBG_INFO("FrameCount = %d", FrameCount);
    
    /* set specifc flag  */
    if(cmd[0] == protocol->mutual_signal)
    {
        core_mp->m_signal = true;
        raw_sin = kzalloc(FrameCount * sizeof(int32_t), GFP_KERNEL);
		if (ERR_ALLOC_MEM(raw_sin))
		{
			DBG_ERR("Failed to allocate signal buffer, %ld", PTR_ERR(raw_sin));
			res = -ENOMEM;
			goto out;
        }       
    }  
    else if(cmd[0] == protocol->mutual_dac)
    {
        core_mp->m_dac = true;        
    }

    /* raw buffer is used to receive data from firmware */
    raw = kzalloc(FrameCount * sizeof(int32_t), GFP_KERNEL);
    if (ERR_ALLOC_MEM(raw))
    {
        DBG_ERR("Failed to allocate raw buffer, %ld", PTR_ERR(raw));
        res = -ENOMEM;
        goto out;
    }    

    if(core_mp->tItems[index].buf == NULL)
    {
        /* Create a buffer that belogs to itself */
        core_mp->tItems[index].buf = kzalloc(FrameCount * sizeof(int32_t), GFP_KERNEL);
    }
    else
    {
        /* erase data if this buffrer was not cleaned and freed */
        memset(core_mp->tItems[index].buf, 0x0, FrameCount * sizeof(int32_t));
    }

    /* sending command to get raw data, converting it and comparing it with threshold */
    res = exec_cdc_command(EXEC_WRITE, cmd, 2, NULL);
    if(res < 0)
    {
        DBG_ERR("I2C error");
        goto out;
    }
    else
    {
        mutual = kzalloc(len, GFP_KERNEL);
        if(ERR_ALLOC_MEM(mutual))
        {
            res = FAIL;
            goto out;
        }

        res = exec_cdc_command(EXEC_READ, 0, len, mutual);
		if(res < 0)
            goto out;
                      
        /* Start to converting data */
        for(i = 0; i < FrameCount; i++)
        {
            if(core_mp->m_dac)
            {
                /* DAC - P */
                if(((mutual[2 * i] & 0x80) >> 7) == 1)
                {
                    /* Negative */
                    inDACp = 0 - (int)(mutual[2 * i] & 0x7F); 
                }
                else
                {
                    inDACp = mutual[2 * i] & 0x7F;
                }

                /* DAC - N */
                if(((mutual[1 + (2 * i)] & 0x80) >> 7) == 1)
                {
                    /* Negative */
                    inDACn = 0 - (int)(mutual[1 + (2 * i)] & 0x7F);
                }
                else
                {
                    inDACn = mutual[1 + (2 * i)] & 0x7F;
                }

                raw[i] = (inDACp + inDACn) / 2;
            }
            else
            {
                /* H byte + L byte */
                raw[i] = (mutual[2 * i] << 8) +  mutual[1 + (2 * i)];

                if(core_mp->m_signal)
                {
                    if((raw[i] * 0x8000) == 0x8000)
                    {
                        raw_sin[i] = raw[i] - 65536;
                    }
                    else
                    {
                        raw_sin[i] = raw[i];
                    }
                }
            }

            if(inCountX == (core_mp->xch_len - 1))
            {
                inCountY++;
                inCountX = 0;
            }
            else
            {
                inCountX++;
            }
        }
    }

    /* assign raw buffer to its own buffer after calculated */
    for(i = 0; i < FrameCount; i ++)
    {
        if(core_mp->m_signal)
            core_mp->tItems[index].buf[i] = raw_sin[i];
        else
            core_mp->tItems[index].buf[i] = raw[i];
    }      
        
out:
    kfree(mutual);
    kfree(raw);
    kfree(raw_sin);
    core_mp->m_signal = false;
    core_mp->m_dac = false;
    core_mp->tItems[index].result = (res != 0 ? "FAIL" : "PASS");   
    return res;    
}

static int self_test(int index, uint8_t val)
{
    int i, res = 0, len = 0;
    int inDACp = 0, inDACn = 0;
    int inCountX = 0, inCountY = 0;
    int FrameCount = 0;
    uint8_t cmd[2] = {0};
    uint8_t *self = NULL;
    int32_t *raw = NULL, *raw_sin = NULL;

    cmd[0] = core_mp->tItems[index].cmd;
    cmd[1] = val;

    /* update self tx/rx length if they're changed */
    core_mp->stx_len = core_config->tp_info->self_tx_channel_num;
    core_mp->srx_len = core_config->tp_info->self_rx_channel_num;

    len = core_mp->stx_len * core_mp->srx_len * 2;
    FrameCount = core_mp->stx_len * core_mp->srx_len;
    
    DBG_INFO("Read SELF X/Y Channel length = %d", len);
    DBG_INFO("FrameCount = %d", FrameCount);    

    /* set specifc flag  */
    if(cmd[0] == protocol->self_signal)
    {
        core_mp->s_signal = true;
        raw_sin = kzalloc(FrameCount * sizeof(int32_t), GFP_KERNEL);
		if (ERR_ALLOC_MEM(raw_sin))
		{
			DBG_ERR("Failed to allocate signal buffer, %ld", PTR_ERR(raw_sin));
			res = -ENOMEM;
			goto out;
        }       
    }  
    else if(cmd[0] == protocol->self_dac)
    {
        core_mp->s_dac = true;        
    }

    /* raw buffer is used to receive data from firmware */
    raw = kzalloc(FrameCount * sizeof(int32_t), GFP_KERNEL);
    if (ERR_ALLOC_MEM(raw))
    {
        DBG_ERR("Failed to allocate raw buffer, %ld", PTR_ERR(raw));
        res = -ENOMEM;
        goto out;
    }    

    if(core_mp->tItems[index].buf == NULL)
    {
        /* Create a buffer that belogs to itself */
        core_mp->tItems[index].buf = kzalloc(FrameCount * sizeof(int32_t), GFP_KERNEL);
    }
    else
    {
        /* erase data if this buffrer was not cleaned and freed */
        memset(core_mp->tItems[index].buf, 0x0, FrameCount * sizeof(int32_t));
    }

    /* sending command to get raw data, converting it and comparing it with threshold */
    res = exec_cdc_command(EXEC_WRITE, cmd, 2, NULL);
    if(res < 0)
    {
        goto out;    
    }
    else
    {
        self = kzalloc(len, GFP_KERNEL);
        if(ERR_ALLOC_MEM(self))
        {
            res = FAIL;
            goto out;
        }

        res = exec_cdc_command(EXEC_READ, 0, len, self);
		if(res < 0)
			goto out;

        /* Start to converting data */
        for(i = 0; i < FrameCount; i++)
        {
            if(core_mp->s_dac)
            {
                /* DAC - P */
                if(((self[2 * i] & 0x80) >> 7) == 1)
                {
                    /* Negative */
                    inDACp = 0 - (int)(self[2 * i] & 0x7F); 
                }
                else
                {
                    inDACp = self[2 * i] & 0x7F;
                }

                /* DAC - N */
                if(((self[1 + (2 * i)] & 0x80) >> 7) == 1)
                {
                    /* Negative */
                    inDACn = 0 - (int)(self[1 + (2 * i)] & 0x7F);
                }
                else
                {
                    inDACn = self[1 + (2 * i)] & 0x7F;
                }

                raw[i] = (inDACp + inDACn) / 2;
            }
            else
            {
                /* H byte + L byte */
                raw[i] = (self[2 * i] << 8) +  self[1 + (2 * i)];

                if(core_mp->s_signal)
                {
                    if((raw[i] * 0x8000) == 0x8000)
                    {
                        raw_sin[i] = raw[i] - 65536;
                    }
                    else
                    {
                        raw_sin[i] = raw[i];
                    }
                }
            }

            if(inCountX == (core_mp->xch_len - 1))
            {
                inCountY++;
                inCountX = 0;
            }
            else
            {
                inCountX++;
            }
        }        
    }

    /* assign raw buffer to its own buffer after calculated */
    for(i = 0; i < FrameCount; i ++)
    {
        if(core_mp->s_signal)
            core_mp->tItems[index].buf[i] = raw_sin[i];
        else
            core_mp->tItems[index].buf[i] = raw[i];
    }      
        
out:
    kfree(self);
    kfree(raw);
    kfree(raw_sin);
    core_mp->s_signal = false;
    core_mp->s_dac = false;
    core_mp->tItems[index].result = (res != 0 ? "FAIL" : "PASS");   
    return res; 
}

static int key_test(int index, uint8_t val)
{
    int i, res = 0;
    int len = 0;
    int inDACp = 0, inDACn = 0;
    uint8_t cmd[2] = {0};
    uint8_t *icon = NULL;
    int32_t *raw = NULL;

    cmd[0] = core_mp->tItems[index].cmd;
    cmd[1] = val;

    /* update key's length if they're changed */
    core_mp->key_len = core_config->tp_info->nKeyCount;
    len = core_mp->key_len * 2;

    DBG_INFO("Read key's length = %d", len);
    DBG_INFO("core_mp->key_len = %d",core_mp->key_len); 

    /* raw buffer is used to receive data from firmware */
    raw = kzalloc(core_mp->key_len  * sizeof(int32_t), GFP_KERNEL);
    if (ERR_ALLOC_MEM(raw))
    {
        DBG_ERR("Failed to allocate raw buffer, %ld", PTR_ERR(raw));
        res = -ENOMEM;
        goto out;
    }

    if(core_mp->tItems[index].buf == NULL)
    {
        /* Create a buffer that belogs to itself */
        core_mp->tItems[index].buf = kzalloc(core_mp->key_len * sizeof(int32_t), GFP_KERNEL);
    }
    else
    {
        /* erase data if this buffrer was not cleaned and freed */
        memset(core_mp->tItems[index].buf, 0x0, core_mp->key_len * sizeof(int32_t));
    }
    
    if(cmd[0] == protocol->key_dac)
        core_mp->key_dac = true;

    /* sending command to get raw data, converting it and comparing it with threshold */
    res = exec_cdc_command(EXEC_WRITE, cmd, 2, NULL);
    if(res < 0)
    {
        goto out;
    }
    else
    {
        icon = kzalloc(len, GFP_KERNEL);
        if(ERR_ALLOC_MEM(icon))
        {
            res = FAIL;
            goto out;
        }

        res = exec_cdc_command(EXEC_READ, 0, len, icon);
		if(res < 0)
            goto out;
        
        /* Start to converting data */
        for(i = 0; i < core_mp->key_len; i++)
        {
            if(core_mp->key_dac)
            {
                /* DAC - P */
                if(((icon[2 * i] & 0x80) >> 7) == 1)
                {
                    /* Negative */
                    inDACp = 0 - (int)(icon[2 * i] & 0x7F); 
                }
                else
                {
                    inDACp = icon[2 * i] & 0x7F;
                }
    
                /* DAC - N */
                if(((icon[1 + (2 * i)] & 0x80) >> 7) == 1)
                {
                    /* Negative */
                    inDACn = 0 - (int)(icon[1 + (2 * i)] & 0x7F);
                }
                else
                {
                    inDACn = icon[1 + (2 * i)] & 0x7F;
                }
    
                raw[i] = (inDACp + inDACn) / 2;
            }
        }
    }

    /* assign raw buffer to its own buffer after calculated */
    for(i = 0; i < core_mp->key_len; i ++)
    {
        if(core_mp->key_dac)
            core_mp->tItems[index].buf[i] = raw[i];
    }

out:
    kfree(icon);
    kfree(raw);
    core_mp->key_dac = false;
    core_mp->tItems[index].result = (res != 0 ? "FAIL" : "PASS");
    return res;    
}

static int st_test(int index, uint8_t val)
{
    int i, res = 0;
    int len = 0;
    int inDACp = 0, inDACn = 0;
    uint8_t cmd[2] = {0};
    uint8_t *st = NULL;
    int32_t *raw = NULL;

    cmd[0] = core_mp->tItems[index].cmd;
    cmd[1] = val;

    /* update side touch's length it they're changed */
    core_mp->st_len = core_config->tp_info->side_touch_type;
    len = core_mp->st_len * 2;

    DBG_INFO("Read st's length = %d", len);
    DBG_INFO("core_mp->st_len = %d", core_mp->st_len); 

    /* raw buffer is used to receive data from firmware */
    raw = kzalloc(core_mp->st_len  * sizeof(int32_t), GFP_KERNEL);
    if (ERR_ALLOC_MEM(raw))
    {
        DBG_ERR("Failed to allocate raw buffer, %ld", PTR_ERR(raw));
        res = -ENOMEM;
        goto out;
    }

    if(core_mp->tItems[index].buf == NULL)
    {
        /* Create a buffer that belogs to itself */
        core_mp->tItems[index].buf = kzalloc(core_mp->st_len * sizeof(int32_t), GFP_KERNEL);
    }
    else
    {
        /* erase data if this buffrer was not cleaned and freed */
        memset(core_mp->tItems[index].buf, 0x0, core_mp->st_len * sizeof(int32_t));
    }
    
    if(cmd[0] == protocol->st_dac)
        core_mp->st_dac = true;

    /* sending command to get raw data, converting it and comparing it with threshold */
    res = exec_cdc_command(EXEC_WRITE, cmd, 2, NULL);
    if(res < 0)
    {
        goto out;      
    }
    else
    {
        st = kzalloc(len, GFP_KERNEL);
        if(ERR_ALLOC_MEM(st))
        {
            res = FAIL;
            goto out;
        }

        res = exec_cdc_command(EXEC_READ, 0, len, st);
		if(res < 0)
			goto out;

        /* Start to converting data */
        for(i = 0; i < core_mp->st_len; i++)
        {
            if(core_mp->st_dac)
            {
                /* DAC - P */
                if(((st[2 * i] & 0x80) >> 7) == 1)
                {
                    /* Negative */
                    inDACp = 0 - (int)(st[2 * i] & 0x7F); 
                }
                else
                {
                    inDACp = st[2 * i] & 0x7F;
                }
    
                /* DAC - N */
                if(((st[1 + (2 * i)] & 0x80) >> 7) == 1)
                {
                    /* Negative */
                    inDACn = 0 - (int)(st[1 + (2 * i)] & 0x7F);
                }
                else
                {
                    inDACn = st[1 + (2 * i)] & 0x7F;
                }
    
                raw[i] = (inDACp + inDACn) / 2;
            }
        }
    }

    /* assign raw buffer to its own buffer after calculated */
    for(i = 0; i < core_mp->st_len; i ++)
    {
        if(core_mp->st_dac)
            core_mp->tItems[index].buf[i] = raw[i];
    }

out:
    kfree(st);
    kfree(raw);
    core_mp->st_dac = false;
    core_mp->tItems[index].result = (res != 0 ? "FAIL" : "PASS");
    return res; 
}

static int tx_rx_delta_test(int index, uint8_t val)
{
    int i, x, y, res = 0;
    int FrameCount = 1;
    int len = 0;
    uint8_t cmd[2] = {0};
    uint8_t *delta = NULL;

    cmd[0] = core_mp->tItems[index].cmd;
    cmd[1] = val;

    /* update X/Y channel length if they're changed */
	core_mp->xch_len = core_config->tp_info->nXChannelNum;
    core_mp->ych_len = core_config->tp_info->nYChannelNum;
    
    len = core_mp->xch_len * core_mp->ych_len;

    DBG_INFO("Read Tx/Rx delta length = %d", len);
    
    /* Because tx/rx have their own buffer to store data speparately, */
    /* I allocate outside buffer instead of its own one to catch it */
	core_mp->tx_delta_buf = kzalloc(len * sizeof(int32_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(core_mp->tx_delta_buf))
	{
		DBG_ERR("Failed to allocate raw buffer, %ld", PTR_ERR(core_mp->tx_delta_buf));
		res = -ENOMEM;
		goto out;
    }

	core_mp->rx_delta_buf = kzalloc(len * sizeof(int32_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(core_mp->rx_delta_buf))
	{
		DBG_ERR("Failed to allocate raw buffer, %ld", PTR_ERR(core_mp->rx_delta_buf));
		res = -ENOMEM;
		goto out;
    }

    DBG_INFO("core_mp->tx_delta_buf = %p", core_mp->tx_delta_buf);
    DBG_INFO("core_mp->rx_delta_buf = %p", core_mp->rx_delta_buf);
    
    res = exec_cdc_command(EXEC_WRITE, cmd, 2, NULL);
    if(res < 0)
    {
        goto out;      
    }
    else
    {
        delta = kzalloc(len, GFP_KERNEL);
        if(ERR_ALLOC_MEM(delta))
        {
            res = FAIL;
            goto out;
        }

        res = exec_cdc_command(EXEC_READ, 0, len, delta);
		if(res < 0)
            goto out;

        for(i = 0; i < FrameCount; i++)
        {
            for(y = 0; y < core_mp->ych_len; y++)
            {
                for(x = 0; x < core_mp->xch_len; x++)
                {
                    /* Rx Delta */
                    if(x != (core_mp->xch_len - 1))
                    {
                        core_mp->rx_delta_buf[x+y] = Mathabs(delta[x+y] - delta[(x+1)+y]);
                    }
    
                    /* Tx Delta */
                    if(y != (core_mp->ych_len - 1))
                    {
                        core_mp->tx_delta_buf[x+y] = Mathabs(delta[x+y] - delta[x+(y+1)]);
                    }
                }
            }
        }
    }

out:
    kfree(delta);
    core_mp->tItems[index].result = (res != 0 ? "FAIL" : "PASS");
    return res; 
}

static int untouch_p2p_test(int index, uint8_t count)
{
    int x, y;
    int res = 0;
    int len = 0;
    uint8_t cmd[2] = {0};
    uint8_t *p2p = NULL;
    int32_t *max_buf = NULL, *min_buf = NULL;

    cmd[0] = core_mp->tItems[index].cmd;
    cmd[1] = 0x0;

    if(count <= 0)
    {
        DBG_ERR("The frame is equal or less than 0");
        res = -EINVAL;
        goto out;
    }

    /* update X/Y channel length if they're changed */
	core_mp->xch_len = core_config->tp_info->nXChannelNum;
    core_mp->ych_len = core_config->tp_info->nYChannelNum;
    
    len = core_mp->xch_len * core_mp->ych_len;

    DBG_INFO("Read length of frame = %d", len);

    if(core_mp->tItems[index].buf == NULL)
    {
        /* Create a buffer that belogs to itself */
        core_mp->tItems[index].buf = kzalloc(len * sizeof(int32_t), GFP_KERNEL);
    }
    else
    {
        /* erase data if this buffrer was not cleaned and freed */
        memset(core_mp->tItems[index].buf, 0x0, len * sizeof(int32_t));
    }

    max_buf = kmalloc(len * sizeof(int32_t), GFP_KERNEL);
    if (ERR_ALLOC_MEM(max_buf))
    {
        DBG_ERR("Failed to allocate MAX buffer, %ld", PTR_ERR(max_buf));
        res = -ENOMEM;
        goto out;
    }

    min_buf = kmalloc(len * sizeof(int32_t), GFP_KERNEL);
    if (ERR_ALLOC_MEM(min_buf))
    {
        DBG_ERR("Failed to allocate MIN buffer, %ld", PTR_ERR(min_buf));
        res = -ENOMEM;
        goto out;
    }

    /* init values only once */
    for(y = 0; y < core_mp->ych_len; y++)
    {
        for(x = 0; x < core_mp->xch_len; x++)
        {
            max_buf[x+y] = -65535;
            min_buf[x+y] = 65535;
        }
    }

    while(count > 0)
    {
        res = exec_cdc_command(EXEC_WRITE, cmd, 2, NULL);
        if(res < 0)
        {
            DBG_ERR("I2C error");
            goto out;      
        }
        else
        {
            if(p2p == NULL)
            {
                p2p = kzalloc(len, GFP_KERNEL);
                if(ERR_ALLOC_MEM(p2p))
                {
                    res = FAIL;
                    DBG_ERR("Failed to create p2p buffer");
                    goto out;
                }
            }
    
            res = exec_cdc_command(EXEC_READ, 0, len, p2p);
            if(res < 0)
                goto out;
    
            /* Start to comparing data with each frame */
            for(y = 0; y < core_mp->ych_len; y++)
            {
                for(x = 0; x < core_mp->xch_len; x++)
                {
                    if(p2p[x+y] > max_buf[x+y])
                    {
                        max_buf[x+y] = p2p[x+y];
                    }

                    if(p2p[x+y] < min_buf[+y])
                    {
                        min_buf[x+y] = p2p[x+y];
                    }
                }
            }
            memset(p2p, 0x0, len * sizeof(uint8_t));
        }
        count--;
    }

    /* Get final result */
    for(y = 0; y < core_mp->ych_len; y++)
    {
        for(x = 0; x < core_mp->xch_len; x++)
        {
            core_mp->tItems[index].buf[x+y] = max_buf[x+y] - min_buf[x+y];
        }
    }

out:
    kfree(p2p);
    kfree(max_buf);
    kfree(min_buf);
    core_mp->tItems[index].result = (res != 0 ? "FAIL" : "PASS");
    return res; 
}

void core_mp_show_result(void)
{
    int i, x, y;
    char *csv = NULL;
    struct file *f = NULL;
    mm_segment_t fs;

    fs = get_fs();
    set_fs(KERNEL_DS);

    csv = kmalloc(1024, GFP_KERNEL);

    DBG_INFO("Open CSV: %s ", CSV_PATH);

    if(f == NULL)
    f = filp_open(CSV_PATH, O_CREAT | O_RDWR , 0644);

    if(ERR_ALLOC_MEM(f))
    {
        DBG_ERR("Failed to open CSV file %s", CSV_PATH);
        goto fail_open;
    }

	for(i = 0; i < ARRAY_SIZE(core_mp->tItems); i++)
	{
        if(core_mp->tItems[i].run)
        {
            printk("\n\n");
            printk(" %s : %s ", core_mp->tItems[i].desp, core_mp->tItems[i].result);
            sprintf(csv,  " %s : %s ", core_mp->tItems[i].desp, core_mp->tItems[i].result);
            f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

            printk("\n");
            sprintf(csv, "\n");
            f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

            if(core_mp->tItems[i].catalog == MUTUAL_TEST)
            {
                /* print X raw */
                for(x = 0; x < core_mp->xch_len; x++)
                {
                    if(x == 0)
                    {
                        printk("           ");
                        sprintf(csv, ",");
                        f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);                      
                    }

                    printk("X%02d      ", x);
                    sprintf(csv, "X%02d, ", x);
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                }

                printk("\n");
                sprintf(csv, "\n");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

                for(y = 0; y < core_mp->ych_len; y++)
                {
                    printk(" Y%02d ", y);
                    sprintf(csv, "Y%02d, ", y);
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

                    for(x = 0; x < core_mp->xch_len; x++)
                    {
                        printk(" %7d ",core_mp->tItems[i].buf[x+y]);
                        sprintf(csv, "%7d,", core_mp->tItems[i].buf[x+y]);
                        f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                    }
                    printk("\n");
                    sprintf(csv, "\n");
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                }
            }
            else if(core_mp->tItems[i].catalog == SELF_TEST)
            {
                printk("\n\n");
                printk(" %s : %s ", core_mp->tItems[i].desp, core_mp->tItems[i].result);
                sprintf(csv,  " %s : %s ", core_mp->tItems[i].desp, core_mp->tItems[i].result);
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
    
                printk("\n");
                sprintf(csv, "\n");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

                /* print X raw */
                for(x = 0; x < core_mp->xch_len; x++)
                {
                    if(x == 0)
                    {
                        printk("           ");
                        sprintf(csv, ",");
                        f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);                      
                    }

                    printk("X%02d      ", x);
                    sprintf(csv, "X%02d, ", x);
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                }

                printk("\n");
                sprintf(csv, "\n");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

                for(y = 0; y < core_mp->ych_len; y++)
                {
                    printk(" Y%02d ", y);
                    sprintf(csv, "Y%02d, ", y);
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

                    for(x = 0; x < core_mp->xch_len; x++)
                    {
                        printk(" %7d ",core_mp->tItems[i].buf[x+y]);
                        sprintf(csv, "%7d,", core_mp->tItems[i].buf[x+y]);
                        f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                    }
                    printk("\n");
                    sprintf(csv, "\n");
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                }
            }
            else if(core_mp->tItems[i].catalog == KEY_TEST)
            {
                for(x = 0; x < core_mp->key_len; x++)
                {
                    printk("KEY_%02d ",x);     
                    sprintf(csv, "KEY_%02d,", x);
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);                     
                }

                printk("\n");
                sprintf(csv, "\n");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

                for(y = 0; y < core_mp->key_len; y++)
                {
                    printk(" %3d   ",core_mp->tItems[i].buf[y]);     
                    sprintf(csv, " %3d, ", core_mp->tItems[i].buf[y]);
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);           
                }
                printk("\n");
                sprintf(csv, "\n");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
            }
            else if(core_mp->tItems[i].catalog == ST_TEST)
            {
                /* TODO: Not implemented yet */
            }
            else if(core_mp->tItems[i].catalog == TX_RX_DELTA)
            {
                /* print X raw */
                for(x = 0; x < core_mp->xch_len; x++)
                {
                    if(x == 0)
                    {
                        printk("           ");
                        sprintf(csv, ",");
                        f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);                      
                    }

                    printk("X%02d       ", x);
                    sprintf(csv, "X%02d, ", x);
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                }

                printk("\n");
                sprintf(csv, "\n");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

                for(y = 0; y < core_mp->ych_len; y++)
                {
                    printk(" Y%02d ", y);
                    sprintf(csv, "Y%02d, ", y);
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

                    for(x = 0; x < core_mp->xch_len; x++)
                    {
                        /* Threshold with RX delta */
                        if(core_mp->rx_delta_buf[x+y] <= core_mp->RxDeltaMax &&
                            core_mp->rx_delta_buf[x+y] >= core_mp->RxDeltaMin)
                        {
                            printk(" %7d ",core_mp->rx_delta_buf[x+y]); 
                            sprintf(csv, "%7d,", core_mp->rx_delta_buf[x+y]);
                            f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                        }
                        else
                        {
                            if(core_mp->rx_delta_buf[x+y] > core_mp->RxDeltaMax)
                            {
                                printk(" *%7d ",core_mp->rx_delta_buf[x+y]);
                                sprintf(csv, "*%7d,", core_mp->rx_delta_buf[x+y]);
                                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                            }
                            else
                            {
                                printk(" #%7d ",core_mp->rx_delta_buf[x+y]);
                                sprintf(csv, "#%7d,", core_mp->rx_delta_buf[x+y]);
                                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                            }
                        }
                    }
                    printk("\n");
                    sprintf(csv, "\n");
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                }

                printk("\n");
                sprintf(csv, "\n");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                
                for(y = 0; y < core_mp->ych_len; y++)
                {
                    printk(" Y%02d ", y);
                    sprintf(csv, "Y%02d, ", y);
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

                    for(x = 0; x < core_mp->xch_len; x++)
                    {   
                        /* Threshold with TX delta */
                        if(core_mp->tx_delta_buf[x+y] <= core_mp->TxDeltaMax &&
                            core_mp->tx_delta_buf[x+y] >= core_mp->TxDeltaMin)
                        {
                            printk(" %7d ",core_mp->tx_delta_buf[x+y]);
                            sprintf(csv, "%7d,", core_mp->tx_delta_buf[x+y]);
                            f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                        }
                        else
                        {
                            if(core_mp->tx_delta_buf[x+y] > core_mp->TxDeltaMax)
                            {
                                printk(" *%7d ",core_mp->tx_delta_buf[x+y]);
                                sprintf(csv, "*%7d,", core_mp->tx_delta_buf[x+y]);
                                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                                
                            }
                            else
                            {
                                printk(" #%7d ",core_mp->tx_delta_buf[x+y]);
                                sprintf(csv, "#%7d,", core_mp->tx_delta_buf[x+y]);
                                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                            }
                        }
                    }
                    printk("\n");
                    sprintf(csv, "\n");
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                }
                printk("\n");
                sprintf(csv, "\n");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
            }
            else if(core_mp->tItems[i].catalog == UNTOUCH_P2P)
            {
                /* print X raw */
                for(x = 0; x < core_mp->xch_len; x++)
                {
                    if(x == 0)
                    {
                        printk("           ");
                        sprintf(csv, ",");
                        f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);                      
                    }

                    printk("X0%2d       ", x);
                    sprintf(csv, "X%02d, ", x);
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                }

                printk("\n");
                sprintf(csv, "\n");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

                for(y = 0; y < core_mp->ych_len; y++)
                {
                    printk(" Y%02d ", y);
                    sprintf(csv, "Y%02d, ", y);
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

                    for(x = 0; x < core_mp->xch_len; x++)
                    {
                        /* Threshold with P2P */
                        if(core_mp->tItems[i].buf[x+y] <= core_mp->P2PMax &&
                            core_mp->tItems[i].buf[x+y] >= core_mp->P2PMin)
                        {
                            printk(" %7d ",core_mp->tItems[i].buf[x+y]);
                            sprintf(csv, "%7d,", core_mp->tItems[i].buf[x+y]);
                            f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                        }
                        else
                        {
                            if(core_mp->tItems[i].buf[x+y] > core_mp->P2PMax)
                            {
                                printk(" *%7d ",core_mp->tItems[i].buf[x+y]);
                                sprintf(csv, "*%7d,", core_mp->tItems[i].buf[x+y]);
                                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                            }
                            else
                            {
                                printk(" #%7d ",core_mp->tItems[i].buf[x+y]);
                                sprintf(csv, "#%7d,", core_mp->tItems[i].buf[x+y]);
                                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                            }
                        }
                    }
                    printk("\n");
                    sprintf(csv, "\n");
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                }                
            }
            printk("\n");
            sprintf(csv, "\n");
            f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
        }
    }

    mp_test_free();    
    filp_close(f, NULL);
fail_open:
    set_fs(fs);
    kfree(csv);
    return;    
}
EXPORT_SYMBOL(core_mp_show_result);

int core_mp_run_test(const char *name, uint8_t val)
{
	int i = 0, res = 0;

    DBG_INFO("Test name = %s, size = %d", name, (int)ARRAY_SIZE(core_mp->tItems));
    
	for(i = 0; i < ARRAY_SIZE(core_mp->tItems); i++)
	{
        if(strcmp(name, core_mp->tItems[i].name) == 0)
		{
            core_mp->tItems[i].run = true;
            res = core_mp->tItems[i].do_test(i, val);
            DBG_INFO("***** DONE: core_mp->tItems[%d] = %p ", i, core_mp->tItems[i].buf);
            printk("\n\n\n");
			return res;
		}
    }

    DBG_ERR("The name can't be found in the list");
    return FAIL;
}
EXPORT_SYMBOL(core_mp_run_test);

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

int core_mp_init(void)
{
    int res = 0;

    if(!ERR_ALLOC_MEM(core_config->tp_info))
    {
        if(core_mp == NULL)
        {
            core_mp = kzalloc(sizeof(*core_mp), GFP_KERNEL);
            if (ERR_ALLOC_MEM(core_mp))
            {
                DBG_ERR("Failed to init core_mp, %ld", PTR_ERR(core_mp));
				res = -ENOMEM;
				goto out;
            }

            core_mp->xch_len = core_config->tp_info->nXChannelNum;
            core_mp->ych_len = core_config->tp_info->nYChannelNum;

            core_mp->stx_len = core_config->tp_info->self_tx_channel_num;
            core_mp->srx_len = core_config->tp_info->self_rx_channel_num;

            core_mp->key_len = core_config->tp_info->nKeyCount;
            core_mp->st_len = core_config->tp_info->side_touch_type;

            /* Set spec threshold as initial value */
            core_mp->TxDeltaMax = 0;
            core_mp->RxDeltaMax = 0;
            core_mp->TxDeltaMin = 9999;
            core_mp->RxDeltaMin = 9999;
            core_mp->P2PMax = 0;
            core_mp->P2PMin = 9999;

			/* Initialize MP test functions with its own command from protocol.c */
			memset(core_mp->tItems, 0x0, sizeof(ARRAY_SIZE(core_mp->tItems)));

            core_mp->tItems[0].name = "mutual_dac";
            core_mp->tItems[0].cmd = protocol->mutual_dac;
            core_mp->tItems[0].do_test = mutual_test;
            core_mp->tItems[0].desp = "Calibration Data(DAC/Mutual)";
            core_mp->tItems[0].catalog = MUTUAL_TEST;
                        
            core_mp->tItems[1].name = "mutual_bg";
            core_mp->tItems[1].cmd = protocol->mutual_bg;
            core_mp->tItems[1].do_test = mutual_test;
            core_mp->tItems[1].desp = "Baseline Data(BG)";
            core_mp->tItems[1].catalog = MUTUAL_TEST;

            core_mp->tItems[2].name = "mutual_signal";
            core_mp->tItems[2].cmd = protocol->mutual_signal;
            core_mp->tItems[2].do_test = mutual_test;
            core_mp->tItems[2].desp = "Signal Data(BG - RAW - 4096)";
            core_mp->tItems[2].catalog = MUTUAL_TEST;
                        
            core_mp->tItems[3].name = "mutual_no_bk";
            core_mp->tItems[3].cmd = protocol->mutual_no_bk;
            core_mp->tItems[3].do_test = mutual_test;
            core_mp->tItems[3].desp = "Raw Data(No BK)";
            core_mp->tItems[3].catalog = MUTUAL_TEST;

            core_mp->tItems[4].name = "mutual_has_bk";
            core_mp->tItems[4].cmd = protocol->mutual_has_bk;
            core_mp->tItems[4].do_test = mutual_test;
            core_mp->tItems[4].desp = "Raw Data(Have BK)";
            core_mp->tItems[4].catalog = MUTUAL_TEST;

            core_mp->tItems[5].name = "mutual_bk_dac";
            core_mp->tItems[5].cmd = protocol->mutual_bk_dac;
            core_mp->tItems[5].do_test = mutual_test;
            core_mp->tItems[5].desp = "Manual BK Data(Mutual)";
            core_mp->tItems[5].catalog = MUTUAL_TEST;

            core_mp->tItems[6].name = "self_dac";
            core_mp->tItems[6].cmd = protocol->self_dac;
            core_mp->tItems[6].do_test = self_test;
            core_mp->tItems[6].desp = "Calibration Data(DAC/Self_Tx/Self_Rx)";
            core_mp->tItems[6].catalog = SELF_TEST;
			
            core_mp->tItems[7].name = "self_bg";
            core_mp->tItems[7].cmd = protocol->self_bg;
            core_mp->tItems[7].do_test = self_test;
            core_mp->tItems[7].desp = "Baselin Data(BG,Self_Tx,Self_Rx)";
            core_mp->tItems[7].catalog = SELF_TEST;

            core_mp->tItems[8].name = "self_signal";
            core_mp->tItems[8].cmd = protocol->self_signal;
            core_mp->tItems[8].do_test = self_test;
            core_mp->tItems[8].desp = "Signal Data(Self_Tx,Self_Rx/RAW -4096/Have BK)";
            core_mp->tItems[8].catalog = SELF_TEST;

            core_mp->tItems[9].name = "self_no_bk";
            core_mp->tItems[9].cmd = protocol->self_no_bk;
            core_mp->tItems[9].do_test = self_test;
            core_mp->tItems[9].desp = "Raw Data(Self_Tx/Self_Rx/No BK)";
            core_mp->tItems[9].catalog = SELF_TEST;

            core_mp->tItems[10].name = "self_has_bk";
            core_mp->tItems[10].cmd = protocol->self_has_bk;
            core_mp->tItems[10].do_test = self_test;
            core_mp->tItems[10].desp = "Raw Data(Self_Tx/Self_Rx/Have BK)";
            core_mp->tItems[10].catalog = SELF_TEST;

            core_mp->tItems[11].name = "self_bk_dac";
            core_mp->tItems[11].cmd = protocol->self_bk_dac;
            core_mp->tItems[11].do_test = self_test;
            core_mp->tItems[11].desp = "Manual BK DAC Data(Self_Tx,Self_Rx)";
            core_mp->tItems[11].catalog = SELF_TEST;

            core_mp->tItems[12].name = "key_dac";
            core_mp->tItems[12].cmd = protocol->key_dac;
            core_mp->tItems[12].do_test = key_test;
            core_mp->tItems[12].desp = "Calibration Data(DAC/ICON)";
            core_mp->tItems[12].catalog = KEY_TEST;

            core_mp->tItems[13].name = "key_bg";
            core_mp->tItems[13].cmd = protocol->key_bg;
            core_mp->tItems[13].do_test = key_test;
            core_mp->tItems[13].desp = "Baselin Data(BG,Self_Tx,Self_Rx)";
            core_mp->tItems[13].catalog = KEY_TEST;

            core_mp->tItems[14].name = "key_no_bk";
            core_mp->tItems[14].cmd = protocol->key_no_bk;
            core_mp->tItems[14].do_test = key_test;
            core_mp->tItems[14].desp = "ICON Raw Data";
            core_mp->tItems[14].catalog = KEY_TEST;

            core_mp->tItems[15].name = "key_has_bk";
            core_mp->tItems[15].cmd = protocol->key_has_bk;
            core_mp->tItems[15].do_test = key_test;
            core_mp->tItems[15].desp = "ICON Raw Data(Have BK)";
            core_mp->tItems[15].catalog = KEY_TEST;

            core_mp->tItems[16].name = "key_open";
            core_mp->tItems[16].cmd = protocol->key_open;
            core_mp->tItems[16].do_test = key_test;
            core_mp->tItems[16].desp = "ICON Open Data";
            core_mp->tItems[16].catalog = KEY_TEST;

            core_mp->tItems[17].name = "key_short";
            core_mp->tItems[17].cmd = protocol->key_short;
            core_mp->tItems[17].do_test = key_test;
            core_mp->tItems[17].desp = "ICON Short Data";
            core_mp->tItems[17].catalog = KEY_TEST;

            core_mp->tItems[18].name = "st_dac";
            core_mp->tItems[18].cmd = protocol->st_dac;
            core_mp->tItems[18].do_test = st_test;
            core_mp->tItems[18].desp = "ST DAC";
            core_mp->tItems[18].catalog = ST_TEST;

            core_mp->tItems[19].name = "st_bg";
            core_mp->tItems[19].cmd = protocol->st_bg;
            core_mp->tItems[19].do_test = st_test;
            core_mp->tItems[19].desp = "ST BG";
            core_mp->tItems[19].catalog = ST_TEST;

            core_mp->tItems[20].name = "st_no_bk";
            core_mp->tItems[20].cmd = protocol->st_no_bk;
            core_mp->tItems[20].do_test = st_test;
            core_mp->tItems[20].desp = "ST NO BK";
            core_mp->tItems[20].catalog = ST_TEST;

            core_mp->tItems[21].name = "st_has_bk";
            core_mp->tItems[21].cmd = protocol->st_has_bk;
            core_mp->tItems[21].do_test = st_test;
            core_mp->tItems[21].desp = "ST Has BK";
            core_mp->tItems[21].catalog = ST_TEST;

            core_mp->tItems[22].name = "st_open";
            core_mp->tItems[22].cmd = protocol->st_open;
            core_mp->tItems[22].do_test = st_test;
            core_mp->tItems[22].desp = "ST Open";
            core_mp->tItems[22].catalog = ST_TEST;

            core_mp->tItems[23].name = "tx_short";
            core_mp->tItems[23].cmd = protocol->tx_short;
            core_mp->tItems[23].do_test = mutual_test;
            core_mp->tItems[23].desp = "TX Short";
            core_mp->tItems[23].catalog = MUTUAL_TEST;

            core_mp->tItems[24].name = "rx_short";
            core_mp->tItems[24].cmd = protocol->rx_short;
            core_mp->tItems[24].do_test = mutual_test;
            core_mp->tItems[24].desp = "RX Short";
            core_mp->tItems[24].catalog = MUTUAL_TEST;

            core_mp->tItems[25].name = "rx_open";
            core_mp->tItems[25].cmd = protocol->rx_open;
            core_mp->tItems[25].do_test = mutual_test;
            core_mp->tItems[25].desp = "RX Open";
            core_mp->tItems[25].catalog = MUTUAL_TEST;

            core_mp->tItems[26].name = "cm_data";
            core_mp->tItems[26].cmd = protocol->cm_data;
            core_mp->tItems[26].do_test = mutual_test;
            core_mp->tItems[26].desp = "CM Data";
            core_mp->tItems[26].catalog = MUTUAL_TEST;

            core_mp->tItems[27].name = "cs_data";
            core_mp->tItems[27].cmd = protocol->cs_data;
            core_mp->tItems[27].do_test = mutual_test;
            core_mp->tItems[27].desp = "CS Data";
            core_mp->tItems[27].catalog = MUTUAL_TEST;

            core_mp->tItems[28].name = "tx_rx_delta";
            core_mp->tItems[28].cmd = protocol->tx_rx_delta;
            core_mp->tItems[28].do_test = tx_rx_delta_test;
            core_mp->tItems[28].desp = "Tx/Rx Delta Data";
            core_mp->tItems[28].catalog = TX_RX_DELTA;

            core_mp->tItems[29].name = "p2p";
            core_mp->tItems[29].cmd = protocol->mutual_signal;
            core_mp->tItems[29].do_test = untouch_p2p_test;
            core_mp->tItems[29].desp = "Untounch Peak to Peak";
            core_mp->tItems[29].catalog = UNTOUCH_P2P;
        }
    }
    else
    {
        DBG_ERR("Failed to get TP information");
		res = -EINVAL;
    }

out:
	return res;
}
EXPORT_SYMBOL(core_mp_init);

void core_mp_remove(void)
{
    DBG_INFO("Remove core-mp members");
    kfree(core_mp);
}
EXPORT_SYMBOL(core_mp_remove);
