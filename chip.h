/*
 * This header file include all definiations with different types of ILITEK Touch IC.
 */

#define SUCCESS		0

#define DBG_LEVEL

#define DBG_INFO(fmt, arg...) \
			printk(KERN_INFO "ILITEK: (%s): " fmt "\n", __func__, ##arg);

#define DBG_ERR(fmt, arg...) \
			printk(KERN_ERR "ILITEK: (%s): " fmt "\n", __func__, ##arg);

/*
 * The short name of data type
 */

#define uint8_t		unsigned char
#define uint16_t	unsigned short
#define uint32_t	unsigned int
#define int8_t		signed char
#define int16_t		signed short
#define int32_t		signed int
//#define int64_t  int64_t
//#define uint64_t  uint64_t

#define SUPP_CHIP_NUM	2

/*
 *  ILI21xx
 */

#define CHIP_TYPE_ILI2120   (0x2120)
#define CHIP_TYPE_ILI2121   (0x2121)
#define ILI21XX_SLAVE_ADDR		(0x41)
#define ILI21XX_ICE_MODE_ADDR	(0x181062)
#define ILI21XX_PID_ADDR		(0x4009C)

// Constant value define for ILI21XX
#define ILI21XX_DEMO_MODE_PACKET_LENGTH  (53)

#define ILI21XX_FIRMWARE_MODE_UNKNOWN_MODE (0xFF)
#define ILI21XX_FIRMWARE_MODE_DEMO_MODE    (0x00)
#define ILI21XX_FIRMWARE_MODE_DEBUG_MODE   (0x01)

// i2c command for ilitek touch ic
#define ILITEK_TP_CMD_READ_DATA			   (0x10)
#define ILITEK_TP_CMD_READ_SUB_DATA		   (0x11)

#define ILITEK_TP_CMD_READ_DATA_CONTROL    (0xF6)
#define ILITEK_TP_CMD_GET_RESOLUTION       (0x20) // panel information
#define ILITEK_TP_CMD_GET_KEY_INFORMATION  (0x22)
#define ILITEK_TP_ILI2121_CMD_GET_FIRMWARE_VERSION (0x40)
#define ILITEK_TP_ILI2121_CMD_GET_PROTOCOL_VERSION (0x42)
#define ILITEK_TP_ILI2120_CMD_GET_FIRMWARE_VERSION (0x21)
#define ILITEK_TP_ILI2120_CMD_GET_PROTOCOL_VERSION (0x22)
#define ILITEK_TP_CMD_GET_MCU_KERNEL_VERSION (0x61)


// The size of hex files for ILI21xx shall be smaller than 160KB.
#define ILITEK_ILI21XX_FIRMWARE_SIZE (160)
#define ILITEK_UPDATE_FIRMWARE_PAGE_LENGTH (128)

// The szie of ili file shall be large enough for stored any kind firmware size of ILI21XX(256KB).
#define ILITEK_MAX_UPDATE_FIRMWARE_BUFFER_SIZE (256)


/*
 * Protocol's definition
 */

 #define ILITEK_PROTOCOL_VERSION_3_2		(0x302)
