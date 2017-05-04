#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_gpio.h>
#define DTS_INT_GPIO	"touch,irq-gpio"
#define DTS_RESET_GPIO	"touch,reset-gpio"
#endif

#include "platform.h"

#define I2C_DEVICE_ID	"ILITEK_TP_ID"

struct work_struct irq_work_queue;
struct mutex MUTEX;
spinlock_t SPIN_LOCK;

platform_info *TIC;

MODULE_AUTHOR("ILITEK");
MODULE_LICENSE("GPL");

static void ilitek_platform_finger_report(void)
{
	DBG_INFO();
}

static void ilitek_platform_work_queue(struct work_struct *work)
{
    unsigned long nIrqFlag;

	DBG_INFO();

    spin_lock_irqsave(&SPIN_LOCK, nIrqFlag);

	if(!TIC->isIrqEnable)
	{
		ilitek_platform_finger_report();

		enable_irq(TIC->gpio_to_irq);
	}

	spin_unlock_irqrestore(&SPIN_LOCK, nIrqFlag);

	TIC->isIrqEnable = true;
}

static irqreturn_t ilitek_platform_irq_handler(int irq, void *dev_id)
{
    unsigned long nIrqFlag;

	DBG_INFO();

    spin_lock_irqsave(&SPIN_LOCK, nIrqFlag);

	if(TIC->isIrqEnable)
	{
		disable_irq_nosync(TIC->gpio_to_irq);

		schedule_work(&irq_work_queue);
	}

    spin_unlock_irqrestore(&SPIN_LOCK, nIrqFlag);

	TIC->isIrqEnable = false;

    return IRQ_HANDLED;
}

static int ilitek_platform_isr_register(void)
{
	int res = 0;
	
	INIT_WORK(&irq_work_queue, ilitek_platform_work_queue);

	TIC->gpio_to_irq = gpio_to_irq(TIC->int_gpio);

	DBG_INFO("GPIO_TO_IRQ = %d", TIC->gpio_to_irq);

    res = request_threaded_irq(
				TIC->gpio_to_irq,
				NULL,
				ilitek_platform_irq_handler,
				IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				"ilitek",
				NULL);

	if(res != 0)
	{
		DBG_ERR("Failed to register irq handler, irq = %d, res = %d",
				TIC->gpio_to_irq,
				res);
		return res;
	}

	TIC->isIrqEnable = true;

	return res;
}

static void ilitek_platform_tp_poweron(void)
{
	DBG_INFO();

	gpio_direction_output(TIC->reset_gpio, 1);
	mdelay(10);
	gpio_set_value(TIC->reset_gpio, 0);
	mdelay(100);
	gpio_set_value(TIC->reset_gpio, 1);
	mdelay(25);
}

static int ilitek_platform_gpio(void)
{
	int res = 0;
#ifdef CONFIG_OF
	struct device_node *dev_node = TIC->client->dev.of_node;
	uint32_t flag;
#endif
	uint32_t gpios[2] = {0};// 0: int, 1:reset

#ifdef CONFIG_OF
	gpios[0] = of_get_named_gpio_flags(dev_node, DTS_INT_GPIO, 0, &flag);
	gpios[1] = of_get_named_gpio_flags(dev_node, DTS_RESET_GPIO, 0, &flag);
#endif

	if(!gpio_is_valid(gpios[0]))
	{
		DBG_ERR("Invalid irq gpio: %d", gpios[0]);
		return -EBADR;
	}

	if(!gpio_is_valid(gpios[1]))
	{
		DBG_ERR("Invalid reset gpio: %d", gpios[1]);
		return -EBADR;
	}

	res = gpio_request(gpios[0], "ILITEK_TP_IRQ");
	if(res > 0)
	{
		DBG_ERR("Request IRQ GPIO failed, res = %d", res);
		return res;
	}

	res = gpio_request(gpios[1], "ILITEK_TP_RESET");
	if(res > 0)
	{
		DBG_ERR("Request RESET GPIO failed, res = %d", res);
		return res;
	}

	gpio_direction_input(gpios[0]);

	DBG_INFO("int gpio = %d", gpios[0]);
	DBG_INFO("reset gpio = %d", gpios[1]);

	TIC->int_gpio = gpios[0];
	TIC->reset_gpio = gpios[1];

	return res;
}

static int ilitek_platform_read_tp_info(void)
{
	int res = 0;

	TIC->chip_id = core_config_GetChipID();

	DBG_INFO("CHIP ID = 0x%x", TIC->chip_id);

	TIC->firmware_ver = core_config_GetFWVer();

	if(!TIC->firmware_ver)
	{
		DBG_ERR("Getting FW Ver error");
		return -EFAULT;
	}

	DBG_INFO("Firmware Version = %d.%d.%d.%d", 
			*TIC->firmware_ver, 
			*(TIC->firmware_ver+1),
			*(TIC->firmware_ver+2),
			*(TIC->firmware_ver+3));

	TIC->protocol_ver = core_config_GetProtocolVer();

	if(TIC->protocol_ver < 0)
	{
		DBG_ERR("Getting Protocol Ver error");
		return -EFAULT;
	}

	DBG_INFO("Protocol Version = %x", TIC->protocol_ver);

	TIC->tp_info = core_config_GetResolution();
	TIC->tp_info = core_config_GetKeyInfo();

	if(TIC->tp_info == NULL)
	{
		DBG_ERR("Getting TP Resolution/key info failed");
		return -EFAULT;
	}

    DBG_INFO("nMaxX=%d, nMaxY=%d, nXChannelNum=%d, nYChannelNum=%d, nMaxTouchNum=%d, nMaxKeyButtonNum=%d, nKeyCount=%d",
			TIC->tp_info->nMaxX,
			TIC->tp_info->nMaxY,
			TIC->tp_info->nXChannelNum,
			TIC->tp_info->nYChannelNum,
			TIC->tp_info->nMaxTouchNum,
			TIC->tp_info->nMaxKeyButtonNum,
			TIC->tp_info->nKeyCount);

	return res;
}

static int ilitek_platform_init_core(void)
{
	DBG_INFO();

	if(core_config_init(TIC->chip_id) < 0 ||
		core_i2c_init(TIC->client) < 0)
			return -EINVAL;

	return SUCCESS;
}

static int ilitek_platform_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int res = 0;

	DBG_INFO();

    if (client == NULL)
    {
        DBG_ERR("i2c client is NULL");
        return -ENODEV;
	}

	TIC = (platform_info*)kmalloc(sizeof(platform_info), GFP_KERNEL);

	TIC->client = client;
	TIC->i2c_id = id;
	TIC->chip_id = CHIP_TYPE_ILI2121;
	TIC->isIrqEnable = false;

    mutex_init(&MUTEX);
    spin_lock_init(&SPIN_LOCK);


	res = ilitek_platform_init_core();
	if(res > 0)
	{
		DBG_ERR("Failed to init core APIs");
		return -EINVAL;
	}

	res = ilitek_platform_gpio();
	if(res < 0)
	{
		DBG_ERR("Failed to request gpios ");
		return -EINVAL;
	}

	ilitek_platform_tp_poweron();

	res = ilitek_platform_read_tp_info();
	if(res < 0)
	{
		DBG_ERR("Failed to read TP info");
		return -EINVAL;
	}

	ilitek_platform_isr_register();

	return res;
}

static int ilitek_platform_remove(struct i2c_client *client)
{
	DBG_INFO("Enter remove function");

	if(TIC->isIrqEnable)
	{
		disable_irq_nosync(TIC->gpio_to_irq);
	}

	free_irq(TIC->gpio_to_irq, (void *)TIC->i2c_id);

	gpio_free(TIC->int_gpio);
	gpio_free(TIC->reset_gpio);

	core_config_remove();
	core_i2c_remove();

	ilitek_proc_remove();
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

	ilitek_proc_init();

	DBG_INFO("Succeed to add i2c driver");

	return res;
}

static void __exit ilitek_platform_exit(void)
{
	DBG_INFO("i2c driver has been removed");

	i2c_del_driver(&tp_i2c_driver);
}

module_init(ilitek_platform_init);
module_exit(ilitek_platform_exit);
