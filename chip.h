/*
 * This header file include all definiations with different types of ILITEK Touch IC.
 */

#define DBG_LEVEL

#define DBG_INFO(fmt, arg...) \
			printk(KERN_INFO "ILITEK: (%s): " fmt "\n", __func__, ##arg);

#define DBG_ERR(fmt, arg...) \
			printk(KERN_ERR "ILITEK: (%s): " fmt "\n", __func__, ##arg);

#define SUPP_CHIP_NUM	3

/*
 *  ILI2121
 */
#define CHIP_TYPE_ILI2121		0x2121
#define ILI21XX_SLAVE_ADDR		0x41
#define ILI21XX_ICE_MODE_ADDR	0x181062
#define ILI21XX_PID_ADDR		0x4009C

// firmware mode
#define ILI21XX_FIRMWARE_UNKNOWN_MODE		0xFF
#define ILI21XX_FIRMWARE_DEMO_MODE			0x00
#define ILI21XX_FIRMWARE_DEBUG_MODE			0x01

// length of finger touch packet
#define ILI21XX_DEMO_MODE_PACKET_LENGTH		53
#define ILI21XX_MAX_TOUCH_NUM           	5

// i2c command
#define ILITEK_TP_CMD_READ_DATA			0x10
#define ILITEK_TP_CMD_READ_SUB_DATA		0x11

// The size of hex files for ILI21xx shall be smaller than 160KB.
#define ILITEK_ILI21XX_FIRMWARE_SIZE				160
#define ILITEK_UPDATE_FIRMWARE_PAGE_LENGTH			128

// The szie of ili file shall be large enough for stored any kind firmware size of ILI21XX(256KB).
#define ILITEK_MAX_UPDATE_FIRMWARE_BUFFER_SIZE		256


/*
 * ILI7807
 *
 */
#define CHIP_TYPE_ILI7807		0x7807
#define ILI7807_SLAVE_ADDR		0x41
#define ILI7807_ICE_MODE_ADDR	0x181062
#define ILI7807_PID_ADDR		0x4009C

// firmware mode
#define ILI7807_FIRMWARE_UNKNOWN_MODE		0xFF
#define ILI7807_FIRMWARE_DEMO_MODE			0x00
#define ILI7807_FIRMWARE_DEBUG_MODE			0x01

// length of finger touch packet
#define ILI7807_DEMO_MODE_PACKET_LENGTH  	43
#define ILI7807_DEBUG_MODE_PACKET_LENGTH   	1280
#define ILI7807_MAX_TOUCH_NUM           	5

/*
 * 
 * Protocol commands 
 */
#define ILITEK_PROTOCOL_V3_2			0x302
#define PCMD_3_2_GET_TP_INFORMATION		0x20
#define PCMD_3_2_GET_KEY_INFORMATION	0x22
#define PCMD_3_2_GET_FIRMWARE_VERSION	0x40
#define PCMD_3_2_GET_PROTOCOL_VERSION	0x42

#define ILITEK_PROTOCOL_V5_0			0x50
#define PCMD_5_0_GET_TP_INFORMATION		0x20
#define PCMD_5_0_GET_KEY_INFORMATION	0x27
#define PCMD_5_0_GET_FIRMWARE_VERSION	0x21
#define PCMD_5_0_GET_PROTOCOL_VERSION	0x22

