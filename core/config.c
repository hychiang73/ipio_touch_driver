#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/delay.h>

#include "../chip.h"
#include "config.h"
#include "i2c.h"

#define SUCCESS 0

#define DBG_LEVEL

#define DBG_INFO(fmt, arg...) \
			printk(KERN_INFO "ILITEK: (%s): %d: " fmt "\n", __func__, __LINE__, ##arg);

#define DBG_ERR(fmt, arg...) \
			printk(KERN_ERR "ILITEK: (%s): %d: " fmt "\n", __func__, __LINE__, ##arg);

static int ICEInit_212x(void);

typedef struct _CONFIG_TAB {
    unsigned int chip_id;
    unsigned int slave_i2c_addr;
    unsigned int ice_mode_addr;
    unsigned int pid_addr;
    int (*IceModeInit)(void);
} CONFIG_TAB;

CONFIG_TAB CTAB[] = {
    {CHIP_TYPE_ILI2120, 0x41, 0x181062, 0x4009C, ICEInit_212x}
};

unsigned int SLAVE_I2C_ADDR = 0;
unsigned int ICE_MODE_ADDR = 0;
unsigned int PID_ADDR = 0;

static signed int ReadWriteICEMode(unsigned int addr)
{
    int rc;
    unsigned int data = 0;
    char szOutBuf[64] = {0};

    DBG_INFO();

    szOutBuf[0] = 0x25;
    szOutBuf[1] = (char)((addr & 0x000000FF) >> 0);
    szOutBuf[2] = (char)((addr & 0x0000FF00) >> 8);
    szOutBuf[3] = (char)((addr & 0x00FF0000) >> 16);

//    rc = core_i2c_write(SLAVE_I2C_ADDR, szOutBuf, 4);
//    rc = core_i2c_read(SLAVE_I2C_ADDR, szOutBuf, 4);

    data = (szOutBuf[0] + szOutBuf[1] * 256 + szOutBuf[2] * 256 * 256 + szOutBuf[3] * 256 * 256 * 256);

    DBG_INFO("nData=0x%x, szOutBuf[0]=%x, szOutBuf[1]=%x, szOutBuf[2]=%x, szOutBuf[3]=%x\n", data, szOutBuf[0], szOutBuf[1], szOutBuf[2], szOutBuf[3]);

    return data;

}

static int WriteICEMode(unsigned int addr, unsigned int data, unsigned int size)
{
    int rc = 0, i = 0;
    char szOutBuf[64] = {0};

    DBG_INFO();

    szOutBuf[0] = 0x25;
    szOutBuf[1] = (char)((addr & 0x000000FF) >> 0);
    szOutBuf[2] = (char)((addr & 0x0000FF00) >> 8);
    szOutBuf[3] = (char)((addr & 0x00FF0000) >> 16);

    for (i = 0; i < size; i ++)
    {
        szOutBuf[i + 4] = (char)(data >> (8 * i));
    }

//    rc = core_i2c_write(SLAVE_I2C_ADDR, szOutBuf, size + 4);

    return rc;
}

static signed int ReadWriteOneByte(unsigned int addr)
{
    int rc = 0;
    signed int data = 0;
    char szOutBuf[64] = {0};

    DBG_INFO();

    szOutBuf[0] = 0x25;
    szOutBuf[1] = (char)((addr & 0x000000FF) >> 0);
    szOutBuf[2] = (char)((addr & 0x0000FF00) >> 8);
    szOutBuf[3] = (char)((addr & 0x00FF0000) >> 16);

//    rc = core_i2c_write(SLAVE_I2C_ID_DWI2C, szOutBuf, 4);
//    rc = core_i2c_read(SLAVE_I2C_ID_DWI2C, szOutBuf, 1);

    data = (szOutBuf[0]);

    return data;
}

static signed int vfIceRegRead(unsigned int addr)
{
    int i, nInTimeCount = 100;
    unsigned char szBuf[4] = {0};

    DBG_INFO();

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
    int MS_TS_MSG_IC_GPIO_RST = 0;

    DBG_INFO();

    if (MS_TS_MSG_IC_GPIO_RST > 0)
    {
    //    DrvTouchDeviceHwReset();
    }
	else
	{
        DBG_INFO("ICE mode reset\n");

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
    }

    mdelay(50);

    return SUCCESS;
}

static int ICEInit_212x(void)
{
    DBG_INFO();

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

static int EnterICEMode(void)
{
    DBG_INFO();

    // Entry ICE Mode
    if (WriteICEMode(ICE_MODE_ADDR, 0x0, 0) < 0)
        return -EFAULT;

	return SUCCESS;
}

int core_config_get_chip(unsigned int id)
{
    int i, rc = 0;
    unsigned int RealID = 0, PIDData = 0;

    DBG_INFO();

    if(id != 0)
    {
        for(i = 0; i < sizeof(CTAB); i++)
        {
            if(id == CTAB[i].chip_id)
            {
                SLAVE_I2C_ADDR = CTAB[i].slave_i2c_addr;
                ICE_MODE_ADDR = CTAB[i].ice_mode_addr;
                PID_ADDR = CTAB[i].pid_addr;

                rc = EnterICEMode();

                if(rc)
                {
                    CTAB[i].IceModeInit();
                    PIDData = ReadWriteICEMode(PID_ADDR);
                    if ((PIDData & 0xFFFFFF00) == 0)
                    {
                        RealID = (vfIceRegRead(0xF001) << (8 * 1)) + (vfIceRegRead(0xF000));

                        DBG_INFO("Chip = 0x%x", RealID);

                        if (0xFFFF == RealID)
                        {
                            RealID = CHIP_TYPE_ILI2120;
                        }
                    }

                    ExitIceMode();
                }

                DBG_INFO(" CHIP ID = %x ", RealID);

                if(RealID != CTAB[i].chip_id)
                {
                    DBG_ERR("Get Wrong Chip ID %x : %x", RealID, CTAB[i].chip_id);
                    return -ENODEV;
                }
                return RealID;
            }
        }
        return -ENODEV;
    }
    return -EFAULT;
}
