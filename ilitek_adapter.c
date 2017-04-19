#include "ilitek.h"
#include "core/config.h"
#include "core/dbbus.h"
#include "core/firmware.h"
#include "core/fr.h"
#include "core/gesture.h"
#include "core/glove.h"
#include "core/i2c.h"

ilitek_device *ilitek_adapter;

unsigned short SupportChipTable[] = {
	CHIP_TYPE_ILI2121,
	CHIP_TYPE_ILI2120
};

int ilitek_get_chip_type(void)
{
	int i = 0;
	unsigned int res = 0;

//	res = core_config_chip(ilitek_adapter);

	for(; i < sizeof(SupportChipTable); i++)
	{
		if(res == SupportChipTable[i])
			return SupportChipTable[i];
	}

	return -EINVAL;
}

int ilitek_read_tp_info(void)
{
	unsigned int chip_id = 0;
	unsigned char szWriteBuf[2] = {0};
	unsigned char szReadBuf[64] = {0};

	DBG_INFO();

	chip_id = ilitek_get_chip_type();

	ilitek_adapter->chip_id = chip_id;

	return 0;
}

ilitek_device *ilitek_init(struct i2c_client *client, const struct i2c_device_id *id)
{
	DBG_INFO();

	ilitek_adapter = (ilitek_device*)kmalloc(sizeof(ilitek_device), GFP_KERNEL);

	ilitek_adapter->client = client;

	ilitek_adapter->id = id;

	return ilitek_adapter;
}

EXPORT_SYMBOL(ilitek_init);
