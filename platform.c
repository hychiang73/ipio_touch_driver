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

#include "common.h"
#include "core/config.h"
#include "core/i2c.h"
#include "core/firmware.h"
#include "core/finger_report.h"
#include "core/flash.h"
#include "platform.h"

#define DTS_INT_GPIO	"touch,irq-gpio"
#define DTS_RESET_GPIO	"touch,reset-gpio"

#ifdef PLATFORM_MTK
#define DTS_OF_NAME		"mediatek,cap_touch"
#include "tpd.h"
extern struct tpd_device *tpd;
#define MTK_RST_GPIO 2
#define MTK_INT_GPIO 1
#else
#define DTS_OF_NAME		"tchip,ilitek"
#endif

#define I2C_DEVICE_ID	"ILITEK_TP_ID"
#define POWER_STATUS_PATH "/sys/class/power_supply/battery/status"

#ifdef USE_KTHREAD
static DECLARE_WAIT_QUEUE_HEAD(waiter);
#endif

struct ilitek_platform_data *ipd;

void ilitek_platform_disable_irq(void)
{
	unsigned long nIrqFlag;

	DBG("IRQ = %d", ipd->isEnableIRQ);

	spin_lock_irqsave(&ipd->SPIN_LOCK, nIrqFlag);

	if(ipd->isEnableIRQ)
	{
		if (ipd->isr_gpio)
		{
			disable_irq_nosync(ipd->isr_gpio);
			ipd->isEnableIRQ = false;
			DBG("IRQ was disabled");
		}
		else
			DBG_ERR("The number of gpio to irq is incorrect");
	}
	else
		DBG("IRQ was already disabled");

	spin_unlock_irqrestore(&ipd->SPIN_LOCK, nIrqFlag);
}
EXPORT_SYMBOL(ilitek_platform_disable_irq);

void ilitek_platform_enable_irq(void)
{
	unsigned long nIrqFlag;

	DBG("IRQ = %d", ipd->isEnableIRQ);

	spin_lock_irqsave(&ipd->SPIN_LOCK, nIrqFlag);

	if(!ipd->isEnableIRQ)
	{
		if (ipd->isr_gpio)
		{
			enable_irq(ipd->isr_gpio);
			ipd->isEnableIRQ = true;
			DBG("IRQ was enabled");
		}
		else
			DBG_ERR("The number of gpio to irq is incorrect");
	}
	else
		DBG("IRQ was already enabled");

	spin_unlock_irqrestore(&ipd->SPIN_LOCK, nIrqFlag);
}
EXPORT_SYMBOL(ilitek_platform_enable_irq);

void ilitek_platform_tp_hw_reset(bool isEnable)
{
	DBG("HW Reset: %d ", isEnable);
	if (isEnable)
	{
#ifdef PLATFORM_MTK
		tpd_gpio_output(ipd->reset_gpio, 1);
		mdelay(ipd->delay_time_high);
		tpd_gpio_output(ipd->reset_gpio, 0);
		mdelay(ipd->delay_time_low);
		tpd_gpio_output(ipd->reset_gpio, 1);
		mdelay(ipd->edge_delay);
#else
		gpio_direction_output(ipd->reset_gpio, 1);
		mdelay(ipd->delay_time_high);
		gpio_set_value(ipd->reset_gpio, 0);
		mdelay(ipd->delay_time_low);
		gpio_set_value(ipd->reset_gpio, 1);
		mdelay(ipd->edge_delay);
#endif
	}
	else
	{
#ifdef PLATFORM_MTK
		tpd_gpio_output(ipd->reset_gpio, 0);
#else
		gpio_set_value(ipd->reset_gpio, 0);
#endif
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
				DBG_ERR("regulator_enable vdd fail");
		}	
		if (ipd->vdd_i2c) 
		{
			res = regulator_enable(ipd->vdd_i2c);
			if (res < 0) 
				DBG_ERR("regulator_enable vdd_i2c fail");
		}	
	}
	else 
	{
		if (ipd->vdd) 
		{
			res = regulator_disable(ipd->vdd);
			if (res < 0) 
				DBG_ERR("regulator_enable vdd fail");
		}	
		if (ipd->vdd_i2c)
		 {
			res = regulator_disable(ipd->vdd_i2c);
			if (res < 0) 
				DBG_ERR("regulator_enable vdd_i2c fail");
		}	
	}

	mdelay(5);
	return;
}
EXPORT_SYMBOL(ilitek_regulator_power_on);
#endif

static void read_power_status(uint8_t *buf)
{
	struct file *f = NULL;
	mm_segment_t old_fs;
	ssize_t byte = 0;

	old_fs = get_fs();
	set_fs(get_ds());

	f = filp_open(POWER_STATUS_PATH, O_RDONLY, 0);
	if(IS_ERR(f))
	{
		DBG_ERR("Failed to open %s", POWER_STATUS_PATH);
		return;
	}

	f->f_op->llseek(f, 0, SEEK_SET);
	byte = f->f_op->read(f, buf, 20, &f->f_pos);
	
	DBG_INFO("Read %d bytes", (int)byte);

	set_fs(old_fs);
	filp_close(f, NULL);
}

static void ilitek_platform_vpower_notify(struct work_struct *pWork)
{
	uint8_t charge_status[20] = {0};
	uint8_t plug_ctrl[2] = {0x11, 0x0};
	static int charge_mode = 0;

	DBG("isEnableCheckPower = %d", ipd->isEnablePollCheckPower);
	read_power_status(charge_status);
	DBG("Batter Status: %s", charge_status);

	if(strstr(charge_status, "Charging") != NULL || strstr(charge_status, "Full") != NULL 
			|| strstr(charge_status, "Fully charged") != NULL)
	{
		if(charge_mode != 1)
		{
			DBG("Charging mode");
			plug_ctrl[1] = 0x0; //plug in
			core_config_func_ctrl(plug_ctrl);
			charge_mode = 1;
		}
	}
	else
	{
		if(charge_mode != 2)
		{
			DBG_INFO("Not charging mode");
			plug_ctrl[1] = 0x1; //plug out
			core_config_func_ctrl(plug_ctrl);
			charge_mode = 2;
		}
	}

	if(ipd->isEnablePollCheckPower)
		queue_delayed_work(ipd->check_power_status_queue, &ipd->check_power_status_work, ipd->work_delay);

	return;
}

#ifdef PLATFORM_MTK
static void tpd_resume(struct device *h)
{
	DBG_INFO("TPD wake up");

	core_config_ic_resume();
	ilitek_platform_enable_irq();

	DBG_INFO("TPD wake up done");
}

static void tpd_suspend(struct device *h)
{
    DBG_INFO("TPD enter sleep");

    if(!core_config->isEnableGesture)
    {
	    DBG_INFO("gesture not enabled");
	    ilitek_platform_disable_irq();
    }

    core_config_ic_suspend();

    DBG_INFO("TPD enter sleep done");
}
#else
#ifdef CONFIG_FB
static int ilitek_platform_notifier_fb(struct notifier_block *self,
									   unsigned long event, void *data)
{
	int *blank;
	struct fb_event *evdata = data;

	DBG("Notifier's event = %ld", event);

	if (event == FB_EVENT_BLANK)
	{
		blank = evdata->data;
		if (*blank == FB_BLANK_POWERDOWN)
		{
			DBG_INFO("Touch Suspend");

			if(!core_config->isEnableGesture)	
				ilitek_platform_disable_irq();

			if(ipd->isEnablePollCheckPower)
				cancel_delayed_work_sync(&ipd->check_power_status_work);

			core_config_ic_suspend();
		}
		else if (*blank == FB_BLANK_UNBLANK)
		{
			DBG_INFO("Touch Resuem");
			core_config_ic_resume();
			ilitek_platform_enable_irq();

			if(ipd->isEnablePollCheckPower)
				queue_delayed_work(ipd->check_power_status_queue, &ipd->check_power_status_work, ipd->work_delay);
		}
	}

	return NOTIFY_OK;
}
#else // CONFIG_HAS_EARLYSUSPEND
static void ilitek_platform_early_suspend(struct early_suspend *h)
{
	DBG_INFO();

	//TODO: there is doing nothing if an upgrade firmware's processing.

	core_fr_touch_release(0, 0, 0);

	input_sync(core_fr->input_device);

	core_fr->isEnableFR = false;

	if(!core_config->isEnableGesture)		
		ilitek_platform_disable_irq();

	if(ipd->isEnablePollCheckPower)
		cancel_delayed_work_sync(&ipd->check_power_status_work);

	core_config_ic_suspend();
}

static void ilitek_platform_late_resume(struct early_suspend *h)
{
	DBG_INFO();

	core_fr->isEnableFR = true;
	core_config_ic_resume();
	ilitek_platform_enable_irq();

	if(ipd->isEnablePollCheckPower)
		queue_delayed_work(ipd->check_power_status_queue, &ipd->check_power_status_work, ipd->work_delay);
}
#endif
#endif /* PLATFORM_MTK */

/**
 * reg_power_check - register a thread to inquery status at certain time.
 */
static int ilitek_platform_reg_power_check(void)
{
	int res = 0;

	INIT_DELAYED_WORK(&ipd->check_power_status_work, ilitek_platform_vpower_notify);
	ipd->check_power_status_queue = create_workqueue("ilitek_check_power_status");
	ipd->work_delay = msecs_to_jiffies(2000);
	if(!ipd->check_power_status_queue)
	{
		DBG_ERR("Failed to create a work thread to check power status");
		ipd->vpower_reg_nb = false;
		res = -1;
	}
	else
	{
		DBG_INFO("Created a work thread to check power status at every %u jiffies", (unsigned)ipd->work_delay);

		if(ipd->isEnablePollCheckPower)
		{
			queue_delayed_work(ipd->check_power_status_queue, &ipd->check_power_status_work, ipd->work_delay);	
			ipd->vpower_reg_nb = true;
		}
	}

	return res;
}

/**
 * Register a callback function when the event of suspend and resume occurs.
 *
 * The default used to wake up the cb function comes from notifier block mechnaism.
 * If you'd rather liek to use early suspend, CONFIG_HAS_EARLYSUSPEND in kernel config
 * must be enabled.
 */
static int ilitek_platform_reg_suspend(void)
{
	int res = 0;

#ifdef PLATFORM_MTK
	DBG_INFO("Platform is MTK, do nothing");
#else
	DBG_INFO("Register suspend/resume callback function");

#ifdef CONFIG_FB
	ipd->notifier_fb.notifier_call = ilitek_platform_notifier_fb;
	res = fb_register_client(&ipd->notifier_fb);
#else
	ipd->early_suspend->suspend = ilitek_platform_early_suspend;
	ipd->early_suspend->esume = ilitek_platform_late_resume;
	ipd->early_suspend->level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	res = register_early_suspend(ipd->early_suspend);
#endif
#endif /* PLATFORM_MTK */

	return res;
}

#ifdef USE_KTHREAD
static int ilitek_platform_irq_kthread(void *arg)
{
	struct sched_param param = { .sched_priority = 4};
	sched_setscheduler(current, SCHED_RR, &param);

	DBG("irq_trigger = %d", ipd->irq_trigger);

	// mainloop
	while(!kthread_should_stop() && !ipd->free_irq_thread)
	{
		set_current_state(TASK_INTERRUPTIBLE);
		wait_event_interruptible(waiter, ipd->irq_trigger);
		ipd->irq_trigger = false;
		set_current_state(TASK_RUNNING);

		core_fr_handler();
		ilitek_platform_enable_irq();
	}
	return 0;
}
#else
static void ilitek_platform_work_queue(struct work_struct *work)
{
	DBG("IRQ = %d", ipd->isEnableIRQ);

	core_fr_handler();

	if (!ipd->isEnableIRQ)
		ilitek_platform_enable_irq();		
}
#endif

static irqreturn_t ilitek_platform_irq_handler(int irq, void *dev_id)
{
//	unsigned long nIrqFlag;

	DBG("IRQ = %d", ipd->isEnableIRQ);

//	spin_lock_irqsave(&ipd->SPIN_LOCK, nIrqFlag);

	if (ipd->isEnableIRQ)
	{
		ilitek_platform_disable_irq();
#ifdef USE_KTHREAD
		ipd->irq_trigger = true;
		DBG("irq_trigger = %d", ipd->irq_trigger);
		wake_up_interruptible(&waiter);
#else
		schedule_work(&ipd->report_work_queue);
#endif
	}

//	spin_unlock_irqrestore(&ipd->SPIN_LOCK, nIrqFlag);

	return IRQ_HANDLED;
}

static int ilitek_platform_isr_register(void)
{
	int res = 0;
#ifdef PLATFORM_MTK
	struct device_node *node;
#endif

#ifdef USE_KTHREAD
	ipd->irq_thread = kthread_run(ilitek_platform_irq_kthread, NULL, "ilitek_irq_thread");
	if (ipd->irq_thread == (struct task_struct*)ERR_PTR)
	{
		ipd->irq_thread = NULL;
		DBG_ERR("Failed to create kthread");
		res = -ENOMEM;
		goto out;
	}
	ipd->irq_trigger = false;
	ipd->free_irq_thread = false;
#else
	INIT_WORK(&ipd->report_work_queue, ilitek_platform_work_queue);
#endif

#ifdef PLATFORM_MTK
	node = of_find_matching_node(NULL, touch_of_match);
	if (node)
	{
		ipd->isr_gpio = irq_of_parse_and_map(node, 0);
	}
#else
	ipd->isr_gpio = gpio_to_irq(ipd->int_gpio);
#endif

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

	ipd->isEnableIRQ = true;

out:
	return res;
}

static int ilitek_platform_gpio(void)
{
	int res = 0;
#ifdef PLATFORM_MTK
	ipd->int_gpio = MTK_INT_GPIO;
	ipd->reset_gpio = MTK_RST_GPIO;
#else
#ifdef CONFIG_OF
	struct device_node *dev_node = ipd->client->dev.of_node;
	uint32_t flag;

	ipd->int_gpio = of_get_named_gpio_flags(dev_node, DTS_INT_GPIO, 0, &flag);
	ipd->reset_gpio = of_get_named_gpio_flags(dev_node, DTS_RESET_GPIO, 0, &flag);
#endif
#endif /* PLATFORM_MTK */

	DBG_INFO("GPIO INT: %d", ipd->int_gpio);
	DBG_INFO("GPIO RESET: %d", ipd->reset_gpio);

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

static int ilitek_platform_read_tp_info(void)
{
	int res = -1;
	
		if(core_config_get_chip_id() < 0)
			goto out;
		if(core_config_get_fw_ver() < 0)
			goto out;
		if(core_config_get_core_ver() < 0)
			goto out;
		if(core_config_get_protocol_ver() < 0)
			goto out;
		if(core_config_get_tp_info() < 0)
			goto out;
		if(core_config_get_key_info() < 0)
			goto out;
	
		res = 0;
	out:
		return res;
}

static int ilitek_platform_input_init(void)
{
	int res = 0;

#ifdef PLATFORM_MTK
	int i;
	ipd->input_device = tpd->dev;

	if (tpd_dts_data.use_tpd_button) {
		for (i = 0; i < tpd_dts_data.tpd_key_num; i ++) {
			input_set_capability(ipd->input_device, EV_KEY, tpd_dts_data.tpd_key_local[i]);
		}
	}
	core_fr_input_set_param(ipd->input_device);
	return res;
#else
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
#endif
}

/**
 * Remove Core APIs memeory being allocated.
 */
static void ilitek_platform_core_remove(void)
{
	DBG_INFO("Remove all core's compoenets");
	core_config_remove();
	core_i2c_remove();
	core_firmware_remove();
	core_fr_remove();
	core_flash_remove();
	ilitek_proc_remove();
}

/**
 * The function is to initialise all necessary structurs in those core APIs,
 * they must be called before the i2c dev probes up successfully.
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

	return 0;
}

static int ilitek_platform_remove(struct i2c_client *client)
{
	DBG("Remove platform components");

	if(ipd->isEnableIRQ)
	{
		disable_irq_nosync(ipd->isr_gpio);
	}

	if(ipd->isr_gpio != 0 && ipd->int_gpio != 0 && ipd->reset_gpio != 0)
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

#ifdef USE_KTHREAD
	if(ipd->irq_thread != NULL)
	{
		ipd->irq_trigger = true;
		ipd->free_irq_thread = true;
		wake_up_interruptible(&waiter);
		kthread_stop(ipd->irq_thread);
		ipd->irq_thread = NULL;
	}
#endif

	if(ipd->input_device != NULL)
	{
		input_unregister_device(ipd->input_device);
		input_free_device(ipd->input_device);
	}

	if(ipd->vpower_reg_nb)
	{
		cancel_delayed_work_sync(&ipd->check_power_status_work);
		destroy_workqueue(ipd->check_power_status_queue);
	}

	kfree(ipd);
	ilitek_platform_core_remove();

	return 0;
}

/**
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
#ifdef PLATFORM_MTK
	const char *vdd_name = "vtouch";
#else
	const char *vdd_name = "vdd";
#endif
	const char *vcc_i2c_name = "vcc_i2c";
#endif

	if (client == NULL)
	{
		DBG_ERR("i2c client is NULL");
		return -ENODEV;
	}

	/* Set i2c slave addr if it's not configured */
	if(client->addr != 0x41){
		client->addr = 0x41;
		DBG_INFO(" I2C addr : 0x%x\n",client->addr);
	}

	/* initialise the struct of touch ic memebers. */
	ipd = kzalloc(sizeof(*ipd), GFP_KERNEL);
	ipd->client = client;
	ipd->i2c_id = id;
	ipd->chip_id = ON_BOARD_IC;
	ipd->isEnableIRQ = false;
	ipd->isEnablePollCheckPower = false;

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
#ifdef PLATFORM_MTK
	ipd->vdd = regulator_get(tpd->tpd_dev, vdd_name);
	tpd->reg = ipd->vdd;
#else
	ipd->vdd = regulator_get(&ipd->client->dev, vdd_name);
#endif
	if (IS_ERR(ipd->vdd))
	{
		DBG_ERR("regulator_get vdd fail");
		ipd->vdd = NULL;
	}
	else 
	{
		res = regulator_set_voltage(ipd->vdd, 1800000, 1800000); 
		if (res < 0)
			DBG_ERR("Failed to set vdd 1800mv.");
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
		if (res < 0) 
			DBG_ERR("Failed to set vdd_i2c 1800mv.");
	}
	ilitek_regulator_power_on(true);
#endif

	res = ilitek_platform_gpio();
	if (res < 0)
		DBG_ERR("Failed to request gpios ");

	res = ilitek_platform_core_init();
	if (res < 0)
	{
		DBG_ERR("Failed to init core APIs");
		goto out;
	}

	ilitek_platform_tp_hw_reset(true);

	// get our tp ic information
	res = ilitek_platform_read_tp_info();
	if(res < 0)
	{
		DBG_ERR("Failed to get TP info");
		goto out;
	}

	res = ilitek_platform_input_init();
	if (res < 0)
		DBG_ERR("Failed to init input device in kernel");

	res = ilitek_platform_isr_register();
	if (res < 0)
		DBG_ERR("Failed to register ISR");

	// To make sure our ic runing well before the work,
	// pulling RESET pin as low/high once after read TP info.
	ilitek_platform_tp_hw_reset(true);

	res = ilitek_platform_reg_suspend();
	if (res < 0)
		DBG_ERR("Failed to register suspend/resume function");

	res = ilitek_platform_reg_power_check();
	if(res < 0)
		DBG_ERR("Failed to register power check function");

	/* Create nodes for users */
	ilitek_proc_init();
		
#ifdef PLATFORM_MTK
		tpd_load_status = 1;
#endif

out:
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

#ifdef PLATFORM_MTK
static int tpd_detect(struct i2c_client *client, struct i2c_board_info *info)
{
	DBG_INFO("TPD detect i2c device");
	strcpy(info->type, TPD_DEVICE);
	return 0;
}
#endif

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
#ifdef PLATFORM_MTK
	.detect = tpd_detect,
#endif
};

#ifdef PLATFORM_MTK
static int tpd_local_init(void)
{
	DBG_INFO("TPD init device driver");

	if (i2c_add_driver(&tp_i2c_driver) != 0)
	{
		DBG_ERR("Unable to add i2c driver");
		return -1;
	}
	if (tpd_load_status == 0) 
	{
		DBG_ERR("Add error touch panel driver");

		i2c_del_driver(&tp_i2c_driver);
		return -1;
	}

	if (tpd_dts_data.use_tpd_button)
	{
		tpd_button_setting(tpd_dts_data.tpd_key_num, tpd_dts_data.tpd_key_local,
		tpd_dts_data.tpd_key_dim_local);
	}

	tpd_type_cap = 1;

	return 0; 
}

static struct tpd_driver_t tpd_device_driver = {
    .tpd_device_name = I2C_DEVICE_ID,
    .tpd_local_init = tpd_local_init,
    .suspend = tpd_suspend,
    .resume = tpd_resume,
};
#endif

static int __init ilitek_platform_init(void)
{
	int res = 0;

#ifdef PLATFORM_MTK
	tpd_get_dts_info();
	res = tpd_driver_add(&tpd_device_driver);
	if (res < 0) 
	{
		DBG_ERR("TPD add TP driver failed");
		tpd_driver_remove(&tpd_device_driver);
		return -ENODEV;
	}
#else
	res = i2c_add_driver(&tp_i2c_driver);
	if (res < 0)
	{
		DBG_ERR("Failed to add i2c driver");
		i2c_del_driver(&tp_i2c_driver);
		return -ENODEV;
	}
#endif

	DBG_INFO("Succeed to add i2c driver");
	return res;
}

static void __exit ilitek_platform_exit(void)
{
	DBG_INFO("I2C driver has been removed");

#ifdef PLATFORM_MTK
	tpd_driver_remove(&tpd_device_driver);
#else
	i2c_del_driver(&tp_i2c_driver);
#endif
}

module_init(ilitek_platform_init);
module_exit(ilitek_platform_exit);
MODULE_AUTHOR("ILITEK");
MODULE_LICENSE("GPL");
