# Introduction

These directories provide the implementation of the porting layer on various platforms.  The `common` directory contains `.c` files that are common to more than one platform (e.g. files for `amazon-freertos` which may be used by `espressif` and also by other platforms that run `amazon-freertos`).

# Structure

Each platform directory will include the following sub-directories:

- `sdk` - build files for the SDKs on which that platform can be used.
- `src` - the `.c` files that implement the porting layer for that platform.
- `cfg` - definitions for the specific HW on which that platform is supported.  This shall include, as a minimum:
  - the following pins, where -1 is used to indicate that there is no such pin:
    - `CELLULAR_CFG_PIN_ENABLE_POWER` the GPIO output pin that enables power to the cellular module, may be -1.
    - `CELLULAR_CFG_PIN_CP_ON` the GPIO output pin that is connected to the CP_ON pin of the cellular module.
    - `CELLULAR_CFG_PIN_VINT` the GPIO input pin that is connected to the VInt pin of the cellular module, may be -1.
    - `CELLULAR_CFG_PIN_TXD` the GPIO output pin that sends UART data to the cellular module.
    - `CELLULAR_CFG_PIN_RXD` the GPIO input pin that receives UART data from the cellular module.
    - `CELLULAR_CFG_PIN_CTS` the GPIO input pin that the cellular module will use to indicate that it is ready to receive data on `CELLULAR_CFG_PIN_TXD`, may be -1.
    - `CELLULAR_CFG_PIN_RTS` the GPIO output pin that tells the cellular module that it can send more data on `CELLULAR_CFG_PIN_RXD`, may be -1.
  - `CELLULAR_CFG_UART` the UART HW block to use inside the chipset.
  - `CELLULAR_CFG_RTS_THRESHOLD` the buffer threshold at which RTS is deasserted to stop the cellular module sending data to into the UART, must be specified if `CELLULAR_CFG_WHRE_PIN_RTS` is not -1.