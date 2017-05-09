#include <linux/errno.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/input/mt.h>
#include <linux/i2c.h>

#include "../chip.h"
#include "config.h"
#include "i2c.h"
#include "finger_report.h"

extern uint32_t SUP_CHIP_LIST[SUPP_CHIP_NUM];

CORE_FINGER_REPORT *core_fr;

#define USE_TYPE_B_PROTOCOL 
//#define ENABLE_GESTURE_WAKEUP

#define MUTUAL_MAX_TOUCH_NUM	10
#define TOUCH_SCREEN_X_MIN	0
#define TOUCH_SCREEN_Y_MIN	0
#define TOUCH_SCREEN_X_MAX	1080
#define TOUCH_SCREEN_Y_MAX	1920



static int input_device_create(struct i2c_client *client)
{
	int res = 0;

	DBG_INFO();

	core_fr->input_device = input_allocate_device();

	if(IS_ERR(core_fr->input_device))
	{
		DBG_ERR("Failed to allocate touch input device");
		return -ENOMEM;
	}
	DBG_INFO("client->name = %s", client->name);	
	core_fr->input_device->name = client->name;
	core_fr->input_device->phys = "I2C";
	core_fr->input_device->dev.parent = &client->dev;
	core_fr->input_device->id.bustype = BUS_I2C;

    // set the supported event type for input device
    set_bit(EV_ABS, core_fr->input_device->evbit);
    set_bit(EV_SYN, core_fr->input_device->evbit);
    set_bit(EV_KEY, core_fr->input_device->evbit);
    set_bit(BTN_TOUCH, core_fr->input_device->keybit);
    set_bit(INPUT_PROP_DIRECT, core_fr->input_device->propbit);
    
	//TODO: set virtual keys
	
#ifdef ENABLE_GESTURE_WAKEUP
    input_set_capability(core_fr->input_device, EV_KEY, KEY_POWER);
    input_set_capability(core_fr->input_device, EV_KEY, KEY_UP);
    input_set_capability(core_fr->input_device, EV_KEY, KEY_DOWN);
    input_set_capability(core_fr->input_device, EV_KEY, KEY_LEFT);
    input_set_capability(core_fr->input_device, EV_KEY, KEY_RIGHT);
    input_set_capability(core_fr->input_device, EV_KEY, KEY_W);
    input_set_capability(core_fr->input_device, EV_KEY, KEY_Z);
    input_set_capability(core_fr->input_device, EV_KEY, KEY_V);
    input_set_capability(core_fr->input_device, EV_KEY, KEY_O);
    input_set_capability(core_fr->input_device, EV_KEY, KEY_M);
    input_set_capability(core_fr->input_device, EV_KEY, KEY_C);
    input_set_capability(core_fr->input_device, EV_KEY, KEY_E);
    input_set_capability(core_fr->input_device, EV_KEY, KEY_S);
#endif

    input_set_abs_params(core_fr->input_device, ABS_MT_TRACKING_ID, 0, (MUTUAL_MAX_TOUCH_NUM-1), 0, 0);

    input_set_abs_params(core_fr->input_device, ABS_MT_TOUCH_MAJOR, 0, 255, 0, 0);
    input_set_abs_params(core_fr->input_device, ABS_MT_WIDTH_MAJOR, 0, 255, 0, 0);

#ifdef USE_TYPE_B_PROTOCOL
    input_set_abs_params(core_fr->input_device, ABS_MT_POSITION_X, TOUCH_SCREEN_X_MIN, TOUCH_SCREEN_X_MAX, 0, 0);
    input_set_abs_params(core_fr->input_device, ABS_MT_POSITION_Y, TOUCH_SCREEN_Y_MIN, TOUCH_SCREEN_Y_MAX, 0, 0);
    input_set_abs_params(core_fr->input_device, ABS_MT_PRESSURE, 0, 255, 0, 0);

    set_bit(BTN_TOOL_FINGER, core_fr->input_device->keybit);
    input_mt_init_slots(core_fr->input_device, MUTUAL_MAX_TOUCH_NUM, 0);
#endif

    /* register the input device to input sub-system */
    res = input_register_device(core_fr->input_device);
    if (res < 0)
    {
        DBG_ERR("Failed to register touch input device, res = %d", res);
		input_free_device(core_fr->input_device);
        return res;
    }

	return res;
}

struct hashtab {
	uint32_t chip_id;
	uint16_t protocol_ver;
	void(*finger_report)(void);
	void(*parse_date)(void);
};

struct hashtab fr_t[] = {
	{CHIP_TYPE_ILI2121, 0x0, NULL, NULL},
	{CHIP_TYPE_ILI7807, 0x0, NULL, NULL},
};

void core_fr_handler(void)
{

}

int core_fr_init(uint32_t id, struct i2c_client *pClient)
{
	int i = 0, res = 0;

	for(; i < SUPP_CHIP_NUM; i++)
	{
		if(SUP_CHIP_LIST[i] == id)
		{
			core_fr  = (CORE_FINGER_REPORT*)kmalloc(sizeof(*core_fr), GFP_KERNEL);

			if(SUP_CHIP_LIST[i] == CHIP_TYPE_ILI2121)
			{
			}

			if(SUP_CHIP_LIST[i] == CHIP_TYPE_ILI7807)
			{
			}
		}
	}

	if(IS_ERR(core_fr))
	{
		DBG_ERR("Failed to init core_fr APIs");
		res = -ENOMEM;
	} else
		res = input_device_create(pClient);

	return res;
}
EXPORT_SYMBOL(core_fr_init);

void core_fr_remove(void)
{
	DBG_INFO();

	input_unregister_device(core_fr->input_device);
	input_free_device(core_fr->input_device);
	kfree(core_fr);
}
EXPORT_SYMBOL(core_fr_remove);
