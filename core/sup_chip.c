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

#include <linux/kernel.h>
#include "../chip.h"

/*
 * The two vars will be exported to all core modules to them identifying which of 
 * chips is exactly used on the board.
 *
 * They initialise configurations acorrding to the result from the list in the init 
 * function of each core modules.
 */

uint32_t SUP_CHIP_LIST[] = {
	CHIP_TYPE_ILI2121,
	CHIP_TYPE_ILI7807,
};

int nums_chip = sizeof(SUP_CHIP_LIST) / sizeof(SUP_CHIP_LIST[0]);
