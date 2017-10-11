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


struct description
{
	char *item;
	char *des;
};

struct description mp_des[] =
{
	{"mutual_dac", "Calibration Data(DAC,Mutual)"},
	{"mutual_bg",  "Baseline Data(BG)"},
	{"mutual_signal", "Signal Data(BG - RAW - 4096)"},
	{"mutual_no_bk", "Raw Data(No BK)"},
	{"mutual_has_bk", "Raw Data(Have BK"},
	{"mutual_bk_dac", "Manual BK Data(Mutual)"},
	
	{"self_dac", "Calibration Data(DAC,Self_Tx,Self_Rx)"},
	{"self_bg", "Baselin Data(BG,Self_Tx,Self_Rx)"},
	{"self_signal", "Signal Data(Self_Tx,Self_Rx,RAW -4096,Have BK)"},
	{"self_no_bk", "Raw Data(Self_Tx,Self_Rx, No BK)"},
	{"self_has_bk", "Raw Data(Self_Tx,Self_Rx,Have BK)"},
	{"self_bk_dac", "Manual BK DAC Data(Self_Tx,Self_Rx)"},

	{"key_dac", "Calibration Data(DAC,ICON)"},
	{"key_bg", "ICON Baselin Data(BG)"},
	{"key_no_bk", "ICON Raw Data"},
	{"key_has_bk", "ICON Raw Data(Have BK)"},
	{"key_open", "ICON Open Data"},
    {"key_short", "ICON Short Data"},
    
    {"tx_rx_delta", "Tx/Rx Delta Data"},
};

/* Convert raw data with the test item */
static int convert_key_cdc(uint8_t *buf);
static int convert_mutual_cdc(uint8_t *buf);
static int convert_txrx_delta_cdc(uint8_t *buf);

/* Handle its own test item */
static int mutual_test(uint8_t val, uint8_t p);
static int self_test(uint8_t val, uint8_t p);
static int key_test(uint8_t val, uint8_t p);
static int st_test(uint8_t val, uint8_t p);
static int tx_rx_delta_test(uint8_t val, uint8_t p);

static void cdc_print_result(const char *name, int result);
static void cdc_revert_status(void);

static int exec_cdc_command(bool write, uint8_t *item, int length, uint8_t *buf);

struct core_mp_test_data *core_mp = NULL;

static void cdc_print_result(const char *name, int result)
{
	int i, x, y;

	for(i = 0; i < ARRAY_SIZE(mp_des); i++)
	{
		if(strcmp(mp_des[i].item, name) == 0)
		{
            DBG_INFO("==============================");	
			DBG_INFO(" %s : %s ", mp_des[i].des, (result != 0 ? "FAIL" : "PASS"));
            DBG_INFO("==============================");	
            if(core_mp->mutual_test)
            {
                for(y = 0; y < core_mp->ych_len; y++)
                {
                    for(x = 0; x < core_mp->xch_len; x++)
                    {
                        if(core_mp->m_dac)
                        {
                            printk(" %d ",core_mp->m_raw_buf[x+y]);
                        }
                        else
                        {
                            if(core_mp->m_signal)
                            {
                                printk(" %d ",core_mp->m_sin_buf[x+y]);
                            }
                            else
                            {
                                printk(" %d",core_mp->m_raw_buf[x+y]);
                            }
                        }
                    }
                    printk("\n");
                }
            }
            else if(core_mp->self_test)
            {
                for(y = 0; y < core_mp->stx_len; y++)
                {
                    for(x = 0; x < core_mp->xch_len; x++)
                    {
                        if(core_mp->s_dac)
                        {
                            printk(" %d ",core_mp->s_raw_buf[x+y]);
                        }
                        else
                        {
                            if(core_mp->s_signal)
                            {
                                printk(" %d ",core_mp->s_sin_buf[x+y]);
                            }
                            else
                            {
                                printk(" %d ",core_mp->s_raw_buf[x+y]);
                            }
                        }
                    }
                    printk("\n");
                }                
            }
            else if(core_mp->key_test)
            {
                for(x = 0; x < core_mp->key_len; x++)
                {
                    printk("KEY_%d   ",x);                      
                }
                printk("\n");
                for(y = 0; y < core_mp->key_len; y++)
                {
                    printk("%d    ",core_mp->key_raw_buf[y]);                        
                }
                printk("\n");
            }
            else if(core_mp->tx_rx_delta_test)
            {
                for(y = 0; y < core_mp->ych_len; y++)
                {
                    for(x = 0; x < core_mp->xch_len; x++)
                    {
                        /* Threshold with RX delta */
                        if(core_mp->rx_delta_buf[x+y] <= core_mp->RxDeltaMax &&
                            core_mp->rx_delta_buf[x+y] >= core_mp->RxDeltaMin)
                        {
                            printk(" %d ",core_mp->rx_delta_buf[x+y]); 
                        }
                        else
                        {
                            if(core_mp->rx_delta_buf[x+y] > core_mp->RxDeltaMax)
                            {
                                printk(" *%d ",core_mp->rx_delta_buf[x+y]);
                            }
                            else
                            {
                                printk(" #%d ",core_mp->rx_delta_buf[x+y]);
                            }
                        }

                        /* Threshold with TX delta */
                        if(core_mp->tx_delta_buf[x+y] <= core_mp->TxDeltaMax &&
                            core_mp->tx_delta_buf[x+y] >= core_mp->TxDeltaMin)
                        {
                            printk(" %d ",core_mp->tx_delta_buf[x+y]); 
                        }
                        else
                        {
                            if(core_mp->tx_delta_buf[x+y] > core_mp->TxDeltaMax)
                            {
                                printk(" *%d ",core_mp->tx_delta_buf[x+y]);
                            }
                            else
                            {
                                printk(" #%d ",core_mp->tx_delta_buf[x+y]);
                            }
                        }
                    }
                    printk("\n");
                }
            }
            break;
        }
	}
}

static void cdc_revert_status(void)
{
    core_mp->mutual_test = false;
	core_mp->self_test = false;
	core_mp->key_test = false;
    core_mp->st_test = false;
    core_mp->tx_rx_delta_test = false;
    
	core_mp->m_signal = false;
	core_mp->m_dac = false;
	core_mp->s_signal = false;
	core_mp->s_dac = false;
	core_mp->key_dac = false;
	core_mp->st_dac = false;

    kfree(core_mp->m_raw_buf);
    core_mp->m_raw_buf = NULL;
    kfree(core_mp->m_sin_buf);
    core_mp->m_sin_buf = NULL;
    kfree(core_mp->key_raw_buf);
    core_mp->key_raw_buf = NULL;
    kfree(core_mp->s_raw_buf);
    core_mp->s_raw_buf = NULL;
    kfree(core_mp->s_sin_buf);
    core_mp->s_sin_buf = NULL;
    kfree(core_mp->tx_delta_buf);
    core_mp->tx_delta_buf = NULL;
    kfree(core_mp->rx_delta_buf);
    core_mp->rx_delta_buf = NULL;
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

static int convert_txrx_delta_cdc(uint8_t *buf)
{
    int i,  x, y, res = 0;
    int FrameCount = 1;
    uint8_t *ori = buf;

    if(buf == NULL)
    {
        DBG_ERR("The data in buffer is null");
        res = -ENOMEM;
        goto out;
    }

	core_mp->tx_delta_buf = kzalloc(core_mp->xch_len * core_mp->xch_len * sizeof(int32_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(core_mp->tx_delta_buf))
	{
		DBG_ERR("Failed to allocate raw buffer, %ld", PTR_ERR(core_mp->tx_delta_buf));
		res = -ENOMEM;
		goto out;
    }

	core_mp->rx_delta_buf = kzalloc(core_mp->xch_len * core_mp->xch_len * sizeof(int32_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(core_mp->rx_delta_buf))
	{
		DBG_ERR("Failed to allocate raw buffer, %ld", PTR_ERR(core_mp->rx_delta_buf));
		res = -ENOMEM;
		goto out;
    }    

    DBG_INFO("core_mp->tx_delta_buf = %p", core_mp->tx_delta_buf);
    DBG_INFO("core_mp->rx_delta_buf = %p", core_mp->rx_delta_buf);

    for(i = 0; i < FrameCount; i++)
    {
        for(y = 0; y < core_mp->ych_len; y++)
        {
            for(x = 0; x < core_mp->xch_len; x++)
            {
                /* Rx Delta */
                if(x != (core_mp->xch_len - 1))
                {
                    core_mp->rx_delta_buf[x+y] = Mathabs(ori[x+y] - ori[(x+1)+y]);
                }

                /* Tx Delta */
                if(y != (core_mp->ych_len - 1))
                {
                    core_mp->tx_delta_buf[x+y] = Mathabs(ori[x+y] - ori[x+(y+1)]);
                }
            }
        }
    }

out:
    return res;
}

static int convert_key_cdc(uint8_t *buf)
{
    int i, res = 0;
    int inDACp = 0, inDACn = 0;
    int FrameCount = 0;
    uint8_t *ori = buf;

    if(buf == NULL)
    {
        DBG_ERR("The data in buffer is null");
        res = -ENOMEM;
        goto out;
    }

    FrameCount = core_mp->key_len;

	core_mp->key_raw_buf = kzalloc(FrameCount * sizeof(int32_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(core_mp->key_raw_buf))
	{
		DBG_ERR("Failed to allocate raw buffer, %ld", PTR_ERR(core_mp->key_raw_buf));
		res = -ENOMEM;
		goto out;
    }
    
    DBG_INFO("FrameCount = %d, key_dac = %d",FrameCount, core_mp->key_dac);
    DBG_INFO("core_mp->key_raw_buf = %p", core_mp->key_raw_buf);   

    for(i = 0; i < FrameCount; i++)
    {
		if(core_mp->key_dac)
		{
            /* DAC - P */
            if(((ori[2 * i] & 0x80) >> 7) == 1)
            {
                /* Negative */
                inDACp = 0 - (int)(ori[2 * i] & 0x7F); 
            }
            else
            {
                inDACp = ori[2 * i] & 0x7F;
            }

            /* DAC - N */
            if(((ori[1 + (2 * i)] & 0x80) >> 7) == 1)
            {
                /* Negative */
                inDACn = 0 - (int)(ori[1 + (2 * i)] & 0x7F);
            }
            else
            {
                inDACn = ori[1 + (2 * i)] & 0x7F;
            }

			core_mp->key_raw_buf[i] = (inDACp + inDACn) / 2;
		}
    }

out:
	return res;
}

static int convert_mutual_cdc(uint8_t *buf)
{
    int i, res = 0;
    int inDACp = 0, inDACn = 0;
    int inCountX = 0, inCountY = 0;
    int FrameCount = 0;
    uint8_t *ori = buf;

    if(buf == NULL)
    {
        DBG_ERR("The data in buffer is null");
        res = -ENOMEM;
        goto out;
    }

    FrameCount = core_mp->xch_len * core_mp->ych_len;

	core_mp->m_raw_buf = kzalloc(FrameCount * sizeof(int32_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(core_mp->m_raw_buf))
	{
		DBG_ERR("Failed to allocate raw buffer, %ld", PTR_ERR(core_mp->m_raw_buf));
		res = -ENOMEM;
		goto out;
    }
    
	if(core_mp->m_signal)
	{
		core_mp->m_sin_buf = kzalloc(FrameCount * sizeof(int32_t), GFP_KERNEL);
		if (ERR_ALLOC_MEM(core_mp->m_sin_buf))
		{
			DBG_ERR("Failed to allocate signal buffer, %ld", PTR_ERR(core_mp->m_sin_buf));
			res = -ENOMEM;
			goto out;
        }
	}

    DBG_INFO("FrameCount = %d, DAC = %d, Signal = %d",
        FrameCount, core_mp->m_dac, core_mp->m_signal);
    
    DBG_INFO("core_mp->m_raw_buf = %p", core_mp->m_raw_buf);
    DBG_INFO("core_mp->m_sin_buf = %p", core_mp->m_sin_buf);

    /* Start to converting data */
    for(i = 0; i < FrameCount; i++)
    {
        if(core_mp->m_dac)
        {
            /* DAC - P */
            if(((ori[2 * i] & 0x80) >> 7) == 1)
            {
                /* Negative */
                inDACp = 0 - (int)(ori[2 * i] & 0x7F); 
            }
            else
            {
                inDACp = ori[2 * i] & 0x7F;
            }

            /* DAC - N */
            if(((ori[1 + (2 * i)] & 0x80) >> 7) == 1)
            {
                /* Negative */
                inDACn = 0 - (int)(ori[1 + (2 * i)] & 0x7F);
            }
            else
            {
                inDACn = ori[1 + (2 * i)] & 0x7F;
            }

            core_mp->m_raw_buf[i] = (inDACp + inDACn) / 2;
        }
        else
        {
            /* H byte + L byte */
            core_mp->m_raw_buf[i] = (ori[2 * i] << 8) +  ori[1 + (2 * i)];

            if(core_mp->m_signal)
            {
				if((core_mp->m_raw_buf[i] * 0x8000) == 0x8000)
				{
					core_mp->m_sin_buf[i] = core_mp->m_raw_buf[i] - 65536;

				}
				else
				{
					core_mp->m_sin_buf[i] = core_mp->m_raw_buf[i];
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

out:
    return res;
}

static int mutual_test(uint8_t val, uint8_t p)
{
    int res = 0;
    int len = 0;
    uint8_t cmd[2] = {0};
    uint8_t *mutual = NULL;

    cmd[0] = p;
    cmd[1] = val;

    /* update X/Y channel length if they're changed */
	core_mp->xch_len = core_config->tp_info->nXChannelNum;
	core_mp->ych_len = core_config->tp_info->nYChannelNum;
    
    len = core_mp->xch_len * core_mp->ych_len * 2;

    DBG_INFO("Read X/Y Channel length = %d", len);

    /* set flag */
    core_mp->mutual_test = true;
    
    if(cmd[0] == protocol->mutual_signal)
        core_mp->m_signal = true;
    
    if(cmd[0] == protocol->mutual_dac)
        core_mp->m_dac = true;

    res = exec_cdc_command(EXEC_WRITE, cmd, 2, NULL);
    if(res < 0)
    {
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
            
        DBG_INFO("mutual = %p", mutual);

        res = convert_mutual_cdc(mutual);
		if(res < 0)
			goto out;
    }

out:
    kfree(mutual);
    return res;    
}

static int self_test(uint8_t val, uint8_t p)
{
    int res = 0;
    int len = 0;
    uint8_t cmd[2] = {0};
    uint8_t *self = NULL;

    cmd[0] = p;
    cmd[1] = val;

    /* update self tx/rx length if they're changed */
    core_mp->stx_len = core_config->tp_info->self_tx_channel_num;
    core_mp->srx_len = core_config->tp_info->self_rx_channel_num;

    len = core_mp->stx_len * core_mp->srx_len * 2;

    /* set flag */
    core_mp->self_test = true;

    DBG_INFO("Read self Tx/Rx length = %d", len);
    
    if(cmd[0] == protocol->self_signal)
        core_mp->s_signal = true;
    
    if(cmd[0] == protocol->self_dac)
        core_mp->s_dac = true;

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

        //res = convert_mutual_cdc(self);
		//if(res < 0)
		//	goto out;
    }

out:
    kfree(self);
    return res;
}

static int key_test(uint8_t val, uint8_t p)
{
    int res = 0;
    int len = 0;
    uint8_t cmd[2] = {0};
    uint8_t *icon = NULL;

    cmd[0] = p;
    cmd[1] = val;

    /* update key's length if they're changed */
    core_mp->key_len = core_config->tp_info->nKeyCount;
    len = core_mp->key_len * 2;

    /* set flag */
    core_mp->key_test = true;

    DBG_INFO("Read key's length = %d", len);
    
    if(cmd[0] == protocol->key_dac)
        core_mp->key_dac = true;

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
          
        res = convert_key_cdc(icon);
		if(res < 0)
			goto out;
    }

out:
    kfree(icon);
    return res;    
}

static int st_test(uint8_t val, uint8_t p)
{
    int res = 0;
    int len = 0;
    uint8_t cmd[2] = {0};
    uint8_t *st = NULL;

    cmd[0] = p;
    cmd[1] = val;

    /* update side touch's length it they're changed */
    core_mp->st_len = core_config->tp_info->side_touch_type;
    len = core_mp->st_len * 2;

    /* set flag */
    core_mp->st_test = true;

    DBG_INFO("Read st's length = %d", len);
    
    if(cmd[0] == protocol->st_dac)
        core_mp->st_dac = true;

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

        res = convert_key_cdc(st);
		if(res < 0)
			goto out;
    }

out:
    kfree(st);
    return res;  
}

static int tx_rx_delta_test(uint8_t val, uint8_t p)
{
    int res = 0;
    int len = 0;
    uint8_t cmd[2] = {0};
    uint8_t *delta = NULL;

    cmd[0] = p;
    cmd[1] = val;

    /* update X/Y channel length if they're changed */
	core_mp->xch_len = core_config->tp_info->nXChannelNum;
    core_mp->ych_len = core_config->tp_info->nYChannelNum;
    
    len = core_mp->xch_len * core_mp->ych_len;

    /* set flag */
    core_mp->tx_rx_delta_test = true;

    DBG_INFO("Read Tx/Rx delta length = %d", len);
    
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

        res = convert_txrx_delta_cdc(delta);
		if(res < 0)
			goto out;
    }

out:
    kfree(delta);
    return res; 
}

int core_mp_run_test(const char *name, uint8_t val)
{
	int i = 0, res = 0;

	DBG_INFO("Test name = %s, size = %d", name, (int)ARRAY_SIZE(core_mp->tItems));

	for(i = 0; i < ARRAY_SIZE(core_mp->tItems); i++)
	{
        if(strcmp(name, core_mp->tItems[i].name) == 0)
		{
			res = core_mp->tItems[i].do_test(val, core_mp->tItems[i].cmd);
            cdc_print_result(core_mp->tItems[i].name, res);         
			cdc_revert_status();
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

			/* Initialize MP test functions with its own command from protocol.c */
			memset(core_mp->tItems, 0x0, sizeof(ARRAY_SIZE(core_mp->tItems)));

            core_mp->tItems[0].name = "mutual_dac";
            core_mp->tItems[0].cmd = protocol->mutual_dac;
            core_mp->tItems[0].do_test = mutual_test;

            core_mp->tItems[1].name = "mutual_bg";
            core_mp->tItems[1].cmd = protocol->mutual_bg;
            core_mp->tItems[1].do_test = mutual_test;

            core_mp->tItems[2].name = "mutual_signal";
            core_mp->tItems[2].cmd = protocol->mutual_signal;
            core_mp->tItems[2].do_test = mutual_test;

            core_mp->tItems[3].name = "mutual_no_bk";
            core_mp->tItems[3].cmd = protocol->mutual_no_bk;
            core_mp->tItems[3].do_test = mutual_test;

            core_mp->tItems[4].name = "mutual_has_bk";
            core_mp->tItems[4].cmd = protocol->mutual_has_bk;
            core_mp->tItems[4].do_test = mutual_test;

            core_mp->tItems[5].name = "mutual_bk_dac";
            core_mp->tItems[5].cmd = protocol->mutual_bk_dac;
            core_mp->tItems[5].do_test = mutual_test;
			
            core_mp->tItems[6].name = "self_dac";
            core_mp->tItems[6].cmd = protocol->self_dac;
            core_mp->tItems[6].do_test = self_test;
			
            core_mp->tItems[7].name = "self_bg";
            core_mp->tItems[7].cmd = protocol->self_bg;
            core_mp->tItems[7].do_test = self_test;

            core_mp->tItems[8].name = "self_signal";
            core_mp->tItems[8].cmd = protocol->self_signal;
            core_mp->tItems[8].do_test = self_test;

            core_mp->tItems[9].name = "self_no_bk";
            core_mp->tItems[9].cmd = protocol->self_no_bk;
            core_mp->tItems[9].do_test = self_test;

            core_mp->tItems[10].name = "self_has_bk";
            core_mp->tItems[10].cmd = protocol->self_has_bk;
            core_mp->tItems[10].do_test = self_test;

            core_mp->tItems[11].name = "self_bk_dac";
            core_mp->tItems[11].cmd = protocol->self_bk_dac;
            core_mp->tItems[11].do_test = self_test;

            core_mp->tItems[12].name = "key_dac";
            core_mp->tItems[12].cmd = protocol->key_dac;
            core_mp->tItems[12].do_test = key_test;

            core_mp->tItems[13].name = "key_bg";
            core_mp->tItems[13].cmd = protocol->key_bg;
            core_mp->tItems[13].do_test = key_test;

            core_mp->tItems[14].name = "key_no_bk";
            core_mp->tItems[14].cmd = protocol->key_no_bk;
            core_mp->tItems[14].do_test = key_test;

            core_mp->tItems[15].name = "key_has_bk";
            core_mp->tItems[15].cmd = protocol->key_has_bk;
            core_mp->tItems[15].do_test = key_test;

            core_mp->tItems[16].name = "key_open";
            core_mp->tItems[16].cmd = protocol->key_open;
            core_mp->tItems[16].do_test = key_test;

            core_mp->tItems[17].name = "key_short";
            core_mp->tItems[17].cmd = protocol->key_short;
            core_mp->tItems[17].do_test = key_test;

            core_mp->tItems[18].name = "st_dac";
            core_mp->tItems[18].cmd = protocol->st_dac;
            core_mp->tItems[18].do_test = st_test;

            core_mp->tItems[19].name = "st_bg";
            core_mp->tItems[19].cmd = protocol->st_bg;
            core_mp->tItems[19].do_test = st_test;

            core_mp->tItems[20].name = "st_no_bk";
            core_mp->tItems[20].cmd = protocol->st_no_bk;
            core_mp->tItems[20].do_test = st_test;

            core_mp->tItems[21].name = "st_has_bk";
            core_mp->tItems[21].cmd = protocol->st_has_bk;
            core_mp->tItems[21].do_test = st_test;

            core_mp->tItems[22].name = "st_open";
            core_mp->tItems[22].cmd = protocol->st_open;
            core_mp->tItems[22].do_test = st_test;

            core_mp->tItems[23].name = "tx_short";
            core_mp->tItems[23].cmd = protocol->tx_short;
            core_mp->tItems[23].do_test = mutual_test;

            core_mp->tItems[24].name = "rx_short";
            core_mp->tItems[24].cmd = protocol->rx_short;
            core_mp->tItems[24].do_test = mutual_test;

            core_mp->tItems[25].name = "rx_open";
            core_mp->tItems[25].cmd = protocol->rx_open;
            core_mp->tItems[25].do_test = mutual_test;

            core_mp->tItems[26].name = "cm_data";
            core_mp->tItems[26].cmd = protocol->cm_data;
            core_mp->tItems[26].do_test = mutual_test;

            core_mp->tItems[27].name = "cs_data";
            core_mp->tItems[27].cmd = protocol->cs_data;
            core_mp->tItems[27].do_test = mutual_test;

            core_mp->tItems[28].name = "tx_rx_delta";
            core_mp->tItems[28].cmd = protocol->tx_rx_delta;
            core_mp->tItems[28].do_test = tx_rx_delta_test;
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
