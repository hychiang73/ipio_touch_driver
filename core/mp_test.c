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

/* You must declare a new test at here before running a new process of mp test */
struct mp_test_items tItems[] = {
    {"mutual_dac", "Untouch Calibration Data(DAC) - Mutual", "false", MUTUAL_TEST, 0x0, false, 0, 0, 0, NULL, NULL},
    {"mutual_bg", "Baseline Data(BG)", "false", MUTUAL_TEST, 0x0, false, 0, 0, 0, NULL, NULL},
    {"mutual_signal", "Untouch Signal Data(BG-Raw-4096) - Mutual", "false", MUTUAL_TEST, 0x0, false, 0, 0, 0, NULL, NULL},
    {"mutual_no_bk", "Untouch Raw Data(No BK) - Mutual", "false", MUTUAL_TEST, 0x0, false, 0, 0, 0, NULL, NULL},
    {"mutual_has_bk", "Untouch Raw Data(Have BK) - Mutual", "false", MUTUAL_TEST, 0x0, false, 0, 0, 0, NULL, NULL},
    {"mutual_bk_dac", "Manual BK Data(Mutual)", "false", MUTUAL_TEST, 0x0, false, 0, 0, 0, NULL, NULL},
    
    {"self_dac", "Untouch Calibration Data(DAC) - Self", "false", SELF_TEST, 0x0, false, 0, 0, 0, NULL, NULL},
    {"self_bg", "Baselin Data(BG,Self_Tx,Self_Rx)", "false", SELF_TEST, 0x0, false, 0, 0, 0, NULL, NULL},
    {"self_signal", "Untouch Signal Data(BG–Raw-4096) - Self", "false", SELF_TEST, 0x0, false, 0, 0, 0, NULL, NULL},
    {"self_no_bk", "Untouch Raw Data(No BK) - Self", "false", SELF_TEST, 0x0, false, 0, 0, 0, NULL, NULL},
    {"self_has_bk", "Untouch Raw Data(Have BK) - Self", "false", SELF_TEST, 0x0, false, 0, 0, 0, NULL, NULL},
    {"self_bk_dac", "Manual BK DAC Data(Self_Tx,Self_Rx)", "false", SELF_TEST, 0x0, false, 0, 0, 0, NULL, NULL},

    {"key_dac", "Calibration Data(DAC/ICON)", "false", KEY_TEST, 0x0, false, 0, 0, 0, NULL, NULL},
    {"key_bg", "Key Baseline Data", "false", KEY_TEST, 0x0, false, 0, 0, 0, NULL, NULL},
    {"key_no_bk", "Key Raw Data", "false", KEY_TEST, 0x0, false, 0, 0, 0, NULL, NULL},
    {"key_has_bk", "Key Raw BK DAC", "false", KEY_TEST, 0x0, false, 0, 0, 0, NULL, NULL},
    {"key_open", "Key Raw Open Test", "false", KEY_TEST, 0x0, false, 0, 0, 0, NULL, NULL},
    {"key_short", "Key Raw Short Test", "false", KEY_TEST, 0x0, false, 0, 0, 0, NULL, NULL},

    {"st_dac", "ST Calibration Data(DAC)", "false", ST_TEST, 0x0, false, 0, 0, 0, NULL, NULL},
    {"st_bg", "ST Baseline Data(BG)", "false", ST_TEST, 0x0, false, 0, 0, 0, NULL, NULL},
    {"st_no_bk", "ST Raw Data(No BK)", "false", ST_TEST, 0x0, false, 0, 0, 0, NULL, NULL},
    {"st_has_bk", "ST Raw(Have BK)", "false", ST_TEST, 0x0, false, 0, 0, 0, NULL, NULL},
    {"st_open", "ST Open Data", "false", ST_TEST, 0x0, false, 0, 0, 0, NULL},

    {"tx_short", "Tx Short Test", "false", MUTUAL_TEST, 0x0, false, 0, 0, 0, NULL, NULL},
    {"rx_short", "Short Test (Rx)", "false", MUTUAL_TEST, 0x0, false, 0, 0, 0, NULL, NULL},
    {"rx_open", "RX Open", "false", MUTUAL_TEST, 0x0, false, 0, 0, 0, NULL, NULL},

    {"cm_data", "Untouch Cm Data", "false", MUTUAL_TEST, 0x0, false, 0, 0, 0, NULL, NULL},
    {"cs_data", "Untouch Cs Data", "false", MUTUAL_TEST, 0x0, false, 0, 0, 0, NULL, NULL},

    {"tx_rx_delta", "Tx/Rx Delta", "false", TX_RX_DELTA, 0x0, false, 0, 0, 0, NULL, NULL},
 
    {"p2p", "Untouch Peak to Peak", "false", UNTOUCH_P2P, 0x0, false, 0, 0, 0, NULL, NULL},

    {"pixel_no_bk", "Pixel Raw (No BK)", "false", PIXEL, 0x0, false, 0, 0, 0, NULL, NULL},
    {"pixel_has_bk", "Pixel Raw (Have BK)", "false", PIXEL, 0x0, false, 0, 0, 0, NULL, NULL},

    {"open_integration", "Open Test(integration)", "false", OPEN_TEST, 0x0, false, 0, 0, 0, NULL, NULL},
    {"open_cap", "Open Test(Cap)", "false", OPEN_TEST, 0x0, false, 0, 0, 0, NULL, NULL},
};

int32_t *frame_buf = NULL;
int32_t *key_buf = NULL;
struct core_mp_test_data *core_mp = NULL;

static void dump_data(void *data, int type, int len)
{
    int i;
    uint8_t *p8 = NULL;
    int32_t *p32 = NULL;

    if(ipio_debug_level == DEBUG_MP_TEST)
    {
        if(data == NULL)
        {
            DBG_ERR("The data going to dump is NULL\n");
            return;        
        }
    
        printk("\n  Original Data: \n");
    
        if(type == 8)
        p8 = (uint8_t *)data;
        if(type == 32)
            p32 = (int32_t *)data;
    
        for(i = 0; i < len; i++)
        {   
            if(type == 8)
                printk(" %4x ", p8[i]);
            else if(type == 32)
                printk(" %4x ", p32[i]);
    
            if((i % 32) == 0)
                printk("\n");
        }
        printk("\n\n");
    }
}

static void print_cdc_data(int32_t *data, int max_ts, int min_ts, struct file *f, char *csv)
{
    int x, y;
    int32_t *tmp = data;
    
    /* print X raw only */
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
            /* Print/Write actual data to terminal/csv */
            if(tmp[y * core_mp->xch_len + x] <= max_ts &&
                tmp[y * core_mp->xch_len + x] >= min_ts)
            {
                DUMP(DEBUG_MP_TEST," %7d ", tmp[y * core_mp->xch_len + x]);
                sprintf(csv, "%d,", tmp[y * core_mp->xch_len + x]);
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
            }
            else
            {
                if(tmp[y * core_mp->xch_len + x] > max_ts)
                {
                    DUMP(DEBUG_MP_TEST," *%7d ",tmp[y * core_mp->xch_len + x]);
                    sprintf(csv, "*%d,", tmp[y * core_mp->xch_len + x]);
                    f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                }
                else
                {
                    DUMP(DEBUG_MP_TEST," #%7d ",tmp[y * core_mp->xch_len + x]);
                    sprintf(csv, "#%d,", tmp[y * core_mp->xch_len + x]);
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

static int create_mp_test_frame_buffer(int index)
{
    int res = 0;

    if(tItems[index].catalog == TX_RX_DELTA)
    {
        /* 
         * Because tx/rx have their own buffer to store data speparately, I allocate both buffers
         *  in outside instead of creating it in their strcture.
         */
        core_mp->tx_delta_buf = kzalloc(core_mp->frame_len * sizeof(int32_t), GFP_KERNEL);
        if (ERR_ALLOC_MEM(core_mp->tx_delta_buf))
        {
            DBG_ERR("Failed to allocate TX Delta buffer, %ld\n", PTR_ERR(core_mp->tx_delta_buf));
            res = -ENOMEM;
            goto out;
        }

        core_mp->rx_delta_buf = kzalloc(core_mp->frame_len * sizeof(int32_t), GFP_KERNEL);
        if (ERR_ALLOC_MEM(core_mp->rx_delta_buf))
        {
            DBG_ERR("Failed to allocate RX Delta buffer, %ld\n", PTR_ERR(core_mp->rx_delta_buf));
            res = -ENOMEM;
            goto out;
        }

        core_mp->tx_max_buf = kzalloc(core_mp->frame_len * sizeof(int32_t), GFP_KERNEL);
        if (ERR_ALLOC_MEM(core_mp->tx_max_buf))
        {
            DBG_ERR("Failed to allocate TX Max buffer, %ld\n", PTR_ERR(core_mp->tx_max_buf));
            res = -ENOMEM;
            goto out;
        }
    
        core_mp->tx_min_buf = kzalloc(core_mp->frame_len * sizeof(int32_t), GFP_KERNEL);
        if (ERR_ALLOC_MEM(core_mp->tx_min_buf))
        {
            DBG_ERR("Failed to allocate TX Min buffer, %ld\n", PTR_ERR(core_mp->tx_min_buf));
            res = -ENOMEM;
            goto out;
        }

        core_mp->rx_max_buf = kzalloc(core_mp->frame_len * sizeof(int32_t), GFP_KERNEL);
        if (ERR_ALLOC_MEM(core_mp->rx_max_buf))
        {
            DBG_ERR("Failed to allocate RX Max buffer, %ld\n", PTR_ERR(core_mp->rx_max_buf));
            res = -ENOMEM;
            goto out;
        }
    
        core_mp->rx_min_buf = kzalloc(core_mp->frame_len * sizeof(int32_t), GFP_KERNEL);
        if (ERR_ALLOC_MEM(core_mp->rx_min_buf))
        {
            DBG_ERR("Failed to allocate RX Min buffer, %ld\n", PTR_ERR(core_mp->rx_min_buf));
            res = -ENOMEM;
            goto out;
        } 
    }
    else
    {
        tItems[index].buf = kzalloc(core_mp->frame_len * sizeof(int32_t), GFP_KERNEL);
        if (ERR_ALLOC_MEM(tItems[index].buf))
        {
            DBG_ERR("Failed to allocate FRAME buffer, %ld\n", PTR_ERR(tItems[index].buf));
            res = -ENOMEM;
            goto out;
        }
    
        tItems[index].max_buf = kzalloc(core_mp->frame_len * sizeof(int32_t), GFP_KERNEL);
        if (ERR_ALLOC_MEM(tItems[index].max_buf))
        {
            DBG_ERR("Failed to allocate MAX buffer, %ld\n", PTR_ERR(tItems[index].max_buf));
            res = -ENOMEM;
            goto out;
        }
    
        tItems[index].min_buf = kzalloc(core_mp->frame_len * sizeof(int32_t), GFP_KERNEL);
        if (ERR_ALLOC_MEM(tItems[index].min_buf))
        {
            DBG_ERR("Failed to allocate MIN buffer, %ld\n", PTR_ERR(tItems[index].min_buf));
            res = -ENOMEM;
            goto out;
        }
    }

out:
    return res;
}

static int allnode_key_cdc_data(int index)
{
    int i, res = 0, len = 0;
    int inDACp = 0, inDACn = 0;
    uint8_t cmd[3] = {0};
    uint8_t *ori = NULL;

    /* CDC init */
    cmd[0] = protocol->cmd_cdc;
    cmd[1] = tItems[index].cmd;
    cmd[2] = 0;

    res = core_i2c_write(core_config->slave_i2c_addr, cmd, 3);
    if(res < 0)
    {
        DBG_ERR("I2C Write Error while initialising cdc \n");
        goto out;        
    }

    /* Check busy */
    if(core_config_check_cdc_busy() < 0)
    {
        DBG_ERR("Check busy is timout !\n");
        res = FAIL;
        goto out;         
    }

    /* Prepare to get cdc data */
    cmd[0] = protocol->cmd_read_ctrl;
    cmd[1] = protocol->cmd_get_cdc;
    DBG(DEBUG_MP_TEST,"R: cmd[0] = 0x%x, cmd[1] = 0x%x\n",cmd[0],cmd[1]);
    res = core_i2c_write(core_config->slave_i2c_addr, cmd, 2);
    if(res < 0)
    {
        DBG_ERR("I2C Read Error \n");
        goto out;        
    }

    len = core_mp->key_len * 2;

    DBG(DEBUG_MP_TEST,"Read key's length = %d\n", len);
    DBG(DEBUG_MP_TEST,"core_mp->key_len = %d\n",core_mp->key_len);

    if(len <= 0)
        goto out;

    /* Allocate a buffer for the original */
    ori = kzalloc(len * sizeof(uint8_t), GFP_KERNEL);
    if(ERR_ALLOC_MEM(ori))
    {
        DBG_ERR("Failed to allocate ori mem (%ld) \n", PTR_ERR(ori));
        goto out;
    }

    /* Get original frame(cdc) data */
    res = core_i2c_read(core_config->slave_i2c_addr, ori, len);
    if(res < 0)
    {
        DBG_ERR("I2C Read Error while getting original cdc data \n");
        goto out;        
    }

    dump_data(ori, 8, len);

    if(key_buf == NULL)
    {
        key_buf = kzalloc(core_mp->key_len * sizeof(int32_t), GFP_KERNEL);
        if(ERR_ALLOC_MEM(key_buf))
        {
            DBG_ERR("Failed to allocate FrameBuffer mem (%ld) \n", PTR_ERR(key_buf));
            goto out;
        }
    }

    /* Convert original data to the physical one in each node */
    for(i = 0; i < core_mp->frame_len; i++)
    {
        if(tItems[index].cmd == protocol->key_dac)
        {
            /* DAC - P */
            if(((ori[(2 * i) + 1] & 0x80) >> 7) == 1)
            {
                /* Negative */
                inDACp = 0 - (int)(ori[(2 * i) + 1] & 0x7F); 
            }
            else
            {
                inDACp = ori[(2 * i) + 1] & 0x7F;
            }

            /* DAC - N */
            if(((ori[(1 + (2 * i))+ 1] & 0x80) >> 7) == 1)
            {
                /* Negative */
                inDACn = 0 - (int)(ori[(1 + (2 * i)) + 1] & 0x7F);
            }
            else
            {
                inDACn = ori[(1 + (2 * i)) + 1] & 0x7F;
            }

            key_buf[i] = (inDACp + inDACn) / 2;
        }
    }

    dump_data(key_buf, 32, core_mp->frame_len);

out:
    kfree(ori);
    return res;
}

static int allnode_mutual_cdc_data(int index)
{
    int i = 0, res = 0, len = 0;
    int inDACp = 0, inDACn = 0;
    uint8_t cmd[3] = {0};
    uint8_t *ori = NULL;

    /* CDC init */
    cmd[0] = protocol->cmd_cdc;
    cmd[1] = tItems[index].cmd;
    cmd[2] = 0;

    if(strcmp(tItems[index].name, "open_integration") == 0)
        cmd[2] = 0x2;
    if(strcmp(tItems[index].name, "open_cap") == 0)
        cmd[2] = 0x3;

    res = core_i2c_write(core_config->slave_i2c_addr, cmd, 3);
    if(res < 0)
    {
        DBG_ERR("I2C Write Error while initialising cdc \n");
        goto out;        
    }

    /* Check busy */
    if(core_config_check_cdc_busy() < 0)
    {
        DBG_ERR("Check busy is timout !\n");
        res = FAIL;
        goto out;         
    }

    /* Prepare to get cdc data */
    cmd[0] = protocol->cmd_read_ctrl;
    cmd[1] = protocol->cmd_get_cdc;
    DBG(DEBUG_MP_TEST,"R: cmd[0] = 0x%x, cmd[1] = 0x%x\n",cmd[0],cmd[1]);
    res = core_i2c_write(core_config->slave_i2c_addr, cmd, 2);
    if(res < 0)
    {
        DBG_ERR("I2C Read Error \n");
        goto out;        
    }

    /* Multipling by 2 is due to the 16 bit in each node */
    len = (core_mp->xch_len * core_mp->ych_len * 2) + 2;

    DBG(DEBUG_MP_TEST,"Read X/Y Channel length = %d\n", len);
    DBG(DEBUG_MP_TEST,"core_mp->frame_len = %d \n", core_mp->frame_len);

    if(len <= 2)
        goto out;

    /* Allocate a buffer for the original */
    ori = kzalloc(len * sizeof(uint8_t), GFP_KERNEL);
    if(ERR_ALLOC_MEM(ori))
    {
        DBG_ERR("Failed to allocate ori mem (%ld) \n", PTR_ERR(ori));
        goto out;
    }

    /* Get original frame(cdc) data */
    res = core_i2c_read(core_config->slave_i2c_addr, ori, len);
    if(res < 0)
    {
        DBG_ERR("I2C Read Error while getting original cdc data \n");
        goto out;        
    }

    dump_data(ori, 8, len);

    if(frame_buf == NULL)
    {
        frame_buf = kzalloc(core_mp->frame_len * sizeof(int32_t), GFP_KERNEL);
        if(ERR_ALLOC_MEM(frame_buf))
        {
            DBG_ERR("Failed to allocate FrameBuffer mem (%ld) \n", PTR_ERR(frame_buf));
            goto out;
        }
    }
    else
    {
        memset(frame_buf, 0, core_mp->frame_len * sizeof(int32_t));
    }

    /* Convert original data to the physical one in each node */
    for(i = 0; i < core_mp->frame_len; i++)
    {
        if(tItems[index].cmd == protocol->mutual_dac)
        {
            /* DAC - P */
            if(((ori[(2 * i) + 1] & 0x80) >> 7) == 1)
            {
                /* Negative */
                inDACp = 0 - (int)(ori[(2 * i) + 1] & 0x7F); 
            }
            else
            {
                inDACp = ori[(2 * i) + 1] & 0x7F;
            }

            /* DAC - N */
            if(((ori[(1 + (2 * i))+ 1] & 0x80) >> 7) == 1)
            {
                /* Negative */
                inDACn = 0 - (int)(ori[(1 + (2 * i)) + 1] & 0x7F);
            }
            else
            {
                inDACn = ori[(1 + (2 * i)) + 1] & 0x7F;
            }

            frame_buf[i] = (inDACp + inDACn) / 2;
        }
        else
        {
             /* H byte + L byte */
            int32_t tmp = (ori[(2 * i) + 1] << 8) +  ori[(1 + (2 * i)) + 1];
         
            if((tmp & 0x8000) == 0x8000)
                frame_buf[i] = tmp - 65536;
            else
                frame_buf[i] = tmp;
        }
    }

    dump_data(frame_buf, 32, core_mp->frame_len);

out:
    kfree(ori);
    return res;
}

static void run_pixel_test(int index)
{
    int i, x, y;
    int32_t *p_comb = frame_buf;

    for(y = 0; y < core_mp->ych_len; y++)
    {
        for(x = 0; x < core_mp->xch_len; x++)
        {
            int tmp[4] = {0}, max = 0;
            int shift = y * core_mp->xch_len;
            int centre = p_comb[shift + x];

            /* if its position is in corner, the number of point 
                we have to minus is around 2 to 3.  */
            if(y == 0 && x == 0)
            {
                tmp[0] = Mathabs(centre - p_comb[(shift+1)+x]); // down
                tmp[1] = Mathabs(centre - p_comb[shift+(x+1)]); // right
            }
            else if(y == (core_mp->ych_len - 1) && x == 0)
            {
                tmp[0] = Mathabs(centre - p_comb[(shift-1)+x]); // up
                tmp[1] = Mathabs(centre - p_comb[shift+(x+1)]); // right
            }
            else if(y == 0 && x == (core_mp->xch_len - 1))
            {
                tmp[0] = Mathabs(centre - p_comb[(shift+1)+x]); // down
                tmp[1] = Mathabs(centre - p_comb[shift+(x-1)]); // left
            }
            else if(y == (core_mp->ych_len - 1) && x == (core_mp->xch_len - 1) )
            {
                tmp[0] = Mathabs(centre - p_comb[(shift-1)+x]); // up
                tmp[1] = Mathabs(centre - p_comb[shift+(x-1)]); // left
            }
            else if (y == 0 && x != 0)
            {
                tmp[0] = Mathabs(centre - p_comb[(shift+1)+x]); // down
                tmp[1] = Mathabs(centre - p_comb[shift+(x-1)]); // left
                tmp[2] = Mathabs(centre - p_comb[shift+(x+1)]); // right
            }
            else if (y != 0 && x == 0)
            {
                tmp[0] = Mathabs(centre - p_comb[(shift-1)+x]); // up
                tmp[1] = Mathabs(centre - p_comb[shift+(x+1)]); // right
                tmp[2] = Mathabs(centre - p_comb[(shift+1)+x]); // down

            }
            else if(y == (core_mp->ych_len - 1) && x != 0 )
            {
                tmp[0] = Mathabs(centre - p_comb[(shift-1)+x]); // up
                tmp[1] = Mathabs(centre - p_comb[shift+(x-1)]); // left
                tmp[2] = Mathabs(centre - p_comb[shift+(x+1)]); // right
            }
            else if(y != 0 && x == (core_mp->xch_len - 1) )
            {
                tmp[0] = Mathabs(centre - p_comb[(shift-1)+x]); // up
                tmp[1] = Mathabs(centre - p_comb[shift+(x-1)]); // left
                tmp[2] = Mathabs(centre - p_comb[(shift+1)+x]); // down
            }
            else
            {
                /* middle minus four directions */
                tmp[0] = Mathabs(centre - p_comb[(shift-1)+x]); // up
                tmp[1] = Mathabs(centre - p_comb[(shift+1)+x]); // down
                tmp[2] = Mathabs(centre - p_comb[shift+(x-1)]); // left
                tmp[3] = Mathabs(centre - p_comb[shift+(x+1)]); // right
            }

            max = tmp[0];

            for(i = 0; i < 4; i++)
            {
                if(tmp[i] > max)
                    max = tmp[i];
            }

            tItems[index].buf[shift+x] = max;
        }
    }
}

static int run_open_test(int index)
{
    int i, x, y, res = 0;
    int32_t *p_comb = frame_buf;

    if(strcmp(tItems[index].name, "open_integration") == 0)
    {
        for(i = 0; i < core_mp->frame_len; i++)
            tItems[index].buf[i] = p_comb[i];
    }
    else if(strcmp(tItems[index].name, "open_cap") == 0)
    {
        /* Each result is getting from a 3 by 3 grid depending on where the centre location is.
        So if the centre is at corner, the number of node grabbed from a grid will be different. */
        for(y = 0; y < core_mp->ych_len; y++)
        {
            for(x = 0; x < core_mp->xch_len; x++)
            {
                int tmp[8] = {0};
                int sum = 0, avg = 0, count = 0;
                int shift = y * core_mp->xch_len;
                int centre = p_comb[shift + x];

                if(y == 0 && x == 0)
                {
                    tmp[0] = p_comb[(shift+1)+x]; // down
                    tmp[1] = p_comb[shift+(x+1)]; // right
                    tmp[2] = p_comb[(shift+1)+(x+1)]; // lower right
                    count = 3;                   
                }
                else if(y == (core_mp->ych_len - 1) && x == 0)
                {
                    tmp[0] = p_comb[(shift-1)+x]; // up
                    tmp[1] = p_comb[shift+(x+1)]; // right
                    tmp[2] = p_comb[(shift-1)+(x+1)]; // upper right                   
                    count = 3;
                }
                else if(y == 0 && x == (core_mp->xch_len - 1))
                {
                    tmp[0] = p_comb[(shift+1)+x]; // down
                    tmp[1] = p_comb[shift+(x-1)]; // left
                    tmp[2] = p_comb[(shift+1)+(x+1)]; // lower right
                    count = 3;
                }
                else if(y == (core_mp->ych_len - 1) && x == (core_mp->xch_len - 1) )
                {
                    tmp[0] = p_comb[(shift-1)+x]; // up
                    tmp[1] = p_comb[shift+(x-1)]; // left
                    tmp[2] = p_comb[(shift-1)+(x-1)]; // upper left
                    count = 3;
                }
                else if (y == 0 && x != 0)
                {
                    tmp[0] = p_comb[(shift+1)+x]; // down
                    tmp[1] = p_comb[shift+(x-1)]; // left
                    tmp[2] = p_comb[shift+(x+1)]; // right
                    tmp[3] = p_comb[(shift+1)+(x-1)]; // lower left
                    tmp[4] = p_comb[(shift+1)+(x+1)]; // lower right
                    count = 5;
                }
                else if (y != 0 && x == 0)
                {
                    tmp[0] = p_comb[(shift-1)+x]; // up
                    tmp[1] = p_comb[shift+(x+1)]; // right
                    tmp[2] = p_comb[(shift+1)+x]; // down
                    tmp[3] = p_comb[(shift-1)+(x+1)]; // upper right
                    tmp[4] = p_comb[(shift+1)+(x+1)]; // lower right

                    count = 5;                        
                }
                else if(y == (core_mp->ych_len - 1) && x != 0 )
                {
                    tmp[0] = p_comb[(shift-1)+x]; // up
                    tmp[1] = p_comb[shift+(x+1)]; // right
                    tmp[2] = p_comb[shift+(x-1)]; // left
                    tmp[3] = p_comb[(shift-1)+(x-1)]; // upper left 
                    tmp[4] = p_comb[(shift-1)+(x+1)]; // upper right                       
                    count = 5;                                                
                }
                else if(y != 0 && x == (core_mp->xch_len - 1) )
                {
                    tmp[0] = p_comb[(shift-1)+x]; // up
                    tmp[1] = p_comb[shift+(x-1)]; // left
                    tmp[2] = p_comb[(shift+1)+x]; // down
                    tmp[3] = p_comb[(shift-1)+(x-1)]; // upper left
                    tmp[4] = p_comb[(shift+1)+(x-1)]; // lower left
                    count = 5;                                                
                }
                else
                {
                    tmp[0] = p_comb[(shift-1)+x]; // up
                    tmp[1] = p_comb[(shift-1)+(x-1)]; // upper left                        
                    tmp[2] = p_comb[shift+(x-1)]; // left
                    tmp[3] = p_comb[(shift+1)+(x-1)]; // lower left                        
                    tmp[4] = p_comb[(shift+1)+x]; // down
                    tmp[5] = p_comb[shift+(x+1)]; // right
                    tmp[6] = p_comb[(shift-1)+(x+1)]; // upper right
                    tmp[7] = p_comb[(shift+1)+(x+1)]; // lower right
                    count = 8;
                }

                for(i = 0; i < 8; i++)
                    sum += tmp[i];

                avg = (sum+centre)/(count+1); // plus 1 becuase of centre
                tItems[index].buf[shift + x] = (centre * 100) / avg;
            }
        }
    }
    return res;
}

static void run_tx_rx_delta_test(int index)
{
    int x, y;
    int32_t *p_comb = frame_buf;
   
    for(y = 0; y < core_mp->ych_len; y++)
    {
        for(x = 0; x < core_mp->xch_len; x++)
        {
            int shift = y * core_mp->xch_len;

            /* Tx Delta */
            if(y != (core_mp->ych_len - 1))
            {
                core_mp->tx_delta_buf[shift + x] = Mathabs(p_comb[shift + x] - p_comb[(shift+1)+x]);
            }

            /* Rx Delta */
            if(x != (core_mp->xch_len - 1))
            {
                core_mp->rx_delta_buf[shift + x] = Mathabs(p_comb[shift + x] - p_comb[shift+(x+1)]);
            }
        }
    }
}

static void run_untouch_p2p_test(int index)
{
    int x, y;
    int32_t *p_comb = frame_buf;

    for(y = 0; y < core_mp->ych_len; y++)
    {
        for(x = 0; x < core_mp->xch_len; x++)
        {
            int shift = y * core_mp->xch_len;

            if(p_comb[shift+x] > tItems[index].max_buf[shift+x])
            {
                tItems[index].max_buf[shift+x] = p_comb[shift+x];
            }

            if(p_comb[shift+x] < tItems[index].min_buf[shift+y])
            {
                tItems[index].min_buf[shift+x] = p_comb[shift+x];
            }

            tItems[index].buf[shift+x] = tItems[index].max_buf[shift+x] - tItems[index].min_buf[shift+x];
        }
    }
}

static void compare_MaxMin_result(int index, int32_t *data)
{
    int x, y;

    for(y = 0; y < core_mp->ych_len; y++)
    {
        for(x= 0; x < core_mp->xch_len; x++)
        {
            int shift = y * core_mp->xch_len;

            if(tItems[index].catalog == UNTOUCH_P2P)
                return;
            else if(tItems[index].catalog == TX_RX_DELTA)
            {
                /* Tx max/min comparison */
                if(core_mp->tx_delta_buf[shift+x] < data[shift+x])
                {
                    core_mp->tx_max_buf[shift+x] = data[shift+x];
                }
                
                if(core_mp->tx_delta_buf[shift+x] > data[shift+x])
                {
                    core_mp->tx_min_buf[shift+x] = data[shift+x];
                }

                /* Rx max/min comparison */
                if(core_mp->rx_delta_buf[shift+x] < data[shift+x])
                {
                    core_mp->rx_max_buf[shift+x] = data[shift+x];
                }
                
                if(core_mp->rx_delta_buf[shift+x] > data[shift+x])
                {
                    core_mp->rx_min_buf[shift+x] = data[shift+x];
                }
            }
            else
            {
                if(tItems[index].max_buf[shift+x] < tItems[index].buf[shift+x])
                {
                    tItems[index].max_buf[shift+x] = tItems[index].buf[shift+x];
                }
                
                if(tItems[index].min_buf[shift+x] > tItems[index].buf[shift+x])
                {
                    tItems[index].min_buf[shift+x] = tItems[index].buf[shift+x];
                }
            }
        }
    }
}

static int mutual_test(int index)
{
    int i = 0, x = 0, y = 0, res = 0;

    DBG(DEBUG_MP_TEST,"Item = %s, CMD = 0x%x, Frame Count = %d\n", 
    tItems[index].name, tItems[index].cmd, tItems[index].frame_count);

    if(tItems[index].frame_count == 0)
    {
        DBG_ERR("Frame count is zero, which at least sets as 1\n");
        goto out;
    }

    res = create_mp_test_frame_buffer(index);
    if(res < 0)
        goto out;

    /* Init Max/Min buffer */
    for(y = 0; y < core_mp->ych_len; y++)
    {
        for(x = 0; x < core_mp->xch_len; x++)
        {
            if(tItems[i].catalog == TX_RX_DELTA)
            {
                core_mp->tx_max_buf[y * core_mp->xch_len + x] = -65535;
                core_mp->rx_max_buf[y * core_mp->xch_len + x] = -65535;
                core_mp->tx_min_buf[y * core_mp->xch_len + x] = 65535;
                core_mp->rx_min_buf[y * core_mp->xch_len + x] = 65535;
            }
            else
            {
                tItems[index].max_buf[y * core_mp->xch_len + x] = -65535;
                tItems[index].min_buf[y * core_mp->xch_len + x] = 65535;
            }
        }
    }

    for(i = 0; i < tItems[index].frame_count; i++)
    {
        res = allnode_mutual_cdc_data(index);
        if (res < 0)
        {
            DBG_ERR("Failed to initialise CDC data, %d\n", res);
            goto out;
        }

        switch(tItems[index].catalog)
        {
            case PIXEL:
                run_pixel_test(index);
                break;
            case UNTOUCH_P2P:
                run_untouch_p2p_test(index);
                break;
            case OPEN_TEST:
                run_open_test(index);
                break;
            case TX_RX_DELTA:
                run_tx_rx_delta_test(index);
                break;
            default:
                for(i = 0; i < core_mp->frame_len; i++)
                    tItems[index].buf[i] = frame_buf[i];
                break;
        }
    }

    compare_MaxMin_result(index, tItems[index].buf);
        
out:
    kfree(frame_buf);
    return res;
}

static int key_test(int index)
{
    int i, res = 0;

    DBG(DEBUG_MP_TEST,"Item = %s, CMD = 0x%x, Frame Count = %d\n", 
    tItems[index].name, tItems[index].cmd, tItems[index].frame_count);

    if(tItems[index].frame_count == 0)
    {
        DBG_ERR("Frame count is zero, which at least sets as 1\n");
        res = -EINVAL;
        goto out;
    }
 
    res = create_mp_test_frame_buffer(index);
    if(res < 0)
        goto out;
    
    for(i = 0; i < tItems[index].frame_count; i++)
    {
        res = allnode_key_cdc_data(index);
        if (res < 0)
        {
            DBG_ERR("Failed to initialise CDC data, %d\n", res);
            goto out;
        }

        for(i = 0; i < core_mp->key_len; i++)
            tItems[index].buf[i] = key_buf[i];
    }

    compare_MaxMin_result(index, tItems[index].buf);

out:
    kfree(key_buf);
    return res;    
}

static int self_test(int index)
{
    DBG_ERR("TDDI has no self to be tested currently \n");
    return -1; 
}

static int st_test(int index)
{
    DBG_ERR("ST Test is not suppored by the driver\n");
    return FAIL; 
}

static void mp_test_init_item(void)
{
    int i;

    core_mp->mp_items = ARRAY_SIZE(tItems);

    /* assign test functions run on MP flow according to their catalog */
    for(i = 0; i < ARRAY_SIZE(tItems); i++)
    {
        if(tItems[i].catalog == MUTUAL_TEST)
            tItems[i].do_test = mutual_test;
       
        if(tItems[i].catalog == TX_RX_DELTA)
           tItems[i].do_test = mutual_test;
      
        if(tItems[i].catalog == UNTOUCH_P2P)
            tItems[i].do_test = mutual_test;
        
        if(tItems[i].catalog == PIXEL)
            tItems[i].do_test = mutual_test;
        
        if(tItems[i].catalog == OPEN_TEST)
            tItems[i].do_test = mutual_test;
                 
        if(tItems[i].catalog == KEY_TEST)
            tItems[i].do_test = key_test;

        if(tItems[i].catalog == SELF_TEST)
            tItems[i].do_test = self_test;
         
        if(tItems[i].catalog == ST_TEST)
            tItems[i].do_test = st_test;
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

void core_mp_test_free(void)
{
    int i;

    DBG_INFO("Free all allocated mem \n");
     
    for(i = 0; i < ARRAY_SIZE(tItems); i++)
    {
        tItems[i].run = false;
        tItems[i].result = "false";

        if(tItems[i].buf != NULL)
        {
            if(tItems[i].catalog == TX_RX_DELTA)
            {
                kfree(core_mp->rx_delta_buf);
                core_mp->rx_delta_buf = NULL;
                kfree(core_mp->tx_delta_buf);
                core_mp->tx_delta_buf = NULL;
                kfree(core_mp->tx_max_buf);
                core_mp->tx_max_buf = NULL;
                kfree(core_mp->tx_min_buf);
                core_mp->tx_min_buf = NULL;
                kfree(core_mp->rx_max_buf);
                core_mp->rx_max_buf = NULL;
                kfree(core_mp->rx_min_buf);
                core_mp->rx_min_buf = NULL;
            }
            else
            {
                kfree(tItems[i].buf);
                tItems[i].buf = NULL;
                kfree(tItems[i].max_buf);
                tItems[i].max_buf = NULL;
                kfree(tItems[i].min_buf);
                tItems[i].min_buf = NULL;
            }
        }
    }
    kfree(frame_buf);
    frame_buf = NULL;
    kfree(key_buf);
    key_buf = NULL;
}
EXPORT_SYMBOL(core_mp_test_free);

void core_mp_show_result(void)
{
    int i, x, y;
    char *csv = NULL;
    struct file *f = NULL;
    mm_segment_t fs;

    fs = get_fs();
    set_fs(KERNEL_DS);

    csv = kmalloc(1024 * 100, GFP_KERNEL);

    DBG_INFO("Open CSV: %s\n ", CSV_NAME_PATH);

    if(f == NULL)
        f = filp_open(CSV_NAME_PATH, O_CREAT | O_RDWR , 0644);

    if(ERR_ALLOC_MEM(f))
    {
        DBG_ERR("Failed to open CSV file %s\n", CSV_NAME_PATH);
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

            if(tItems[i].catalog == TX_RX_DELTA)
            {
                if(ERR_ALLOC_MEM(core_mp->rx_delta_buf) || ERR_ALLOC_MEM(core_mp->tx_delta_buf))
                {
                    DBG_ERR("This test item (%s) has no data inside its buffer \n", tItems[i].desp);
                    continue;                   
                }
            }
            else
            {
                if(ERR_ALLOC_MEM(tItems[i].buf) || ERR_ALLOC_MEM(tItems[i].max_buf) || ERR_ALLOC_MEM(tItems[i].min_buf))
                {
                    DBG_ERR("This test item (%s) has no data inside its buffer \n", tItems[i].desp);
                    continue;
                }
            }

            if(tItems[i].catalog == KEY_TEST)
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
            else if(tItems[i].catalog == TX_RX_DELTA)
            {
                printk(" %s ", "TX Max Hold");
                sprintf(csv,  "  %s ", "Max Hold");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                printk("\n");
                sprintf(csv, "\n");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
    
                print_cdc_data(core_mp->tx_max_buf, core_mp->TxDeltaMax, core_mp->TxDeltaMin, f, csv);
    
                printk(" %s ", "TX Min Hold");
                sprintf(csv,  "  %s ", "Min Hold");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                printk("\n");
                sprintf(csv, "\n");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);   
    
                print_cdc_data(core_mp->tx_min_buf, core_mp->TxDeltaMax, core_mp->TxDeltaMin, f, csv);
                
                printk(" %s ", "RX Max Hold");
                sprintf(csv,  "  %s ", "Max Hold");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                printk("\n");
                sprintf(csv, "\n");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
    
                print_cdc_data(core_mp->rx_max_buf, core_mp->RxDeltaMax, core_mp->RxDeltaMin, f, csv);
    
                printk(" %s ", "RX Min Hold");
                sprintf(csv,  "  %s ", "Min Hold");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                printk("\n");
                sprintf(csv, "\n");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);   
    
                print_cdc_data(core_mp->rx_min_buf, core_mp->RxDeltaMax, core_mp->RxDeltaMin, f, csv);
            }
            else
            {
                printk(" %s ", "Max Hold");
                sprintf(csv,  "  %s ", "Max Hold");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                printk("\n");
                sprintf(csv, "\n");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
    
                print_cdc_data(tItems[i].max_buf, tItems[i].max, tItems[i].min, f, csv);
    
                printk(" %s ", "Min Hold");
                sprintf(csv,  "  %s ", "Min Hold");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                printk("\n");
                sprintf(csv, "\n");
                f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);   
    
                print_cdc_data(tItems[i].min_buf, tItems[i].max, tItems[i].min, f, csv);

                // printk(" %s ", "Frame 1");
                // sprintf(csv,  "  %s ", "Frame 1");
                // f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);
                // printk("\n");
                // sprintf(csv, "\n");
                // f->f_op->write(f, csv, strlen(csv) * sizeof(char), &f->f_pos);   
    
                // print_cdc_data(tItems[i].buf, tItems[i].max, tItems[i].min, f, csv);
            }
        }
    }

    filp_close(f, NULL);
fail_open:
    set_fs(fs);
    kfree(csv);
    return;    
}
EXPORT_SYMBOL(core_mp_show_result);

void core_mp_run_test(void)
{
    int i = 0;

    /* update X/Y channel length if they're changed */
	core_mp->xch_len = core_config->tp_info->nXChannelNum;
    core_mp->ych_len = core_config->tp_info->nYChannelNum;

    /* update key's length if they're changed */
    core_mp->key_len = core_config->tp_info->nKeyCount;
        
    /* compute the total length in one frame */
    core_mp->frame_len = core_mp->xch_len * core_mp->ych_len;
    
	for(i = 0; i < ARRAY_SIZE(tItems); i++)
	{
        if(tItems[i].run)
        {
            DBG_INFO("Runing Test Item : %s \n", tItems[i].desp);
            tItems[i].do_test(i);            
        }
    }
}
EXPORT_SYMBOL(core_mp_run_test);

int core_mp_move_code(void)
{
    DBG_INFO("Prepaing to enter Test Mode \n");

    if(core_config_check_cdc_busy() < 0)
    {
       DBG_ERR("Check busy is timout ! Enter Test Mode failed\n");
       return -1;
    }

    if(core_config_ice_mode_enable() < 0)
    {
        DBG_ERR("Failed to enter ICE mode\n");
        return -1;
    }

    /* DMA Trigger */
    core_config_ice_mode_write(0x41010, 0xFF, 1);

    mdelay(30);

    /* Code reset */
    core_config_ice_mode_write(0x40040, 0xAE, 1);

    core_config_ice_mode_disable();

    if(core_config_check_cdc_busy() < 0)
    {
        DBG_ERR("Check busy is timout ! Enter Test Mode failed\n");
        return - 1;
    }

    DBG_INFO("FW Test Mode ready \n");
    return 0;
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
