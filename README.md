# Introduce

Ipio touch driver is implemented by ILI Technology Corp, which is mainly used on its new generation touch ICs, TDDI.

# Support ICs

The following lists which of TDDI IC types supported by the driver.

* ILI7807F
* ILI7807H
* ILI9881F

# Support platform

* RK3288
* MTK

The default in this driver works on RK3288.

If you woule like to port it on MTK platforms, you must open the macro which shows on below:

**common.h**
```
#define PLATFORM_MTK
```
# Functions

## Gesture

To enable the support of this feature, you can either open it by its node under /proc :

```
echo on > /proc/ilitek/gesture
echo off > /proc/ilitek/gesture
```

or change its variable from the file **config.c** :
```
core_config->isEnableGesture = false;
```

## Check battery status

Some specific cases with power charge may affect on our TDDI IC so that we need to protect it if the event is occuring.

To enable the thread, you can also open it by its node or change its variable from the file **platform.c** to enable it at very first.

```
echo on > /proc/ilitek/check_battery
echo off > /procilitek/check_battery
```

```
ipd->isEnablePollCheckPower = false;
```

## DMA

If your platform needs to use DMA with I2C, you can open its macro from **common.h** :
```
#define ENABLE_DMA 
```
Note, it is disabled as default.

## Glove/Proximity/Phone cover

These features need to be opened by the node only.

```
echo enaglove > /proc/ilitek/ioctl  --> enale glove
echo disglove > /proc/ilitek/ioctl  --> disable glove
echo enaprox > /proc/ilitek/ioctl   --> enable proximity
echo disprox > /proc/ilitek/ioctl   --> disable proximity
echo enapcc > /proc/ilitek/ioctl    --> enable phone cover
echo dispcc > /proc/ilitek/ioctl    --> disable phone cover
```

# Debug message

It is important to see more details with debug messages if an error happends somehow. To look up them, you can set up debug level via a node called debug_level under /proc.

At the begining, you should read its node by cat to see what the levels you can choose for.

```
cat /proc/ilitek/debug_level

DEBUG_NONE = 0
DEBUG_IRQ = 1
DEBUG_FINGER_REPORT = 2
DEBUG_FIRMWARE = 4
DEBUG_CONFIG = 8
DEBUG_I2C = 16
DEBUG_BATTERY = 32
DEBUG_MP_TEST = 64
DEBUG_IOCTL = 128
DEBUG_NETLINK = 256
DEBUG_ALL = -1
```

The default level is zero once you get the driver at the first time. Let's say that we want to check out what is going on when the driver is upgrading firmware.

```
echo 4 > /proc/ilitek/debug_level
```

The result will only print the debug message with the process of firmware. Futhermore, you can also add two or three numbers to see multiple debug levels.

```
echo 7 > /proc/ilitek/debug_level
```

In this case the debusg message prints the status of IRQ, FINGER_REPORT and FIRMWARE. Finally, you can definitly see all of them without any thoughts.

```
echo -1 > /proc/ilitek/debug_level
```
# File sturcture

```
├── common.h
├── core
│   ├── config.c
│   ├── config.h
│   ├── finger_report.c
│   ├── finger_report.h
│   ├── firmware.c
│   ├── firmware.h
│   ├── flash.c
│   ├── flash.h
│   ├── i2c.c
│   ├── i2c.h
│   ├── Makefile
│   ├── mp_test.c
│   ├── mp_test.h
│   └── sup_chip.c
├── Makefile
├── m.sh
├── platform.c
├── platform.h
├── README.md
└── userspace.c
```

* The concepts of this driver is suppose to hide details, and you should ignore the core functions and follow up the example code **platform.c**
to write your own platform c file (like copy it to platform_mtk.c and modify gpio numbers, dts's match name etc.)

* If you want to create more nodes on the device driver for user space, just refer to **userspace.c**.

* In **common.h** file where has a lots of definations about IC's addres and protocol commands on firmware.

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

* V1.0.0.10

 * Fixed the issue of I2CUart when its length of next buffer is larger
   than previous one.
 * Add FW upgrade at boot stage. It will be actiaved when the version of FW that is going to
   upgrade is different the previous version, otherwise it will deny to upgrade.

* V1.0.0.9
  * Add a new way to program firmware if the hex file includes block information.
  * Define the rule to show up detailed debug messages.
  * Add a node to enable the thread to check battery status.
  * Fix some bugs.

* V1.0.0.8
  * Add support of MTK and DMA with I2C.
  * Add kthread to handle interrupt event.
  * Add support of gesture wake up in suspend/resume.
  * Remove power supply notifier at check battery status.
  * Add the functions such as glove, proximity and phone cover.
  * Support new calculation with i2cuart mode.

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
