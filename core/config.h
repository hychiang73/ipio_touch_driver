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
	//struct key_info_t key_info[10];
	VIRTUAL_KEYS virtual_key[10];
} TP_INFO;

typedef struct _CORE_CONFIG {
    uint32_t chip_id;
    uint32_t slave_i2c_addr;
    uint32_t ice_mode_addr;
    uint32_t pid_addr;
	/* a list of chip supported by the driver */
	uint16_t *scl;
	int scl_size;
    int (*IceModeInit)(void);
	TP_INFO *tp_info;
	uint8_t *firmware_ver;
	uint16_t protocol_ver;
	uint32_t int_gpio;
	uint32_t reset_gpio;
} CORE_CONFIG;

extern CORE_CONFIG *core_config;
extern void core_config_HWReset(void);
extern int core_config_GetKeyInfo(void);
extern TP_INFO* core_config_GetResolution(void);
extern uint16_t core_config_GetProtocolVer(void);
extern uint8_t* core_config_GetFWVer(void);
extern uint32_t core_config_GetChipID(void);
extern int core_config_init(uint32_t *platform_info);
extern void core_config_remove(void);

#endif
