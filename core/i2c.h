#ifndef __I2C_H
#define __I2C_H

typedef struct _CORE_I2C {
	struct i2c_client *client;

} CORE_I2C;


extern int core_i2c_write(uint8_t nSlaveId, uint8_t *pBuf, uint16_t nSize);
extern int core_i2c_read(uint8_t nSlaveId, uint8_t *pBuf, uint16_t nSize);
extern int core_i2c_init(struct i2c_client *client);
extern void core_i2c_remove(void);

#endif
