#include "ilitek.h"

#define I2C_DEVICE_ID	"RK3288_TP_ID"

MODULE_AUTHOR("ILITEK");
MODULE_LICENSE("GPL");

ilitek_device *ilitek;

static int rk3288_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int type;

	DBG_INFO("Enter probe function"); 

    if (client == NULL)
    {
        DBG_ERR("i2c client is NULL");
        return -ENODEV;
	}

	ilitek = ilitek_init(client, id);


//	DBG_INFO("DeviceInfo = %x", DeviceInfo);

	return 0;
}

static int rk3288_remove(struct i2c_client *client)
{
	DBG_INFO("Enter remove function");
}

static const struct i2c_device_id tp_device_id[] =
{
    {I2C_DEVICE_ID, 0},
    {}, /* should not omitted */
};

MODULE_DEVICE_TABLE(i2c, tp_device_id);

static struct of_device_id tp_match_table[] = {
	{ .compatible = "tchip,ilitek"},
    {},
};

static struct i2c_driver tp_i2c_driver =
{
    .driver = {
        .name = I2C_DEVICE_ID,
        .owner = THIS_MODULE,
        .of_match_table = tp_match_table,
    },
    .probe = rk3288_probe,
    .remove = rk3288_remove,
    .id_table = tp_device_id,
};

static int __init rk3288_init(void)
{
	int ret = 0;

	ret = i2c_add_driver(&tp_i2c_driver);

	if(ret < 0)
	{
		DBG_ERR("Failed to add i2c driver");
		return -ENODEV;
	}

	DBG_INFO("Succeed to add i2c driver");

	return ret;
}

static void __exit rk3288_exit(void)
{
	DBG_INFO("i2c driver has been removed");

	i2c_del_driver(&tp_i2c_driver);
}


module_init(rk3288_init);
module_exit(rk3288_exit);
