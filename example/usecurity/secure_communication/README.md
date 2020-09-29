# Introduction
This example demonstrates how to use end to end encryption in a supported u-blox module (e.g. SARA-R5) and exchange these end-to-end encrypted messages using the Thingstream service.  The module must have been security sealed (see the `seal` example for that) for this example to complete.

# Usage
This build requires that you have a Thingstream account.  Please visit https://https://thingstream.io/ to set one up.

To build and run this example on a supported platform you need to travel down into the `port/platform/<vendor>/<chipset>/<sdk>` directory of your choice and follow the instructions in the `README.md` there.  Note that examples are built and run in exactly the same way as the unit tests, so look in the `unit_test` build for all detailed instructions.

# Compilation Flags/#defines

## Platform Related
The instructions for your chosen platform will tell you how to set/override conditional compilation flags.  There are a number of conditional compilation flags that must be set correctly for your platform in order to build and run this example.

This build requires a module that supports u-blox root of trust, e.g. SARA-R5 or SARA-R412M-03B.  Consult the `cfg/cellular_cfg_module.h` file to determine which module you intend to use and then define the relevant conditional compilation flag.  For instance, to use SARA-R5 `CELLULAR_CFG_MODULE_SARA_R5` must be defined.

The default values for the MCU pins connecting your module to your choice of MCU are defined in the file `port/platform/<vendor>/<chipset>/cfg/cellular_cfg_hw_platform_specific.h`.  You should check if these are correct for your chosen hardware and, if not, override the values of the #defines (where -1 means "not connected").

To include the unit tests and examples in the build, define `CELLULAR_CFG_UBLOX_TEST`.  To run ONLY this example (otherwise all of the unit test and all of the examples will be run) set the #define `CELLULAR_CFG_TEST_FILTER` to the value `exampleUsecuritySecureCommunication` (noting that NO quotation marks should be added around the value part).

## Specific To This Example
The following values must be set correctly by opening and editing the values in `main.c`.

If your cellular operator requires a specific APN to be set, you should set the value of the #define `MY_APN` to that value in `main.c` (and possibly the associated `MY_USERNAME` and `MY_PASSWORD` if your operator requires them also).

You must have a "thing" in your Thingstream account with which this example can communicate.  Create one in your account and, under "Thing Details", look in the "credentials" tab and set the values of the following #defines in `main.c` (you will see example values in there commented out):

- `MY_THINGSTREAM_CLIENT_ID` - this will begin with "device:"; the value is the whole thing, i.e. including "device:" at the start.
- `MY_THINGSTREAM_USERNAME`  - this will be at least 20 characters long.
- `MY_THINGSTREAM_PASSWORD`  - this will be at least 40 characters long and may have `/` characters within it.
