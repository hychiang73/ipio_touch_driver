# Introduce

Ipio driver is a new touch driver used on the new products of ILITEK touch ICs. It has been verified on the firefly-rk3288 platform with Android 5.0.

# Support ICs

The following lists which of IC types supported by the driver.

* ILI7807F
* ILI9881F

# Release Note

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
