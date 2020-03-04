# Introduction
This directory contains the build infrastructure for the Espressif ESP32 platform under Amazon FreeRTOS.  The Amazon FreeRTOS network manager has no concept of a cellular network connection and so this repo replaces the existing Wifi connectivity under Amazon FreeRTOS with cellular connectivity.

# Usage
Follow the instructions to set up Amazon FreeRTOS and build/run it on the ESP32 platform.  Ensure that you can successfully run the standard Wifi demonstration.

Clone this repo to the same level in your directory as Amazon FreeRTOS, i.e.:

```
..
.
amazon-freertos
cellular
```

Note: you may put this repo in a different location but if you do so you will need to edit the definition of `cellular_dir` in the `CMakeLists.txt` file of this directory.

With this done, find the `CMakeLists.txt` file in the `vendors\espressif\boards\esp32` directory of Amazon FreeRTOS.  Make a back-up of it and then overwrite the `CMakeLists.txt` file with the `CMakeLists.txt` file from this directory.

Follow the Amazon FreeRTOS build instructions for your platform but add the parameter `-DAFR_ESP_LWIP=1` to the CMake build line, i.e. so that the environment variable `AFR_ESP_LWIP` is defined for the compiler and set to 1.  On Windows this would be something like:

```
cmake -DVENDOR=espressif -DBOARD=esp32_wrover_kit -DCOMPILER=xtensa-esp32 -DAFR_ESP_LWIP=1 -GNinja -S . -B build
```

This tells Amazon FreeRTOS to link in the LWIP API, under which the code in this repository provides a cellular driver.

Continue following the Amazon FreeRTOS instructions to generate the binary and download it to the ESP32 platform.