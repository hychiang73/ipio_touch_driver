#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_gpio.h>
#define DTS_INT_GPIO	"touch,irq-gpio"
#define DTS_RESET_GPIO	"touch,reset-gpio"
#endif

#include "platform.h"

// Modify it if want to change the support of an IC by the driver
#define ON_BOARD_IC		CHIP_TYPE_ILI7807
//#define ON_BOARD_IC		CHIP_TYPE_ILI2121

#define I2C_DEVICE_ID	"ILITEK_TP_ID"

extern CORE_CONFIG *core_config;

struct work_struct irq_work_queue;
struct mutex MUTEX;
spinlock_t SPIN_LOCK;
platform_info *TIC;

MODULE_AUTHOR("ILITEK");
MODULE_LICENSE("GPL");

void ilitek_platform_disable_irq(void)
{
    unsigned long nIrqFlag;

	DBG_INFO();

    spin_lock_irqsave(&SPIN_LOCK, nIrqFlag);

	if(TIC->gpio_to_irq && TIC->isIrqEnable == true)
	{
		disable_irq_nosync(TIC->gpio_to_irq);
		TIC->isIrqEnable = false;
	}
	else
		DBG_ERR("Failed to disable IRQ");

    spin_unlock_irqrestore(&SPIN_LOCK, nIrqFlag);
}
EXPORT_SYMBOL(ilitek_platform_disable_irq);

void ilitek_platform_enable_irq(void)
{
    unsigned long nIrqFlag;

	DBG_INFO();

    spin_lock_irqsave(&SPIN_LOCK, nIrqFlag);

	if(TIC->gpio_to_irq && TIC->isIrqEnable == false)
	{
		enable_irq(TIC->gpio_to_irq);
		TIC->isIrqEnable = true;
	}
	else
		DBG_ERR("Failed to enable IRQ");

    spin_unlock_irqrestore(&SPIN_LOCK, nIrqFlag);
}
EXPORT_SYMBOL(ilitek_platform_enable_irq);

/*
 * IC Power on/off
 *
 * The power sequenece should follow a rule defined by a board.
 *
 */
void ilitek_platform_ic_power_on(void)
{
	DBG_INFO();

	gpio_direction_output(TIC->reset_gpio, 1);
	mdelay(10);
	gpio_set_value(TIC->reset_gpio, 0);
	mdelay(10);
	gpio_set_value(TIC->reset_gpio, 1);
	mdelay(5);
}
EXPORT_SYMBOL(ilitek_platform_ic_power_on);

static void ilitek_platform_work_queue(struct work_struct *work)
{
    unsigned long nIrqFlag;

	DBG_INFO();

    spin_lock_irqsave(&SPIN_LOCK, nIrqFlag);

	if(!TIC->isIrqEnable)
	{
		core_fr_handler();

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

/*
 * ISR Register
 *
 * The func not only registers an ISR, but initialises work queue
 * function for the exeuction of finger report.
 *
 */
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

/*
 * Set up INT/RESET pin according to a developement board.
 *
 * You have to figure out how to config the gpios, either
 * by a dts or a board configuration.
 *
 */
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
	if(res < 0)
	{
		DBG_ERR("Request IRQ GPIO failed, res = %d", res);
		return res;
	}

	res = gpio_request(gpios[1], "ILITEK_TP_RESET");
	if(res < 0)
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

/*
 * Get Touch IC information.
 *
 */
static int ilitek_platform_read_ic_info(void)
{
	int res = 0;

	res = core_config_get_chip_id();
	if(res < 0)
	{
		DBG_ERR("Failed to get chip id, res = %d", res);
		return res;
	}

	res = core_config_get_fw_ver();
	if(res < 0)
	{
		DBG_ERR("Failed to get firmware version, res = %d", res);
		return res;
	}

	res = core_config_get_protocol_ver();
	if(res < 0)
	{
		DBG_ERR("Failed to get protocol version, res = %d", res);
		return res;
	}

	res = core_config_get_tp_info();
	if(res < 0)
	{
		DBG_ERR("Failed to get TP information, res = %d", res);
		return res;
	}

	res = core_config_get_key_info();
	if(res < 0)
	{
		DBG_ERR("Failed to get key information, res = %d", res);
		return res;
	}

	return res;
}

/*
 * The func is to init all necessary structurs in those core APIs.
 * It must be called if want to use featurs of toucn ic.
 *
 */
static int ilitek_platform_core_init(void)
{
	DBG_INFO();

	if(core_config_init(TIC->chip_id) < 0 ||
		core_i2c_init(TIC->client) < 0 ||
		core_firmware_init(TIC->chip_id) < 0 ||
		core_fr_init(TIC->chip_id, TIC->client) < 0)
			return -EINVAL;

	return 0;
}

/*
 * Remove Core APIs memeory being allocated.
 *
 */
static int ilitek_platform_core_remove(void)
{
	core_config_remove();
	core_i2c_remove();
	core_firmware_remove();
	core_fr_remove();
}


/*
 * The probe func would be called after an i2c device was detected by kernel.
 *
 * The func still returns zero even if it couldn't get a touch ic info.
 * The reason for why we allow it passing the process is because users/developers
 * might want to have access to ICE mode to upgrade a firwmare forcelly.
 *
 */
static int ilitek_platform_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int res = 0;

	DBG_INFO();

    if (client == NULL)
    {
        DBG_ERR("i2c client is NULL");
        return -ENODEV;
	}

	TIC = (platform_info*)kmalloc(sizeof(*TIC), GFP_KERNEL);

	TIC->client = client;
	TIC->i2c_id = id;
	TIC->chip_id = ON_BOARD_IC;
	TIC->isIrqEnable = false;

    mutex_init(&MUTEX);
    spin_lock_init(&SPIN_LOCK);

	res = ilitek_platform_gpio();
	if(res < 0)
	{
		DBG_ERR("Failed to request gpios ");
		return -EINVAL;
	}

	res = ilitek_platform_isr_register();
	if(res < 0)
	{
		DBG_ERR("Failed to register ISR");
	}

	res = ilitek_platform_core_init();
	if(res < 0)
	{
		DBG_ERR("Failed to init core APIs");
		return res;
	}

	ilitek_platform_ic_power_on();

	res = ilitek_platform_read_ic_info();
	if(res < 0)
	{
		DBG_ERR("Failed to read IC info");
	}

	return 0;
}

static int ilitek_platform_remove(struct i2c_client *client)
{
	DBG_INFO();

	if(TIC->isIrqEnable)
	{
		disable_irq_nosync(TIC->gpio_to_irq);
	}

	free_irq(TIC->gpio_to_irq, (void *)TIC->i2c_id);

	gpio_free(TIC->int_gpio);
	gpio_free(TIC->reset_gpio);

	ilitek_platform_core_remove();

	kfree(TIC);
}

static const struct i2c_device_id tp_device_id[] =
{
    {I2C_DEVICE_ID, 0},
    {}, /* should not omitted */
};

MODULE_DEVICE_TABLE(i2c, tp_device_id);

/*
 * The name in the table must match the definiation
 * in a dts file.
 *
 */
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
		i2c_del_driver(&tp_i2c_driver);
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
	ilitek_proc_remove();
}

module_init(ilitek_platform_init);
module_exit(ilitek_platform_exit);
