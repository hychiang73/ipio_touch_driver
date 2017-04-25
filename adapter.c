#include "adapter.h"

ilitek_device *adapter;

static int ilitek_init_core_func(void)
{
	uint32_t send_to_core[] = {
		adapter->chip_id,
		adapter->int_gpio,
		adapter->reset_gpio
	};

	if(core_config_init(send_to_core) < 0 ||
		core_i2c_init(adapter->client) < 0)
			return -EINVAL;

	return SUCCESS;
}

int ilitek_read_tp_info(void)
{
	int res;

	adapter->chip_id = core_config_GetChipID();

	DBG_INFO("CHIP ID = 0x%x", adapter->chip_id);

	adapter->firmware_ver = core_config_GetFWVer();

	if(!adapter->firmware_ver)
	{
		DBG_ERR("Getting FW Ver error");
		return -EFAULT;
	}

	DBG_INFO("Firmware Version = %d.%d.%d.%d", 
			*adapter->firmware_ver, 
			*(adapter->firmware_ver+1),
			*(adapter->firmware_ver+2),
			*(adapter->firmware_ver+3));

	adapter->protocol_ver = core_config_GetProtocolVer();

	if(adapter->protocol_ver < 0)
	{
		DBG_ERR("Getting Protocol Ver error");
		return -EFAULT;
	}

	DBG_INFO("Protocol Version = %x", adapter->protocol_ver);

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

	res = core_config_GetKeyInfo();
	if(res < 0)
	{
		DBG_ERR("Getting key information error, res = %d", res);
		return res;
	}

	return res;
}


int ilitek_init(struct i2c_client *client, const struct i2c_device_id *id, uint32_t *platform_info)
{
	int res = 0;

	DBG_INFO();

	adapter = (ilitek_device*)kmalloc(sizeof(ilitek_device), GFP_KERNEL);

	adapter->client = client;

	adapter->id = id;

	adapter->chip_id = CHIP_TYPE_ILI2121;

	adapter->int_gpio = *platform_info;

	adapter->reset_gpio = *(platform_info+1);

	res = ilitek_init_core_func();
	if(res < 0)
	{
		DBG_ERR("init core funcs failed %d", res);
		return res;
	}

	return res;
}
