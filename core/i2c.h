#ifndef __I2C_H
#define __I2C_H

typedef struct _CORE_I2C {
	struct i2c_client *client;

} CORE_I2C;


extern int core_i2c_write(unsigned char nSlaveId, unsigned char *pBuf, unsigned short nSize);
extern int core_i2c_read(unsigned char nSlaveId, unsigned char *pBuf, unsigned short nSize);
extern int core_i2c_init(struct i2c_client *client);

#endif
