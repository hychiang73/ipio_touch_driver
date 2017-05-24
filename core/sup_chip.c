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
