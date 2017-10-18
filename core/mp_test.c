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

#define DUMP(level, fmt, arg...) \
do { \
    if (level & ipio_debug_level) \
        printk( fmt, ##arg); \
} while (0)

#define CSV_PATH    "/sdcard/ilitek_mp_test.csv"

enum mp_test_catalog
{
    MUTUAL_TEST = 0,
    SELF_TEST = 1,
    KEY_TEST = 2,
    ST_TEST = 3,
    TX_RX_DELTA = 4,
    UNTOUCH_P2P = 5,
    PIXEL = 6,
    OPEN_TEST = 7,
};

struct mp_test_items
{
    char *name;
    char *desp;
    char *result;
    int catalog;
    uint8_t cmd;
    bool run;    
    int32_t* buf;
	int (*do_test)(int, uint8_t);
} tItems[] = {
    {"mutual_dac", "Calibration Data(DAC/Mutual)", "false", MUTUAL_TEST, false, NULL, NULL},
    {"mutual_bg", "Baseline Data(BG)", "false", MUTUAL_TEST, false, NULL, NULL},
    {"mutual_signal", "Signal Data(BG - RAW - 4096)", "false", MUTUAL_TEST, false, NULL, NULL},
    {"mutual_no_bk", "Raw Data(No BK)", "false", MUTUAL_TEST, false, NULL, NULL},
    {"mutual_has_bk", "Raw Data(Have BK)", "false", MUTUAL_TEST, false, NULL, NULL},
    {"mutual_bk_dac", "Manual BK Data(Mutual)", "false", MUTUAL_TEST, false, NULL, NULL},
    
    {"self_dac", "Calibration Data(DAC/Self_Tx/Self_Rx)", "false", SELF_TEST, false, NULL, NULL},
    {"self_bg", "Baselin Data(BG,Self_Tx,Self_Rx)", "false", SELF_TEST, false, NULL, NULL},
    {"self_signal", "Signal Data(Self_Tx,Self_Rx/RAW -4096/Have BK)", "false", SELF_TEST, false, NULL, NULL},
    {"self_no_bk", "Raw Data(Self_Tx/Self_Rx/No BK)", "false", SELF_TEST, false, NULL, NULL},
    {"self_has_bk", "Raw Data(Self_Tx/Self_Rx/Have BK)", "false", SELF_TEST, false, NULL, NULL},
    {"self_bk_dac", "Manual BK DAC Data(Self_Tx,Self_Rx)", "false", SELF_TEST, false, NULL, NULL},

    {"key_dac", "Calibration Data(DAC/ICON)", "false", KEY_TEST, false, NULL, NULL},
    {"key_bg", "Baselin Data(BG,Self_Tx,Self_Rx)", "false", KEY_TEST, false, NULL, NULL},
    {"key_no_bk", "ICON Raw Data", "false", KEY_TEST, false, NULL, NULL},
    {"key_has_bk", "ICON Raw Data(Have BK)", "false", KEY_TEST, false, NULL, NULL},
    {"key_open", "ICON Open Data", "false", KEY_TEST, false, NULL, NULL},
    {"key_short", "ICON Short Data", "false", KEY_TEST, false, NULL, NULL},

    {"st_dac", "ST DAC", "false", ST_TEST, false, NULL, NULL},
    {"st_bg", "ST BG", "false", ST_TEST, false, NULL, NULL},
    {"st_no_bk", "ST NO BK", "false", ST_TEST, false, NULL, NULL},
    {"st_has_bk", "ST Has BK", "false", ST_TEST, false, NULL, NULL},
    {"st_open", "ST Open", "false", ST_TEST, false, NULL, NULL},

    {"tx_short", "TX Short", "false", MUTUAL_TEST, false, NULL, NULL},
    {"rx_short", "RX Short", "false", MUTUAL_TEST, false, NULL, NULL},
    {"rx_open", "RX Open", "false", MUTUAL_TEST, false, NULL, NULL},

    {"cm_data", "CM Data", "false", MUTUAL_TEST, false, NULL, NULL},
    {"cs_data", "CS Data", "false", MUTUAL_TEST, false, NULL, NULL},

    {"tx_rx_delta", "Tx/Rx Delta Data", "false", TX_RX_DELTA, false, NULL, NULL},

    {"p2p", "Untounch Peak to Peak", "false", UNTOUCH_P2P, false, NULL, NULL},

    {"pixel_no_bk", "Pixel No BK", "false", PIXEL, false, NULL, NULL},
    {"pixel_has_bk", "Pixel Has BK", "false", PIXEL, false, NULL, NULL},

    {"open_integration", "Open Test Integration", "false", OPEN_TEST, false, NULL, NULL},
    {"open_cap", "Open Test Cap", "false", OPEN_TEST, false, NULL, NULL},
};

/* Tx/Rx Delta outside buffer */
int32_t *tx_delta_buf = NULL;
int32_t *rx_delta_buf = NULL;

/* Handle its own test item */
static int mutual_test(int index, uint8_t val);
static int self_test(int index, uint8_t val);
static int key_test(int index, uint8_t val);
static int st_test(int index, uint8_t val);
static int tx_rx_delta_test(int index, uint8_t val);
static int untouch_p2p_test(int index, uint8_t val);
static int pixel_test(int index, uint8_t val);
static int open_test(int index, uint8_t val);

static void mp_test_free(void);

/* Read from and write into data via I2c */
static int exec_cdc_command(bool write, uint8_t *item, int length, uint8_t *buf);

struct core_mp_test_data *core_mp = NULL;

static void mp_test_init_item(void)
{
    int i;

    /* assign test functions run on MP flow according to their catalog */
    for(i = 0; i < ARRAY_SIZE(tItems); i++)
    {
        if(tItems[i].catalog == MUTUAL_TEST)
            tItems[i].do_test = mutual_test;

        if(tItems[i].catalog == SELF_TEST)
            tItems[i].do_test = self_test;
         
        if(tItems[i].catalog == KEY_TEST)
            tItems[i].do_test = key_test;
         
        if(tItems[i].catalog == ST_TEST)
            tItems[i].do_test = st_test;
        
        if(tItems[i].catalog == TX_RX_DELTA)
           tItems[i].do_test = tx_rx_delta_test;
      
        if(tItems[i].catalog == UNTOUCH_P2P)
            tItems[i].do_test = untouch_p2p_test;
        
        if(tItems[i].catalog == PIXEL)
            tItems[i].do_test = pixel_test;
        
        if(tItems[i].catalog == OPEN_TEST)
            tItems[i].do_test = open_test;
    }

    /* assign protocol command written into firmware via I2C,
    which might be differnet if the version of protocol was changed. */
    tItems[0].cmd = protocol->mutual_dac;
    tItems[1].cmd = protocol->mutual_bg;
    tItems[2].cmd = protocol->mutual_signal;
    tItems[3].cmd = protocol->mutual_no_bk;
    tItems[4].cmd = protocol->mutual_has_bk;
    tItems[5].cmd = protocol->mutual_bk_dac;
    tItems[6].cmd = protocol->self_dac;
    tItems[7].cmd = protocol->self_bg;
    tItems[8].cmd = protocol->self_signal;
    tItems[9].cmd = protocol->self_no_bk;
    tItems[10].cmd = protocol->self_has_bk;
    tItems[11].cmd = protocol->self_bk_dac;
    tItems[12].cmd = protocol->key_dac;
    tItems[13].cmd = protocol->key_bg;
    tItems[14].cmd = protocol->key_no_bk;
    tItems[15].cmd = protocol->key_has_bk;
    tItems[16].cmd = protocol->key_open;
    tItems[17].cmd = protocol->key_short;
    tItems[18].cmd = protocol->st_dac;
    tItems[19].cmd = protocol->st_bg;
    tItems[20].cmd = protocol->st_no_bk;
    tItems[21].cmd = protocol->st_has_bk;
    tItems[22].cmd = protocol->st_open;
    tItems[23].cmd = protocol->tx_short;
    tItems[24].cmd = protocol->rx_short;
    tItems[25].cmd = protocol->rx_open;
    tItems[26].cmd = protocol->cm_data;
    tItems[27].cmd = protocol->cs_data;
    tItems[28].cmd = protocol->tx_rx_delta;
    tItems[29].cmd = protocol->mutual_signal;
    tItems[30].cmd = protocol->mutual_no_bk;
    tItems[31].cmd = protocol->mutual_has_bk;
    tItems[32].cmd = protocol->rx_open;
    tItems[33].cmd = protocol->rx_open;
}

static void dump_data(void *data, int type)
{
    int x, y;
    uint8_t *p8 = NULL;
    int32_t *p32 = NULL;

    if(type == 8)
    p8 = (uint8_t *)data;
    if(type == 32)
        p32 = (int32_t *)data;

    for(x = 0; x < core_mp->xch_len; x++)
    {
        if(x == 0)
            DUMP(DEBUG_MP_TEST, "           ");

        DUMP(DEBUG_MP_TEST,"X%02d      ", x);
    }

    DUMP(DEBUG_MP_TEST,"\n");

    for(y = 0; y < core_mp->ych_len; y++)
    {
        DUMP(DEBUG_MP_TEST," Y%02d ", y);
        for(x = 0; x < core_mp->xch_len; x++)
        {
            if(type == 8)
                DUMP(DEBUG_MP_TEST," %7d ", p8[x+y]);
            if(type == 32)
                DUMP(DEBUG_MP_TEST," %7d ", p32[x+y]);
        }
        DUMP(DEBUG_MP_TEST,"\n");
    }
    DUMP(DEBUG_MP_TEST,"\n\n\n");
}

static void mp_test_free(void)
{
    int i;
     
    for(i = 0; i < ARRAY_SIZE(tItems); i++)
    {
        tItems[i].run = false;

        if(tItems[i].buf != NULL)
        {
            if(tItems[i].catalog == TX_RX_DELTA)
            {
                kfree(rx_delta_buf);
                rx_delta_buf = NULL;
                kfree(tx_delta_buf);
                tx_delta_buf = NULL;
            }
            else
            {
                kfree(tItems[i].buf);
                tItems[i].buf = NULL;
            }
        }
    }
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
		DBG(DEBUG_MP_TEST,"cmd[0] = 0x%x, cmd[1] = 0x%x, cmd[2] = 0x%x\n",cmd[0],cmd[1],cmd[2]);
        res = core_i2c_write(core_config->slave_i2c_addr, cmd, 1 + length);
        if(res < 0)
            goto out;
    }
    else
    {
        if(ERR_ALLOC_MEM(buf))
        {
            DBG_ERR("Invalid buffer\n");
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

static int pixel_test(int index, uint8_t val)
{
    int i, x, y, len = 0, res = 0;
    int tmp[4] = {0}, max = 0;
    uint8_t cmd[2] = {0};
    uint8_t *pixel = NULL;

    cmd[0] = tItems[index].cmd;
    cmd[1] = val;

    /* update X/Y channel length if they're changed */
	core_mp->xch_len = core_config->tp_info->nXChannelNum;
    core_mp->ych_len = core_config->tp_info->nYChannelNum;
    
    len = core_mp->xch_len * core_mp->ych_len;

    DBG(DEBUG_MP_TEST,"Read Mutual X/Y Channel length = %d\n", len);

    if(cmd[0] == protocol->mutual_no_bk)
        core_mp->p_no_bk = true;
    
    if(cmd[0] == protocol->mutual_has_bk)
        core_mp->p_has_bk = true; 

    if(tItems[index].buf == NULL)
    {
        /* Create a buffer that belogs to itself */
        tItems[index].buf = kzalloc(len * sizeof(int32_t), GFP_KERNEL);
    }
    else
    {
        /* erase data if this buffrer was not cleaned and freed */
        memset(tItems[index].buf, 0x0, len * sizeof(int32_t));
    }
    
    /* sending command to get raw data, converting it and comparing it with threshold */
    res = exec_cdc_command(EXEC_WRITE, cmd, 2, NULL);
    if(res < 0)
    {
        DBG_ERR("I2C error\n");
        goto out;
    }
    else
    {
        pixel = kzalloc(len, GFP_KERNEL);
        if(ERR_ALLOC_MEM(pixel))
        {
            res = FAIL;
            goto out;
        }

        res = exec_cdc_command(EXEC_READ, 0, len, pixel);
		if(res < 0)
            goto out;

        dump_data(pixel, 8);
                   
        /* Start to converting data */
        for(y = 0; y < core_mp->ych_len; y++)
        {
            for(x = 0; x < core_mp->xch_len; x++)
            {
                /* if its position is in corner, the number of point 
                    we have to minus is around 2 to 3.  */
                if(y == 0 && x == 0)
                {
                    tmp[0] = Mathabs(pixel[x+y] - pixel[x+(y+1)]); // down
                    tmp[1] = Mathabs(pixel[x+y] - pixel[(x+1)+y]); // right
                }
                else if(y == (core_mp->ych_len - 1) && x == 0)
                {
                    tmp[0] = Mathabs(pixel[x+y] - pixel[x+(y-1)]); // up
                    tmp[1] = Mathabs(pixel[x+y] - pixel[(x+1)+y]); // right
                }
                else if(y == 0 && x == (core_mp->xch_len - 1))
                {
                    tmp[0] = Mathabs(pixel[x+y] - pixel[x+(y+1)]); // down
                    tmp[1] = Mathabs(pixel[x+y] - pixel[(x-1)+y]); // left
                }
                else if(y == (core_mp->ych_len - 1) && x == (core_mp->xch_len - 1) )
                {
                    tmp[0] = Mathabs(pixel[x+y] - pixel[x+(y-1)]); // up
                    tmp[1] = Mathabs(pixel[x+y] - pixel[(x-1)+y]); // left
                }
                else if (y == 0 && x != 0)
                {
                    tmp[0] = Mathabs(pixel[x+y] - pixel[x+(y+1)]); // down
                    tmp[1] = Mathabs(pixel[x+y] - pixel[(x-1)+y]); // left
                    tmp[2] = Mathabs(pixel[x+y] - pixel[(x+1)+y]); // right
                }
                else if (y != 0 && x == 0)
                {
                    tmp[0] = Mathabs(pixel[x+y] - pixel[x+(y-1)]); // up
                    tmp[1] = Mathabs(pixel[x+y] - pixel[(x+1)+y]); // right
                    tmp[2] = Mathabs(pixel[x+y] - pixel[x+(y+1)]); // down

                }
                else if(y == (core_mp->ych_len - 1) && x != 0 )
                {
                    tmp[0] = Mathabs(pixel[x+y] - pixel[x+(y-1)]); // up
                    tmp[1] = Mathabs(pixel[x+y] - pixel[(x-1)+y]); // left
                    tmp[2] = Mathabs(pixel[x+y] - pixel[(x+1)+y]); // right
                }
                else if(y != 0 && x == (core_mp->xch_len - 1) )
                {
                    tmp[0] = Mathabs(pixel[x+y] - pixel[x+(y-1)]); // up
                    tmp[1] = Mathabs(pixel[x+y] - pixel[(x-1)+y]); // left
                    tmp[2] = Mathabs(pixel[x+y] - pixel[x+(y+1)]); // down
                }
                else
                {
                    /* middle minus four directions */
                    tmp[0] = Mathabs(pixel[x+y] - pixel[x+(y-1)]); // up
                    tmp[1] = Mathabs(pixel[x+y] - pixel[x+(y+1)]); // down
                    tmp[2] = Mathabs(pixel[x+y] - pixel[(x-1)+y]); // left
                    tmp[3] = Mathabs(pixel[x+y] - pixel[(x+1)+y]); // right
                }

                max = tmp[0];

                for(i = 0; i < 4; i++)
                {
                    if(tmp[i] > max)
                        max = tmp[i];
                }

                tItems[index].buf[x+y] = max;
                max = 0;
                memset(tmp, 0, 4 * sizeof(int));
            }
        }
    }

out:
    kfree(pixel);
    core_mp->p_no_bk = false;
    core_mp->p_has_bk = false;
    tItems[index].result = (res != 0 ? "FAIL" : "PASS");   
    return res;   
}

static int open_test(int index, uint8_t val)
{
    int i, x, y, res = 0, len = 0;
    uint8_t cmd[2] = {0};
    uint8_t *open = NULL;
    int32_t *raw = NULL;

    cmd[0] = tItems[index].cmd;

    if( strcmp(tItems[index].name, "open_integration") == 0)
        cmd[1] = 0x2;
    if(strcmp(tItems[index].name, "open_cap") == 0)
        cmd[1] = 0x3;

    /* update X/Y channel length if they're changed */
	core_mp->xch_len = core_config->tp_info->nXChannelNum;
    core_mp->ych_len = core_config->tp_info->nYChannelNum;
    
    len = core_mp->xch_len * core_mp->ych_len;

    DBG(DEBUG_MP_TEST,"Read X/Y Channel length = %d\n", len);

    /* raw buffer is used to receive data from firmware */
    raw = kzalloc(len * sizeof(int32_t), GFP_KERNEL);
    if (ERR_ALLOC_MEM(raw))
    {
        DBG_ERR("Failed to allocate raw buffer, %ld\n", PTR_ERR(raw));
        res = -ENOMEM;
        goto out;
    }

    if(tItems[index].buf == NULL)
    {
        /* Create a buffer that belogs to itself */
        tItems[index].buf = kzalloc(len * sizeof(int32_t), GFP_KERNEL);
    }
    else
    {
        /* erase data if this buffrer was not cleaned and freed */
        memset(tItems[index].buf, 0x0, len * sizeof(int32_t));
    }

    /* sending command to get raw data, converting it and comparing it with threshold */
    res = exec_cdc_command(EXEC_WRITE, cmd, 2, NULL);
    if(res < 0)
    {
        DBG_ERR("I2C error\n");
        goto out;
    }
    else
    {
        open = kzalloc(len, GFP_KERNEL);
        if(ERR_ALLOC_MEM(open))
        {
            res = FAIL;
            goto out;
        }

        res = exec_cdc_command(EXEC_READ, 0, len, open);
		if(res < 0)
            goto out;

        dump_data(open, 8);

        if(cmd[1] == 0x2)
        {
            for(i = 0; i < len; i++)
            {
                /* H byte + L byte */
                raw[i] = (open[2 * i] << 8) +  open[1 + (2 * i)];            
            
                if((raw[i] * 0x8000) == 0x8000)
                {
                    tItems[index].buf[i] = raw[i] - 65536;
                }
                else
                {
                    tItems[index].buf[i] = raw[i];
                }
            }
        }

        if(cmd[1] == 0x3)
        {
            /* Each result is getting from a 3 by 3 grid depending on where the centre location is.
            So if the centre is at corner, the number of node grabbed from a grid will be different. */
            for(y = 0; y < core_mp->ych_len; y++)
            {
                for(x = 0; x < core_mp->xch_len; x++)
                {
                    int tmp[8] = {0};
                    int sum = 0, avg = 0, count = 0;
                    int centre = open[x+y];

                    if(y == 0 && x == 0)
                    {
                        tmp[0] = open[x+(y+1)]; // down
                        tmp[1] = open[(x+1)+y]; // right
                        tmp[2] = open[(x+1)+(y+1)]; //lower right

                        count = 3;                   
                    }
                    else if(y == (core_mp->ych_len - 1) && x == 0)
                    {
                        tmp[0] = open[x+(y-1)]; // up
                        tmp[1] = open[(x+1)+y]; // right
                        tmp[2] = open[(x+1)+(y-1)]; //upper right 
                        count = 3;
                    }
                    else if(y == 0 && x == (core_mp->xch_len - 1))
                    {
                        tmp[0] = open[x+(y+1)]; // down
                        tmp[1] = open[(x-1)+y]; // left
                        tmp[2] = open[(x-1)+(y+1)]; // lower left
                        count = 3;
                    }
                    else if(y == (core_mp->ych_len - 1) && x == (core_mp->xch_len - 1) )
                    {
                        tmp[0] = open[x+(y-1)]; // up
                        tmp[1] = open[(x-1)+y]; // left
                        tmp[2] = open[(x-1)+(y-1)]; // upper left
                        count = 3;
                    }
                    else if (y == 0 && x != 0)
                    {
                        tmp[0] = open[x+(y+1)]; // down
                        tmp[1] = open[(x-1)+y]; // left
                        tmp[2] = open[(x+1)+y]; // right
                        tmp[3] = open[(x-1)+(y+1)]; // lower left
                        tmp[4] = open[(x+1)+(y+1)]; //lower right 
                        count = 5;
                    }
                    else if (y != 0 && x == 0)
                    {
                        tmp[0] = open[x+(y-1)]; // up
                        tmp[1] = open[(x+1)+y]; // right
                        tmp[2] = open[x+(y+1)]; // down
                        tmp[3] = open[(x+1)+(y-1)]; //upper right 
                        tmp[4] = open[(x+1)+(y+1)]; //lower right  
                        count = 5;                        
                    }
                    else if(y == (core_mp->ych_len - 1) && x != 0 )
                    {
                        tmp[0] = open[x+(y-1)]; // up
                        tmp[1] = open[(x-1)+y]; // left
                        tmp[2] = open[(x+1)+y]; // right
                        tmp[3] = open[(x-1)+(y-1)]; // upper left
                        tmp[4] = open[(x+1)+(y-1)]; //upper right
                        count = 5;                                                
                    }
                    else if(y != 0 && x == (core_mp->xch_len - 1) )
                    {
                        tmp[0] = open[x+(y-1)]; // up
                        tmp[1] = open[(x-1)+y]; // left
                        tmp[2] = open[x+(y+1)]; // down
                        tmp[3] = open[(x-1)+(y-1)]; // upper left
                        tmp[4] = open[(x-1)+(y+1)]; // lower left
                        count = 5;                                                
                    }
                    else
                    {
                        tmp[0] = open[x+(y-1)]; // up
                        tmp[1] = open[(x-1)+(y-1)]; // upper left
                        tmp[2] = open[(x-1)+y]; // left
                        tmp[3] = open[(x-1)+(y+1)]; // lower left
                        tmp[4] = open[x+(y+1)]; // down
                        tmp[5] = open[(x+1)+y]; // right
                        tmp[6] = open[(x+1)+(y-1)]; //upper right 
                        tmp[7] = open[(x+1)+(y+1)]; //lower right
                        count = 8;
                    }

                    for(i = 0; i < 8; i++)
                        sum += tmp[i];

                    avg = (sum+centre)/(count+1); // plus 1 becuase of centre
                    tItems[index].buf[x+y] = (centre / avg) * 100;
                }
            }
        }
    }

out:
    kfree(open);
    kfree(raw);
    tItems[index].result = (res != 0 ? "FAIL" : "PASS");   
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

    cmd[0] = tItems[index].cmd;
    cmd[1] = val;

    /* update X/Y channel length if they're changed */
	core_mp->xch_len = core_config->tp_info->nXChannelNum;
	core_mp->ych_len = core_config->tp_info->nYChannelNum;
    
    len = core_mp->xch_len * core_mp->ych_len * 2;
    FrameCount = core_mp->xch_len * core_mp->ych_len;

    DBG(DEBUG_MP_TEST,"Read X/Y Channel length = %d\n", len);
    DBG(DEBUG_MP_TEST,"FrameCount = %d\n", FrameCount);
    
    /* set specifc flag  */
    if(cmd[0] == protocol->mutual_signal)
    {
        core_mp->m_signal = true;
        raw_sin = kzalloc(FrameCount * sizeof(int32_t), GFP_KERNEL);
		if (ERR_ALLOC_MEM(raw_sin))
		{
			DBG_ERR("Failed to allocate signal buffer, %ld\n", PTR_ERR(raw_sin));
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
        DBG_ERR("Failed to allocate raw buffer, %ld\n", PTR_ERR(raw));
        res = -ENOMEM;
        goto out;
    }    

    if(tItems[index].buf == NULL)
    {
        /* Create a buffer that belogs to itself */
        tItems[index].buf = kzalloc(FrameCount * sizeof(int32_t), GFP_KERNEL);
    }
    else
    {
        /* erase data if this buffrer was not cleaned and freed */
        memset(tItems[index].buf, 0x0, FrameCount * sizeof(int32_t));
    }

    /* sending command to get raw data, converting it and comparing it with threshold */
    res = exec_cdc_command(EXEC_WRITE, cmd, 2, NULL);
    if(res < 0)
    {
        DBG_ERR("I2C error\n");
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

        dump_data(mutual, 8);
                      
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
            tItems[index].buf[i] = raw_sin[i];
        else
            tItems[index].buf[i] = raw[i];
    }      
        
out:
    kfree(mutual);
    kfree(raw);
    kfree(raw_sin);
    core_mp->m_signal = false;
    core_mp->m_dac = false;
    tItems[index].result = (res != 0 ? "FAIL" : "PASS");   
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

    cmd[0] = tItems[index].cmd;
    cmd[1] = val;

    /* update self tx/rx length if they're changed */
    core_mp->stx_len = core_config->tp_info->self_tx_channel_num;
    core_mp->srx_len = core_config->tp_info->self_rx_channel_num;

    len = core_mp->stx_len * core_mp->srx_len * 2;
    FrameCount = core_mp->stx_len * core_mp->srx_len;
    
    DBG(DEBUG_MP_TEST,"Read SELF X/Y Channel length = %d\n", len);
    DBG(DEBUG_MP_TEST,"FrameCount = %d\n", FrameCount);    

    /* set specifc flag  */
    if(cmd[0] == protocol->self_signal)
    {
        core_mp->s_signal = true;
        raw_sin = kzalloc(FrameCount * sizeof(int32_t), GFP_KERNEL);
		if (ERR_ALLOC_MEM(raw_sin))
		{
			DBG_ERR("Failed to allocate signal buffer, %ld\n", PTR_ERR(raw_sin));
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
        DBG_ERR("Failed to allocate raw buffer, %ld\n", PTR_ERR(raw));
        res = -ENOMEM;
        goto out;
    }    

    if(tItems[index].buf == NULL)
    {
        /* Create a buffer that belogs to itself */
        tItems[index].buf = kzalloc(FrameCount * sizeof(int32_t), GFP_KERNEL);
    }
    else
    {
        /* erase data if this buffrer was not cleaned and freed */
        memset(tItems[index].buf, 0x0, FrameCount * sizeof(int32_t));
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
        
        dump_data(self, 8);

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
            tItems[index].buf[i] = raw_sin[i];
        else
            tItems[index].buf[i] = raw[i];
    }      
        
out:
    kfree(self);
    kfree(raw);
    kfree(raw_sin);
    core_mp->s_signal = false;
    core_mp->s_dac = false;
    tItems[index].result = (res != 0 ? "FAIL" : "PASS");   
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

    cmd[0] = tItems[index].cmd;
    cmd[1] = val;

    /* update key's length if they're changed */
    core_mp->key_len = core_config->tp_info->nKeyCount;
    len = core_mp->key_len * 2;

    DBG(DEBUG_MP_TEST,"Read key's length = %d\n", len);
    DBG(DEBUG_MP_TEST,"core_mp->key_len = %d\n",core_mp->key_len); 

    /* raw buffer is used to receive data from firmware */
    raw = kzalloc(core_mp->key_len  * sizeof(int32_t), GFP_KERNEL);
    if (ERR_ALLOC_MEM(raw))
    {
        DBG_ERR("Failed to allocate raw buffer, %ld\n", PTR_ERR(raw));
        res = -ENOMEM;
        goto out;
    }

    if(tItems[index].buf == NULL)
    {
        /* Create a buffer that belogs to itself */
        tItems[index].buf = kzalloc(core_mp->key_len * sizeof(int32_t), GFP_KERNEL);
    }
    else
    {
        /* erase data if this buffrer was not cleaned and freed */
        memset(tItems[index].buf, 0x0, core_mp->key_len * sizeof(int32_t));
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

        dump_data(icon, 8);
        
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
            tItems[index].buf[i] = raw[i];
    }

out:
    kfree(icon);
    kfree(raw);
    core_mp->key_dac = false;
    tItems[index].result = (res != 0 ? "FAIL" : "PASS");
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

    cmd[0] = tItems[index].cmd;
    cmd[1] = val;

    /* update side touch's length it they're changed */
    core_mp->st_len = core_config->tp_info->side_touch_type;
    len = core_mp->st_len * 2;

    DBG(DEBUG_MP_TEST,"Read st's length = %d\n", len);
    DBG(DEBUG_MP_TEST,"core_mp->st_len = %d\n", core_mp->st_len); 

    /* raw buffer is used to receive data from firmware */
    raw = kzalloc(core_mp->st_len  * sizeof(int32_t), GFP_KERNEL);
    if (ERR_ALLOC_MEM(raw))
    {
        DBG_ERR("Failed to allocate raw buffer, %ld\n", PTR_ERR(raw));
        res = -ENOMEM;
        goto out;
    }

    if(tItems[index].buf == NULL)
    {
        /* Create a buffer that belogs to itself */
        tItems[index].buf = kzalloc(core_mp->st_len * sizeof(int32_t), GFP_KERNEL);
    }
    else
    {
        /* erase data if this buffrer was not cleaned and freed */
        memset(tItems[index].buf, 0x0, core_mp->st_len * sizeof(int32_t));
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
        
        dump_data(st, 8);

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
            tItems[index].buf[i] = raw[i];
    }

out:
    kfree(st);
    kfree(raw);
    core_mp->st_dac = false;
    tItems[index].result = (res != 0 ? "FAIL" : "PASS");
    return res; 
}

static int tx_rx_delta_test(int index, uint8_t val)
{
    int i, x, y, res = 0;
    int FrameCount = 1;
    int len = 0;
    uint8_t cmd[2] = {0};
    uint8_t *delta = NULL;

    cmd[0] = tItems[index].cmd;
    cmd[1] = val;

    /* update X/Y channel length if they're changed */
	core_mp->xch_len = core_config->tp_info->nXChannelNum;
    core_mp->ych_len = core_config->tp_info->nYChannelNum;
    
    len = core_mp->xch_len * core_mp->ych_len;

    DBG(DEBUG_MP_TEST, "Read Tx/Rx delta length = %d\n", len);
    
    /* Because tx/rx have their own buffer to store data speparately, */
    /* I allocate outside buffer instead of its own one to catch it */
	tx_delta_buf = kzalloc(len * sizeof(int32_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(tx_delta_buf))
	{
		DBG_ERR("Failed to allocate raw buffer, %ld\n", PTR_ERR(tx_delta_buf));
		res = -ENOMEM;
		goto out;
    }

	rx_delta_buf = kzalloc(len * sizeof(int32_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(rx_delta_buf))
	{
		DBG_ERR("Failed to allocate raw buffer, %ld\n", PTR_ERR(rx_delta_buf));
		res = -ENOMEM;
		goto out;
    }
    
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

        dump_data(delta, 8);

        for(i = 0; i < FrameCount; i++)
        {
            for(y = 0; y < core_mp->ych_len; y++)
            {
                for(x = 0; x < core_mp->xch_len; x++)
                {
                    /* Rx Delta */
                    if(x != (core_mp->xch_len - 1))
                    {
                        rx_delta_buf[x+y] = Mathabs(delta[x+y] - delta[(x+1)+y]);
                    }
    
                    /* Tx Delta */
                    if(y != (core_mp->ych_len - 1))
                    {
                        tx_delta_buf[x+y] = Mathabs(delta[x+y] - delta[x+(y+1)]);
                    }
                }
            }
        }
    }

out:
    kfree(delta);
    tItems[index].result = (res != 0 ? "FAIL" : "PASS");
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

    cmd[0] = tItems[index].cmd;
    cmd[1] = 0x0;

    if(count <= 0)
    {
        DBG_ERR("The frame is equal or less than 0\n");
        res = -EINVAL;
        goto out;
    }

    /* update X/Y channel length if they're changed */
	core_mp->xch_len = core_config->tp_info->nXChannelNum;
    core_mp->ych_len = core_config->tp_info->nYChannelNum;
    
    len = core_mp->xch_len * core_mp->ych_len;

    DBG_INFO("Read length of frame = %d\n", len);

    if(tItems[index].buf == NULL)
    {
        /* Create a buffer that belogs to itself */
        tItems[index].buf = kzalloc(len * sizeof(int32_t), GFP_KERNEL);
    }
    else
    {
        /* erase data if this buffrer was not cleaned and freed */
        memset(tItems[index].buf, 0x0, len * sizeof(int32_t));
    }

    max_buf = kmalloc(len * sizeof(int32_t), GFP_KERNEL);
    if (ERR_ALLOC_MEM(max_buf))
    {
        DBG_ERR("Failed to allocate MAX buffer, %ld\n", PTR_ERR(max_buf));
        res = -ENOMEM;
        goto out;
    }

    min_buf = kmalloc(len * sizeof(int32_t), GFP_KERNEL);
    if (ERR_ALLOC_MEM(min_buf))
    {
        DBG_ERR("Failed to allocate MIN buffer, %ld\n", PTR_ERR(min_buf));
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
                    DBG_ERR("Failed to create p2p buffer\n");
                    goto out;
                }
            }
    
            res = exec_cdc_command(EXEC_READ, 0, len, p2p);
            if(res < 0)
                goto out;

            dump_data(p2p, 8);
    
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
            tItems[index].buf[x+y] = max_buf[x+y] - min_buf[x+y];
        }
    }

out:
    kfree(p2p);
    kfree(max_buf);
    kfree(min_buf);
    tItems[index].result = (res != 0 ? "FAIL" : "PASS");
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

    DBG_INFO("Open CSV: %s\n ", CSV_PATH);

    if(f == NULL)
    f = filp_open(CSV_PATH, O_CREAT | O_RDWR , 0644);

    if(ERR_ALLOC_MEM(f))
    {
        DBG_ERR("Failed to open CSV file %s\n", CSV_PATH);
        goto fail_open;
    }

	for(i = 0; i < ARRAY_SIZE(tItems); i++)
	{
        if(tItems[i].run)
        {
            printk("\n\n");
            printk(" %s : %s ", tItems[i].desp, tItems[i].result);
            sprintf(csv,  " %s : %s ", tItems[i].desp, tItems[i].result);
            f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

            printk("\n");
            sprintf(csv, "\n");
            f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

            if(tItems[i].catalog == MUTUAL_TEST)
            {
                /* print X raw */
                for(x = 0; x < core_mp->xch_len; x++)
                {
                    if(x == 0)
                    {
                        DUMP(DEBUG_MP_TEST,"           ");
                        sprintf(csv, ",");
                        f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);                      
                    }

                    DUMP(DEBUG_MP_TEST,"X%02d      ", x);
                    sprintf(csv, "X%02d, ", x);
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                }

                DUMP(DEBUG_MP_TEST,"\n");
                sprintf(csv, "\n");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

                for(y = 0; y < core_mp->ych_len; y++)
                {
                    DUMP(DEBUG_MP_TEST," Y%02d ", y);
                    sprintf(csv, "Y%02d, ", y);
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

                    for(x = 0; x < core_mp->xch_len; x++)
                    {
                        DUMP(DEBUG_MP_TEST," %7d ",tItems[i].buf[x+y]);
                        sprintf(csv, "%7d,", tItems[i].buf[x+y]);
                        f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                    }
                    DUMP(DEBUG_MP_TEST,"\n");
                    sprintf(csv, "\n");
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                }
            }
            else if(tItems[i].catalog == SELF_TEST)
            {
                DUMP(DEBUG_MP_TEST,"\n\n");
                DUMP(DEBUG_MP_TEST," %s : %s ", tItems[i].desp, tItems[i].result);
                sprintf(csv,  " %s : %s ", tItems[i].desp, tItems[i].result);
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
    
                DUMP(DEBUG_MP_TEST,"\n");
                sprintf(csv, "\n");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

                /* print X raw */
                for(x = 0; x < core_mp->xch_len; x++)
                {
                    if(x == 0)
                    {
                        DUMP(DEBUG_MP_TEST,"           ");
                        sprintf(csv, ",");
                        f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);                      
                    }

                    DUMP(DEBUG_MP_TEST,"X%02d      ", x);
                    sprintf(csv, "X%02d, ", x);
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                }

                DUMP(DEBUG_MP_TEST,"\n");
                sprintf(csv, "\n");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

                for(y = 0; y < core_mp->ych_len; y++)
                {
                    DUMP(DEBUG_MP_TEST," Y%02d ", y);
                    sprintf(csv, "Y%02d, ", y);
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

                    for(x = 0; x < core_mp->xch_len; x++)
                    {
                        DUMP(DEBUG_MP_TEST," %7d ",tItems[i].buf[x+y]);
                        sprintf(csv, "%7d,", tItems[i].buf[x+y]);
                        f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                    }
                    DUMP(DEBUG_MP_TEST,"\n");
                    sprintf(csv, "\n");
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                }
            }
            else if(tItems[i].catalog == KEY_TEST)
            {
                for(x = 0; x < core_mp->key_len; x++)
                {
                    DUMP(DEBUG_MP_TEST,"KEY_%02d ",x);     
                    sprintf(csv, "KEY_%02d,", x);
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);                     
                }

                DUMP(DEBUG_MP_TEST,"\n");
                sprintf(csv, "\n");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

                for(y = 0; y < core_mp->key_len; y++)
                {
                    DUMP(DEBUG_MP_TEST," %3d   ",tItems[i].buf[y]);     
                    sprintf(csv, " %3d, ", tItems[i].buf[y]);
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);           
                }
                DUMP(DEBUG_MP_TEST,"\n");
                sprintf(csv, "\n");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
            }
            else if(tItems[i].catalog == ST_TEST)
            {
                /* TODO: Not implemented yet */
            }
            else if(tItems[i].catalog == TX_RX_DELTA)
            {
                /* print X raw */
                for(x = 0; x < core_mp->xch_len; x++)
                {
                    if(x == 0)
                    {
                        DUMP(DEBUG_MP_TEST,"           ");
                        sprintf(csv, ",");
                        f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);                      
                    }

                    DUMP(DEBUG_MP_TEST,"X%02d       ", x);
                    sprintf(csv, "X%02d, ", x);
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                }

                DUMP(DEBUG_MP_TEST,"\n");
                sprintf(csv, "\n");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

                for(y = 0; y < core_mp->ych_len; y++)
                {
                    DUMP(DEBUG_MP_TEST," Y%02d ", y);
                    sprintf(csv, "Y%02d, ", y);
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

                    for(x = 0; x < core_mp->xch_len; x++)
                    {
                        /* Threshold with RX delta */
                        if(rx_delta_buf[x+y] <= core_mp->RxDeltaMax &&
                            rx_delta_buf[x+y] >= core_mp->RxDeltaMin)
                        {
                            DUMP(DEBUG_MP_TEST," %7d ",rx_delta_buf[x+y]); 
                            sprintf(csv, "%7d,", rx_delta_buf[x+y]);
                            f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                        }
                        else
                        {
                            if(rx_delta_buf[x+y] > core_mp->RxDeltaMax)
                            {
                                DUMP(DEBUG_MP_TEST," *%7d ",rx_delta_buf[x+y]);
                                sprintf(csv, "*%7d,", rx_delta_buf[x+y]);
                                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                            }
                            else
                            {
                                DUMP(DEBUG_MP_TEST," #%7d ",rx_delta_buf[x+y]);
                                sprintf(csv, "#%7d,", rx_delta_buf[x+y]);
                                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                            }
                        }
                    }
                    DUMP(DEBUG_MP_TEST,"\n");
                    sprintf(csv, "\n");
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                }

                DUMP(DEBUG_MP_TEST,"\n");
                sprintf(csv, "\n");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                
                for(y = 0; y < core_mp->ych_len; y++)
                {
                    DUMP(DEBUG_MP_TEST," Y%02d ", y);
                    sprintf(csv, "Y%02d, ", y);
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

                    for(x = 0; x < core_mp->xch_len; x++)
                    {   
                        /* Threshold with TX delta */
                        if(tx_delta_buf[x+y] <= core_mp->TxDeltaMax &&
                            tx_delta_buf[x+y] >= core_mp->TxDeltaMin)
                        {
                            DUMP(DEBUG_MP_TEST," %7d ",tx_delta_buf[x+y]);
                            sprintf(csv, "%7d,", tx_delta_buf[x+y]);
                            f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                        }
                        else
                        {
                            if(tx_delta_buf[x+y] > core_mp->TxDeltaMax)
                            {
                                DUMP(DEBUG_MP_TEST," *%7d ",tx_delta_buf[x+y]);
                                sprintf(csv, "*%7d,", tx_delta_buf[x+y]);
                                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                                
                            }
                            else
                            {
                                DUMP(DEBUG_MP_TEST," #%7d ",tx_delta_buf[x+y]);
                                sprintf(csv, "#%7d,", tx_delta_buf[x+y]);
                                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                            }
                        }
                    }
                    DUMP(DEBUG_MP_TEST,"\n");
                    sprintf(csv, "\n");
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                }
                DUMP(DEBUG_MP_TEST,"\n");
                sprintf(csv, "\n");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
            }
            else if(tItems[i].catalog == UNTOUCH_P2P)
            {
                /* print X raw */
                for(x = 0; x < core_mp->xch_len; x++)
                {
                    if(x == 0)
                    {
                        DUMP(DEBUG_MP_TEST,"           ");
                        sprintf(csv, ",");
                        f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);                      
                    }

                    DUMP(DEBUG_MP_TEST,"X0%2d       ", x);
                    sprintf(csv, "X%02d, ", x);
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                }

                DUMP(DEBUG_MP_TEST,"\n");
                sprintf(csv, "\n");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

                for(y = 0; y < core_mp->ych_len; y++)
                {
                    DUMP(DEBUG_MP_TEST," Y%02d ", y);
                    sprintf(csv, "Y%02d, ", y);
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

                    for(x = 0; x < core_mp->xch_len; x++)
                    {
                        /* Threshold with P2P */
                        if(tItems[i].buf[x+y] <= core_mp->P2PMax &&
                            tItems[i].buf[x+y] >= core_mp->P2PMin)
                        {
                            DUMP(DEBUG_MP_TEST," %7d ",tItems[i].buf[x+y]);
                            sprintf(csv, "%7d,", tItems[i].buf[x+y]);
                            f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                        }
                        else
                        {
                            if(tItems[i].buf[x+y] > core_mp->P2PMax)
                            {
                                DUMP(DEBUG_MP_TEST," *%7d ",tItems[i].buf[x+y]);
                                sprintf(csv, "*%7d,", tItems[i].buf[x+y]);
                                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                            }
                            else
                            {
                                DUMP(DEBUG_MP_TEST," #%7d ",tItems[i].buf[x+y]);
                                sprintf(csv, "#%7d,", tItems[i].buf[x+y]);
                                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                            }
                        }
                    }
                    DUMP(DEBUG_MP_TEST,"\n");
                    sprintf(csv, "\n");
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                }                
            }
            else if (tItems[i].catalog == PIXEL)
            {
                /* print X raw */
                for(x = 0; x < core_mp->xch_len; x++)
                {
                    if(x == 0)
                    {
                        DUMP(DEBUG_MP_TEST,"           ");
                        sprintf(csv, ",");
                        f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);                      
                    }

                    DUMP(DEBUG_MP_TEST,"X%02d      ", x);
                    sprintf(csv, "X%02d, ", x);
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                }

                DUMP(DEBUG_MP_TEST,"\n");
                sprintf(csv, "\n");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

                for(y = 0; y < core_mp->ych_len; y++)
                {
                    DUMP(DEBUG_MP_TEST," Y%02d ", y);
                    sprintf(csv, "Y%02d, ", y);
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

                    for(x = 0; x < core_mp->xch_len; x++)
                    {
                        DUMP(DEBUG_MP_TEST," %7d ",tItems[i].buf[x+y]);
                        sprintf(csv, "%7d,", tItems[i].buf[x+y]);
                        f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                    }
                    DUMP(DEBUG_MP_TEST,"\n");
                    sprintf(csv, "\n");
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                }
            }
            else if(tItems[i].catalog == OPEN_TEST)
            {
                /* print X raw */
                for(x = 0; x < core_mp->xch_len; x++)
                {
                    if(x == 0)
                    {
                        DUMP(DEBUG_MP_TEST,"           ");
                        sprintf(csv, ",");
                        f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);                      
                    }

                    DUMP(DEBUG_MP_TEST,"X%02d      ", x);
                    sprintf(csv, "X%02d, ", x);
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                }

                DUMP(DEBUG_MP_TEST,"\n");
                sprintf(csv, "\n");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

                for(y = 0; y < core_mp->ych_len; y++)
                {
                    DUMP(DEBUG_MP_TEST," Y%02d ", y);
                    sprintf(csv, "Y%02d, ", y);
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);

                    for(x = 0; x < core_mp->xch_len; x++)
                    {
                        DUMP(DEBUG_MP_TEST," %7d ",tItems[i].buf[x+y]);
                        sprintf(csv, "%7d,", tItems[i].buf[x+y]);
                        f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                    }
                    DUMP(DEBUG_MP_TEST,"\n");
                    sprintf(csv, "\n");
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                }
            }

            DUMP(DEBUG_MP_TEST,"\n");
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

    DBG_INFO("Test name = %s, size = %d\n", name, (int)ARRAY_SIZE(tItems));
    
	for(i = 0; i < ARRAY_SIZE(tItems); i++)
	{
        if(strcmp(name, tItems[i].name) == 0)
		{
            tItems[i].run = true;
            res = tItems[i].do_test(i, val);
            DBG_INFO("***** DONE: tItems[%d] = %p\n ", i, tItems[i].buf);
            printk("\n\n\n");
			return res;
		}
    }

    DBG_ERR("The name can't be found in the list\n");
    return FAIL;
}
EXPORT_SYMBOL(core_mp_run_test);

void core_mp_move_code(void)
{
    if(core_config_check_cdc_busy() < 0)
        DBG_ERR("Check busy is timout !\n");

    if(core_config_ice_mode_enable() < 0)
    {
        DBG_ERR("Failed to enter ICE mode\n");
        return;
    }

    /* DMA Trigger */
    core_config_ice_mode_write(0x41010, 0xFF, 1);

    mdelay(30);

    /* Code reset */
    core_config_ice_mode_write(0x40040, 0xAE, 1);

    core_config_ice_mode_disable();

    if(core_config_check_cdc_busy() < 0)
        DBG_ERR("Check busy is timout !\n");
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
                DBG_ERR("Failed to init core_mp, %ld\n", PTR_ERR(core_mp));
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

			mp_test_init_item();
       }
    }
    else
    {
        DBG_ERR("Failed to get TP information\n");
		res = -EINVAL;
    }

out:
	return res;
}
EXPORT_SYMBOL(core_mp_init);

void core_mp_remove(void)
{
    DBG_INFO("Remove core-mp members\n");
    kfree(core_mp);
}
EXPORT_SYMBOL(core_mp_remove);
