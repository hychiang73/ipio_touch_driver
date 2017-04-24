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

	unsigned short nMaxX;

	unsigned short nMaxY;
	
	unsigned short nMinX;

	unsigned short nMinY;

	unsigned char nMaxTouchNum;

	unsigned char nMaxKeyButtonNum;

	unsigned char nXChannelNum;

	unsigned char nYChannelNum;

	unsigned char nHandleKeyFlag;

	unsigned char nKeyCount;

	unsigned short nKeyAreaXLength;

	unsigned short nKeyAreaYLength;

	//struct key_info_t key_info[10];
	VIRTUAL_KEYS virtual_key[10];

} TP_INFO;

typedef struct _CORE_CONFIG {

    unsigned int chip_id;

    unsigned int slave_i2c_addr;

    unsigned int ice_mode_addr;

    unsigned int pid_addr;

	/* a list of chip supported by the driver */
	unsigned short *scl;
	int scl_size;

    int (*IceModeInit)(void);

	TP_INFO *tp_info;

	unsigned char *firmware_ver;

	unsigned short protocol_ver;

} CORE_CONFIG;

extern CORE_CONFIG *core_config;
extern void core_config_HWReset(void);
extern int core_config_GetKeyInfo(void);
extern TP_INFO* core_config_GetResolution(void);
extern unsigned short core_config_GetProtocolVer(void);
extern unsigned char* core_config_GetFWVer(void);
extern int core_config_GetChipID(void);
extern int core_config_init(unsigned int chip_type);

#endif
