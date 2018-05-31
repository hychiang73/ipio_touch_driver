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
#include "parser.h"

#define EXEC_READ  0
#define EXEC_WRITE 1

#define INT_CHECK 0
#define POLL_CHECK 1
#define DELAY_CHECK 2

#define CSV_FILE_SIZE   (400 * 1024)

#define Mathabs(x) ({						\
		long ret;							\
		if (sizeof(x) == sizeof(long)) {	\
		long __x = (x);						\
		ret = (__x < 0) ? -__x : __x;		\
		} else {							\
		int __x = (x);						\
		ret = (__x < 0) ? -__x : __x;		\
		}									\
		ret;								\
	})

#define DUMP(level, fmt, arg...)		\
	do {								\
		if(level & ipio_debug_level)	\
		printk(fmt, ##arg);				\
	} while (0)

enum mp_test_catalog {
	MUTUAL_TEST = 0,
	SELF_TEST = 1,
	KEY_TEST = 2,
	ST_TEST = 3,
	TX_RX_DELTA = 4,
	UNTOUCH_P2P = 5,
	PIXEL = 6,
	OPEN_TEST = 7,
	PEAK_TO_PEAK_TEST = 8,	
	SHORT_TEST = 9,		
};

/* You must declare a new test in this struct before running a new process of mp test */
struct mp_test_items tItems[] = {
	{"mutual_dac", "Calibration Data(DAC)", "FAIL", MUTUAL_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"mutual_bg", "Baseline Data(BG)", "FAIL", MUTUAL_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"mutual_signal", "Untouch Signal Data(BG-Raw-4096) - Mutual", "FAIL", MUTUAL_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"mutual_no_bk", "Raw Data(No BK)", "FAIL", MUTUAL_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"mutual_has_bk", "Raw Data(Have BK)", "FAIL", MUTUAL_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"mutual_has_bk_lcm_off", "Raw Data(Have BK)(LCM OFF)", "FAIL", MUTUAL_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"mutual_bk_dac", "Manual BK Data(Mutual)", "FAIL", MUTUAL_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},

	{"self_dac", "Calibration Data(DAC) - Self", "FAIL", SELF_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"self_bg", "Baselin Data(BG,Self_Tx,Self_Rx)", "FAIL", SELF_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"self_signal", "Untouch Signal Data(BG–Raw-4096) - Self", "FAIL", SELF_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"self_no_bk", "Raw Data(No BK) - Self", "FAIL", SELF_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"self_has_bk", "Raw Data(Have BK) - Self", "FAIL", SELF_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"self_bk_dac", "Manual BK DAC Data(Self_Tx,Self_Rx)", "FAIL", SELF_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},

	{"key_dac", "Calibration Data(DAC/ICON)", "FAIL", KEY_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"key_bg", "Key Baseline Data", "FAIL", KEY_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"key_no_bk", "Key Raw Data", "FAIL", KEY_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"key_has_bk", "Key Raw BK DAC", "FAIL", KEY_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"key_open", "Key Raw Open Test", "FAIL", KEY_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"key_short", "Key Raw Short Test", "FAIL", KEY_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},

	{"st_dac", "ST Calibration Data(DAC)", "FAIL", ST_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"st_bg", "ST Baseline Data(BG)", "FAIL", ST_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"st_no_bk", "ST Raw Data(No BK)", "FAIL", ST_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"st_has_bk", "ST Raw(Have BK)", "FAIL", ST_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"st_open", "ST Open Data", "FAIL", ST_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},

	{"tx_short", "Tx Short Test", "FAIL", MUTUAL_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"rx_short", "Short Test -ILI9881", "FAIL", SHORT_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"rx_open", "RX Open", "FAIL", MUTUAL_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},

	{"cm_data", "Untouch Cm Data", "FAIL", MUTUAL_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"cs_data", "Untouch Cs Data", "FAIL", MUTUAL_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},

	{"tx_rx_delta", "Tx/Rx Delta", "FAIL", TX_RX_DELTA, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},

	{"p2p", "Untouch Peak to Peak", "FAIL", UNTOUCH_P2P, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},

	{"pixel_no_bk", "Pixel Raw (No BK)", "FAIL", PIXEL, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"pixel_has_bk", "Pixel Raw (Have BK)", "FAIL", PIXEL, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},

	{"open_integration", "Open Test(integration)", "FAIL", OPEN_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"open_cap", "Open Test(Cap)", "FAIL", OPEN_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"noise_peak_to_peak_ic", "Noise Peak to Peak(IC Only)", "FAIL", PEAK_TO_PEAK_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"noise_peak_to_peak_ic_lcm_off", "Noise Peak to Peak(IC Only)(LCM OFF)", "FAIL", PEAK_TO_PEAK_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"noise_peak_to_peak_panel", "Noise Peak To Peak(With Panel)", "FAIL", PEAK_TO_PEAK_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},

	{"doze_raw", "Doze Raw Data", "FAIL", MUTUAL_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
	{"doze_p2p", "Doze Peak To Peak", "FAIL", MUTUAL_TEST, 0x0, false, 0, 0, 0, 0, 0, NULL, NULL, NULL, NULL, NULL, NULL, NULL},
};

int32_t *frame_buf = NULL;
int32_t *key_buf = NULL;
struct core_mp_test_data *core_mp = NULL;

static void dump_data(void *data, int type, int len)
{
	int i;
	uint8_t *p8 = NULL;
	int32_t *p32 = NULL;

	if (ipio_debug_level & DEBUG_MP_TEST) {
		if (data == NULL) {
			ipio_err("The data going to dump is NULL\n");
			return;
		}

		printk("\n  Original Data:\n");

		if (type == 8)
			p8 = (uint8_t *) data;
		if (type == 32)
			p32 = (int32_t *) data;

		for (i = 0; i < len; i++) {
			if (type == 8)
				printk(" %4x ", p8[i]);
			else if (type == 32)
				printk(" %4x ", p32[i]);

			if ((i % 32) == 0)
				printk("\n");
		}
		printk("\n\n");
	}
}

static void dump_benchmark_data(int32_t* max_ptr, int32_t* min_ptr)
{
	int i;

	if (ipio_debug_level & DEBUG_MP_TEST) {
		ipio_info("benchmark max\n");

		for(i = 0; i < core_mp->frame_len; i++) {
			printk("%d, ", max_ptr[i]);
			if(i % core_mp->xch_len == core_mp->xch_len - 1)
				printk("\n");
		}

		ipio_info("benchmark min\n");

		for(i = 0; i < core_mp->frame_len; i++) {
			printk("%d, ", min_ptr[i]);
			if(i % core_mp->xch_len == core_mp->xch_len - 1)
				printk("\n");
		}
	}
}

static void print_benchmark_cdc_data(int index, bool max, bool tx, char *csv, int *csv_len)
{
	int x, y, tmp_len = *csv_len;
	int  mp_result = 1;
	char *line_breaker = "\n";
	int32_t *tmp = NULL;

	pr_info("%s\n", tItems[index].desp);
	tmp_len += sprintf(csv + tmp_len, " %s ", tItems[index].desp);

	pr_info("\n");
	tmp_len += sprintf(csv + tmp_len, "%s", line_breaker);              

	pr_info("Frame count = %d\n",tItems[index].frame_count);
	tmp_len += sprintf(csv + tmp_len, "Frame count = %d", tItems[index].frame_count);

	pr_info("\n");
	tmp_len += sprintf(csv + tmp_len, "%s", line_breaker);                                                

	DUMP(DEBUG_MP_TEST, " %s \n", "Bench mark data");
	tmp_len += sprintf(csv + tmp_len, "Bench mark data\n");

	tmp = tItems[index].buf;
	/* print X raw only */
	for (x = 0; x < core_mp->xch_len; x++) {
		if (x == 0) {
			DUMP(DEBUG_MP_TEST, "           ");
			tmp_len += sprintf(csv + tmp_len, ",");
		}

		DUMP(DEBUG_MP_TEST, "X%02d      ", x);
		tmp_len += sprintf(csv + tmp_len, "X%02d, ", x);
	}

	DUMP(DEBUG_MP_TEST, "\n");
	tmp_len += sprintf(csv + tmp_len, "%s", line_breaker);

	for (y = 0; y < core_mp->ych_len; y++) {
		DUMP(DEBUG_MP_TEST, " Y%02d ", y);
		tmp_len += sprintf(csv + tmp_len, "Y%02d, ", y);

		for (x = 0; x < core_mp->xch_len; x++) {
			int shift = y * core_mp->xch_len + x;

			if (tmp[shift] <= tItems[index].bench_mark_max[shift] && tmp[shift] >=  tItems[index].bench_mark_min[shift]) {
				DUMP(DEBUG_MP_TEST, " %7d ", tmp[shift]);
				tmp_len += sprintf(csv + tmp_len, " %7d, ", tmp[shift]);
			} else {
				if (tmp[shift] > tItems[index].bench_mark_max[shift]) {
					DUMP(DEBUG_MP_TEST, " *%7d ", tmp[shift]);
					tmp_len += sprintf(csv + tmp_len, "*%7d,", tmp[shift]);
				} else {
					DUMP(DEBUG_MP_TEST, " #%7d ", tmp[shift]);
					tmp_len += sprintf(csv + tmp_len, "#%7d,", tmp[shift]);
				}
				mp_result = -1;
			}
		}

		DUMP(DEBUG_MP_TEST, "\n");
		tmp_len += sprintf(csv + tmp_len, "%s", line_breaker);
	}

	if (mp_result < 0)
		sprintf(tItems[index].result, "%s", "FAIL");
	else
		sprintf(tItems[index].result, "%s", "PASS");


	pr_info("\n Result : %s\n", tItems[index].result);
	tmp_len += sprintf(csv + tmp_len, " Result : %s ", tItems[index].result);

	pr_info("\n");
	tmp_len += sprintf(csv + tmp_len, "%s", line_breaker);

	if (strcmp(tItems[index].result, "FAIL") == 0)
		core_mp->final_result = false;

	tmp_len += sprintf(csv + tmp_len, "%s", line_breaker);        

	*csv_len = tmp_len;
        
}

static void print_cdc_data(int index, bool max, bool tx, char *csv, int *csv_len)
{
	int x, y, tmp_len = *csv_len;
	int max_ts, min_ts, mp_result = 1;
	char *line_breaker = "\n";
	int32_t *tmp = NULL;

	if (tItems[index].catalog == TX_RX_DELTA) {
		if (tx) {
			if (max)
				tmp = core_mp->tx_max_buf;
			else
				tmp = core_mp->tx_min_buf;

			max_ts = core_mp->TxDeltaMax;
			min_ts = core_mp->TxDeltaMin;
		} else {
			if (max)
				tmp = core_mp->rx_max_buf;
			else
				tmp = core_mp->rx_min_buf;

			max_ts = core_mp->RxDeltaMax;
			min_ts = core_mp->RxDeltaMin;
		}
	} else {
		if (max)
			tmp = tItems[index].max_buf;
		else
			tmp = tItems[index].min_buf;

		max_ts = tItems[index].max;
		min_ts = tItems[index].min;
	}

	/* print X raw only */
	for (x = 0; x < core_mp->xch_len; x++) {
		if (x == 0) {
			DUMP(DEBUG_MP_TEST, "           ");
			tmp_len += sprintf(csv + tmp_len, ",");
		}

		DUMP(DEBUG_MP_TEST, "X%02d      ", x);
		tmp_len += sprintf(csv + tmp_len, "X%02d, ", x);
	}

	DUMP(DEBUG_MP_TEST, "\n");
	tmp_len += sprintf(csv + tmp_len, "%s", line_breaker);

	for (y = 0; y < core_mp->ych_len; y++) {
		DUMP(DEBUG_MP_TEST, " Y%02d ", y);
		tmp_len += sprintf(csv + tmp_len, "Y%02d, ", y);

		for (x = 0; x < core_mp->xch_len; x++) {
			int shift = y * core_mp->xch_len + x;

			if (tmp[shift] <= max_ts && tmp[shift] >= min_ts) {
				DUMP(DEBUG_MP_TEST, " %7d ", tmp[shift]);
				tmp_len += sprintf(csv + tmp_len, " %7d, ", tmp[shift]);
			} else {
				if (tmp[shift] > max_ts) {
					DUMP(DEBUG_MP_TEST, " *%7d ", tmp[shift]);
					tmp_len += sprintf(csv + tmp_len, "*%7d,", tmp[shift]);
				} else {
					DUMP(DEBUG_MP_TEST, " #%7d ", tmp[shift]);
					tmp_len += sprintf(csv + tmp_len, "#%7d,", tmp[shift]);
				}
				mp_result = -1;
			}
		}

		DUMP(DEBUG_MP_TEST, "\n");
		tmp_len += sprintf(csv + tmp_len, "%s", line_breaker);
	}

	if (mp_result < 0)
		sprintf(tItems[index].result, "%s", "FAIL");
	else
		sprintf(tItems[index].result, "%s", "PASS");

	*csv_len = tmp_len;
}

static int create_mp_test_frame_buffer(int index)
{
	ipio_debug(DEBUG_MP_TEST, "Create MP frame buffers (index = %d)\n",index);

	if (tItems[index].catalog == TX_RX_DELTA) {
		core_mp->tx_delta_buf = kcalloc(core_mp->frame_len, sizeof(int32_t), GFP_KERNEL);
		core_mp->rx_delta_buf = kcalloc(core_mp->frame_len, sizeof(int32_t), GFP_KERNEL);

		core_mp->tx_max_buf = kcalloc(core_mp->frame_len, sizeof(int32_t), GFP_KERNEL);
		core_mp->tx_min_buf = kcalloc(core_mp->frame_len, sizeof(int32_t), GFP_KERNEL);

		core_mp->rx_max_buf = kcalloc(core_mp->frame_len, sizeof(int32_t), GFP_KERNEL);
		core_mp->rx_min_buf = kcalloc(core_mp->frame_len, sizeof(int32_t), GFP_KERNEL);

		if (ERR_ALLOC_MEM(core_mp->tx_delta_buf) || ERR_ALLOC_MEM(core_mp->rx_delta_buf)) {
			ipio_err("Failed to allocate Tx/Rx Delta buffer\n");
			return -ENOMEM;
		}

		if (ERR_ALLOC_MEM(core_mp->tx_max_buf) || ERR_ALLOC_MEM(core_mp->tx_min_buf)) {
			ipio_err("Failed to allocate Tx Max/Min buffer\n");
			return -ENOMEM;
		}

		if (ERR_ALLOC_MEM(core_mp->rx_max_buf) || ERR_ALLOC_MEM(core_mp->rx_min_buf)) {
			ipio_err("Failed to allocate Rx Max/Min buffe\n");
			return -ENOMEM;
		}

	} else {
		tItems[index].buf = kcalloc(core_mp->frame_len, sizeof(int32_t), GFP_KERNEL);
		tItems[index].max_buf = kcalloc(core_mp->frame_len, sizeof(int32_t), GFP_KERNEL);
		tItems[index].min_buf = kcalloc(core_mp->frame_len, sizeof(int32_t), GFP_KERNEL);
		
		if (tItems[index].spec_option == BENCHMARK) {
			tItems[index].bench_mark_max = kcalloc(core_mp->frame_len, sizeof(int32_t), GFP_KERNEL);
			tItems[index].bench_mark_min = kcalloc(core_mp->frame_len, sizeof(int32_t), GFP_KERNEL);          
			if (ERR_ALLOC_MEM(tItems[index].bench_mark_max) || ERR_ALLOC_MEM(tItems[index].bench_mark_min)){
				ipio_err("Failed to allocate bench_mark FRAME buffer\n");
				return -ENOMEM;
			}                                    
		}                                                                        
                                                                                        
		if (ERR_ALLOC_MEM(tItems[index].buf) || ERR_ALLOC_MEM(tItems[index].max_buf) ||
				ERR_ALLOC_MEM(tItems[index].min_buf)) {
			ipio_err("Failed to allocate FRAME buffer\n");
			return -ENOMEM;
		}
	}

	return 0;
}

static int mp_cdc_init_cmd_common(uint8_t *cmd, int len, int index)
{
	int ret = 0;

	cmd[0] = protocol->cmd_cdc;
	cmd[1] = tItems[index].cmd;
	cmd[2] = 0;

	if (strcmp(tItems[index].name, "open_integration") == 0)
		cmd[2] = 0x2;
	if (strcmp(tItems[index].name, "open_cap") == 0)
		cmd[2] = 0x3;

	if(tItems[index].catalog == PEAK_TO_PEAK_TEST) {
		cmd[2] = ((tItems[index].frame_count & 0xff00) >> 8);
		cmd[3] = tItems[index].frame_count & 0xff;
		cmd[4] = 0;

		if (strcmp(tItems[index].name, "noise_peak_to_peak_cut") == 0)
			cmd[4] = 0x1;

		ipio_debug(DEBUG_MP_TEST, "P2P CMD: %d,%d,%d,%d,%d\n",
				cmd[0],cmd[1],cmd[2],cmd[3],cmd[4]);
	}

	return ret;
}

static int mp_cdc_init_cmd_p2p_ic(uint8_t *cmd, int len, int index)
{
	int i, ret = -1;

	ipio_info("cmd = %p, len = %d, tdf = %d\n", cmd, len, core_mp->tdf);

	if (cmd == NULL) {
		ipio_err("CMD is invalid\n");
		goto out;
	}

	if (len != 15) {
		ipio_err("The length of cdc command must be 15\n");
		goto out;
	}

	/* TODO read cmds from ini */
	cmd[0] = protocol->cmd_cdc;
	cmd[1] = protocol->peak_to_peak;
	cmd[2] = 0x1;
	cmd[3] = 0xFF;
	cmd[4] = (core_mp->tdf >> 8) && 0xFF;//tdf_h
	cmd[5] = core_mp->tdf & 0xFF; //tdf_l
	cmd[6] = 3; //NODP
	cmd[7] = 0xFF;
	cmd[8] = 0xFF;
	cmd[9] = 0xFF;
	cmd[10] = 0xFF;
	cmd[11] = 0xFF;
	cmd[12] = 0xFF;
	cmd[13] = 0xFF;
	cmd[14] = 0xFF;

	for (i = 0; i < protocol->cdc_len; i++) {
		ipio_info("P2P IC: cmd[%d] = %x\n",i,cmd[i]);
	}

	ret = 0;
out:
	return ret;
}

static int mp_cdc_init_cmd_p2p_panel(uint8_t *cmd, int len, int index)
{
	int i, ret = -1;

	ipio_info("cmd = %p, len = %d, tdf = %d\n", cmd, len, core_mp->tdf);

	if (cmd == NULL) {
		ipio_err("CMD is invalid\n");
		goto out;
	}

	if (len != 15) {
		ipio_err("The length of cdc command must be 15\n");
		goto out;
	}

	/* TODO read cmds from ini */
	cmd[0] = protocol->cmd_cdc;
	cmd[1] = protocol->peak_to_peak;
	cmd[2] = 0x0;
	cmd[3] = 0xFF;
	cmd[4] = (core_mp->tdf >> 8) && 0xFF;
	cmd[5] = core_mp->tdf & 0xFF;
	cmd[6] = 3; //NODP
	cmd[7] = 0xFF;
	cmd[8] = 0xFF;
	cmd[9] = 0xFF;
	cmd[10] = 0xFF;
	cmd[11] = 0xFF;
	cmd[12] = 0xFF;
	cmd[13] = 0xFF;
	cmd[14] = 0xFF;

	for (i = 0; i < protocol->cdc_len; i++) {
		ipio_info("P2P IC: cmd[%d] = %x\n",i,cmd[i]);
	}

	ret = 0;
out:
	return ret;
}

static int mp_cdc_init_cmd_dac(uint8_t *cmd, int len, int index)
{
	int i, ret = -1;

	ipio_info("cmd = %p, len = %d, tdf = %d\n", cmd, len, core_mp->tdf);

	if (protocol->major >= 5 && protocol->mid < 4) {
		ipio_info("version is lower than 5.4.0, running common one\n");
		return mp_cdc_init_cmd_common(cmd, len, index);
	}

	if (cmd == NULL) {
		ipio_err("CMD is invalid\n");
		goto out;
	}

	if (len != 15) {
		ipio_err("The length of cdc command must be 15\n");
		goto out;
	}

	/* TODO read cmds from ini */
	cmd[0] = protocol->cmd_cdc;
	cmd[1] = protocol->mutual_dac;
	cmd[2] = 0;
	cmd[3] = (core_mp->tdf >> 8) && 0xFF;
	cmd[4] = core_mp->tdf & 0xFF;
	cmd[5] = 3; //NODP
	cmd[6] = 0xFF;
	cmd[7] = 0xFF;
	cmd[8] = 0xFF;
	cmd[9] = 0xFF;
	cmd[10] = 0xFF;
	cmd[11] = 0xFF;
	cmd[12] = 0xFF;
	cmd[13] = 0xFF;
	cmd[14] = 0xFF;

	for (i = 0; i < protocol->cdc_len; i++) {
		ipio_info("DAC: cmd[%d] = %x\n",i,cmd[i]);
	}

	ret = 0;
out:
	return ret;
}

static int mp_cdc_init_cmd_raw_bk(uint8_t *cmd, int len, int index)
{
	int i, ret = -1;

	ipio_info("cmd = %p, len = %d, tdf = %d\n", cmd, len, core_mp->tdf);

	if (protocol->major >= 5 && protocol->mid < 4) {
		ipio_info("version is lower than 5.4.0, running common one\n");
		return mp_cdc_init_cmd_common(cmd, len, index);
	}

	if (cmd == NULL) {
		ipio_err("CMD is invalid\n");
		goto out;
	}

	if (len != 15) {
		ipio_err("The length of cdc command must be 15\n");
		goto out;
	}

	/* TODO read cmds from ini */
	cmd[0] = protocol->cmd_cdc;
	cmd[1] = protocol->mutual_has_bk;
	cmd[2] = 0;
	cmd[3] = (core_mp->tdf >> 8) && 0xFF;
	cmd[4] = core_mp->tdf & 0xFF;
	cmd[5] = 3; //NODP
	cmd[6] = 0xFF;
	cmd[7] = 0xFF;
	cmd[8] = 0xFF;
	cmd[9] = 0xFF;
	cmd[10] = 0xFF;
	cmd[11] = 0xFF;
	cmd[12] = 0xFF;
	cmd[13] = 0xFF;
	cmd[14] = 0xFF;

	for (i = 0; i < protocol->cdc_len; i++){
		ipio_info("RAW BK: cmd[%d] = %x\n",i,cmd[i]);
	}

	ret = 0;
out:
	return ret;
}

static int mp_cdc_init_cmd_raw_no_bk(uint8_t *cmd, int len, int index)
{
	int i, ret = -1;

	ipio_info("cmd = %p, len = %d, tdf = %d\n",cmd ,len, core_mp->tdf);

	if (protocol->major >= 5 && protocol->mid < 4) {
		ipio_info("version is lower than 5.4.0, running common one\n");
		return mp_cdc_init_cmd_common(cmd, len, index);
	}

	if (cmd == NULL) {
		ipio_err("CMD is invalid\n");
		goto out;
	}

	if (len != 15) {
		ipio_err("The length of cdc command must be 15\n");
		goto out;
	}

	/* TODO read cmds from ini */
	cmd[0] = protocol->cmd_cdc;
	cmd[1] = protocol->mutual_no_bk;
	cmd[2] = 0;
	cmd[3] = (core_mp->tdf >> 8) && 0xFF;
	cmd[4] = core_mp->tdf & 0xFF;
	cmd[5] = 3; //NODP
	cmd[6] = 0x6;
	cmd[7] = 0xFF;
	cmd[8] = 0xFF;
	cmd[9] = 0xFF;
	cmd[10] = 0xFF;
	cmd[11] = 0xFF;
	cmd[12] = 0xFF;
	cmd[13] = 0xFF;
	cmd[14] = 0xFF;


	for (i = 0; i < protocol->cdc_len; i++) {
		ipio_info("RAW NO BK: cmd[%d] = %x\n",i,cmd[i]);
	}

	ret = 0;
out:
	return ret;
}

static int mp_cdc_init_cmd_doze_p2p(uint8_t *cmd, int len, int index)
{
	int i, ret = -1;

	ipio_info("cmd = %p, len = %d, tdf = %d\n", cmd, len, core_mp->tdf);

	if (cmd == NULL) {
		ipio_err("CMD is invalid\n");
		goto out;
	}

	if (len != 15) {
		ipio_err("The length of cdc command must be 15\n");
		goto out;
	}

	/* TODO read cmds from ini */
	cmd[0] = protocol->cmd_cdc;
	cmd[1] = protocol->doze_p2p;
	cmd[2] = 0;
	//cmd[3] = (core_mp->tdf >> 8) && 0xFF;
	//cmd[4] = core_mp->tdf & 0xFF;
	cmd[3] = 0xFF;
	cmd[4] = 0xFF;
	cmd[5] = 0xFF; //NODP
	cmd[6] = 0x6;
	cmd[7] = 0xFF;
	cmd[8] = 0xFF;
	cmd[9] = 0xFF;
	cmd[10] = 0xFE;
	cmd[11] = 0xFF;
	cmd[12] = 0xFF;
	cmd[13] = 0xFF;
	cmd[14] = 0xFF;


	for (i = 0; i < protocol->cdc_len; i++){
		ipio_info("DOZE P2P: cmd[%d] = %x\n",i,cmd[i]);
	}

	ret = 0;
out:
	return ret;
}

static int mp_cdc_init_cmd_doze_raw(uint8_t *cmd, int len, int index)
{
	int i, ret = -1;

	ipio_info("cmd = %p, len = %d, tdf = %d\n", cmd, len, core_mp->tdf);

	if (cmd == NULL) {
		ipio_err("CMD is invalid\n");
		goto out;
	}

	if (len != 15) {
		ipio_err("The length of cdc command must be 15\n");
		goto out;
	}

	/* TODO read cmds from ini */
	cmd[0] = protocol->cmd_cdc;
	cmd[1] = protocol->doze_raw;
	cmd[2] = 0;
	//cmd[3] = (core_mp->tdf >> 8) && 0xFF;
	//cmd[4] = core_mp->tdf & 0xFF;
	cmd[3] = 0xFF;
	cmd[4] = 0xFF;
	cmd[5] = 0xFF; //NODP
	cmd[6] = 0xFE;
	cmd[7] = 0xFF;
	cmd[8] = 0xFF;
	cmd[9] = 0xFF;
	cmd[10] = 0xFF;
	cmd[11] = 0xFF;
	cmd[12] = 0xFF;
	cmd[13] = 0xFF;
	cmd[14] = 0xFF;


	for (i = 0; i < protocol->cdc_len; i++){
		ipio_info("DOZE RAW: cmd[%d] = %x\n",i,cmd[i]);
	}

	ret = 0;
out:
	return ret;
}

static int mp_cdc_init_cmd_short(uint8_t *cmd, int len, int index)
{
	int i, ret = -1;

	ipio_info("cmd = %p, len = %d\n",cmd,len);

	if (protocol->major >= 5 && protocol->mid < 4) {
		ipio_info("version is lower than 5.4.0, running common one\n");
		return mp_cdc_init_cmd_common(cmd, len, index);
	}

	if (cmd == NULL) {
		ipio_err("CMD is invalid\n");
		goto out;
	}

	if (len != 15) {
		ipio_err("The length of cdc command must be 15\n");
		goto out;
	}

	/* TODO read cmds from ini */
	cmd[0] = protocol->cmd_cdc;
	cmd[1] = protocol->rx_short;
	cmd[2] = 0;
	cmd[3] = 0; //TDF_1 H
	cmd[4] = 0x8C;//TDF_1 L
	cmd[5] = 0; //TDF_2 H
	cmd[6] = 0x40;//TDF_2 L
	cmd[7] = 0x09;//NODP,Drop NUM
	cmd[8] = 0xFF;
	cmd[9] = 0xFF;
	cmd[10] = 0xFF;
	cmd[11] = 0xFF;
	cmd[12] = 0xFF;
	cmd[13] = 0xFF;
	cmd[14] = 0xFF;


	for (i = 0; i < protocol->cdc_len; i++){
		ipio_info("Short: cmd[%d] = %x\n",i,cmd[i]);
	}

	ret = 0;
out:
	return ret;
}

// static void mp_cdc_init_cmd_open(void)
// {

// }

static int mp_ctrl_lcd_status(bool on)
{
	int ret = 0, ctrl = 0, delay = 0;
	uint8_t lcd[15] = {0};
	uint8_t header = 0x0F;

	memset(&lcd, 0xFF, ARRAY_SIZE(lcd));

	ctrl = ((on) ? 1 : 2);
	delay = ((on) ? 100 : 10);

	lcd[0] = header;
	lcd[1] = protocol->mutual_bg;
	lcd[2] = 0;
	lcd[3] = ctrl;

	ipio_info("LCD command: delay time = %d \n", delay);
	dump_data(lcd, 8, ARRAY_SIZE(lcd));

	ret = core_write(core_config->slave_i2c_addr, lcd, ARRAY_SIZE(lcd));
	if (ret < 0) {
		ipio_err("Failed to write LCD command\n");
		goto out;
	}

	mdelay(delay);

out:
	return ret;
}

static int mp_calc_long_v_nodp(int index)
{
	int nodp = 0;


	ipio_info("Long V: nodp = %d\n",nodp);
	return 0;
}

static void mp_calc_nodp(bool long_v)
{
	uint8_t at, phase;
	uint8_t r2d, rst, rst_back, dac_td, qsh_pw, qsh_td, dn;
	uint16_t tshd, qsh_tdf, dp2tp, twc, twcm, tp2dp;
	uint16_t multi_term_num, tp_tsdh_wait, ddi_width;
	uint32_t real_tx_dura, nodp, ftw, tp_width, txpw;

	ipio_info("DDI Mode = %d\n", long_v);

	tshd = core_mp->nodp.tshd;
	qsh_tdf = core_mp->nodp.qsh_tdf;
	at = core_mp->nodp.auto_trim;
	dp2tp = core_mp->nodp.dp_to_tp;
	twc = core_mp->nodp.tx_wait_const;
	twcm = core_mp->nodp.tx_wait_const_multi;
	tp2dp = core_mp->nodp.tp_to_dp;
	phase = core_mp->nodp.phase_adc;
	r2d = core_mp->nodp.r2d_pw;
	rst = core_mp->nodp.rst_pw;
	rst_back = core_mp->nodp.rst_pw_back;
	dac_td = core_mp->nodp.dac_td;
	qsh_pw = core_mp->nodp.qsh_pw;
	qsh_td = core_mp->nodp.qsh_td;
	dn = core_mp->nodp.drop_nodp;

	if (core_mp->nodp.is60HZ) {
		multi_term_num = core_mp->nodp.multi_term_num_60;
		tp_tsdh_wait = core_mp->nodp.tp_tshd_wait_60;
		ddi_width = core_mp->nodp.ddi_width_60;
	} else {
		multi_term_num = core_mp->nodp.multi_term_num_120;
		tp_tsdh_wait = core_mp->nodp.tp_tshd_wait_120;
		ddi_width = core_mp->nodp.ddi_width_120;
	}

	multi_term_num = MIN(multi_term_num, 1);

	if (long_v) {
		// TODO: Long V:
		// Term Real TX duration = ((TSHD duration-AutoTrimVariation)*64-DDI_WIDTH_120*(11)-64-DP2TP-TX_WAIT_CONST-(Phase ADC*64)-((TX_WAIT_CONST_MULTI+Phase ADC*64+TP2DP*64)*(11)))/12
	} else {
		real_tx_dura = (((tshd << 2) - (at << 6) - tp_tsdh_wait - ddi_width * (multi_term_num - 1) - 64 - dp2tp - twc) * 5 - (phase << 5) - ((twcm * 5 + (phase << 5 ) + (tp2dp * 5 << 6)) * (multi_term_num - 1))) / ( multi_term_num * 5 );
	}

	ipio_info("Read Tx Duration = %d\n",real_tx_dura);

	core_mp->nodp.first_tp_width = (dp2tp * 5  + twc * 5  + (phase << 5 ) + real_tx_dura * 5) / 5;
	core_mp->nodp.tp_width = ((tp2dp + phase) * 64 + real_tx_dura);
	core_mp->nodp.txpw = (qsh_tdf + rst + qsh_pw + qsh_td + 2);
	core_mp->nodp.nodp = real_tx_dura / txpw / 2;

	ipio_info("First TP Width = %d\n",core_mp->nodp.first_tp_width);
	ipio_info("TP Width = %d\n",core_mp->nodp.tp_width);
	ipio_info("TXPW = %d\n",core_mp->nodp.txpw);
	ipio_info("NODP = %d\n",core_mp->nodp.nodp);
}

static int mp_calc_timing_nodp(int index)
{
	int i, ret = 0;
	uint8_t test_type = 0x0;
	uint8_t timing_cmd[15] = {0};
	uint8_t get_timing[39] = {0};

	memset(timing_cmd, 0xFF, protocol->cdc_len);

	timing_cmd[0] = protocol->cmd_cdc;
	timing_cmd[1] = protocol->get_timing;
	timing_cmd[2] = test_type;

	/* 
	 * To calculate NODP, we need to get timing parameters first from fw,
	 * which returnes 40 bytes data.
	 */
	ipio_info("Timing command :\n");
	dump_data(timing_cmd, 8, protocol->cdc_len);

	ret = core_write(core_config->slave_i2c_addr, timing_cmd, protocol->cdc_len);
	if (ret < 0) {
		ipio_err("Failed to write timing command\n");
		goto out;
	}

	ret = core_read(core_config->slave_i2c_addr, get_timing, ARRAY_SIZE(get_timing));
	if (ret < 0) {
		ipio_err("Failed to read timing parameters\n");
		goto out;
	}

	for (i = 0; i < ARRAY_SIZE(get_timing); i++) {
		ipio_info("get_timing[%d] = 0x%x\n",i,get_timing[i]);
	}

	/* Combine timing data */
	core_mp->nodp.is60HZ = false; // This will get from ini file by default.
	core_mp->nodp.isLongV = get_timing[2];
	core_mp->nodp.tshd = (get_timing[3] << 8 ) + get_timing[4];
	core_mp->nodp.multi_term_num_120 = get_timing[5];
	core_mp->nodp.multi_term_num_60 = get_timing[6];
	core_mp->nodp.tsvd_to_tshd = (get_timing[7] << 8 ) + get_timing[8];
	core_mp->nodp.qsh_tdf = (get_timing[9] << 8 ) + get_timing[10];
	core_mp->nodp.auto_trim = get_timing[11];
	core_mp->nodp.tp_tshd_wait_120 = (get_timing[12] << 8 ) + get_timing[13];
	core_mp->nodp.ddi_width_120 = (get_timing[14] << 8 ) + get_timing[15];
	core_mp->nodp.tp_tshd_wait_60 = (get_timing[16] << 8 ) + get_timing[17];
	core_mp->nodp.ddi_width_60 = (get_timing[18] << 8 ) + get_timing[19];
	core_mp->nodp.dp_to_tp = (get_timing[20] << 8 ) + get_timing[21];
	core_mp->nodp.tx_wait_const = (get_timing[22] << 8 ) + get_timing[23];
	core_mp->nodp.tx_wait_const_multi = (get_timing[24] << 8 ) + get_timing[25];
	core_mp->nodp.tp_to_dp = (get_timing[26] << 8 ) + get_timing[27];
	core_mp->nodp.phase_adc = get_timing[28];
	core_mp->nodp.r2d_pw = get_timing[29];
	core_mp->nodp.rst_pw = get_timing[30];
	core_mp->nodp.rst_pw_back = get_timing[31];
	core_mp->nodp.dac_td = get_timing[32];
	core_mp->nodp.qsh_pw = get_timing[33];
	core_mp->nodp.qsh_td = get_timing[34];
	core_mp->nodp.drop_nodp = get_timing[35];

	ipio_info("60HZ = %d\n",core_mp->nodp.is60HZ);
	ipio_info("Long V = %d\n",core_mp->nodp.isLongV);
	ipio_info("TSHD = %d\n",core_mp->nodp.tshd);
	ipio_info("Multi Term Num (120Hz) = %d\n",core_mp->nodp.multi_term_num_120);
	ipio_info("Multi Term Num (60Hz) = %d\n",core_mp->nodp.multi_term_num_60);
	ipio_info("TSVD to TSHD = %d\n",core_mp->nodp.tsvd_to_tshd);
	ipio_info("QSH TDF = %d\n",core_mp->nodp.qsh_tdf);
	ipio_info("AutoTrim Variation = %d\n",core_mp->nodp.auto_trim);
	ipio_info("TP TSHD Wait (120Hz) = %d\n",core_mp->nodp.tp_tshd_wait_120);
	ipio_info("DDI Width (120Hz) = %d\n",core_mp->nodp.ddi_width_120);
	ipio_info("TP TSHD Wait (60Hz) = %d\n",core_mp->nodp.tp_tshd_wait_60);
	ipio_info("DDI Width (60Hz) =%d\n",core_mp->nodp.ddi_width_60);
	ipio_info("DP to TP %d\n",core_mp->nodp.dp_to_tp);
	ipio_info("TX Wait Const = %d\n",core_mp->nodp.tx_wait_const);
	ipio_info("TX Wait Const Multi = %d\n",core_mp->nodp.tx_wait_const_multi);
	ipio_info("TP to DP = %d\n",core_mp->nodp.tp_to_dp);
	ipio_info("Phase ADC = %d\n",core_mp->nodp.phase_adc);
	ipio_info("R2D PW = %d\n",core_mp->nodp.r2d_pw);
	ipio_info("RST PW = %d\n",core_mp->nodp.rst_pw);
	ipio_info("RST PW Back = %d\n",core_mp->nodp.rst_pw_back);
	ipio_info("DAC TD = %d\n",core_mp->nodp.dac_td);
	ipio_info("QSH PW = %d\n",core_mp->nodp.qsh_pw);
	ipio_info("QSH TD = %d\n",core_mp->nodp.qsh_td);
	ipio_info("Drop NODP Num = %d\n",core_mp->nodp.drop_nodp);

	mp_calc_nodp(core_mp->nodp.isLongV);

out:
	return ret;
}

static int allnode_key_cdc_data(int index)
{
	int i, res = 0, len = 0;
	int inDACp = 0, inDACn = 0;
	uint8_t cmd[3] = { 0 };
	uint8_t *ori = NULL;

	len = core_mp->key_len * 2;

	ipio_debug(DEBUG_MP_TEST, "Read key's length = %d\n", len);
	ipio_debug(DEBUG_MP_TEST, "core_mp->key_len = %d\n", core_mp->key_len);

	if (len <= 0) {
		ipio_err("Length is invalid\n");
		res = -1;
		goto out;
	}

	/* CDC init */
	cmd[0] = protocol->cmd_cdc;
	cmd[1] = tItems[index].cmd;
	cmd[2] = 0;

	res = core_write(core_config->slave_i2c_addr, cmd, 3);
	if (res < 0) {
		ipio_err("I2C Write Error while initialising cdc\n");
		goto out;
	}

	mdelay(1);

	/* Check busy */
	if (core_config_check_cdc_busy(50) < 0) {
		ipio_err("Check busy is timout !\n");
		res = -1;
		goto out;
	}

	mdelay(1);

	/* Prepare to get cdc data */
	cmd[0] = protocol->cmd_read_ctrl;
	cmd[1] = protocol->cmd_get_cdc;

	res = core_write(core_config->slave_i2c_addr, cmd, 2);
	if (res < 0) {
		ipio_err("I2C Write Error\n");
		goto out;
	}

	mdelay(1);

	res = core_write(core_config->slave_i2c_addr, &cmd[1], 1);
	if (res < 0) {
		ipio_err("I2C Write Error\n");
		goto out;
	}

	/* Allocate a buffer for the original */
	ori = kcalloc(len, sizeof(uint8_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(ori)) {
		ipio_err("Failed to allocate ori mem (%ld)\n", PTR_ERR(ori));
		goto out;
	}

	mdelay(1);

	/* Get original frame(cdc) data */
	res = core_read(core_config->slave_i2c_addr, ori, len);
	if (res < 0) {
		ipio_err("I2C Read Error while getting original cdc data\n");
		goto out;
	}

	dump_data(ori, 8, len);

	if (key_buf == NULL) {
		key_buf = kcalloc(core_mp->key_len, sizeof(int32_t), GFP_KERNEL);
		if (ERR_ALLOC_MEM(key_buf)) {
			ipio_err("Failed to allocate FrameBuffer mem (%ld)\n", PTR_ERR(key_buf));
			goto out;
		}
	} else {
		memset(key_buf, 0x0, core_mp->key_len);
	}

	/* Convert original data to the physical one in each node */
	for (i = 0; i < core_mp->frame_len; i++) {
		if (tItems[index].cmd == protocol->key_dac) {
			/* DAC - P */
			if (((ori[(2 * i) + 1] & 0x80) >> 7) == 1) {
				/* Negative */
				inDACp = 0 - (int)(ori[(2 * i) + 1] & 0x7F);
			} else {
				inDACp = ori[(2 * i) + 1] & 0x7F;
			}

			/* DAC - N */
			if (((ori[(1 + (2 * i)) + 1] & 0x80) >> 7) == 1) {
				/* Negative */
				inDACn = 0 - (int)(ori[(1 + (2 * i)) + 1] & 0x7F);
			} else {
				inDACn = ori[(1 + (2 * i)) + 1] & 0x7F;
			}

			key_buf[i] = (inDACp + inDACn) / 2;
		}
	}

	dump_data(key_buf, 32, core_mp->frame_len);

out:
	ipio_kfree((void **)&ori);
	return res;
}

static int allnode_mutual_cdc_data(int index)
{
	static int i = 0, res = 0, len = 0;
	int inDACp = 0, inDACn = 0;
	static uint8_t cmd[15] = {0};
	uint8_t *ori = NULL;

	/* Multipling by 2 is due to the 16 bit in each node */
	len = (core_mp->xch_len * core_mp->ych_len * 2) + 2;

	ipio_debug(DEBUG_MP_TEST, "Read X/Y Channel length = %d\n", len);
	ipio_debug(DEBUG_MP_TEST, "core_mp->frame_len = %d\n", core_mp->frame_len);

	if (len <= 2) {
		ipio_err("Length is invalid\n");
		res = -1;
		goto out;
	}

	memset(cmd, 0xFF, protocol->cdc_len);

	/* CDC init */
	if (protocol->major >= 5 && protocol->mid >= 4) {
		res = mp_calc_timing_nodp(index);
		if(res < 0) {
			ipio_err("Failed to get timing parameters\n");
			goto out;
		}
	}

	res = tItems[index].get_cdc_init_cmd(cmd, protocol->cdc_len, index);
	if (res < 0) {
		ipio_err("Failed to get cdc init command\n");
		goto out;
	}

	dump_data(cmd, 8, protocol->cdc_len);

	res = core_write(core_config->slave_i2c_addr, cmd, protocol->cdc_len);
	if (res < 0) {
		ipio_err("I2C Write Error while initialising cdc\n");
		goto out;
	}

	/* Check busy */
	ipio_info("Check busy method = %d\n",core_mp->busy_cdc);
	if (core_mp->busy_cdc == POLL_CHECK) {
		res = core_config_check_cdc_busy(50);
	} else if (core_mp->busy_cdc == INT_CHECK) {
		res = core_config_check_int_status(false);
	} else if (core_mp->busy_cdc == DELAY_CHECK) {
		mdelay(600);	
	}

	if (res < 0) {
		ipio_err("Check busy is timout !\n");
		res = -1;
		goto out;		
	}

	/* Prepare to get cdc data */
	cmd[0] = protocol->cmd_read_ctrl;
	cmd[1] = protocol->cmd_get_cdc;

	res = core_write(core_config->slave_i2c_addr, cmd, 2);
	if (res < 0) {
		ipio_err("I2C Write Error\n");
		goto out;
	}

	mdelay(1);

	res = core_write(core_config->slave_i2c_addr, &cmd[1], 1);
	if (res < 0) {
		ipio_err("I2C Write Error\n");
		goto out;
	}

	mdelay(1);

	/* Allocate a buffer for the original */
	ori = kcalloc(len, sizeof(uint8_t), GFP_KERNEL);
	if (ERR_ALLOC_MEM(ori)) {
		ipio_err("Failed to allocate ori mem (%ld)\n", PTR_ERR(ori));
		goto out;
	}

	/* Get original frame(cdc) data */
	res = core_read(core_config->slave_i2c_addr, ori, len);
	if (res < 0) {
		ipio_err("I2C Read Error while getting original cdc data\n");
		goto out;
	}

	dump_data(ori, 8, len);

	if (frame_buf == NULL) {
		frame_buf = kcalloc(core_mp->frame_len, sizeof(int32_t), GFP_KERNEL);
		if (ERR_ALLOC_MEM(frame_buf)) {
			ipio_err("Failed to allocate FrameBuffer mem (%ld)\n", PTR_ERR(frame_buf));
			goto out;
		}
	} else {
		memset(frame_buf, 0x0, core_mp->frame_len);
	}

	/* Convert original data to the physical one in each node */
	for (i = 0; i < core_mp->frame_len; i++) {
		if (tItems[index].cmd == protocol->mutual_dac) {
			/* DAC - P */
			if (((ori[(2 * i) + 1] & 0x80) >> 7) == 1) {
				/* Negative */
				inDACp = 0 - (int)(ori[(2 * i) + 1] & 0x7F);
			} else {
				inDACp = ori[(2 * i) + 1] & 0x7F;
			}

			/* DAC - N */
			if (((ori[(1 + (2 * i)) + 1] & 0x80) >> 7) == 1) {
				/* Negative */
				inDACn = 0 - (int)(ori[(1 + (2 * i)) + 1] & 0x7F);
			} else {
				inDACn = ori[(1 + (2 * i)) + 1] & 0x7F;
			}

			frame_buf[i] = (inDACp + inDACn) / 2;
		} else {
			/* H byte + L byte */
			int32_t tmp = (ori[(2 * i) + 1] << 8) + ori[(1 + (2 * i)) + 1];

			if ((tmp & 0x8000) == 0x8000)
				frame_buf[i] = tmp - 65536;
			else
				frame_buf[i] = tmp;

			if(strcmp(tItems[index].name, "mutual_no_bk") == 0)
			{
				if(core_config->chip_id == CHIP_TYPE_ILI9881)
				{
					if(core_config->chip_type == ILI9881_TYPE_H)
						frame_buf[i] -= RAWDATA_NO_BK_DATA_SHIFT_9881H;
					if(core_config->chip_type == ILI9881_TYPE_F)
						frame_buf[i] -= RAWDATA_NO_BK_DATA_SHIFT_9881F;
				}
			}
		}
	}

	dump_data(frame_buf, 32, core_mp->frame_len);

out:
	ipio_kfree((void **)&ori);
	return res;
}

static void run_pixel_test(int index)
{
	int i, x, y;
	int32_t *p_comb = frame_buf;

	for (y = 0; y < core_mp->ych_len; y++) {
		for (x = 0; x < core_mp->xch_len; x++) {
			int tmp[4] = { 0 }, max = 0;
			int shift = y * core_mp->xch_len;
			int centre = p_comb[shift + x];

			/* if its position is in corner, the number of point
			   we have to minus is around 2 to 3.  */
			if (y == 0 && x == 0) {
				tmp[0] = Mathabs(centre - p_comb[(shift + 1) + x]);	/* down */
				tmp[1] = Mathabs(centre - p_comb[shift + (x + 1)]);	/* right */
			} else if (y == (core_mp->ych_len - 1) && x == 0) {
				tmp[0] = Mathabs(centre - p_comb[(shift - 1) + x]);	/* up */
				tmp[1] = Mathabs(centre - p_comb[shift + (x + 1)]);	/* right */
			} else if (y == 0 && x == (core_mp->xch_len - 1)) {
				tmp[0] = Mathabs(centre - p_comb[(shift + 1) + x]);	/* down */
				tmp[1] = Mathabs(centre - p_comb[shift + (x - 1)]);	/* left */
			} else if (y == (core_mp->ych_len - 1) && x == (core_mp->xch_len - 1)) {
				tmp[0] = Mathabs(centre - p_comb[(shift - 1) + x]);	/* up */
				tmp[1] = Mathabs(centre - p_comb[shift + (x - 1)]);	/* left */
			} else if (y == 0 && x != 0) {
				tmp[0] = Mathabs(centre - p_comb[(shift + 1) + x]);	/* down */
				tmp[1] = Mathabs(centre - p_comb[shift + (x - 1)]);	/* left */
				tmp[2] = Mathabs(centre - p_comb[shift + (x + 1)]);	/* right */
			} else if (y != 0 && x == 0) {
				tmp[0] = Mathabs(centre - p_comb[(shift - 1) + x]);	/* up */
				tmp[1] = Mathabs(centre - p_comb[shift + (x + 1)]);	/* right */
				tmp[2] = Mathabs(centre - p_comb[(shift + 1) + x]);	/* down */

			} else if (y == (core_mp->ych_len - 1) && x != 0) {
				tmp[0] = Mathabs(centre - p_comb[(shift - 1) + x]);	/* up */
				tmp[1] = Mathabs(centre - p_comb[shift + (x - 1)]);	/* left */
				tmp[2] = Mathabs(centre - p_comb[shift + (x + 1)]);	/* right */
			} else if (y != 0 && x == (core_mp->xch_len - 1)) {
				tmp[0] = Mathabs(centre - p_comb[(shift - 1) + x]);	/* up */
				tmp[1] = Mathabs(centre - p_comb[shift + (x - 1)]);	/* left */
				tmp[2] = Mathabs(centre - p_comb[(shift + 1) + x]);	/* down */
			} else {
				/* middle minus four directions */
				tmp[0] = Mathabs(centre - p_comb[(shift - 1) + x]);	/* up */
				tmp[1] = Mathabs(centre - p_comb[(shift + 1) + x]);	/* down */
				tmp[2] = Mathabs(centre - p_comb[shift + (x - 1)]);	/* left */
				tmp[3] = Mathabs(centre - p_comb[shift + (x + 1)]);	/* right */
			}

			max = tmp[0];

			for (i = 0; i < 4; i++) {
				if (tmp[i] > max)
					max = tmp[i];
			}

			tItems[index].buf[shift + x] = max;
		}
	}
}

static int run_open_test(int index)
{
	int i, x, y, k, res = 0;
	int border_x[] = {-1, 0, 1, 1, 1, 0, -1, -1};
	int border_y[] = {-1, -1, -1, 0, 1, 1, 1, 0};
	int32_t *p_comb = frame_buf;

	if (strcmp(tItems[index].name, "open_integration") == 0) {
		for (i = 0; i < core_mp->frame_len; i++)
			tItems[index].buf[i] = p_comb[i];
	} else if (strcmp(tItems[index].name, "open_cap") == 0) {
		/* Each result is getting from a 3 by 3 grid depending on where the centre location is.
		   So if the centre is at corner, the number of node grabbed from a grid will be different. */
		for (y = 0; y < core_mp->ych_len; y++) {
			for (x = 0; x < core_mp->xch_len; x++) {
				int sum = 0, avg = 0, count = 0;
				int shift = y * core_mp->xch_len;
				int centre = p_comb[shift + x];

				for (k = 0; k < 8; k++) {
					if (((y + border_y[k] >= 0) && (y + border_y[k] < core_mp->ych_len)) &&
								((x + border_x[k] >= 0) && (x + border_x[k] < core_mp->xch_len))) {
						count++;
						sum += p_comb[(y + border_y[k]) * core_mp->xch_len + (x + border_x[k])];
					}
				}

				avg = (sum + centre) / (count + 1);	/* plus 1 because of centre */
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

	for (y = 0; y < core_mp->ych_len; y++) {
		for (x = 0; x < core_mp->xch_len; x++) {
			int shift = y * core_mp->xch_len;

			/* Tx Delta */
			if (y != (core_mp->ych_len - 1)) {
				core_mp->tx_delta_buf[shift + x] = Mathabs(p_comb[shift + x] - p_comb[(shift + 1) + x]);
			}

			/* Rx Delta */
			if (x != (core_mp->xch_len - 1)) {
				core_mp->rx_delta_buf[shift + x] = Mathabs(p_comb[shift + x] - p_comb[shift + (x + 1)]);
			}
		}
	}
}

static void run_untouch_p2p_test(int index)
{
	int x, y;
	int32_t *p_comb = frame_buf;

	for (y = 0; y < core_mp->ych_len; y++) {
		for (x = 0; x < core_mp->xch_len; x++) {
			int shift = y * core_mp->xch_len;

			if (p_comb[shift + x] > tItems[index].max_buf[shift + x]) {
				tItems[index].max_buf[shift + x] = p_comb[shift + x];
			}

			if (p_comb[shift + x] < tItems[index].min_buf[shift + x]) {
				tItems[index].min_buf[shift + x] = p_comb[shift + x];
			}

			tItems[index].buf[shift + x] =
			    tItems[index].max_buf[shift + x] - tItems[index].min_buf[shift + x];
		}
	}
}

static void compare_MaxMin_result(int index, int32_t *data)
{
	int x, y;

	for (y = 0; y < core_mp->ych_len; y++) {
		for (x = 0; x < core_mp->xch_len; x++) {
			int shift = y * core_mp->xch_len;

			if (tItems[index].catalog == UNTOUCH_P2P)
				return;
			else if (tItems[index].catalog == TX_RX_DELTA) {
				/* Tx max/min comparison */
				if (core_mp->tx_delta_buf[shift + x] < data[shift + x]) {
					core_mp->tx_max_buf[shift + x] = data[shift + x];
				}

				if (core_mp->tx_delta_buf[shift + x] > data[shift + x]) {
					core_mp->tx_min_buf[shift + x] = data[shift + x];
				}

				/* Rx max/min comparison */
				if (core_mp->rx_delta_buf[shift + x] < data[shift + x]) {
					core_mp->rx_max_buf[shift + x] = data[shift + x];
				}

				if (core_mp->rx_delta_buf[shift + x] > data[shift + x]) {
					core_mp->rx_min_buf[shift + x] = data[shift + x];
				}
			} else {
				if (tItems[index].max_buf[shift + x] < tItems[index].buf[shift + x]) {
					tItems[index].max_buf[shift + x] = tItems[index].buf[shift + x];
				}

				if (tItems[index].min_buf[shift + x] > tItems[index].buf[shift + x]) {
					tItems[index].min_buf[shift + x] = tItems[index].buf[shift + x];
				}
			}
		}
	}
}

int codeToOhm(int32_t Code)
{
	int douTDF1 = 0;
	int douTDF2 = 0;
	int douTVCH = 24;//2.4
	int douTVCL = 8;//0.8
	int douCint = 7;//10^(-12)
	int douVariation = 64;//0.64
	int douRinternal = 930;
	int32_t temp = 0;

	if (core_mp->nodp.isLongV) {
		douTDF1 = 300;//(10^(-8))
		douTDF2 = 100;//(10^(-8))
	} else {
		douTDF1 = 219;//(10^(-8));
		douTDF2 = 100;//(10^(-8))
	}

	temp = ((douTVCH - douTVCL) * douVariation * (douTDF1 - douTDF2) * (1<<14) / (36 * Code * douCint)) * 100;
	temp = (temp - douRinternal) / 1000;//1000000 M Ohm
	return temp;   
}

static int short_test(int index)
{
	int j = 0, len = 6, code[6] = {7849,2632,1581,1130,879,791},res = 0;

	if(protocol->major >= 5 && protocol->mid >= 4) {
		//Calculate code to ohm and save to tItems[index].buf
		for (j = 0; j < core_mp->frame_len; j++)
			tItems[index].buf[j] = codeToOhm(frame_buf[j]);
	} else {
		for (j = 0; j < core_mp->frame_len; j++)
			tItems[index].buf[j] = frame_buf[j];		
	}

out:
	return res;
}

static int mutual_test(int index)
{
	int i = 0, j = 0, x = 0, y = 0, res = 0 ,get_frame_cont =1 ;

	ipio_debug(DEBUG_MP_TEST, "Item = %s, CMD = 0x%x, Frame Count = %d\n",
	    tItems[index].name, tItems[index].cmd, tItems[index].frame_count);

	/*
	 * We assume that users who are calling the test forget to config frame count
	 * as 1, so we just help them to set it up.
	 */
	if (tItems[index].frame_count <= 0) {
		ipio_err("Frame count is zero, which is at least set as 1\n");
		tItems[index].frame_count = 1;
	}

	res = create_mp_test_frame_buffer(index);
	if (res < 0)
		goto out;

	/* Init Max/Min buffer */
	for (y = 0; y < core_mp->ych_len; y++) {
		for (x = 0; x < core_mp->xch_len; x++) {
			if (tItems[i].catalog == TX_RX_DELTA) {
				core_mp->tx_max_buf[y * core_mp->xch_len + x] = -65535;
				core_mp->rx_max_buf[y * core_mp->xch_len + x] = -65535;
				core_mp->tx_min_buf[y * core_mp->xch_len + x] = 65535;
				core_mp->rx_min_buf[y * core_mp->xch_len + x] = 65535;
			} else {
				tItems[index].max_buf[y * core_mp->xch_len + x] = -65535;
				tItems[index].min_buf[y * core_mp->xch_len + x] = 65535;
			}
		}
	}
	if (tItems[index].catalog != PEAK_TO_PEAK_TEST)
		get_frame_cont = tItems[index].frame_count;

	if (tItems[index].spec_option == BENCHMARK) {
		core_parser_benchmark(tItems[index].bench_mark_max, tItems[index].bench_mark_min, tItems[index].type_option, tItems[index].desp);
		dump_benchmark_data(tItems[index].bench_mark_max , tItems[index].bench_mark_min);
	}

	if (tItems[index].spec_option == BENCHMARK) {
		core_parser_benchmark(tItems[index].bench_mark_max, tItems[index].bench_mark_min, tItems[index].type_option,tItems[index].desp);
		if (ipio_debug_level&&DEBUG_PARSER > 0)                               
			dump_benchmark_data(tItems[index].bench_mark_max , tItems[index].bench_mark_min);
	}
	for (i = 0; i < get_frame_cont; i++) {
		res = allnode_mutual_cdc_data(index);
		if (res < 0) {
			ipio_err("Failed to initialise CDC data, %d\n", res);
			goto out;
		}
		switch (tItems[index].catalog) {
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
		case SHORT_TEST:
			short_test(index);
			break;			
		default:
			for (j = 0; j < core_mp->frame_len; j++)
				tItems[index].buf[j] = frame_buf[j];
			break;
		}
	}

	compare_MaxMin_result(index, tItems[index].buf);

out:
	return res;
}

static int key_test(int index)
{
	int i, j = 0, res = 0;

	ipio_debug(DEBUG_MP_TEST, "Item = %s, CMD = 0x%x, Frame Count = %d\n",
	    tItems[index].name, tItems[index].cmd, tItems[index].frame_count);

	if (tItems[index].frame_count == 0) {
		ipio_err("Frame count is zero, which at least sets as 1\n");
		res = -EINVAL;
		goto out;
	}

	res = create_mp_test_frame_buffer(index);
	if (res < 0)
		goto out;

	for (i = 0; i < tItems[index].frame_count; i++) {
		res = allnode_key_cdc_data(index);
		if (res < 0) {
			ipio_err("Failed to initialise CDC data, %d\n", res);
			goto out;
		}

		for (j = 0; j < core_mp->key_len; j++)
			tItems[index].buf[j] = key_buf[j];
	}

	compare_MaxMin_result(index, tItems[index].buf);

out:
	return res;
}

static int self_test(int index)
{
	ipio_err("TDDI has no self to be tested currently\n");
	return -1;
}

static int st_test(int index)
{
	ipio_err("ST Test is not supported by the driver\n");
	return -1;
}

void core_mp_show_result(void)
{
	int i, x, y, csv_len = 0;
	char *csv = NULL;
	char csv_name[128] = { 0 };
	char *line_breaker = "\n";
	struct file *f = NULL;
	mm_segment_t fs;
	loff_t pos;

	csv = vmalloc(CSV_FILE_SIZE);
	if (ERR_ALLOC_MEM(csv)) {
		ipio_err("Failed to allocate CSV mem\n");
		goto fail_open;
	}

	for (i = 0; i < ARRAY_SIZE(tItems); i++) {
		if (tItems[i].run) {
			if ( tItems[i].spec_option == BENCHMARK)
			{       
				print_benchmark_cdc_data(i, true, false, csv, &csv_len);
				continue;
			}
                        
			pr_info("%s\n", tItems[i].desp);
			csv_len += sprintf(csv + csv_len, " %s ", tItems[i].desp);

			pr_info("\n");
			csv_len += sprintf(csv + csv_len, "%s", line_breaker);

			pr_info("Max = %d\n",tItems[i].max);
			csv_len += sprintf(csv + csv_len, "Max = %d", tItems[i].max);

			pr_info("\n");
			csv_len += sprintf(csv + csv_len, "%s", line_breaker);

			pr_info("Min = %d\n",tItems[i].min);
			csv_len += sprintf(csv + csv_len, "Min = %d", tItems[i].min);

			pr_info("\n");
			csv_len += sprintf(csv + csv_len, "%s", line_breaker);

			pr_info("Frame count = %d\n",tItems[i].frame_count);
			csv_len += sprintf(csv + csv_len, "Frame count = %d", tItems[i].frame_count);

			pr_info("\n");
			csv_len += sprintf(csv + csv_len, "%s", line_breaker);

			if (tItems[i].catalog == TX_RX_DELTA) {
				if (ERR_ALLOC_MEM(core_mp->rx_delta_buf) || ERR_ALLOC_MEM(core_mp->tx_delta_buf)) {
					ipio_err("This test item (%s) has no data inside its buffer\n", tItems[i].desp);
					continue;
				}
			} else {
				if (ERR_ALLOC_MEM(tItems[i].buf) || ERR_ALLOC_MEM(tItems[i].max_buf) ||
						ERR_ALLOC_MEM(tItems[i].min_buf)) {
					ipio_err("This test item (%s) has no data inside its buffer\n", tItems[i].desp);
					continue;
				}
			}

			if (tItems[i].catalog == KEY_TEST) {
				for (x = 0; x < core_mp->key_len; x++) {
					DUMP(DEBUG_MP_TEST, "KEY_%02d ", x);
					csv_len += sprintf(csv + csv_len, "KEY_%02d,", x);
				}

				DUMP(DEBUG_MP_TEST, "\n");
				csv_len += sprintf(csv + csv_len, "%s", line_breaker);

				for (y = 0; y < core_mp->key_len; y++) {
					DUMP(DEBUG_MP_TEST, " %3d   ", tItems[i].buf[y]);
					csv_len += sprintf(csv + csv_len, " %3d, ", tItems[i].buf[y]);
				}

				DUMP(DEBUG_MP_TEST, "\n");
				csv_len += sprintf(csv + csv_len, "%s", line_breaker);
			} else if (tItems[i].catalog == TX_RX_DELTA) {
				DUMP(DEBUG_MP_TEST, " %s ", "TX Max Hold");
				csv_len += sprintf(csv + csv_len, "  %s ", "TX Max Hold");

				DUMP(DEBUG_MP_TEST, "\n");
				csv_len += sprintf(csv + csv_len, "%s", line_breaker);

				print_cdc_data(i, true, true, csv, &csv_len);

				DUMP(DEBUG_MP_TEST, " %s ", "TX Min Hold");
				csv_len += sprintf(csv + csv_len, "  %s ", "TX Min Hold");

				DUMP(DEBUG_MP_TEST, "\n");
				csv_len += sprintf(csv + csv_len, "%s", line_breaker);

				print_cdc_data(i, false, true, csv, &csv_len);

				DUMP(DEBUG_MP_TEST, " %s ", "RX Max Hold");
				csv_len += sprintf(csv + csv_len, "  %s ", "RX Max Hold");

				DUMP(DEBUG_MP_TEST, "\n");
				csv_len += sprintf(csv + csv_len, "%s", line_breaker);

				print_cdc_data(i, true, false, csv, &csv_len);

				DUMP(DEBUG_MP_TEST, " %s ", "RX Min Hold");
				csv_len += sprintf(csv + csv_len, "  %s ", "RX Min Hold");

				DUMP(DEBUG_MP_TEST, "\n");
				csv_len += sprintf(csv + csv_len, "%s", line_breaker);

				print_cdc_data(i, false, false, csv, &csv_len);
			} else {
				DUMP(DEBUG_MP_TEST, " %s ", "Max Hold");
				csv_len += sprintf(csv + csv_len, "  %s ", "Max Hold");

				DUMP(DEBUG_MP_TEST, "\n");
				csv_len += sprintf(csv + csv_len, "%s", line_breaker);

				print_cdc_data(i, true, false, csv, &csv_len);

				DUMP(DEBUG_MP_TEST, " %s ", "Min Hold");
				csv_len += sprintf(csv + csv_len, "  %s ", "Min Hold");

				DUMP(DEBUG_MP_TEST, "\n");
				csv_len += sprintf(csv + csv_len, "%s", line_breaker);

				print_cdc_data(i, false, false, csv, &csv_len);
			}

			pr_info("\n Result : %s\n", tItems[i].result);
			csv_len += sprintf(csv + csv_len, " Result : %s ", tItems[i].result);

			pr_info("\n");
			csv_len += sprintf(csv + csv_len, "%s", line_breaker);

			if (strcmp(tItems[i].result, "FAIL") == 0)
				core_mp->final_result = false;

			csv_len += sprintf(csv + csv_len, "%s", line_breaker);
			csv_len += sprintf(csv + csv_len, "%s", line_breaker);
		}
	}

	memset(csv_name, 0, 128 * sizeof(char));

	if (core_mp->final_result)
		sprintf(csv_name, "%s/%s.csv", CSV_PATH, "mp_pass");
	else
		sprintf(csv_name, "%s/%s.csv", CSV_PATH, "mp_fail");

	ipio_info("Open CSV : %s\n", csv_name);

	if (f == NULL)
		f = filp_open(csv_name, O_WRONLY | O_CREAT | O_TRUNC, 644);

	if (ERR_ALLOC_MEM(f)) {
		ipio_err("Failed to open CSV file");
		goto fail_open;
	}

	ipio_info("Open CSV succeed, its length = %d\n ", csv_len);

	if (csv_len >= CSV_FILE_SIZE) {
		ipio_err("The length saved to CSV is too long !\n");
		goto fail_open;
	}

	fs = get_fs();
	set_fs(KERNEL_DS);
	pos = 0;
	vfs_write(f, csv, csv_len, &pos);
	set_fs(fs);
	filp_close(f, NULL);

	ipio_info("Writing Data into CSV succeed\n");

fail_open:
	if(csv != NULL)
		vfree(csv);
}
EXPORT_SYMBOL(core_mp_show_result);

void core_mp_run_test(char *item, bool ini)
{
	int i = 0;
	char str[512] = { 0 };

	/* update X/Y channel length if they're changed */
	core_mp->xch_len = core_config->tp_info->nXChannelNum;
	core_mp->ych_len = core_config->tp_info->nYChannelNum;

	/* update key's length if they're changed */
	core_mp->key_len = core_config->tp_info->nKeyCount;

	/* compute the total length in one frame */
	core_mp->frame_len = core_mp->xch_len * core_mp->ych_len;

	/* We don't control lcm on and off in general case. */
	core_mp->ctrl_lcm = false;

	if (item == NULL || strncmp(item, " ", strlen(item)) == 0) {
		ipio_err("Invaild string\n");
		return;
	}

	ipio_debug(DEBUG_MP_TEST, "item = %s\n", item);

	for (i = 0; i < core_mp->mp_items; i++) {
		if (strncmp(item, tItems[i].desp, strlen(item)) == 0) {
			if (ini) {
				core_parser_get_int_data(item, "Enable", str);
				tItems[i].run = katoi(str);
				core_parser_get_int_data(item, "SPEC Option", str);
				tItems[i].spec_option= katoi(str);
				core_parser_get_int_data(item, "Type Option", str);
				tItems[i].type_option= katoi(str);				
 				core_parser_get_int_data(item, "Frame Count", str);
				tItems[i].frame_count= katoi(str);                               

				/* Get threshold from ini structure in parser */
				if (strcmp(item, "Tx/Rx Delta") == 0) {
					core_parser_get_int_data(item, "Tx Max", str);
					core_mp->TxDeltaMax = katoi(str);
					core_parser_get_int_data(item, "Tx Min", str);
					core_mp->TxDeltaMin = katoi(str);
					core_parser_get_int_data(item, "Rx Max", str);
					core_mp->RxDeltaMax = katoi(str);
					core_parser_get_int_data(item, "Rx Min", str);
					core_mp->RxDeltaMin = katoi(str);
					ipio_debug(DEBUG_MP_TEST, "%s: Tx Max = %d, Tx Min = %d, Rx Max = %d,  Rx Min = %d\n",
							tItems[i].desp, core_mp->TxDeltaMax, core_mp->TxDeltaMin,
							core_mp->RxDeltaMax, core_mp->RxDeltaMin);
				} else {
					core_parser_get_int_data(item, "Max", str);
					tItems[i].max = katoi(str);
					core_parser_get_int_data(item, "Min", str);
					tItems[i].min = katoi(str);
				}

				core_parser_get_int_data(item, "Frame Count", str);
				tItems[i].frame_count = katoi(str);

				ipio_debug(DEBUG_MP_TEST, "%s: run = %d, max = %d, min = %d, frame_count = %d\n", tItems[i].desp,
						tItems[i].run, tItems[i].max, tItems[i].min, tItems[i].frame_count);
			}

			if (strncmp(tItems[i].name, "mutual_has_bk_lcm_off", strlen("mutual_has_bk_lcm_off")) == 0 || 
				strncmp(tItems[i].name, "noise_peak_to_peak_ic_lcm_off", strlen("noise_peak_to_peak_ic_lcm_off")) == 0) {
				core_mp->ctrl_lcm = true;
			}

			if (core_mp->ctrl_lcm)
				mp_ctrl_lcd_status(false);

			if (tItems[i].run) {
				ipio_info("Running Test Item : %s\n", tItems[i].desp);
				tItems[i].do_test(i);
			}

			if (core_mp->ctrl_lcm)
				mp_ctrl_lcd_status(true);

			break;
		}
	}
}
EXPORT_SYMBOL(core_mp_run_test);

int core_mp_move_code(void)
{
	ipio_info("Prepaing to enter Test Mode\n");
#ifdef HOST_DOWNLOAD
	ilitek_platform_tp_hw_reset(true);
#else
	if (core_config_check_cdc_busy(50) < 0) {
		ipio_err("Check busy is timout ! Enter Test Mode failed\n");
		return -1;
	}

	if (core_config_ice_mode_enable() < 0) {
		ipio_err("Failed to enter ICE mode\n");
		return -1;
	}

	/* Disable watch dog */
	core_config_reset_watch_dog();

	/* DMA Trigger */
	core_config_ice_mode_write(0x41010, 0xFF, 1);

	mdelay(30);

	/* CS High */
	core_config_ice_mode_write(0x041000, 0x1, 1);

	mdelay(60);

	/* Code reset */
	core_config_ice_mode_write(0x40040, 0xAE, 1);

	core_config_ice_mode_disable();

	if (core_config_check_cdc_busy(300) < 0) {
		ipio_err("Check busy is timout ! Enter Test Mode failed\n");
		return -1;
	}
#endif
	ipio_info("FW Test Mode ready\n");
	return 0;
}
EXPORT_SYMBOL(core_mp_move_code);

void core_mp_test_free(void)
{
	int i;

	ipio_info("Free all allocated mem\n");

	core_mp->final_result = true;

	for (i = 0; i < ARRAY_SIZE(tItems); i++) {
		tItems[i].run = false;
		sprintf(tItems[i].result, "%s", "FAIL");

		if (tItems[i].catalog == TX_RX_DELTA) {
			ipio_kfree((void **)&core_mp->rx_delta_buf);
			ipio_kfree((void **)&core_mp->tx_delta_buf);
			ipio_kfree((void **)&core_mp->tx_max_buf);
			ipio_kfree((void **)&core_mp->tx_min_buf);
			ipio_kfree((void **)&core_mp->rx_max_buf);
			ipio_kfree((void **)&core_mp->rx_min_buf);
		} else {
			if (tItems[i].spec_option == BENCHMARK) {
				ipio_kfree((void **)&tItems[i].bench_mark_max);
				ipio_kfree((void **)&tItems[i].bench_mark_min);                                                
			}      		
			ipio_kfree((void **)&tItems[i].buf);
			ipio_kfree((void **)&tItems[i].max_buf);
			ipio_kfree((void **)&tItems[i].min_buf);
		}
	}

	ipio_kfree((void **)&frame_buf);
	ipio_kfree((void **)&key_buf);
	ipio_kfree((void **)&core_mp);
}
EXPORT_SYMBOL(core_mp_test_free);

static void mp_test_init_item(void)
{
	int i;

	core_mp->mp_items = ARRAY_SIZE(tItems);

	/* assign test functions run on MP flow according to their catalog */
	for (i = 0; i < ARRAY_SIZE(tItems); i++) {
		if (tItems[i].catalog == MUTUAL_TEST) {
			tItems[i].do_test = mutual_test;
		} else if (tItems[i].catalog == TX_RX_DELTA) {
			tItems[i].do_test = mutual_test;
		} else if (tItems[i].catalog == UNTOUCH_P2P) {
			tItems[i].do_test = mutual_test;
		} else if (tItems[i].catalog == PIXEL) {
			tItems[i].do_test = mutual_test;
		} else if (tItems[i].catalog == OPEN_TEST) {
			tItems[i].do_test = mutual_test;
		} else if (tItems[i].catalog == KEY_TEST) {
			tItems[i].do_test = key_test;
		} else if (tItems[i].catalog == SELF_TEST) {
			tItems[i].do_test = self_test;
		} else if (tItems[i].catalog == ST_TEST) {
			tItems[i].do_test = st_test;
		} else if (tItems[i].catalog == PEAK_TO_PEAK_TEST) {
			tItems[i].do_test = mutual_test;
		} else if (tItems[i].catalog == SHORT_TEST) {
			tItems[i].do_test = mutual_test;			
		}

		if (strncmp(tItems[i].name, "mutual_dac", strlen("mutual_dac")) == 0) {
			tItems[i].get_cdc_init_cmd = mp_cdc_init_cmd_dac;
		} else if (strncmp(tItems[i].name, "mutual_has_bk", strlen("mutual_has_bk")) == 0 ||
					strncmp(tItems[i].name, "mutual_has_bk_lcm_off", strlen("mutual_has_bk_lcm_off")) == 0) {
			tItems[i].get_cdc_init_cmd = mp_cdc_init_cmd_raw_bk;
		} else if (strncmp(tItems[i].name, "mutual_no_bk", strlen("mutual_no_bk")) == 0) {
			tItems[i].get_cdc_init_cmd = mp_cdc_init_cmd_raw_no_bk;
		} else if (strncmp(tItems[i].name, "noise_peak_to_peak_ic", strlen("noise_peak_to_peak_ic")) == 0 ||
					strncmp(tItems[i].name, "noise_peak_to_peak_ic_lcm_off", strlen("noise_peak_to_peak_ic_lcm_off")) == 0) {
			tItems[i].get_cdc_init_cmd = mp_cdc_init_cmd_p2p_ic;
		} else if (strncmp(tItems[i].name, "noise_peak_to_peak_panel", strlen("noise_peak_to_peak_panel")) == 0) {
			tItems[i].get_cdc_init_cmd = mp_cdc_init_cmd_p2p_panel;
		} else if (strncmp(tItems[i].name, "doze_raw", strlen("doze_raw")) == 0) {
			tItems[i].get_cdc_init_cmd = mp_cdc_init_cmd_doze_raw;
		} else if (strncmp(tItems[i].name, "doze_p2p", strlen("doze_p2p")) == 0) {
			tItems[i].get_cdc_init_cmd = mp_cdc_init_cmd_doze_p2p;
		} else {
			tItems[i].get_cdc_init_cmd = mp_cdc_init_cmd_common;
		}

		tItems[i].result = kmalloc(16, GFP_KERNEL);
        sprintf(tItems[i].result, "%s", "FAIL");
	}

	/*
	 * assign protocol command written into firmware via I2C,
	 * which might be differnet if the version of protocol was changed.
	 */
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
	tItems[34].cmd = protocol->peak_to_peak;
}

int core_mp_init(void)
{
	int res = 0;

	if (!ERR_ALLOC_MEM(core_config->tp_info)) {
		if (core_mp == NULL) {
			core_mp = kzalloc(sizeof(*core_mp), GFP_KERNEL);
			if (ERR_ALLOC_MEM(core_mp)) {
				ipio_err("Failed to init core_mp, %ld\n", PTR_ERR(core_mp));
				res = -ENOMEM;
				goto out;
			}

			core_mp->xch_len = core_config->tp_info->nXChannelNum;
			core_mp->ych_len = core_config->tp_info->nYChannelNum;

			core_mp->stx_len = core_config->tp_info->self_tx_channel_num;
			core_mp->srx_len = core_config->tp_info->self_rx_channel_num;

			core_mp->key_len = core_config->tp_info->nKeyCount;
			core_mp->st_len = core_config->tp_info->side_touch_type;

			core_mp->tdf = 240;
			core_mp->busy_cdc = INT_CHECK;

			core_mp->final_result = true;

			mp_test_init_item();
		}
	} else {
		ipio_err("Failed to allocate core_mp mem as did not find TP info\n");
		res = -ENOMEM;
	}

out:
	return res;
}
EXPORT_SYMBOL(core_mp_init);