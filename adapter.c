#include "adapter.h"

ilitek_device *adapter;

static int ilitek_init_core_func(void)
{
	if(core_config_init(CHIP_TYPE_ILI2121) < 0 ||
		core_i2c_init(adapter->client) < 0)
			return -EINVAL;

	return SUCCESS;
}

int ilitek_get_keyinfo(void)
{
	int res;

	res = core_config_GetKeyInfo();
	if(res < 0)
	{
		DBG_ERR("Getting key information error, res = %d", res);
		return res;
	}

	return SUCCESS;

}

int ilitek_get_resolution(void)
{
	adapter->tp_info = core_config_GetResolution();

	if(adapter->tp_info == NULL)
	{
		DBG_ERR("Getting TP Resolution failed");
		return -EFAULT;
	}

    DBG_INFO("nMaxX=%d, nMaxY=%d, nXChannelNum=%d, nYChannelNum=%d, nMaxTouchNum=%d, nMaxKeyButtonNum=%d, nKeyCount=%d",
			adapter->tp_info->nMaxX,
			adapter->tp_info->nMaxY,
			adapter->tp_info->nXChannelNum,
			adapter->tp_info->nYChannelNum,
			adapter->tp_info->nMaxTouchNum,
			adapter->tp_info->nMaxKeyButtonNum,
			adapter->tp_info->nKeyCount);

	return SUCCESS;
}

uint32_t ilitek_get_chip_type(void)
{

	adapter->chip_id = core_config_GetChipID();

	DBG_INFO("CHIP ID = 0x%x", adapter->chip_id);

	return adapter->chip_id;
}

uint8_t* ilitek_get_fw_ver(void)
{
	adapter->firmware_ver = core_config_GetFWVer();

	if(!adapter->firmware_ver)
	{
		DBG_ERR("Getting FW Ver error");
		return NULL;
	}

	DBG_INFO("Firmware Version = %d.%d.%d.%d", 
			*adapter->firmware_ver, 
			*(adapter->firmware_ver+1),
			*(adapter->firmware_ver+2),
			*(adapter->firmware_ver+3));

	return adapter->firmware_ver;
}

uint16_t ilitek_get_protocol_ver(void)
{
	uint16_t ptl_ver = -1;

	ptl_ver = core_config_GetProtocolVer();

	if(ptl_ver < 0)
	{
		DBG_ERR("Getting Protocol Ver error");
		return -EFAULT;
	}

	adapter->protocol_ver = ptl_ver;

	DBG_INFO("Protocol Version = %x", ptl_ver);

	return ptl_ver;
}

int ilitek_init(struct i2c_client *client, const struct i2c_device_id *id)
{
	int res;

	DBG_INFO();

	adapter = (ilitek_device*)kmalloc(sizeof(ilitek_device), GFP_KERNEL);

	adapter->client = client;

	adapter->id = id;

	res = ilitek_init_core_func();
	if(res < 0)
	{
		DBG_ERR("init core funcs failed %d", res);
		return res;
	}

	return SUCCESS;
}
EXPORT_SYMBOL(ilitek_init);
