#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include "../chip.h"
#include "config.h"
#include "i2c.h"
#include "firmware.h"

bool upgraded = false;

CORE_FIRMWARE *core_firmware;
extern uint32_t SUP_CHIP_LIST[SUPP_CHIP_NUM];

static int core_firmware_upgrade_ili21xx(void)
{
	DBG_INFO();

	return 0;
}


int core_firmware_init(uint32_t chip_type)
{
	int i = 0;	
	int length = sizeof(SUP_CHIP_LIST)/sizeof(uint32_t);

	DBG_INFO();

	for(; i < length; i++)
	{
		if(SUP_CHIP_LIST[i] == chip_type)
		{
			core_firmware = (CORE_FIRMWARE*)kmalloc(sizeof(*core_firmware), GFP_KERNEL);
		}
	}

	if(core_firmware == NULL) 
	{
		DBG_ERR("Can't find an id from the support list, init core-config failed ");
		return -EINVAL;
	}

	return 0;
}

int core_firmware_upgrade(uint32_t chip_id)
{

	DBG_INFO("chip id = %x", chip_id);

	//TODO: call a function to disable finger report
	//		before updating firmware
	
	core_firmware_init(CHIP_TYPE_ILI2121);
}
