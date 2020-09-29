# Introduction
These directories provide the source code examples that show how to use the various APIs.  To build and run these source files on a supported platform you need to travel down into the `port/platform/<vendor>/<chipset>/<sdk>` directory of your choice and follow the instructions in the `README.md` there.  For instance to build the examples on an ESP32 chip you would go to `port/platform/espressif/esp32/esp-idf` and follow the instructions in the `README.md` there to both install the Espressif development environment and build/run the examples.

Note that examples are built and run in exactly the same way as the unit tests, so look in the `unit_test` build for all detailed instructions.

# Examples

- `usecurity` contains examples of how to use special u-blox security features on u-blox modules that support root of trust (e.g. SARA-R5).