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

int ilitek_get_chip_type(void)
{
	int res;

	res = core_config_GetChipID();

	ilitek_adapter->chip_id = res;

	DBG_INFO("CHIP ID = 0x%x", ilitek_adapter->chip_id);

	return res;
}

unsigned char* ilitek_get_fw_ver(void)
{
	unsigned char *fw_ver;

	fw_ver = core_config_GetFWVer();

	if(!fw_ver)
	{
		DBG_ERR("Getting FW Ver error");
		return NULL;
	}

	ilitek_adapter->firmware_ver = fw_ver;

	DBG_INFO("Firmware Version = %d.%d.%d.%d", *fw_ver, *(fw_ver+1), *(fw_ver+2), *(fw_ver+3));

	return fw_ver;
}

unsigned short ilitek_get_protocol_ver(void)
{
	unsigned short ptl_ver = -1;

	ptl_ver = core_config_GetProtocolVer();

	if(ptl_ver < 0)
	{
		DBG_ERR("Getting Protocol Ver error");
		return -EFAULT;
	}

	ilitek_adapter->protocol_ver = ptl_ver;

	DBG_INFO("Protocol Version = %x", ptl_ver);

	return ptl_ver;
}

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
