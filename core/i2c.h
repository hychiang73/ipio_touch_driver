#ifndef __I2C_H
#define __I2C_H

typedef struct {
	struct i2c_client *client;
} CORE_I2C;
extern int core_i2c_write(uint8_t, uint8_t *, uint16_t);
extern int core_i2c_read(uint8_t, uint8_t *, uint16_t);
extern int core_i2c_init(struct i2c_client *);
extern void core_i2c_remove(void);

#endif
