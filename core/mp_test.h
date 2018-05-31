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

#ifndef __MP_TEST_H
#define __MP_TEST_H

#define BENCHMARK 1
#define RAWDATA_NO_BK_DATA_SHIFT_9881H 8192
#define RAWDATA_NO_BK_DATA_SHIFT_9881F 4096

struct mp_test_items {
	char *name;
	/* The description must be the same as ini's section name */
	char *desp;
	char *result;
	int catalog;
	uint8_t cmd;
	uint8_t spec_option;        
	uint8_t type_option;  	
	bool run;
	int max;
	int min;
	int frame_count;
	int32_t *buf;
	int32_t *max_buf;
	int32_t *min_buf;
	int32_t *bench_mark_max;
	int32_t *bench_mark_min;
	int (*get_cdc_init_cmd)(uint8_t *cmd, int len, int index);
	int (*do_test)(int index);
};

struct mp_nodp_calc {
	bool is60HZ;
	bool isLongV;

	uint16_t tshd;
	uint8_t multi_term_num_120;
	uint8_t multi_term_num_60;
	uint16_t tsvd_to_tshd;
	uint16_t qsh_tdf;

	uint8_t auto_trim;
	uint16_t tp_tshd_wait_120;
	uint16_t ddi_width_120;
	uint16_t tp_tshd_wait_60;
	uint16_t ddi_width_60;

	uint16_t dp_to_tp;
	uint16_t tx_wait_const;
	uint16_t tx_wait_const_multi;

	uint16_t tp_to_dp;
	uint8_t phase_adc;
	uint8_t r2d_pw;
	uint8_t rst_pw;
	uint8_t rst_pw_back;
	uint8_t dac_td;
	uint8_t qsh_pw;
	uint8_t qsh_td;
	uint8_t drop_nodp; 

	uint16_t nodp;
};

struct core_mp_test_data {
	/* A flag shows a test run in particular */
	bool m_signal;
	bool m_dac;
	bool s_signal;
	bool s_dac;
	bool key_dac;
	bool st_dac;
	bool p_no_bk;
	bool p_has_bk;
	bool open_integ;
	bool open_cap;

	int xch_len;
	int ych_len;
	int stx_len;
	int srx_len;
	int key_len;
	int st_len;
	int frame_len;
	int mp_items;
	bool final_result;

	/* Tx/Rx threshold & buffer */
	int TxDeltaMax;
	int TxDeltaMin;
	int RxDeltaMax;
	int RxDeltaMin;
	int32_t *tx_delta_buf;
	int32_t *rx_delta_buf;
	int32_t *tx_max_buf;
	int32_t *tx_min_buf;
	int32_t *rx_max_buf;
	int32_t *rx_min_buf;

	int tdf;
	bool busy_cdc;
	bool ctrl_lcm;

	struct mp_nodp_calc nodp;
};

extern struct core_mp_test_data *core_mp;
extern struct mp_test_items tItems[];

extern void core_mp_test_free(void);
extern void core_mp_show_result(void);
extern void core_mp_run_test(char *item, bool ini);
extern int core_mp_move_code(void);
extern int core_mp_init(void);
extern void ilitek_platform_tp_hw_reset(bool isEnable);
#endif
