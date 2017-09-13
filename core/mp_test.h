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

extern int core_mp_cm_test(uint8_t value);

extern int core_mp_tx_short_test(uint8_t value);

extern int core_mp_rx_open_test(uint8_t value);
extern int core_mp_rx_short_test(uint8_t value);

extern int core_mp_key_open_test(uint8_t value);
extern int core_mp_key_short_test(uint8_t value);
extern int core_mp_key_has_bg_test(uint8_t value);
extern int core_mp_key_no_bk_test(uint8_t value);
extern int core_mp_key_has_bk_test(uint8_t value);
extern int core_mp_key_dac_test(uint8_t value);

extern int core_mp_self_signal_test(uint8_t value);
extern int core_mp_self_no_bk_test(uint8_t value);
extern int core_mp_self_has_bk_test(uint8_t value);
extern int core_mp_self_dac_test(uint8_t value);

extern int core_mp_mutual_signal_test(uint8_t value);
extern int core_mp_mutual_no_bk_test(uint8_t value);
extern int core_mp_mutual_has_bk_test(uint8_t value);
extern int core_mp_mutual_dac_test(uint8_t value);

extern int core_mp_run_test(const char *name, uint8_t value);
extern void core_mp_move_code(void);
extern void core_mp_init(void);
extern void core_mp_remove(void);

#endif
