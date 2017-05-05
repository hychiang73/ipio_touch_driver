#include <linux/kernel.h>
#include "../chip.h"

uint32_t SUP_CHIP_LIST[SUPP_CHIP_NUM] = {
	CHIP_TYPE_ILI2121,
	CHIP_TYPE_ILI2120
};
EXPORT_SYMBOL(SUP_CHIP_LIST);
