# Introduction
This example demonstrates sealing of a module, a pre-requisite for all of the u-blox security tests.

# Usage
This example performs security sealing of a u-blox using a "device information" string, which you must obtain from u-blox, and the IMEI of the cellular module as a serial number.  You must obtain the device information string from u-blox [Hamed to describe how].

When you have this string, set it as the value of the compilation flag `MY_SECURITY_DEVICE_INFO`, either by editing `main.c` or by setting the compilation flag in the manner defined for your chosen platform.

If `MY_SECURITY_DEVICE_INFO` is NOT defined this example will still run but it will not actually perform the security sealing step: this can be a good "dry run".

# Other Compilation Flags/#defines
The instructions for your chosen platform will tell you how to set/override conditional compilation flags.  There are a number of conditional compilation flags that must be set correctly for your platform in order to build and run this example.

This build requires a module that supports u-blox root of trust, e.g. SARA-R5 or SARA-R412M-03B.  Consult the `cfg/cellular_cfg_module.h` file to determine which module you intend to use and then define the relevant conditional compilation flag.  For instance, to use SARA-R5 `CELLULAR_CFG_MODULE_SARA_R5` must be defined.

The default values for the MCU pins connecting your module to your choice of MCU are defined in the file `port/platform/<vendor>/<chipset>/cfg/cellular_cfg_hw_platform_specific.h`.  You should check if these are correct for your chosen hardware and, if not, override the values of the #defines (where -1 means "not connected").

To include the unit tests and examples in the build, define `CELLULAR_CFG_UBLOX_TEST`.  To run ONLY this example (otherwise all of the unit test and all of the examples will be run) set the #define `CELLULAR_CFG_TEST_FILTER` to the value `exampleUsecuritySeal` (noting that NO quotation marks should be added around the value part).