#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/slab.h>

#ifdef CONFIG_OF
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#endif

#include "../chip.h"
#include "config.h"
#include "i2c.h"

CORE_CONFIG *core_config;
extern uint32_t SUP_CHIP_LIST[SUPP_CHIP_NUM];

static int ICEInit_212x(void)
{
    // close watch dog
    if (core_config_WriteIceMode(0x5200C, 0x0000, 2) < 0)
        return -EFAULT;
    if (core_config_WriteIceMode(0x52020, 0x01, 1) < 0)
        return -EFAULT;
    if (core_config_WriteIceMode(0x52020, 0x00, 1) < 0)
        return -EFAULT;
    if (core_config_WriteIceMode(0x42000, 0x0F154900, 4) < 0)
        return -EFAULT;
    if (core_config_WriteIceMode(0x42014, 0x02, 1) < 0)
        return -EFAULT;
    if (core_config_WriteIceMode(0x42000, 0x00000000, 4) < 0)
        return -EFAULT;
        //---------------------------------
    if (core_config_WriteIceMode(0x041000, 0xab, 1) < 0)
        return -EFAULT;
    if (core_config_WriteIceMode(0x041004, 0x66aa5500, 4) < 0)
        return -EFAULT;
    if (core_config_WriteIceMode(0x04100d, 0x00, 1) < 0)
        return -EFAULT;
    if (core_config_WriteIceMode(0x04100b, 0x03, 1) < 0)
        return -EFAULT;
    if (core_config_WriteIceMode(0x041009, 0x0000, 2) < 0)
        return -EFAULT;

    return SUCCESS;
}

uint32_t core_config_ReadWriteOneByte(uint32_t addr)
{
    int res = 0;
    uint32_t data = 0;
    uint8_t szOutBuf[64] = {0};

    szOutBuf[0] = 0x25;
    szOutBuf[1] = (char)((addr & 0x000000FF) >> 0);
    szOutBuf[2] = (char)((addr & 0x0000FF00) >> 8);
    szOutBuf[3] = (char)((addr & 0x00FF0000) >> 16);

    res = core_i2c_write(core_config->slave_i2c_addr, szOutBuf, 4);
	if(res < 0)
	{
		DBG_ERR("Failed to write data via i2c, res = %d", res);
		return -EFAULT;
	}

    res = core_i2c_read(core_config->slave_i2c_addr, szOutBuf, 1);
	if(res < 0)
	{
		DBG_ERR("Failed to read data via i2c, res = %d", res);
		return -EFAULT;
	}

    data = (szOutBuf[0]);

    return data;
}
EXPORT_SYMBOL(core_config_ReadWriteOneByte);

uint32_t core_config_ReadIceMode(uint32_t addr)
{
    int res = 0;
    uint8_t szOutBuf[64] = {0};
    uint32_t data = 0;

    szOutBuf[0] = 0x25;
    szOutBuf[1] = (char)((addr & 0x000000FF) >> 0);
    szOutBuf[2] = (char)((addr & 0x0000FF00) >> 8);
    szOutBuf[3] = (char)((addr & 0x00FF0000) >> 16);

    res = core_i2c_write(core_config->slave_i2c_addr, szOutBuf, 4);
	if(res < 0)
	{
		DBG_ERR("Failed to write data via i2c, res = %d", res);
		return -EFAULT;
	}

    res = core_i2c_read(core_config->slave_i2c_addr, szOutBuf, 4);
	if(res < 0)
	{
		DBG_ERR("Failed to read data via i2c, res = %d", res);
		return -EFAULT;
	}

    data = (szOutBuf[0] + szOutBuf[1] * 256 + szOutBuf[2] * 256 * 256 + szOutBuf[3] * 256 * 256 * 256);

    return data;

}
EXPORT_SYMBOL(core_config_ReadIceMode);

int core_config_WriteIceMode(uint32_t addr, uint32_t data, uint32_t size)
{
    int res = 0, i;
    uint8_t szOutBuf[64] = {0};

    szOutBuf[0] = 0x25;
    szOutBuf[1] = (char)((addr & 0x000000FF) >> 0);
    szOutBuf[2] = (char)((addr & 0x0000FF00) >> 8);
    szOutBuf[3] = (char)((addr & 0x00FF0000) >> 16);

    for(i = 0; i < size; i ++)
    {
        szOutBuf[i + 4] = (char)(data >> (8 * i));
    }

    res = core_i2c_write(core_config->slave_i2c_addr, szOutBuf, size + 4);
	if(res < 0)
	{
		DBG_ERR("Failed to write data via i2c, res = %d", res);
		return -EFAULT;
	}

    return res;
}
EXPORT_SYMBOL(core_config_WriteIceMode);

uint32_t vfIceRegRead(uint32_t addr)
{
    int i, nInTimeCount = 100;
    uint8_t szBuf[4] = {0};

    core_config_WriteIceMode(0x41000, 0x3B | (addr << 8), 4);
    core_config_WriteIceMode(0x041004, 0x66AA5500, 4);

    // Check Flag
    // Check Busy Flag
    for (i = 0; i < nInTimeCount; i ++)
    {
        szBuf[0] = core_config_ReadWriteOneByte(0x41011);

        if ((szBuf[0] & 0x01) == 0)
        {
			break;
        }
        mdelay(5);
    }

    return core_config_ReadWriteOneByte(0x41012);
}
EXPORT_SYMBOL(vfIceRegRead);

int core_config_ExitIceMode(void)
{
    int res = 0;

	res = core_config_WriteIceMode(0x04004C, 0x2120, 2);
	if (res < 0)
	{
		DBG_ERR("OutWrite(0x04004C, 0x2120, 2) error, res = %d\n", res);
		return res;
	}
	mdelay(10);

	res = core_config_WriteIceMode(0x04004E, 0x01, 1);
	if (res < 0)
	{
		DBG_ERR("OutWrite(0x04004E, 0x01, 1) error, res = %d\n", res);
		return res;
	}

    mdelay(50);

    return res;
}
EXPORT_SYMBOL(core_config_ExitIceMode);

int core_config_EnterICEMode(void)
{
    // Entry ICE Mode
    if (core_config_WriteIceMode(core_config->ice_mode_addr, 0x0, 0) < 0)
        return -EFAULT;

	return SUCCESS;
}

TP_INFO* core_config_GetKeyInfo(void)
{
	int res = 0, i;
	uint8_t szWriteBuf[2] = {0};
	uint8_t szReadBuf[64] = {0};

	if(core_config->protocol_ver == ILITEK_PROTOCOL_VERSION_3_2)
	{
		szWriteBuf[0] = ILITEK_TP_CMD_GET_KEY_INFORMATION;
		res = core_i2c_write(core_config->slave_i2c_addr, &szWriteBuf[0], 1);
		if (res < 0)
		{
			DBG_ERR("Failed to write a command to get key info, res = %d\n", res);
			return NULL;
		}

		res = core_i2c_read(core_config->slave_i2c_addr, &szReadBuf[0], 29);
		if (res < 0)
		{
			DBG_ERR("Failed to read buffer of key info, res = %d\n", res);
			return NULL;
		}

		if(core_config->tp_info->nKeyCount)
		{
			if(core_config->tp_info->nKeyCount > 5)
			{
				res = core_i2c_read(core_config->slave_i2c_addr, (szReadBuf+29), 25);
				if(res < 0)
				{
					DBG_ERR("Failed to read buffer of key info, res = %d\n", res);
					return NULL;
				}
			}

			core_config->tp_info->nKeyAreaXLength = (szReadBuf[0] << 8) + szReadBuf[1];
			core_config->tp_info->nKeyAreaYLength = (szReadBuf[2] << 8) + szReadBuf[3];

			for (i = 0; i < core_config->tp_info->nKeyCount; i ++)
			{
				core_config->tp_info->virtual_key[i].nId = szReadBuf[i*5+4];
				core_config->tp_info->virtual_key[i].nX = (szReadBuf[i*5+5] << 8) + szReadBuf[i*5+6];
				core_config->tp_info->virtual_key[i].nY = (szReadBuf[i*5+7] << 8) + szReadBuf[i*5+8];
				core_config->tp_info->virtual_key[i].nStatus = 0;
			}
		}
	}

	return core_config->tp_info;
}
EXPORT_SYMBOL(core_config_GetKeyInfo);

TP_INFO* core_config_GetResolution(void)
{
	int res = 0, i = 0, resolution_length = 10;
	uint8_t szWriteBuf[2] = {0};
	uint8_t szReadBuf[10] = {0};
    
	szWriteBuf[0] = ILITEK_TP_CMD_GET_RESOLUTION;

    res = core_i2c_write(core_config->slave_i2c_addr, &szWriteBuf[0], 1);
	if(res < 0)
	{
		DBG_ERR("Get firmware version error %d", res);
		return NULL;
	}
        
	mdelay(10);

    res = core_i2c_read(core_config->slave_i2c_addr, &szReadBuf[0], resolution_length);
	if(res < 0)
	{
		DBG_ERR("Get firmware version error %d", res);
		return NULL;
	}

	core_config->tp_info->nMaxX = szReadBuf[0];
	core_config->tp_info->nMaxX += (szReadBuf[1]*256);
	core_config->tp_info->nMaxY = szReadBuf[2];
	core_config->tp_info->nMaxY += (szReadBuf[3]*256);
	core_config->tp_info->nMinX = 0;
	core_config->tp_info->nMinY = 0;
	core_config->tp_info->nXChannelNum = szReadBuf[4];
	core_config->tp_info->nYChannelNum = szReadBuf[5];
	core_config->tp_info->nMaxTouchNum = szReadBuf[6];
	core_config->tp_info->nMaxKeyButtonNum = szReadBuf[7];
	core_config->tp_info->nKeyCount = szReadBuf[8];

	return core_config->tp_info;
}
EXPORT_SYMBOL(core_config_GetResolution);

uint16_t core_config_GetProtocolVer(void)
{
	int res = 0, i = 0, protocol_length = 2;
	uint8_t szWriteBuf[2] = {0};
	uint8_t szReadBuf[2] = {0};

    szWriteBuf[0] = ILITEK_TP_ILI2121_CMD_GET_PROTOCOL_VERSION;

    res = core_i2c_write(core_config->slave_i2c_addr, &szWriteBuf[0], 1);
	if(res < 0)
	{
		DBG_ERR("Get firmware version error %d", res);
		return -EFAULT;
	}
        
	mdelay(10);

    res = core_i2c_read(core_config->slave_i2c_addr, &szReadBuf[0], protocol_length);
	if(res < 0)
	{
		DBG_ERR("Get firmware version error %d", res);
		return -EFAULT;
	}
        
	core_config->protocol_ver = (szReadBuf[0] << 8) + szReadBuf[1];

	return core_config->protocol_ver;
}
EXPORT_SYMBOL(core_config_GetProtocolVer);

uint8_t* core_config_GetFWVer(void)
{
	int res = 0, fw_length = 4;
	uint8_t szWriteBuf[2] = {0};
	uint8_t szReadBuf[4] = {0};

    szWriteBuf[0] = ILITEK_TP_ILI2121_CMD_GET_FIRMWARE_VERSION;

    res = core_i2c_write(core_config->slave_i2c_addr, &szWriteBuf[0], 1);
	if(res < 0)
	{
		DBG_ERR("Get firmware version error %d", res);
		return NULL;
	}

    mdelay(10);

    res = core_i2c_read(core_config->slave_i2c_addr, &szReadBuf[0], fw_length);
	if(res < 0)
	{
		DBG_ERR("Get firmware version error %d", res);
		return NULL;
	}

	core_config->firmware_ver = szReadBuf;

	return core_config->firmware_ver;
}
EXPORT_SYMBOL(core_config_GetFWVer);

uint32_t core_config_GetChipID(void)
{
    int i, res = 0;
    uint32_t RealID = 0, PIDData = 0;

	res = core_config_EnterICEMode();
	if(res < 0)
	{
		DBG_ERR("Failed to enter ICE mode, res = %d", res);
		return res;
	}

	res = core_config->IceModeInit();
	if(res < 0)
	{
		DBG_ERR("Failed to initialize ICE Mode, res = %d", res);
		return res;
	}

	PIDData = core_config_ReadIceMode(core_config->pid_addr);

	if ((PIDData & 0xFFFFFF00) == 0)
	{
		RealID = (vfIceRegRead(0xF001) << (8 * 1)) + (vfIceRegRead(0xF000));
		
		if (0xFFFF == RealID)
		{
			RealID = CHIP_TYPE_ILI2121;
		}

		res = core_config_ExitIceMode();
		if(res < 0)
			DBG_ERR("Failed to exit ICE mode");
		
			
		if(core_config->chip_id == RealID)
			return RealID;
		else
		{
			DBG_ERR("CHIP ID error : 0x%x , ", RealID, core_config->chip_id);
			return -ENODEV;
		}
	}

	DBG_ERR("PID DATA error : 0x%x", PIDData);

	return -ENODEV;
}
EXPORT_SYMBOL(core_config_GetChipID);

int core_config_init(uint32_t id)
{
	int i = 0;
	int length = sizeof(SUP_CHIP_LIST)/sizeof(uint32_t);

	DBG_INFO();

	for(; i < length; i++)
	{
		if(SUP_CHIP_LIST[i] == id)
		{
			core_config = (CORE_CONFIG*)kmalloc(sizeof(*core_config), GFP_KERNEL);
			core_config->tp_info = (TP_INFO*)kmalloc(sizeof(*core_config->tp_info), GFP_KERNEL);

			if(SUP_CHIP_LIST[i] = CHIP_TYPE_ILI2121)
			{
				core_config->chip_id = id;
				core_config->slave_i2c_addr = ILI21XX_SLAVE_ADDR;
				core_config->ice_mode_addr = ILI21XX_ICE_MODE_ADDR;
				core_config->pid_addr = ILI21XX_PID_ADDR;
				core_config->IceModeInit = ICEInit_212x;
			}
		}
	}

	if(core_config == NULL) 
	{
		DBG_ERR("Can't find an id from the support list, init core-config failed ");
		return -EINVAL;
	}

	return SUCCESS;
}
EXPORT_SYMBOL(core_config_init);

void core_config_remove(void)
{
	DBG_INFO();

	kfree(core_config);

	kfree(core_config->tp_info);
}
EXPORT_SYMBOL(core_config_remove);

