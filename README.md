# Introduce

Ipio driver is a new touch driver used on the new products of ILITEK touch ICs. It has been verified on the firefly-rk3288 platform with Android 5.0.

# Support ICs

The following lists which of IC types supported by the driver.

* ILI7807F
* ILI7807H
* ILI9881F

# Debugging

In general case, the default debug level in kernel is set as 7, which number you will see all logs outputed by KERNEL_INFO and KERNEL_ERR. 

You can check your kernel level by this command:

```
# cat /proc/sys/kernel/printk
```

If you'd like to see more details (like finger report packet), you can also dynamically adjust the level by echo without modifying driver code:

```
# echo "8 4 1 7" > /proc/sys/kernel/printk
```
then will out logs with pr_debug. The debug defines at local directory **chip.h**

```
#define DBG_INFO(fmt, arg...) \
			pr_info("ILITEK: (%s, %d): " fmt "\n", __func__, __LINE__, ##arg);

#define DBG_ERR(fmt, arg...) \
			pr_err("ILITEK: (%s, %d): " fmt "\n", __func__, __LINE__, ##arg);

#define DBG(fmt, arg...) \
			pr_debug( "ILITEK: (%s, %d): " fmt "\n", __func__, __LINE__, ##arg);
```

# File sturcture

```
├── chip.h                 
├── core                   
│   ├── config.c
│   ├── config.h
│   ├── finger_report.c
│   ├── finger_report.h
│   ├── firmware.c
│   ├── firmware.h
│   ├── i2c.c
│   ├── i2c.h
│   └── sup_chip.c
├── Makefile
├── platform.c
├── platform.h
├── README.md
└── userspace.c
```

* The concepts of this driver is suppose to hide details, and you should ignore the core functions and follow up the example code **platform.c**
to write your own platform c file (like copy it to platform_mtk.c and modify gpio numbers, dts's match name etc.)

* If you want to create more nodes on the device driver for user space, just refer to **userspace.c**.

* In **chip.h* there have a lots of definations about IC's addres and protocol commands on firmware.

* The directory **Core** includes our touch ic settings, functions and other features.

# Porting

## Tell driver what IC types it should support to.

To find a macro called *ON_BOARD_IC* in **chip.h** and chose one of them:

```
// This macro defines what types of chip supported by the driver.
#define ON_BOARD_IC		0x7807
//#define ON_BOARD_IC		0x9881
```
 In this case the driver now support ILI7807.
 
## Check your DTS file and i2c bus number

The DTS file with RK3288 development platform locates at **arch/arm/boot/dts/firefly-rk3288.dts**.

Open it and add the gpio number in one of i2c buses.

```
&i2c1 {
		ts@41 {
			status = "okay";
        	compatible = "tchip,ilitek";
      		reg = <0x41>;
         	touch,irq-gpio = <&gpio8 GPIO_A7 IRQ_TYPE_EDGE_RISING>;
         	touch,reset-gpio = <&gpio8 GPIO_A6 GPIO_ACTIVE_LOW>;
    	};

};
```
In this case the slave address is 0x41, and the name of table calls **tchip,ilitek**.

**touch,irq-gpio** and **touch,reset-gpio** represent INT pin and RESET pin.

## Copy **platform.c** as your platform c. file.

Once you have done that, the first thing you should make sure is what the i2c name calls in your dts file:

```
/*
 * The name in the table must match the definiation
 * in a dts file.
 *
 */
static struct of_device_id tp_match_table[] = {
	{ .compatible = "tchip,ilitek"},
    {},
};
```
Change it according to your DTS file. 

The next we should modify is the name of gpios. Find out the two macros and chang it.

```
#define DTS_INT_GPIO	"touch,irq-gpio"
#define DTS_RESET_GPIO	"touch,reset-gpio"
```

For more information about gpio settins, you can refer to the function called **ilitek_platform_gpio**

```
static int ilitek_platform_gpio(void)
{
	int res = 0;
#ifdef CONFIG_OF
	struct device_node *dev_node = TIC->client->dev.of_node;
	uint32_t flag;
#endif
	uint32_t gpios[2] = {0};// 0: int, 1:reset

#ifdef CONFIG_OF
	gpios[0] = of_get_named_gpio_flags(dev_node, DTS_INT_GPIO, 0, &flag);
	gpios[1] = of_get_named_gpio_flags(dev_node, DTS_RESET_GPIO, 0, &flag);
#endif

    ....
}
```

# Release Note

* V1.0.0.7
  * Fixed issue of showing upgrade status while using APK
  * Added a flag to enable if resolution is set by default or fw
  * support the length of i2cuart and send to users with one package
  * i2c error is no long showing up if doing ic reset
  * Added a feature to check battery status 
  * fixed the error while programming fw (garbage data left)

* V1.0.0.6
  * Finger report won't send to user if checksum error occurs
  * Fixed the error sometimer work queue couldn't be executed while interrupting

* V1.0.0.5
  * Fix the error of checksum with finger report
  * Add flash table in order to get flash information dynamatically.
  * fix the error crc to calculate length instead of address.
  * Optimise the calculation of fw upgrade 
  * Remove ILI2121 from support list
  * Remove the old method of fw upgrade
  * Add the delay of 1ms after sending 0xF6 command

* V1.0.0.4
  * Add 7807H in the support list
  * Support 7807 FW upgrade
  * Set 1920*1080 as fixed resolution
  * Add 0xF6 cmd for P5.0 befre reading data
  * Change Netlink's port from 31 to 21
  * Add Regulator power on
  * Add I2CUART mode
  * Add a new way to program flash

* V1.0.0.3
  * Fixed the issue of create skb buff in netlink
  * Fixed the issue of doing bitwise with tp info got from ic
  * Improved the stability with IRAM upgrade.
  * Improved the stability while getting chip id, particularly 7807F.
  * Added Notifier FB as main suspend/resume function called.
  * Added MP Test in Test mode but 7807F not supported it yet.
  * Optimised code structure.

* V1.0.0.2
  * Fixed the issue of no resposne while using A protocol.
  * Now the resolution of input device can correctly be set by TP info.
  * Added compiler flag -Wall
  * Added disable/enable irq while chiang firmware mode before and after.
  * Added dynamic debug outputs.
  * Added a feature that allows firmware upgrading into IRAM directly.

* V1.0.0.1
  * Support firmware upgrade for 9881F
  * Improved the stability while upgrading firmware
  * Improved the stability while reading chid id from touch ic
  * Fixed some bugs

* V1.0.0.0
  * Support ILI7807F, ILI9881F
  * Support protocol v5.0
  * Support upgrade firmware for 7807F (9881F not yet)
  * Support mode switch (demo/debug/test/i2cUart)
  * Support demo/debug mode with packet ID while reporting figner touch.
  * Support early suspend
  * Fixed some bugs
