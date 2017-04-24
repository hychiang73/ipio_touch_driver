#include "adapter.h"
#include "chip.h"

#define I2C_DEVICE_ID	"ILITEK_TP_ID"

MODULE_AUTHOR("ILITEK");
MODULE_LICENSE("GPL");

extern ilitek_device *adapter;

static int ilitek_platform_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int res = 0;
#ifdef CONFIG_OF
	struct device_node *dev_node = client->dev.of_node;
	uint32_t flag;
	uint32_t pData[2] = {0};
#endif

    if (client == NULL)
    {
        DBG_ERR("i2c client is NULL");
        return -ENODEV;
	}

#ifdef CONFIG_OF
	//adapter->irq_gpio = of_get_named_gpio_flags(dev_node, DTS_IRQ_GPIO, 0, &flag);
	//adapter->reset_gpio = of_get_named_gpio_flags(dev_node, DTS_RESET_GPIO, 0, &flag);
	pData[0] = of_get_named_gpio_flags(dev_node, DTS_IRQ_GPIO, 0, &flag);
	pData[1] = of_get_named_gpio_flags(dev_node, DTS_RESET_GPIO, 0, &flag);

	if(!gpio_is_valid(pData[0]))
	{
		DBG_ERR("Invalid irq gpio: %d", pData[0]);
		return -EBADR;
	}

	if(!gpio_is_valid(pData[1]))
	{
		DBG_ERR("Invalid reset gpio: %d", pData[1]);
		return -EBADR;
	}
	DBG_INFO("gpio = %d", pData[0]);

	DBG_INFO("gpio = %d", pData[1]);
#endif

	res = ilitek_init(client, id, pData);
	if(res < 0)
	{
        DBG_ERR("Initialising ilitek adapter failed %d ", res);
		return -EINVAL;
	}

	ilitek_read_tp_info();

	return SUCCESS;
}

static int ilitek_platform_remove(struct i2c_client *client)
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
    .probe = ilitek_platform_probe,
    .remove = ilitek_platform_remove,
    .id_table = tp_device_id,
};

static int __init ilitek_platform_init(void)
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

static void __exit ilitek_platform_exit(void)
{
	DBG_INFO("i2c driver has been removed");

	i2c_del_driver(&tp_i2c_driver);
}


module_init(ilitek_platform_init);
module_exit(ilitek_platform_exit);
