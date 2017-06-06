/*
 * ILITEK Touch IC driver
 *
 * Copyright (C) 2011 ILI Technology Corporation.
 *
 * Author: Dicky Chiang <dicky_chiang@ilitek.com>
 * Based on TDD v7.0 implemented by Mstar & ILITEK
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include "platform.h"

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_gpio.h>
#define DTS_INT_GPIO	"touch,irq-gpio"
#define DTS_RESET_GPIO	"touch,reset-gpio"
#endif

#define I2C_DEVICE_ID	"ILITEK_TP_ID"

struct work_struct report_work_queue;
struct mutex MUTEX;
spinlock_t SPIN_LOCK;
platform_info *TIC;

int isr_gpio = 0;

extern CORE_CONFIG			*core_config;
extern CORE_FINGER_REPORT	*core_fr;

/*
 * The function is exported by other c files 
 * allowing them to disable IRQ.
 *
 */
void ilitek_platform_disable_irq(void)
{
    unsigned long nIrqFlag;

	DBG_INFO();

    spin_lock_irqsave(&SPIN_LOCK, nIrqFlag);

	if(TIC->isIrqEnable == true)
	{
		if(isr_gpio)
		{
			disable_irq_nosync(isr_gpio);
			TIC->isIrqEnable = false;
			DBG_INFO("IRQ was disabled");
		}
		else
		{
			DBG_ERR("The number of gpio to irq is incorrect");
		}
	}
	else
	{
		DBG_INFO("IRQ was already disabled");
	}

    spin_unlock_irqrestore(&SPIN_LOCK, nIrqFlag);
}
EXPORT_SYMBOL(ilitek_platform_disable_irq);

/*
 * The function is exported by other c files 
 * allowing them to enable IRQ.
 *
 */
void ilitek_platform_enable_irq(void)
{
    unsigned long nIrqFlag;

	DBG_INFO();

    spin_lock_irqsave(&SPIN_LOCK, nIrqFlag);

	if(TIC->isIrqEnable == false)
	{
		if(isr_gpio)
		{
			enable_irq(isr_gpio);
			TIC->isIrqEnable = true;
			DBG_INFO("IRQ was enabled");
		}
		else
		{
			DBG_ERR("The number of gpio to irq is incorrect");
		}
	}
	else
	{
		DBG_INFO("IRQ was already enabled");
	}

    spin_unlock_irqrestore(&SPIN_LOCK, nIrqFlag);
}
EXPORT_SYMBOL(ilitek_platform_enable_irq);

void ilitek_platform_tp_power_on(bool isEnable)
{
	if(isEnable)
	{
		gpio_direction_output(TIC->reset_gpio, 1);
		mdelay(TIC->delay_time_high);
		gpio_set_value(TIC->reset_gpio, 0);
		mdelay(TIC->delay_time_low);
		gpio_set_value(TIC->reset_gpio, 1);
		mdelay(TIC->delay_time_high);
	}
	else
	{
		gpio_set_value(TIC->reset_gpio, 0);
	}
}
EXPORT_SYMBOL(ilitek_platform_tp_power_on);

#ifdef CONFIG_HAS_EARLYSUSPEND
static void ilitek_platform_late_resume(struct early_suspend *h)
{
	DBG_INFO();

	core_fr->isEnableFR = true;

	ilitek_platform_tp_power_on(true);
}

static void ilitek_platform_early_suspend(struct early_suspend *h)
{
	DBG_INFO();

	//TODO: there is doing nothing if an upgrade firmware's processing.

	core_fr_touch_release(0,0,0);

	input_sync(core_fr->input_device);

	core_fr->isEnableFR = false;

	ilitek_platform_tp_power_on(false);
}
#endif

/*
 * This queue is activated by an interrupt.
 *
 * Typically it only allows one interrupt coming to call before
 * the event of figner touch is completed.
 *
 */
static void ilitek_platform_work_queue(struct work_struct *work)
{
    unsigned long nIrqFlag;

	DBG_INFO();

    spin_lock_irqsave(&SPIN_LOCK, nIrqFlag);

	if(!TIC->isIrqEnable)
	{
		core_fr_handler();

		enable_irq(isr_gpio);
	}

	spin_unlock_irqrestore(&SPIN_LOCK, nIrqFlag);

	TIC->isIrqEnable = true;
}

/*
 * It is registered by ISR to activate a function with work queue
 *
 */
static irqreturn_t ilitek_platform_irq_handler(int irq, void *dev_id)
{
    unsigned long nIrqFlag;

	DBG_INFO();

    spin_lock_irqsave(&SPIN_LOCK, nIrqFlag);

	if(TIC->isIrqEnable)
	{
		disable_irq_nosync(isr_gpio);

		schedule_work(&report_work_queue);
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
	
	INIT_WORK(&report_work_queue, ilitek_platform_work_queue);

	isr_gpio = gpio_to_irq(TIC->int_gpio);

	DBG_INFO("isr_gpio = %d", isr_gpio);

    res = request_threaded_irq(
				isr_gpio,
				NULL,
				ilitek_platform_irq_handler,
				IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
				"ilitek",
				NULL);

	if(res != 0)
	{
		DBG_ERR("Failed to register irq handler, irq = %d, res = %d",
				isr_gpio,
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

	//TODO: implemente gpio config if a platform isn't set up by dts.
	
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
static int ilitek_platform_read_tp_info(void)
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

	res = core_config_get_core_ver();
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
 * Remove Core APIs memeory being allocated.
 *
 */
static int ilitek_platform_core_remove(void)
{
	core_config_remove();
	core_i2c_remove();
	core_firmware_remove();
	core_fr_remove();
	ilitek_proc_remove();
}

/*
 * The function is to initialise all necessary structurs in those core APIs,
 * they must be called before the i2c dev probes up successfully.
 *
 */
static int ilitek_platform_core_init(void)
{
	DBG_INFO();

	if(core_config_init() < 0 ||
		core_i2c_init(TIC->client) < 0 ||
		core_firmware_init() < 0 ||
		core_fr_init(TIC->client) < 0)
	{
		ilitek_platform_core_remove();
		return -EINVAL;
	}

	ilitek_proc_init();

	return 0;
}

static int ilitek_platform_remove(struct i2c_client *client)
{
	DBG_INFO();

	if(TIC->isIrqEnable)
	{
		disable_irq_nosync(isr_gpio);
	}

	if(isr_gpio != 0 && TIC->int_gpio != 0 && TIC->reset_gpio != 0)
	{
		free_irq(isr_gpio, (void *)TIC->i2c_id);

		gpio_free(TIC->int_gpio);
		gpio_free(TIC->reset_gpio);
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&TIC->early_suspend);
#endif

	ilitek_platform_core_remove();

	kfree(TIC);
}

/*
 * The probe func would be called after an i2c device was detected by kernel.
 *
 * It will still return zero even if it couldn't get a touch ic info.
 * The reason for why we allow it passing the process is because users/developers
 * might want to have access to ICE mode to upgrade a firwmare forcelly.
 *
 */
static int ilitek_platform_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int res = 0;

    if (client == NULL)
    {
        DBG_ERR("i2c client is NULL");
        return -ENODEV;
	}

	// initialise the struct of touch ic memebers.
	TIC = (platform_info*)kmalloc(sizeof(*TIC), GFP_KERNEL);
	TIC->client = client;
	TIC->i2c_id = id;
	TIC->chip_id = ON_BOARD_IC; // it must match the chip what you're using on board.
	TIC->isIrqEnable = false;
	
#ifdef CONFIG_HAS_EARLYSUSPEND
	TIC->early_suspend->suspend = ilitek_platform_early_suspend;
	TIC->early_suspend->esume  = ilitek_platform_late_resume;
	TIC->early_suspend->level   = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	register_early_suspend(TIC->early_suspend);
#endif

	DBG_INFO("Driver version : %s", DRIVER_VERSION);
	DBG_INFO("Touch IC supported by the driver is %x", ON_BOARD_IC);

	// Different ICs may require different delay time for the reset.
	// They may also depend on what your platform need to.
	if(TIC->chip_id == CHIP_TYPE_ILI2121)
	{
		TIC->delay_time_high = 10;
		TIC->delay_time_low = 10;
	}
	else if(TIC->chip_id == CHIP_TYPE_ILI7807)
	{
		TIC->delay_time_high = 10;
		TIC->delay_time_low = 5;
	}
	else if(TIC->chip_id == CHIP_TYPE_ILI9881)
	{
		TIC->delay_time_high = 10;
		TIC->delay_time_low = 5;
	}
	else
	{
		TIC->delay_time_high = 10;
		TIC->delay_time_low = 10;
	}

    mutex_init(&MUTEX);
    spin_lock_init(&SPIN_LOCK);

	res = ilitek_platform_gpio();
	if(res < 0)
	{
		DBG_ERR("Failed to request gpios ");
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
		goto out;
	}

	ilitek_platform_tp_power_on(true);

	res = ilitek_platform_read_tp_info();
	if(res < 0)
	{
		DBG_ERR("Failed to read IC info");
	}

	return 0;

out:
	ilitek_platform_remove(TIC->client);
	return res;

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
MODULE_AUTHOR("ILITEK");
MODULE_LICENSE("GPL");
