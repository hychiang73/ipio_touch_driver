#ifndef __CONFIG_H
#define __CONFIG_H

typedef struct _CORE_CONFIG {
    unsigned int chip_id;
    unsigned int slave_i2c_addr;
    unsigned int ice_mode_addr;
    unsigned int pid_addr;
	unsigned short *scl;
	int scl_size;
    int (*IceModeInit)(void);
} CORE_CONFIG;

extern CORE_CONFIG *core_config;

extern void core_config_HWReset(void);
extern int core_config_GetChipID(void);
extern int core_config_init(unsigned int chip_type);

#endif
