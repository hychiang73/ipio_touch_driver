#ifndef __FIRMWARE_H
#define __FIRMWARE_H

typedef struct _CORE_FIRMWARE {

    uint32_t chip_id;

	uint8_t new_fw_ver[4];

	uint8_t old_fw_ver[4];

	uint32_t ap_start_addr;

	uint32_t df_start_addr;

	uint32_t ap_end_addr;

	uint32_t df_end_addr;

	uint32_t ap_checksum;

	uint32_t ap_crc;

	uint32_t df_checksum;

	uint32_t df_crc;

	uint32_t start_addr;

	uint32_t end_addr;

	uint32_t checksum;

	//int size;

	uint8_t *fw_data_max_buff;

//	uint8_t fw_data_buff[size];

    bool isUpgraded;

	bool isCRC;

	int (*upgrade_func)(uint8_t *FwData, uint8_t *iram_data);

} CORE_FIRMWARE;

//extern int core_firmware_iram_upgrade(const char* fpath);
extern int core_firmware_upgrade(const char*);
extern int core_firmware_init(void);
extern void core_firmware_remove(void);
#endif
