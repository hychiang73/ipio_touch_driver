#ifndef __FIRMWARE_H
#define __FIRMWARE_H

typedef struct _CORE_FIRMWARE {

    uint32_t chip_id;

    uint32_t slave_i2c_addr;

} CORE_FIRMWARE;

extern int core_firmware_upgrade(uint32_t chip_id);
#endif
