#include "ilitek.h"

#include "core/config.h"
#include "core/dbbus.h"
#include "core/firmware.h"
#include "core/fr.h"
#include "core/gesture.h"
#include "core/glove.h"
#include "core/i2c.h"

ilitek_device *ilitek_adapter;

static int ilitek_init_core_func(void)
{
	if(core_config_init(CHIP_TYPE_ILI2121) < 0 ||
		core_i2c_init(ilitek_adapter->client) < 0)
			return -EINVAL;

	return SUCCESS;
}

static int ilitek_get_chip_type(void)
{
	int res = 0;

	DBG_INFO();

	res = core_config_GetChipID();

	return res;
}

int ilitek_read_tp_info(void)
{
	unsigned int chip_id = 0;

	DBG_INFO();

	chip_id = ilitek_get_chip_type();

	if(chip_id < 0)
	{
		DBG_ERR("Get Chip ID failed %d", chip_id);
		return -ENODEV;
	}

	ilitek_adapter->chip_id = chip_id;

	return SUCCESS;
}
EXPORT_SYMBOL(ilitek_read_tp_info);

int ilitek_init(struct i2c_client *client, const struct i2c_device_id *id)
{
	int res;

	DBG_INFO();

	ilitek_adapter = (ilitek_device*)kmalloc(sizeof(ilitek_device), GFP_KERNEL);

	ilitek_adapter->client = client;

	ilitek_adapter->id = id;

	res = ilitek_init_core_func();
	if(res < 0)
	{
		DBG_ERR("init core funcs failed %d", res);
		return res;
	}

	return SUCCESS;
}
EXPORT_SYMBOL(ilitek_init);
