#ifndef __FINGER_REPORT_H
#define __FINGER_REPORT_H

// Either B TYPE or A Type in MTP
#define USE_TYPE_B_PROTOCOL 

//#define ENABLE_GESTURE_WAKEUP

// Whether to detect the value of pressure in finger touch
//#define FORCE_TOUCH

// set up width and heigth of a screen
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
extern int core_fr_init(struct i2c_client *);
extern void core_fr_remove(void);

#endif
