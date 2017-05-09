#ifndef __FINGER_REPORT_H
#define __FINGER_REPORT_H



struct mutual_touch_point {
	uint16_t id;
	uint16_t x;
	uint16_t y;
	uint16_t p;
};

typedef struct {

	struct input_dev *input_device;

	struct mutual_touch_point mtp[10];

	uint8_t key_count;

	uint8_t key_code;

	uint16_t firmware_mode;

	uint8_t type;

	uint8_t packet_header;

	uint16_t packet_length;

	uint8_t Mx;
	uint8_t My;
	uint8_t Sd;
	uint8_t Ss;

} CORE_FINGER_REPORT;

extern int core_fr_init(uint32_t chip_id, struct i2c_client *client);
extern void core_fr_remove(void);

#endif
