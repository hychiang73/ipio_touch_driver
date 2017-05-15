#ifndef __CONFIG_H
#define __CONFIG_H

typedef struct {
    int nId;
    int nX;
    int nY;
    int nStatus;
    int nFlag;
} VIRTUAL_KEYS;

typedef struct {
	uint16_t nMaxX;
	uint16_t nMaxY;
	uint16_t nMinX;
	uint16_t nMinY;

	uint8_t nMaxTouchNum;
	uint8_t nMaxKeyButtonNum;

	uint8_t nXChannelNum;
	uint8_t nYChannelNum;
	uint8_t nHandleKeyFlag;
	uint8_t nKeyCount;

	uint16_t nKeyAreaXLength;
	uint16_t nKeyAreaYLength;

	VIRTUAL_KEYS virtual_key[10];

	//added for protocol v5
	uint8_t self_tx_channel_num;
	uint8_t self_rx_channel_num;
	uint8_t side_touch_type;

} TP_INFO;

typedef struct _CORE_CONFIG {

    uint32_t chip_id;

	uint8_t protocol_ver[4];

	uint8_t firmware_ver[4];

	uint16_t use_protocol;

    uint32_t slave_i2c_addr;

    uint32_t ice_mode_addr;

    uint32_t pid_addr;

	uint32_t ic_reset_addr;

    int (*IceModeInit)(void);

	TP_INFO *tp_info;

} CORE_CONFIG;

//extern CORE_CONFIG *core_config;

extern uint32_t vfIceRegRead(uint32_t addr);
extern uint32_t core_config_read_write_onebyte(uint32_t addr);
extern uint32_t core_config_ice_mode_read(uint32_t addr);
extern int core_config_ic_reset(uint32_t id);
extern int core_config_ice_mode_write(uint32_t addr, uint32_t data, uint32_t size);
extern int core_config_ice_mode(void);
extern int core_config_ice_mode_exit(void);
extern int core_config_get_key_info(void);
extern int core_config_get_tp_info(void);
extern int core_config_get_protocol_ver(void);
extern int core_config_get_fw_ver(void);
extern int core_config_get_chip_id(void);
extern int core_config_init(uint32_t id);
extern void core_config_remove(void);

#endif
