# Introduction
This example demonstrates how local communication between the module and the host processor can be protected.  The module must have been security sealed (see the `seal` example for that) for this example to complete.

# Usage
To be completed.

# Compilation Flags/#defines

## Platform Related
The instructions for your chosen platform will tell you how to set/override conditional compilation flags.  There are a number of conditional compilation flags that must be set correctly for your platform in order to build and run this example.

This build requires a module that supports u-blox root of trust, e.g. SARA-R5 or SARA-R412M-03B.  Consult the `cfg/cellular_cfg_module.h` file to determine which module you intend to use and then define the relevant conditional compilation flag.  For instance, to use SARA-R5 `CELLULAR_CFG_MODULE_SARA_R5` must be defined.

The default values for the MCU pins connecting your module to your choice of MCU are defined in the file `port/platform/<vendor>/<chipset>/cfg/cellular_cfg_hw_platform_specific.h`.  You should check if these are correct for your chosen hardware and, if not, override the values of the #defines (where -1 means "not connected").

To include the unit tests and examples in the build, define `CELLULAR_CFG_UBLOX_TEST`.  To run ONLY this example (otherwise all of the unit test and all of the examples will be run) set the #define `CELLULAR_CFG_TEST_FILTER` to the value `exampleThingstreamSecured` (noting that NO quotation marks should be added around the value part).

## Specific To This Example
The following values must be set correctly by opening and editing the values in `main.c`.

To be completed.