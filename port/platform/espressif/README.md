These directories provide the implementation of the porting layer on the Espressif ESP32 platform plus the associated build and board configuration information:

- `cfg`: contains the single file `cellular_cfg_hw.h` which provides default configuration for the u-blox `WHRE` board which includes the `NINA-W1` module, inside of which is an ESP32 chip, and a `SARA-R4` module.
- `esp-idf`: contains the build and test files for the native ESP32 SDK, ESP-IDF.
- `amazon-freertos`: contains the build files for Amazon FreeRTOS, providing ESP32 support in that SDK.
- `src`: contains the implementation of the porting layers for ESP32 in general and also, in a sub-directory, for ESP32-specific parts of the Amazon FreeRTOS integration.