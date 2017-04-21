#include <linux/errno.h>
#include <linux/i2c.h>
#include <linux/slab.h>

#include "../chip.h"
#include "config.h"
#include "i2c.h"

#define DBG_LEVEL

#define DBG_INFO(fmt, arg...) \
			printk(KERN_INFO "ILITEK: (%s): %d: " fmt "\n", __func__, __LINE__, ##arg);

#define DBG_ERR(fmt, arg...) \
			printk(KERN_ERR "ILITEK: (%s): %d: " fmt "\n", __func__, __LINE__, ##arg);

CORE_I2C *core_i2c;
extern CORE_CONFIG *core_config;

int core_i2c_write(unsigned char nSlaveId, unsigned char *pBuf, unsigned short nSize)
{
    int res = -EINVAL, i;

    struct i2c_msg msgs[] =
    {
        {
            .addr = nSlaveId,
            .flags = 0, // if read flag is undefined, then it means write flag.
            .len = nSize,
            .buf = pBuf,
        },
    };

	msgs[0].scl_rate = 400000;

    /*
     * If everything went ok (i.e. 1 msg transmitted), return #bytes
     * transmitted, else error code.
     */
	if(i2c_transfer(core_i2c->client->adapter, msgs, 1) > 0)
		res = nSize;
	else
		DBG_ERR("I2C Write Error");
	
	return res;
}
EXPORT_SYMBOL(core_i2c_write);

int core_i2c_read(unsigned char nSlaveId, unsigned char *pBuf, unsigned short nSize)
{
    int rc = -EINVAL, i;

    struct i2c_msg msgs[] =
    {
        {
            .addr = nSlaveId,
            .flags = I2C_M_RD, // read flag
            .len = nSize,
            .buf = pBuf,
        },
    };

    msgs[0].scl_rate = 400000;

	if(i2c_transfer(core_i2c->client->adapter, msgs, 1) > 0)
		rc = nSize;
	else
		DBG_ERR("I2C Write Error");

    return rc;
}
EXPORT_SYMBOL(core_i2c_read);

int core_i2c_init(struct i2c_client *client)
{
	core_i2c = (CORE_I2C*)kmalloc(sizeof(*core_i2c), GFP_KERNEL);

	DBG_INFO();

	if(core_i2c == NULL) 
	{
		DBG_ERR("init core-i2c failed !");
		return -EINVAL;
	}

	core_i2c->client = client;

	return SUCCESS;
}
