#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>

#include "../chip.h"
#include "config.h"
#include "i2c.h"

#define DBG_LEVEL

#define DBG_INFO(fmt, arg...) \
			printk(KERN_INFO "ILITEK: (%s): %d: " fmt "\n", __func__, __LINE__, ##arg);

#define DBG_ERR(fmt, arg...) \
			printk(KERN_ERR "ILITEK: (%s): %d: " fmt "\n", __func__, __LINE__, ##arg);

static signed int vfIceRegRead(unsigned int addr);
static signed int ReadWriteOneByte(unsigned int addr);
static signed int ReadWriteICEMode(unsigned int addr);
static int WriteICEMode(unsigned int addr, unsigned int data, unsigned int size);
static int EnterICEMode(void);
static int ICEInit_212x(void);

// This array stores the list of chips supported by the driver.
// Add an id here if you're going to support a new chip.
unsigned short SupChipList[] = {
	CHIP_TYPE_ILI2121
};

CORE_CONFIG *core_config;
EXPORT_SYMBOL(core_config);

static signed int ReadWriteICEMode(unsigned int addr)
{
    int rc;
    unsigned int data = 0;
    char szOutBuf[64] = {0};

    szOutBuf[0] = 0x25;
    szOutBuf[1] = (char)((addr & 0x000000FF) >> 0);
    szOutBuf[2] = (char)((addr & 0x0000FF00) >> 8);
    szOutBuf[3] = (char)((addr & 0x00FF0000) >> 16);

    core_i2c_write(core_config->slave_i2c_addr, szOutBuf, 4);
    core_i2c_read(core_config->slave_i2c_addr, szOutBuf, 4);

    data = (szOutBuf[0] + szOutBuf[1] * 256 + szOutBuf[2] * 256 * 256 + szOutBuf[3] * 256 * 256 * 256);

    return data;

}

static int WriteICEMode(unsigned int addr, unsigned int data, unsigned int size)
{
    int rc = 0, i = 0;
    char szOutBuf[64] = {0};

    szOutBuf[0] = 0x25;
    szOutBuf[1] = (char)((addr & 0x000000FF) >> 0);
    szOutBuf[2] = (char)((addr & 0x0000FF00) >> 8);
    szOutBuf[3] = (char)((addr & 0x00FF0000) >> 16);

    for (i = 0; i < size; i ++)
    {
        szOutBuf[i + 4] = (char)(data >> (8 * i));
    }

    rc = core_i2c_write(core_config->slave_i2c_addr, szOutBuf, size + 4);

    return rc;
}

static signed int ReadWriteOneByte(unsigned int addr)
{
    int rc = 0;
    signed int data = 0;
    char szOutBuf[64] = {0};

    szOutBuf[0] = 0x25;
    szOutBuf[1] = (char)((addr & 0x000000FF) >> 0);
    szOutBuf[2] = (char)((addr & 0x0000FF00) >> 8);
    szOutBuf[3] = (char)((addr & 0x00FF0000) >> 16);

    core_i2c_write(core_config->slave_i2c_addr, szOutBuf, 4);
    core_i2c_read(core_config->slave_i2c_addr, szOutBuf, 1);

    data = (szOutBuf[0]);

    return data;
}

static signed int vfIceRegRead(unsigned int addr)
{
    int i, nInTimeCount = 100;
    unsigned char szBuf[4] = {0};

    WriteICEMode(0x41000, 0x3B | (addr << 8), 4);
    WriteICEMode(0x041004, 0x66AA5500, 4);
    // Check Flag
    // Check Busy Flag
    for (i = 0; i < nInTimeCount; i ++)
    {
        szBuf[0] = ReadWriteOneByte(0x41011);

        if ((szBuf[0] & 0x01) == 0)
        {
			break;
        }
        mdelay(5);
    }

    return ReadWriteOneByte(0x41012);
}

static int ExitIceMode(void)
{
    int rc;

	rc = WriteICEMode(0x04004C, 0x2120, 2);
	if (rc < 0)
	{
		DBG_ERR("OutWrite(0x04004C, 0x2120, 2) error, rc = %d\n", rc);
		return rc;
	}
	mdelay(10);

	rc = WriteICEMode(0x04004E, 0x01, 1);
	if (rc < 0)
	{
		DBG_ERR("OutWrite(0x04004E, 0x01, 1) error, rc = %d\n", rc);
		return rc;
	}

    mdelay(50);

    return SUCCESS;
}

static int EnterICEMode(void)
{
    // Entry ICE Mode
    if (WriteICEMode(core_config->ice_mode_addr, 0x0, 0) < 0)
        return -EFAULT;

	return SUCCESS;
}

static int ICEInit_212x(void)
{
    // close watch dog
    if (WriteICEMode(0x5200C, 0x0000, 2) < 0)
        return -EFAULT;
    if (WriteICEMode(0x52020, 0x01, 1) < 0)
        return -EFAULT;
    if (WriteICEMode(0x52020, 0x00, 1) < 0)
        return -EFAULT;
    if (WriteICEMode(0x42000, 0x0F154900, 4) < 0)
        return -EFAULT;
    if (WriteICEMode(0x42014, 0x02, 1) < 0)
        return -EFAULT;
    if (WriteICEMode(0x42000, 0x00000000, 4) < 0)
        return -EFAULT;
        //---------------------------------
    if (WriteICEMode(0x041000, 0xab, 1) < 0)
        return -EFAULT;
    if (WriteICEMode(0x041004, 0x66aa5500, 4) < 0)
        return -EFAULT;
    if (WriteICEMode(0x04100d, 0x00, 1) < 0)
        return -EFAULT;
    if (WriteICEMode(0x04100b, 0x03, 1) < 0)
        return -EFAULT;
    if (WriteICEMode(0x041009, 0x0000, 2) < 0)
        return -EFAULT;

    return SUCCESS;
}

void core_config_HWReset(void)
{
	DBG_INFO();

//	gpio_direction_output(MS_TS_MSG_IC_GPIO_RST, 1);
	mdelay(10);
//	gpio_set_value(MS_TS_MSG_IC_GPIO_RST, 0);
	mdelay(100);
//	gpio_set_value(MS_TS_MSG_IC_GPIO_RST, 1);
	mdelay(25);
}
EXPORT_SYMBOL(core_config_HWReset);

TP_INFO* core_config_GetResolution(void)
{
	int res = 0, i = 0, resolution_length = 10;
	unsigned char szWriteBuf[2] = {0};
	unsigned char szReadBuf[10] = {0};
    
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

unsigned short core_config_GetProtocolVer(void)
{
	int res = 0, i = 0, protocol_length = 2;
	unsigned char szWriteBuf[2] = {0};
	unsigned char szReadBuf[2] = {0};
	unsigned short ptl_ver;

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
        
	ptl_ver = (szReadBuf[0] << 8) + szReadBuf[1];

	return ptl_ver;
}
EXPORT_SYMBOL(core_config_GetProtocolVer);

unsigned char* core_config_GetFWVer(void)
{
	int res = -1, fw_length = 4;
	unsigned char szWriteBuf[2] = {0};
	unsigned char szReadBuf[4] = {0};
	unsigned char *fw_ver;

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

	fw_ver = szReadBuf;

	return fw_ver;
}
EXPORT_SYMBOL(core_config_GetFWVer);

int core_config_GetChipID(void)
{
    int i, res = 0;
    unsigned int RealID = 0, PIDData = 0;

	res = EnterICEMode();
	if(res < 0)
	{
		DBG_ERR("Failed to enter ICE mode");
		return res;
	}

	core_config->IceModeInit();
	PIDData = ReadWriteICEMode(core_config->pid_addr);

	if ((PIDData & 0xFFFFFF00) == 0)
	{
		RealID = (vfIceRegRead(0xF001) << (8 * 1)) + (vfIceRegRead(0xF000));
		
		if (0xFFFF == RealID)
		{
			RealID = CHIP_TYPE_ILI2121;
		}

		res = ExitIceMode();
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

int core_config_init(unsigned int chip_type)
{
	int i = 0;


	for(; i < sizeof(SupChipList); i++)
	{
		if(SupChipList[i] == chip_type)
		{
			core_config = (CORE_CONFIG*)kmalloc(sizeof(*core_config), GFP_KERNEL);
			core_config->tp_info = (TP_INFO*)kmalloc(sizeof(*core_config->tp_info), GFP_KERNEL);
			core_config->scl = SupChipList;
			core_config->scl_size = sizeof(SupChipList);

			if(chip_type = CHIP_TYPE_ILI2121)
			{
				core_config->chip_id = chip_type;
				core_config->slave_i2c_addr = ILI21XX_SLAVE_ADDR;
				core_config->ice_mode_addr = ILI21XX_ICE_MODE_ADDR;
				core_config->pid_addr = ILI21XX_PID_ADDR;
				core_config->IceModeInit = ICEInit_212x;
			}
		}
	}

	if(core_config == NULL) 
	{
		DBG_ERR("Can't find id from support list, init core-config failed ");
		return -EINVAL;
	}

	return SUCCESS;
}
EXPORT_SYMBOL(core_config_init);
