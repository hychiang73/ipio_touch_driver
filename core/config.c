#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/i2c.h>

#ifdef CONFIG_OF
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#endif

#include "../chip.h"
#include "config.h"
#include "i2c.h"

extern uint32_t SUP_CHIP_LIST[];
extern int nums_chip;

CORE_CONFIG *core_config;

// the length returned from touch ic after command.
int fw_cmd_len = 0;
int protocol_cmd_len = 0;
int tp_info_len = 0;
int key_info_len = 0;
int core_cmd_len = 0;

// protocol commands defined on chip.h
// it's able to store 10 commands as default.
uint8_t pcmd[10] = {0};

/*
 * assign i2c commands according to the version of protocol.
 *
 * @protocol_ver: the version is currently using on firmware.
 */
static void set_protocol_cmd(uint32_t protocol_ver)
{

	if(protocol_ver == ILITEK_PROTOCOL_V3_2)
	{
		fw_cmd_len = 4;
		protocol_cmd_len = 4;
		tp_info_len = 10;
		key_info_len = 29;

		pcmd[0] = PCMD_3_2_GET_TP_INFORMATION;
		pcmd[1] = PCMD_3_2_GET_FIRMWARE_VERSION;
		pcmd[2] = PCMD_3_2_GET_PROTOCOL_VERSION;
		pcmd[3] = PCMD_3_2_GET_KEY_INFORMATION;

	}
	else if(protocol_ver == ILITEK_PROTOCOL_V5_0)
	{
		fw_cmd_len = 4;
		protocol_cmd_len = 3;
		tp_info_len = 12;
		key_info_len = 30;
		core_cmd_len = 5;

		pcmd[0] = PCMD_5_0_GET_TP_INFORMATION;
		pcmd[1] = PCMD_5_0_GET_FIRMWARE_VERSION;
		pcmd[2] = PCMD_5_0_GET_PROTOCOL_VERSION;
		pcmd[3] = PCMD_5_0_GET_KEY_INFORMATION;
		pcmd[4] = PCMD_5_0_GET_CORE_VERSION;
	}
}

/*
 * It checks chip id shifting sepcific bits based on chip's requirement.
 *
 * @pid_data: 4 bytes, reading from firmware.
 *
 */
static uint32_t check_chip_id(uint32_t pid_data)
{
	uint32_t id = 0;
	uint32_t flag = 0;

	if(core_config->chip_id == CHIP_TYPE_ILI2121)
	{
		id = (vfIceRegRead(0xF001) << (8 * 1)) + (vfIceRegRead(0xF000));
	}

	if(core_config->chip_id == CHIP_TYPE_ILI7807)
	{
		id = pid_data >> 16;
		flag = pid_data & 0x0000FFFF;

		// ILI7807F
		if(flag == 0x0001)
		{
			core_config->ic_reset_addr = 0x4004C;

		}

		// ILI7807H
		if(flag == 0x1101)
		{
			core_config->ic_reset_addr = 0x40050;
		}
	}

	return id;
}

/*
 * Read & Write one byte in ICE Mode.
 *
 */
uint32_t core_config_read_write_onebyte(uint32_t addr)
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
EXPORT_SYMBOL(core_config_read_write_onebyte);

/*
 * Reading data from firmware in ICE Mode.
 *
 */
uint32_t core_config_ice_mode_read(uint32_t addr)
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
EXPORT_SYMBOL(core_config_ice_mode_read);

/*
 * Write commands into firmware in ICE Mode.
 *
 */
int core_config_ice_mode_write(uint32_t addr, uint32_t data, uint32_t size)
{
    int res = 0, i;
    uint8_t szOutBuf[64] = {0};

	DBG_INFO();

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
EXPORT_SYMBOL(core_config_ice_mode_write);

uint32_t vfIceRegRead(uint32_t addr)
{
    int i, nInTimeCount = 100;
    uint8_t szBuf[4] = {0};

    core_config_ice_mode_write(0x41000, 0x3B | (addr << 8), 4);
    core_config_ice_mode_write(0x041004, 0x66AA5500, 4);

    // Check Flag
    // Check Busy Flag
    for (i = 0; i < nInTimeCount; i ++)
    {
        szBuf[0] = core_config_read_write_onebyte(0x41011);

        if ((szBuf[0] & 0x01) == 0)
        {
			break;
        }
        mdelay(5);
    }

    return core_config_read_write_onebyte(0x41012);
}
EXPORT_SYMBOL(vfIceRegRead);


/*
 * Doing soft reset on IC. The feature may only support to ILI7807.
 *
 */
int core_config_ic_reset(uint32_t id)
{
	DBG_INFO();

	core_config_ice_mode_enable();

	if(id == CHIP_TYPE_ILI7807)
	{
		//DBG_INFO("ic reset addr = 0x%x", core_config->ic_reset_addr);
		return core_config_ice_mode_write(core_config->ic_reset_addr, 0x00017807, 4);
	} 

	return 0;
}
EXPORT_SYMBOL(core_config_ic_reset);


/*
 * Doing Reset in ICE Mode.
 *
 */
int core_config_ice_mode_reset(void)
{
    int res = 0;

	DBG_INFO();

	if(core_config->use_protocol == ILITEK_PROTOCOL_V3_2)
	{
		res = core_config_ice_mode_write(0x04004C, 0x2120, 2);
		if (res < 0)
		{
			DBG_ERR("OutWrite(0x04004C, 0x2120, 2) error, res = %d\n", res);
			return res;
		}

		mdelay(10);

		res = core_config_ice_mode_write(0x04004E, 0x01, 1);
		if (res < 0)
		{
			DBG_ERR("OutWrite(0x04004E, 0x01, 1) error, res = %d\n", res);
			return res;
		}
	}
	else if(core_config->use_protocol == ILITEK_PROTOCOL_V5_0)
	{
		// write chip's key
		res = core_config_ice_mode_write(core_config->ic_reset_addr, 0x7807, 2);
		if (res < 0)
		{
			DBG_ERR("OutWrite(0x04004C, 0x7807, 2) error, res = %d\n", res);
			return res;
		}

		mdelay(10);

		// chip reset doing in ice mode
		res = core_config_ice_mode_write(0x04004E, 0x01, 1);
		if (res < 0)
		{
			DBG_ERR("OutWrite(0x04004E, 0x01, 1) error, res = %d\n", res);
			return res;
		}

		mdelay(10);
	}

    return res;
}
EXPORT_SYMBOL(core_config_ice_mode_reset);

/*
 * Leaving out ICE Mode.
 *
 */
int core_config_ice_mode_disable(void)
{
	uint8_t cmd[4];
	DBG_INFO();

	cmd[0] = 0x1b;
	cmd[1] = 0x62;
	cmd[2] = 0x10;
	cmd[3] = 0x18;

	if(core_i2c_write(core_config->slave_i2c_addr, cmd, 4) < 0)
	{
		DBG_ERR("Failed to disable ice mode");
		return -EFAULT;
	}

	return 0;
}
EXPORT_SYMBOL(core_config_ice_mode_disable);

/*
 * Entering into ICE Mode.
 *
 */
int core_config_ice_mode_enable(void)
{
	DBG_INFO();

    if (core_config_ice_mode_write(0x181062, 0x0, 1) < 0)
        return -EFAULT;

	return 0;
}
EXPORT_SYMBOL(core_config_ice_mode_enable);

/*
 * Reseting Watch Dog
 *
 */
int core_config_reset_watch_dog(void)
{
	DBG_INFO();

	if(core_config->chip_id == CHIP_TYPE_ILI2121)
	{
		// close watch dog
		if (core_config_ice_mode_write(0x5200C, 0x0000, 2) < 0)
			return -EFAULT;
		if (core_config_ice_mode_write(0x52020, 0x01, 1) < 0)
			return -EFAULT;
		if (core_config_ice_mode_write(0x52020, 0x00, 1) < 0)
			return -EFAULT;
		if (core_config_ice_mode_write(0x42000, 0x0F154900, 4) < 0)
			return -EFAULT;
		if (core_config_ice_mode_write(0x42014, 0x02, 1) < 0)
			return -EFAULT;
		if (core_config_ice_mode_write(0x42000, 0x00000000, 4) < 0)
			return -EFAULT;
		//---------------------------------
		if (core_config_ice_mode_write(0x041000, 0xab, 1) < 0)
			return -EFAULT;
		if (core_config_ice_mode_write(0x041004, 0x66aa5500, 4) < 0)
			return -EFAULT;
		if (core_config_ice_mode_write(0x04100d, 0x00, 1) < 0)
			return -EFAULT;
		if (core_config_ice_mode_write(0x04100b, 0x03, 1) < 0)
			return -EFAULT;
		if (core_config_ice_mode_write(0x041009, 0x0000, 2) < 0)
			return -EFAULT;
	}
	else if(core_config->chip_id == CHIP_TYPE_ILI7807)
	{
		core_config_ice_mode_write(0x5100C, 0x7, 1);
		core_config_ice_mode_write(0x5100C, 0x78, 1);
	}

    return 0;
}
EXPORT_SYMBOL(core_config_reset_watch_dog);

/*
 * Getting key information from firmware.
 *
 */
int core_config_get_key_info(void)
{
	int res = 0, i;
	uint8_t szReadBuf[key_info_len];
    
	memset(szReadBuf, 0, sizeof(uint8_t) * key_info_len);

	res = core_i2c_write(core_config->slave_i2c_addr, &pcmd[3], 1);
	if (res < 0)
	{
		DBG_ERR("Failed to write a command to get key info, res = %d\n", res);
		return res;
	}

	res = core_i2c_read(core_config->slave_i2c_addr, &szReadBuf[0], key_info_len);
	if (res < 0)
	{
		DBG_ERR("Failed to read buffer of key info, res = %d\n", res);
		return res;
	}

	//for(; i < key_info_len + 1; i++)
		//DBG_INFO("buf[%d] = %x", i , szReadBuf[i]);

	if(core_config->tp_info->nKeyCount)
	{
		
		if(core_config->use_protocol == ILITEK_PROTOCOL_V3_2)
		{

			if(core_config->tp_info->nKeyCount > 5)
			{
				res = core_i2c_read(core_config->slave_i2c_addr, (szReadBuf+29), 25);
				if(res < 0)
				{
					DBG_ERR("Failed to read buffer of key info, res = %d\n", res);
					return res;
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
		else if(core_config->use_protocol == ILITEK_PROTOCOL_V5_0)
		{
			//TODO: Firmware not ready yet
			#if 0
			core_config->tp_info->nKeyAreaXLength = (szReadBuf[0] << 8) + szReadBuf[1];
			core_config->tp_info->nKeyAreaYLength = (szReadBuf[2] << 8) + szReadBuf[3];

			for (i = 0; i < core_config->tp_info->nKeyCount; i ++)
			{
				core_config->tp_info->virtual_key[i].nId = szReadBuf[i*5+4];
				core_config->tp_info->virtual_key[i].nX = (szReadBuf[i*5+5] << 8) + szReadBuf[i*5+6];
				core_config->tp_info->virtual_key[i].nY = (szReadBuf[i*5+7] << 8) + szReadBuf[i*5+8];
				core_config->tp_info->virtual_key[i].nStatus = 0;
			}
			#endif
		}
	}

	return res;
}
EXPORT_SYMBOL(core_config_get_key_info);

/*
 * Getting TP information from firmware.
 *
 */
int core_config_get_tp_info(void)
{
	int res = 0, i = 0;
	uint8_t szReadBuf[tp_info_len];
    
	memset(szReadBuf, 0, sizeof(uint8_t) * tp_info_len);

    res = core_i2c_write(core_config->slave_i2c_addr, &pcmd[0], 1);
	if(res < 0)
	{
		DBG_ERR("Get firmware version error %d", res);
		return res;
	}
        
	mdelay(10);

    res = core_i2c_read(core_config->slave_i2c_addr, &szReadBuf[0], tp_info_len);
	if(res < 0)
	{
		DBG_ERR("Get firmware version error %d", res);
		return res;
	}

	//for(; i < tp_info_len+1; i++)
	//	DBG_INFO("buf[%d] = %x", i, szReadBuf[i]);

	if(core_config->use_protocol == ILITEK_PROTOCOL_V3_2)
	{
		core_config->tp_info->nMinX = 0;
		core_config->tp_info->nMinY = 0;
		core_config->tp_info->nMaxX = (szReadBuf[1] << 8) + szReadBuf[0];
		core_config->tp_info->nMaxY = (szReadBuf[3] << 8) + szReadBuf[2];
		core_config->tp_info->nXChannelNum = szReadBuf[4];
		core_config->tp_info->nYChannelNum = szReadBuf[5];
		core_config->tp_info->nMaxTouchNum = szReadBuf[6];
		core_config->tp_info->nMaxKeyButtonNum = szReadBuf[7];
		core_config->tp_info->nKeyCount = szReadBuf[8];

		DBG_INFO("nMaxX=%d, nMaxY=%d",
				core_config->tp_info->nMaxX,
				core_config->tp_info->nMaxY);
		DBG_INFO("nXChannelNum=%d, nYChannelNum=%d",
				core_config->tp_info->nXChannelNum,
				core_config->tp_info->nYChannelNum);
		DBG_INFO("nMaxTouchNum=%d, nMaxKeyButtonNum=%d, nKeyCount=%d",
				core_config->tp_info->nMaxTouchNum,
				core_config->tp_info->nMaxKeyButtonNum,
				core_config->tp_info->nKeyCount);
	}
	else if(core_config->use_protocol == ILITEK_PROTOCOL_V5_0)
	{
		// in protocol v5, ignore the first btye because of a header.
		core_config->tp_info->nMinX = szReadBuf[1];
		core_config->tp_info->nMinY = szReadBuf[2];
		core_config->tp_info->nMaxX = (szReadBuf[4] << 8) + szReadBuf[3];
		core_config->tp_info->nMaxY = (szReadBuf[5] << 8) + szReadBuf[6];
		core_config->tp_info->nXChannelNum = szReadBuf[7];
		core_config->tp_info->nYChannelNum = szReadBuf[8];
		core_config->tp_info->self_tx_channel_num = szReadBuf[9];
		core_config->tp_info->self_rx_channel_num = szReadBuf[10];
		core_config->tp_info->side_touch_type = szReadBuf[11];
		core_config->tp_info->nMaxTouchNum = szReadBuf[12];
		core_config->tp_info->nKeyCount = szReadBuf[13];

		core_config->tp_info->nMaxKeyButtonNum = 5;

		DBG_INFO("minX = %d, minY = %d, maxX = %d, maxY = %d",
				core_config->tp_info->nMinX, core_config->tp_info->nMinY,
				core_config->tp_info->nMaxX, core_config->tp_info->nMaxY);
		DBG_INFO("xchannel = %d, ychannel = %d, self_tx = %d, self_rx = %d", 
				core_config->tp_info->nXChannelNum, core_config->tp_info->nYChannelNum,
				core_config->tp_info->self_tx_channel_num, core_config->tp_info->self_rx_channel_num);
		DBG_INFO("side_touch_type = %d, max_touch_num= %d, touch_key_num = %d, max_key_num = %d",
				core_config->tp_info->side_touch_type, core_config->tp_info->nMaxTouchNum,
				core_config->tp_info->nKeyCount, core_config->tp_info->nMaxKeyButtonNum);
	}

	return res;
}
EXPORT_SYMBOL(core_config_get_tp_info);


/*
 * Getting the version of protocol used on firmware.
 *
 */
int core_config_get_protocol_ver(void)
{
	int res = 0, i = 0;
	uint8_t szReadBuf[protocol_cmd_len];
	uint8_t temp[2];

	memset(szReadBuf, 0, sizeof(uint8_t) * protocol_cmd_len);

    res = core_i2c_write(core_config->slave_i2c_addr, &pcmd[2], 1);
	if(res < 0)
	{
		DBG_ERR("Failed to get protocol version error %d", res);
		return res;
	}
        
	mdelay(10);

    res = core_i2c_read(core_config->slave_i2c_addr, &szReadBuf[0], protocol_cmd_len);
	if(res < 0)
	{
		DBG_ERR("Get firmware version error %d", res);
		return res;
	}
        
	for(; i < protocol_cmd_len; i++)
	{
		//DBG_INFO("szReadbuf[%d] = %d", i, szReadBuf[i]);
		core_config->protocol_ver[i] = szReadBuf[i];
	}

	if(core_config->use_protocol == ILITEK_PROTOCOL_V3_2)
	{
		DBG_INFO("Procotol Version = %d.%d", 
				core_config->protocol_ver[0], 
				core_config->protocol_ver[1]); 
	}
	else if(core_config->use_protocol == ILITEK_PROTOCOL_V5_0)
	{
		// in protocol v5, ignore the first btye because of a header.
		DBG_INFO("Procotol Version = %d.%d", 
				core_config->protocol_ver[1], 
				core_config->protocol_ver[2]); 
	}

	return res;
}
EXPORT_SYMBOL(core_config_get_protocol_ver);

/*
 * Getting the version of core used on firmware.
 *
 * It may be only supported by protocol v5.0
 *
 */
int core_config_get_core_ver(void)
{
	int res = 0, i = 0;
	uint8_t szReadBuf[core_cmd_len];

	memset(szReadBuf, 0, sizeof(uint8_t) * core_cmd_len);

    res = core_i2c_write(core_config->slave_i2c_addr, &pcmd[4], 1);
	if(res < 0)
	{
		DBG_ERR("Failed to write cmd to get fw version %d", res);
		return res;
	}

    mdelay(10);

    res = core_i2c_read(core_config->slave_i2c_addr, &szReadBuf[0], core_cmd_len);
	if(res < 0)
	{
		DBG_ERR("Failed to read fw version %d", res);
		return res;
	}

	for(; i < core_cmd_len; i++)
	{
		core_config->core_ver[i] = szReadBuf[i]; 
	}

	if(core_config->use_protocol == ILITEK_PROTOCOL_V5_0)
	{
		// in protocol v5, ignore the first btye because of a header.
		DBG_INFO("Core Version = %d.%d.%d.%d", 
				core_config->core_ver[1], 
				core_config->core_ver[2], 
				core_config->core_ver[3],
				core_config->core_ver[4]);
	}

	return res;
}
EXPORT_SYMBOL(core_config_get_core_ver);

/*
 * Getting the version of firmware used on the current one.
 *
 */
int core_config_get_fw_ver(void)
{
	int res = 0, i = 0;
	uint8_t szReadBuf[fw_cmd_len];

	memset(szReadBuf, 0, sizeof(uint8_t) * fw_cmd_len);

    res = core_i2c_write(core_config->slave_i2c_addr, &pcmd[1], 1);
	if(res < 0)
	{
		DBG_ERR("Failed to write cmd to get fw version %d", res);
		return res;
	}

    mdelay(10);

    res = core_i2c_read(core_config->slave_i2c_addr, &szReadBuf[0], fw_cmd_len);
	if(res < 0)
	{
		DBG_ERR("Failed to read fw version %d", res);
		return res;
	}

	for(; i < fw_cmd_len; i++)
	{
		//DBG_INFO("szReadbuf[%d] = %d", i, szReadBuf[i]);
		core_config->firmware_ver[i] = szReadBuf[i]; 
	}

	if(core_config->use_protocol == ILITEK_PROTOCOL_V3_2)
	{
		DBG_INFO("Firmware Version = %d.%d.%d.%d", 
				core_config->firmware_ver[0], 
				core_config->firmware_ver[1], 
				core_config->firmware_ver[2],
				core_config->firmware_ver[3]);
	}
	else if(core_config->use_protocol == ILITEK_PROTOCOL_V5_0)
	{
		// in protocol v5, ignore the first btye because of a header.
		DBG_INFO("Firmware Version = %d.%d.%d", 
				core_config->firmware_ver[1], 
				core_config->firmware_ver[2], 
				core_config->firmware_ver[3]);
	}

	return res;
}
EXPORT_SYMBOL(core_config_get_fw_ver);

/*
 * Getting the type of chip used on the board.
 *
 */
int core_config_get_chip_id(void)
{
    int i, res = 0;
    uint32_t RealID = 0, PIDData = 0, flag;

	res = core_config_ice_mode_enable();

	if(res < 0)
	{
		DBG_ERR("Failed to enter ICE mode, res = %d", res);
		return res;
	}

	core_config_reset_watch_dog();

	PIDData = core_config_ice_mode_read(core_config->pid_addr);

	if (PIDData)
	{
		RealID = check_chip_id(PIDData);
		DBG_INFO("Read ID = 0x%x", RealID);

		if(RealID == core_config->chip_id)
		{
			core_config_ic_reset(RealID);

			mdelay(60);

			res = core_config_ice_mode_reset();
			res = core_config_ice_mode_disable();
			if(res < 0)
			{
				DBG_ERR("Failed to exit ICE mode");
				return res;
			}
		}
		else
		{
			DBG_ERR("CHIP ID error : 0x%x , ", RealID);
			return -ENODEV;
		}
	}
	else
	{
		DBG_ERR("PID DATA error : 0x%x", PIDData);
		return -ENODEV;
	}

	return res;
}
EXPORT_SYMBOL(core_config_get_chip_id);

int core_config_init(void)
{
	int i = 0, res = 0;

	for(; i < nums_chip; i++)
	{
		if(SUP_CHIP_LIST[i] == ON_BOARD_IC)
		{
			core_config = (CORE_CONFIG*)kmalloc(sizeof(*core_config) * sizeof(uint8_t) * 6, GFP_KERNEL);
			core_config->tp_info = (TP_INFO*)kmalloc(sizeof(*core_config->tp_info), GFP_KERNEL);

			core_config->chip_id = SUP_CHIP_LIST[i];

			if(core_config->chip_id == CHIP_TYPE_ILI2121)
			{
				core_config->use_protocol	= ILITEK_PROTOCOL_V3_2;
				core_config->slave_i2c_addr = ILI2121_SLAVE_ADDR;
				core_config->ice_mode_addr	= ILI2121_ICE_MODE_ADDR;
				core_config->pid_addr		= ILI2121_PID_ADDR;
				core_config->ic_reset_addr	= 0x0;
			}
			else if(core_config->chip_id == CHIP_TYPE_ILI7807)
			{
				core_config->use_protocol	= ILITEK_PROTOCOL_V5_0;
				core_config->slave_i2c_addr = ILI7807_SLAVE_ADDR;
				core_config->ice_mode_addr	= ILI7807_ICE_MODE_ADDR;
				core_config->pid_addr		= ILI7807_PID_ADDR;
				core_config->ic_reset_addr	= 0x0;
			}
		}
	}

	if(IS_ERR(core_config)) 
	{
		DBG_ERR("Can't find an id from the support list, init core-config failed ");
		res = -ENOMEM;
		goto Err;
	}
	
	set_protocol_cmd(core_config->use_protocol);
	return res;

Err:
	core_config_remove();
	return res;
}
EXPORT_SYMBOL(core_config_init);

void core_config_remove(void)
{
	DBG_INFO();

	kfree(core_config);

	kfree(core_config->tp_info);
}
EXPORT_SYMBOL(core_config_remove);

