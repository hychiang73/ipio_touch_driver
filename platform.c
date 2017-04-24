#include "ilitek.h"
#include "chip.h"

#define I2C_DEVICE_ID	"RK3288_TP_ID"

MODULE_AUTHOR("ILITEK");
MODULE_LICENSE("GPL");

extern ilitek_device *ilitek_adapter;

static int rk3288_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int res = 0;
	uint8_t *fw_ver;

	DBG_INFO("Enter probe function"); 

    if (client == NULL)
    {
        DBG_ERR("i2c client is NULL");
        return -ENODEV;
	}

	res = ilitek_init(client, id);
	if(res < 0)
	{
        DBG_ERR("Initialising ilitek-adapter failed %d ", res);
		return -EINVAL;
	}

	ilitek_get_chip_type();

	ilitek_get_fw_ver();

	ilitek_get_protocol_ver();
	
	ilitek_get_resolution();

	ilitek_get_keyinfo();



	return SUCCESS;
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
	int res = 0;

	res = i2c_add_driver(&tp_i2c_driver);

	if(res < 0)
	{
		DBG_ERR("Failed to add i2c driver");
		return -ENODEV;
	}

	DBG_INFO("Succeed to add i2c driver");

	return SUCCESS;
}

static void __exit rk3288_exit(void)
{
	DBG_INFO("i2c driver has been removed");

	i2c_del_driver(&tp_i2c_driver);
}


module_init(rk3288_init);
module_exit(rk3288_exit);
