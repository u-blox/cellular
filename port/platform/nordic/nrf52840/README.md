# Introduction
These directories provide the implementation of the porting layer on the Nordic NRF52840 platform plus the associated build and board configuration information:

- `cfg`: contains the file `cellular_cfg_hw_platform_specific.h` which provides default configuration for an NRF52840 board connecting to a cellular module.  Note that the type of cellular module is NOT specified, you must do that when you perform your build.  Also in here you will find the FreeRTOS configuration header file and the `sdk_config.h` for this build.
- `sdk`: contains the files to build/test for the Nordic NRF52840 platform:
  - `gcc`: contains the build and test files for the Nordic SDK, nRF5 under GCC.
  - `ses`: contains the build and test files for the Nordic SDK, nRF5 under Segger Embedded Studio.
- `src`: contains the implementation of the porting layers for NRF52840.
- `test`: contains the code that runs the unit tests for the cellular code on NRF52840.

# Hardware Requirements
This code may be run on either a Nordic NRF52840 development board or a u-blox NINA-B1 module.  In either case the NRF52840 chip itself is somewhat challenged in the UART department, having only two.  This code needs one to talk to the cellular module leaving one other which might already be required by a customer application.  Hence this code is configured by default to send trace output over the SWDIO (AKA RTT) port which a Segger J-Link debugger can interpret (see the #Debugging section below).

Such a debugger is *already* included on the NRF58240 develoment board however if you're working to a bare NF58240 chip or a bare u-blox NINA-B1 module you REALLY MUST equip yourself with a Segger [J-Link Base](https://www.segger.com/products/debug-probes/j-link/models/j-link-base/) debugger and the right cable to connect it to your board.

For debugging you will need the Segger J-Link tools, of which the Windows ones can be found here:

https://www.segger.com/downloads/jlink/JLink_Windows.exe

If you don't have an NRF52840 board with Segger J-Link built in or you have a bare module etc. and are without a Segger J-Link box, it may be possible to fiddle with the `sdk_config.h` file down in the `cfg` directory to make it spit strings out of the spare UART instead but I don't recommended, it's hell down there.  You would need to enable a UART port, switch off `NRF_LOG_BACKEND_RTT_ENABLED` and fiddle with the likes of `NRF_LOG_BACKEND_UART_ENABLED`, `NRF_LOG_BACKEND_UART_TX_PIN` and `NRF_LOG_BACKEND_UART_BAUDRATE`.  Good luck!

# Chip Resource Requirements
This code requires the use of two `TIMER` peripherals (one for time and unfortunately another to count UART received characters) and one `UARTE` peripheral on the NRF52840 chip.  The default choices are specified in `cellular_cfg_hw_platform_specific.h` and can be overriden at compile time.  Note that the way the `TIMER`, actually used as a counter, has to be used with the `UARTE` requires the PPI (Programmable Peripheral Interconnect) to be enabled.

# Segger RTT Trace Output
To obtain trace output, start JLink Commander from a command-line with:

```
jlink -Device NRF52840_XXAA -If SWD -Speed 500 -Autoconnect 1
```

With this done, from a separate DOS box start `JLinkRTTClient` and it will find the JLink session and connect up to display trace output.  The first run of the target after programming or power-on for some reason takes about 10 seconds to start and then doesn't work properly but all subsequent runs will work.  There may be some character loss, hopefully minimal.

To reset the target, in `jlink` user `r` to stop it and `g` to kick it off again.

