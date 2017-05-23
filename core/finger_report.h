#ifndef __FINGER_REPORT_H
#define __FINGER_REPORT_H

// disable it if want ot use TYPE A in MTP 
#define USE_TYPE_B_PROTOCOL 
//#define ENABLE_GESTURE_WAKEUP
//#define FORCE_TOUCH

#define TOUCH_SCREEN_X_MIN	0
#define TOUCH_SCREEN_Y_MIN	0
#define TOUCH_SCREEN_X_MAX	1080
#define TOUCH_SCREEN_Y_MAX	1920

#define TPD_HEIGHT 2048
#define TPD_WIDTH  2048

typedef struct {

	struct input_dev *input_device;

	int isDisableFR;

	uint32_t chip_id;

	/* mutual firmware info */
	uint8_t fw_unknow_mode;
	uint8_t fw_demo_mode;
	uint8_t fw_debug_mode;
	uint16_t actual_fw_mode;
	uint16_t log_packet_length;
	uint8_t log_packet_header;
	uint8_t type;
	uint8_t Mx;
	uint8_t My;
	uint8_t Sd;
	uint8_t Ss;

} CORE_FINGER_REPORT;

extern void core_fr_handler(void);
extern int core_fr_init(uint32_t chip_id, struct i2c_client *client);
extern void core_fr_remove(void);

#endif
