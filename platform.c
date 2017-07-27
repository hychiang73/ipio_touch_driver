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
#define DEBUG

#include "platform.h"

#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_gpio.h>
#define DTS_INT_GPIO	"touch,irq-gpio"
#define DTS_RESET_GPIO	"touch,reset-gpio"
#define DTS_OF_NAME		"tchip,ilitek"
#endif

#define I2C_DEVICE_ID	"ILITEK_TP_ID"

struct ilitek_platform_data *ipd;

void ilitek_platform_disable_irq(void)
{
	unsigned long nIrqFlag;

	spin_lock_irqsave(&ipd->SPIN_LOCK, nIrqFlag);

	if (ipd->isIrqEnable == true)
	{
		if (ipd->isr_gpio)
		{
			disable_irq_nosync(ipd->isr_gpio);
			ipd->isIrqEnable = false;
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

	spin_unlock_irqrestore(&ipd->SPIN_LOCK, nIrqFlag);
}
EXPORT_SYMBOL(ilitek_platform_disable_irq);

void ilitek_platform_enable_irq(void)
{
	unsigned long nIrqFlag;

	spin_lock_irqsave(&ipd->SPIN_LOCK, nIrqFlag);

	if (ipd->isIrqEnable == false)
	{
		if (ipd->isr_gpio)
		{
			enable_irq(ipd->isr_gpio);
			ipd->isIrqEnable = true;
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

	spin_unlock_irqrestore(&ipd->SPIN_LOCK, nIrqFlag);
}
EXPORT_SYMBOL(ilitek_platform_enable_irq);

void ilitek_platform_tp_hw_reset(bool isEnable)
{
	DBG("HW Reset: %d ", isEnable);
	if (isEnable)
	{
		gpio_direction_output(ipd->reset_gpio, 1);
		mdelay(ipd->delay_time_high);
		gpio_set_value(ipd->reset_gpio, 0);
		mdelay(ipd->delay_time_low);
		gpio_set_value(ipd->reset_gpio, 1);
		mdelay(ipd->edge_delay);
	}
	else
	{
		gpio_set_value(ipd->reset_gpio, 0);
	}
}
EXPORT_SYMBOL(ilitek_platform_tp_hw_reset);

#ifdef ENABLE_REGULATOR_POWER_ON
void ilitek_regulator_power_on(bool status)
{
	int res = 0;
	DBG_INFO("%s", status ? "POWER ON":"POWER OFF");

	if (status)
	{
		if (ipd->vdd) 
		{
			res = regulator_enable(ipd->vdd);
			if (res < 0)
			{
				DBG_ERR("regulator_enable vdd fail");
			}
		}	
		if (ipd->vdd_i2c) 
		{
			res = regulator_enable(ipd->vdd_i2c);
			if (res < 0) 
			{
				DBG_ERR("regulator_enable vdd_i2c fail");
			}
		}	
	}
	else 
	{
		if (ipd->vdd) 
		{
			res = regulator_disable(ipd->vdd);
			if (res < 0) 
			{
				DBG_ERR("regulator_enable vdd fail");
			}
		}	
		if (ipd->vdd_i2c)
		 {
			res = regulator_disable(ipd->vdd_i2c);
			if (res < 0) 
			{
				DBG_ERR("regulator_enable vdd_i2c fail");
			}
		}	
	}

	mdelay(5);
	return;
}
EXPORT_SYMBOL(ilitek_regulator_power_on);
#endif

#ifdef CONFIG_FB
static int ilitek_platform_notifier_fb(struct notifier_block *self,
									   unsigned long event, void *data)
{
	int *blank;
	struct fb_event *evdata = data;

	DBG_INFO("Notifier's event = %ld", event);

	//TODO: can't disable irq if gesture has enabled.

	if (event == FB_EVENT_BLANK)
	{
		blank = evdata->data;
		if (*blank == FB_BLANK_POWERDOWN)
		{
			// the event of suspend
			DBG_INFO("Touch FB_BLANK_POWERDOWN");
			ilitek_platform_disable_irq();
			core_config_ic_suspend();
		}
		else if (*blank == FB_BLANK_UNBLANK)
		{
			// the event of resume
			DBG_INFO("Touch FB_BLANK_UNBLANK");
			core_config_ic_resume();
			ilitek_platform_enable_irq();
		}
	}

	return 0;
}
#else // CONFIG_HAS_EARLYSUSPEND
static void ilitek_platform_late_resume(struct early_suspend *h)
{
	DBG_INFO();

	core_fr->isEnableFR = true;

	ilitek_platform_enable_irq();

	ilitek_platform_tp_hw_reset(true);
}

static void ilitek_platform_early_suspend(struct early_suspend *h)
{
	DBG_INFO();

	//TODO: there is doing nothing if an upgrade firmware's processing.

	core_fr_touch_release(0, 0, 0);

	input_sync(core_fr->input_device);

	core_fr->isEnableFR = false;

	ilitek_platform_disable_irq();

	ilitek_platform_tp_hw_reset(false);
}
#endif

/*
 * Register a callback function when the event of suspend and resume occurs.
 *
 * The default used to wake up the cb function comes from notifier block mechnaism.
 * If you'd rather liek to use early suspend, CONFIG_HAS_EARLYSUSPEND in kernel config
 * must be enabled.
 */
static int ilitek_platform_reg_suspend(void)
{
	int res = 0;

	DBG_INFO("Register suspend/resume callback function");

#ifdef CONFIG_FB
	ipd->notifier_fb.notifier_call = ilitek_platform_notifier_fb;
	res = fb_register_client(&ipd->notifier_fb);
#else
	ipd->early_suspend->suspend = ilitek_platform_early_suspend;
	ipd->early_suspend->esume = ilitek_platform_late_resume;
	ipd->early_suspend->level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	register_early_suspend(ipd->early_suspend);
#endif

	return res;
}

static void ilitek_platform_work_queue(struct work_struct *work)
{
	unsigned long nIrqFlag;

	DBG("IRQ enable = %d", ipd->isIrqEnable);

	spin_lock_irqsave(&ipd->SPIN_LOCK, nIrqFlag);

	if (!ipd->isIrqEnable)
	{
		core_fr_handler();

		enable_irq(ipd->isr_gpio);
		ipd->isIrqEnable = true;
	}

	spin_unlock_irqrestore(&ipd->SPIN_LOCK, nIrqFlag);
}

static irqreturn_t ilitek_platform_irq_handler(int irq, void *dev_id)
{
	unsigned long nIrqFlag;

	DBG("Calling the function in work queue");

	spin_lock_irqsave(&ipd->SPIN_LOCK, nIrqFlag);

	if (ipd->isIrqEnable)
	{
		disable_irq_nosync(ipd->isr_gpio);
		ipd->isIrqEnable = false;
		schedule_work(&ipd->report_work_queue);
	}

	spin_unlock_irqrestore(&ipd->SPIN_LOCK, nIrqFlag);

	return IRQ_HANDLED;
}

static int ilitek_platform_isr_register(void)
{
	int res = 0;

	INIT_WORK(&ipd->report_work_queue, ilitek_platform_work_queue);

	ipd->isr_gpio = gpio_to_irq(ipd->int_gpio);

	DBG("ipd->isr_gpio = %d", ipd->isr_gpio);

	res = request_threaded_irq(
		ipd->isr_gpio,
		NULL,
		ilitek_platform_irq_handler,
		IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
		"ilitek",
		NULL);

	if (res != 0)
	{
		DBG_ERR("Failed to register irq handler, irq = %d, res = %d",
				ipd->isr_gpio, res);
		goto out;
	}

	ipd->isIrqEnable = true;

out:
	return res;
}

static int ilitek_platform_gpio(void)
{
	int res = 0;
#ifdef CONFIG_OF
	struct device_node *dev_node = ipd->client->dev.of_node;
	uint32_t flag;
#endif

#ifdef CONFIG_OF
	ipd->int_gpio = of_get_named_gpio_flags(dev_node, DTS_INT_GPIO, 0, &flag);
	ipd->reset_gpio = of_get_named_gpio_flags(dev_node, DTS_RESET_GPIO, 0, &flag);
#endif

	DBG_INFO("GPIO INT: %d", ipd->int_gpio);
	DBG_INFO("GPIO RESET: %d", ipd->reset_gpio);

	//TODO: implemente gpio config if a platform isn't set up by dts.

	if (!gpio_is_valid(ipd->int_gpio))
	{
		DBG_ERR("Invalid INT gpio: %d", ipd->int_gpio);
		return -EBADR;
	}

	if (!gpio_is_valid(ipd->reset_gpio))
	{
		DBG_ERR("Invalid RESET gpio: %d", ipd->reset_gpio);
		return -EBADR;
	}

	res = gpio_request(ipd->int_gpio, "ILITEK_TP_IRQ");
	if (res < 0)
	{
		DBG_ERR("Request IRQ GPIO failed, res = %d", res);
		gpio_free(ipd->int_gpio);
		res = gpio_request(ipd->int_gpio, "ILITEK_TP_IRQ");
		if(res < 0)
		{
			DBG_ERR("Retrying request INT GPIO still failed , res = %d", res);
			goto out;
		}
	}

	res = gpio_request(ipd->reset_gpio, "ILITEK_TP_RESET");
	if (res < 0)
	{
		DBG_ERR("Request RESET GPIO failed, res = %d", res);
		gpio_free(ipd->reset_gpio);
		res = gpio_request(ipd->reset_gpio, "ILITEK_TP_RESET");
		if(res < 0)
		{
			DBG_ERR("Retrying request RESET GPIO still failed , res = %d", res);
			goto out;
		}
	}

	gpio_direction_input(ipd->int_gpio);

out:
	return res;
}

static void ilitek_platform_read_tp_info(void)
{
	core_config_get_chip_id();
	core_config_get_fw_ver();
	core_config_get_core_ver();
	core_config_get_protocol_ver();
	core_config_get_tp_info();
	core_config_get_key_info();
}

static int ilitek_platform_input_init(void)
{
	int res = 0;

	ipd->input_device = input_allocate_device();

	if (IS_ERR(ipd->input_device))
	{
		DBG_ERR("Failed to allocate touch input device");
		res = -ENOMEM;
		goto fail_alloc;
	}

	ipd->input_device->name = ipd->client->name;
	ipd->input_device->phys = "I2C";
	ipd->input_device->dev.parent = &ipd->client->dev;
	ipd->input_device->id.bustype = BUS_I2C;

	core_fr_input_set_param(ipd->input_device);

	/* register the input device to input sub-system */
	res = input_register_device(ipd->input_device);
	if (res < 0)
	{
		DBG_ERR("Failed to register touch input device, res = %d", res);
		goto out;
	}

	return res;

fail_alloc:
	input_free_device(core_fr->input_device);
	return res;

out:
	input_unregister_device(ipd->input_device);
	input_free_device(core_fr->input_device);
	return res;
}

/*
 * Remove Core APIs memeory being allocated.
 *
 */
static void ilitek_platform_core_remove(void)
{
	DBG_INFO("Remove all core's compoenets");
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
	DBG("Initialise core's components ");

	if (core_config_init() < 0 ||
		core_i2c_init(ipd->client) < 0 ||
		core_firmware_init() < 0 ||
		core_fr_init(ipd->client) < 0)
	{
		ilitek_platform_core_remove();
		DBG_ERR("Failed to initialise core components");
		return -EINVAL;
	}

	ilitek_proc_init();

	return 0;
}

static int ilitek_platform_remove(struct i2c_client *client)
{
	DBG("Remove platform components");

	if (ipd->isIrqEnable)
	{
		disable_irq_nosync(ipd->isr_gpio);
	}

	if (ipd->isr_gpio != 0 && ipd->int_gpio != 0 && ipd->reset_gpio != 0)
	{
		free_irq(ipd->isr_gpio, (void *)ipd->i2c_id);
		gpio_free(ipd->int_gpio);
		gpio_free(ipd->reset_gpio);
	}

#ifdef CONFIG_FB
	fb_unregister_client(&ipd->notifier_fb);
#else
	unregister_early_suspend(&ipd->early_suspend);
#endif

	ilitek_platform_core_remove();

	if (ipd->input_device != NULL)
	{
		input_unregister_device(ipd->input_device);
		input_free_device(ipd->input_device);
	}

	kfree(ipd);

	return 0;
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
#ifdef ENABLE_REGULATOR_POWER_ON
	const char *vdd_name = "vdd";
	const char *vcc_i2c_name = "vcc_i2c";
#endif

	if (client == NULL)
	{
		DBG_ERR("i2c client is NULL");
		return -ENODEV;
	}

	// initialise the struct of touch ic memebers.
	ipd = kzalloc(sizeof(*ipd), GFP_KERNEL);
	ipd->client = client;
	ipd->i2c_id = id;
	ipd->chip_id = ON_BOARD_IC; // it must match the chip what you're using on board.
	ipd->isIrqEnable = false;

	DBG_INFO("Driver version : %s", DRIVER_VERSION);
	DBG_INFO("This driver now supports %x ", ON_BOARD_IC);

	// Different ICs may require different delay time for the reset.
	// They may also depend on what your platform need to.
	if (ipd->chip_id == CHIP_TYPE_ILI7807)
	{
		ipd->delay_time_high = 10;
		ipd->delay_time_low = 5;
		ipd->edge_delay = 200;
	}
	else if (ipd->chip_id == CHIP_TYPE_ILI9881)
	{
		ipd->delay_time_high = 10;
		ipd->delay_time_low = 5;
		ipd->edge_delay = 200;
	}
	else
	{
		ipd->delay_time_high = 10;
		ipd->delay_time_low = 10;
		ipd->edge_delay = 10;
	}

	mutex_init(&ipd->MUTEX);
	spin_lock_init(&ipd->SPIN_LOCK);

#ifdef ENABLE_REGULATOR_POWER_ON
	ipd->vdd = regulator_get(&ipd->client->dev, vdd_name);
	if (IS_ERR(ipd->vdd))
	{
		DBG_ERR("regulator_get vdd fail");
		ipd->vdd = NULL;
	}
	else 
	{
		res = regulator_set_voltage(ipd->vdd, 2800000, 3300000); 
		if (res)
		{
			DBG_ERR("Failed to set 2800mv.");
		}
	}

	ipd->vdd_i2c = regulator_get(&ipd->client->dev, vcc_i2c_name);
	if (IS_ERR(ipd->vdd_i2c))
	{
		DBG_ERR("regulator_get vdd_i2c fail.");
		ipd->vdd_i2c = NULL;
	}
	else
	{
		res = regulator_set_voltage(ipd->vdd_i2c, 1800000, 1800000);  
		if (res) 
		{
			DBG_ERR("Failed to set 1800mv.");
		}
	}
	ilitek_regulator_power_on(true);
#endif

	res = ilitek_platform_gpio();
	if (res < 0)
	{
		DBG_ERR("Failed to request gpios ");
	}

	res = ilitek_platform_core_init();
	if (res < 0)
	{
		DBG_ERR("Failed to init core APIs");
		goto out;
	}

	ilitek_platform_tp_hw_reset(true);

	// get our tp ic information
	ilitek_platform_read_tp_info();

	res = ilitek_platform_input_init();
	if (res < 0)
	{
		DBG_ERR("Failed to init input device in kernel");
	}

	res = ilitek_platform_isr_register();
	if (res < 0)
	{
		DBG_ERR("Failed to register ISR");
	}

	// To make sure our ic runing well before the work,
	// pulling RESET pin as low/high once after read TP info.
	ilitek_platform_tp_hw_reset(true);

	res = ilitek_platform_reg_suspend();
	if (res < 0)
	{
		DBG_ERR("Failed to register suspend/resume CB function");
	}

	return res;

out:
	ilitek_platform_remove(ipd->client);
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
	{.compatible = DTS_OF_NAME},
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

	if (res < 0)
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
	DBG_INFO("I2C driver has been removed");

	i2c_del_driver(&tp_i2c_driver);
}

module_init(ilitek_platform_init);
module_exit(ilitek_platform_exit);
MODULE_AUTHOR("ILITEK");
MODULE_LICENSE("GPL");
