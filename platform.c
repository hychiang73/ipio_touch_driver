#include "adapter.h"
#include "chip.h"

#define I2C_DEVICE_ID	"ILITEK_TP_ID"

struct work_struct irq_workqueue;
struct mutex MUTEX;
spinlock_t SPIN_LOCK;

extern ilitek_device *adapter;

MODULE_AUTHOR("ILITEK");
MODULE_LICENSE("GPL");


static int ilitek_platform_work(struct work_struct *work)
{
	int res = 0;

	DBG_INFO();

	return res;
}

static irqreturn_t ilitek_platform_irq_handler(int irq, void *dev_id)
{
    unsigned long nIrqFlag;

	DBG_INFO();

    spin_lock_irqsave(&SPIN_LOCK, nIrqFlag);

    schedule_work(&irq_workqueue);

    spin_unlock_irqrestore(&SPIN_LOCK, nIrqFlag);


    return IRQ_HANDLED;
}

static int ilitek_platform_isr_register(void)
{
	int res = 0;
	uint32_t irq = 0;
	
	INIT_WORK(&irq_workqueue, ilitek_platform_work);

	irq = gpio_to_irq(adapter->int_gpio);

    res = request_threaded_irq(
				irq,
				NULL,
				ilitek_platform_irq_handler,
				IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				"ilitek",
				NULL);

	if(res != 0)
	{
		DBG_ERR("Failed to register irq handler, irq = %d, res = %d", irq, res);
		return res;
	}

	return res;
}

static void ilitek_platform_tp_poweron(void)
{
	DBG_INFO();

	gpio_direction_output(adapter->reset_gpio, 1);
	mdelay(10);
	gpio_set_value(adapter->reset_gpio, 0);
	mdelay(100);
	gpio_set_value(adapter->reset_gpio, 1);
	mdelay(25);
}

static int ilitek_platform_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int res = 0;
#ifdef CONFIG_OF
	struct device_node *dev_node = client->dev.of_node;
	uint32_t flag;
#endif
	uint32_t pData[2] = {0};

    if (client == NULL)
    {
        DBG_ERR("i2c client is NULL");
        return -ENODEV;
	}

#ifdef CONFIG_OF
	pData[0] = of_get_named_gpio_flags(dev_node, DTS_INT_GPIO, 0, &flag);
	pData[1] = of_get_named_gpio_flags(dev_node, DTS_RESET_GPIO, 0, &flag);
#endif

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
	
	DBG_INFO("int gpio = %d", pData[0]);

	DBG_INFO("reset gpio = %d", pData[1]);

	res = gpio_request(pData[0], "ILITEK_TP_IRQ");
	if(res > 0)
	{
		DBG_ERR("Request IRQ GPIO failed, res = %d", res);
		return res;
	}

	res = gpio_request(pData[1], "ILITEK_TP_RESET");
	if(res > 0)
	{
		DBG_ERR("Request RESET GPIO failed, res = %d", res);
		return res;
	}

	gpio_direction_input(pData[0]);

    mutex_init(&MUTEX);
    spin_lock_init(&SPIN_LOCK);
	

	res = ilitek_init(client, id, pData);
	if(res < 0)
	{
        DBG_ERR("Initialising ilitek adapter failed %d ", res);
		return res; 
	}

	ilitek_platform_tp_poweron();

	ilitek_read_tp_info();

	ilitek_platform_isr_register();

	return res;
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
