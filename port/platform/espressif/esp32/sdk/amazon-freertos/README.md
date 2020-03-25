# Introduction
This directory contains the build infrastructure for the Espressif ESP32 platform under Amazon FreeRTOS.  The Amazon FreeRTOS network manager has no concept of a cellular network connection and so this repo replaces the existing Wifi connectivity under Amazon FreeRTOS with cellular connectivity.

# Usage
Follow the instructions to set up Amazon FreeRTOS and build/run it on the ESP32 platform.  Ensure that you can successfully run the standard Wifi-based demonstration of the default "Hello MQTT" demo.

https://docs.aws.amazon.com/freertos/latest/userguide/getting_started_espressif.html

Clone this repo to the same level in your directory tree as Amazon FreeRTOS, i.e.:

```
..
.
amazon-freertos
cellular
```

Note: you may put this repo in a different location but if you do so you will need to edit the definition of `cellular_dir` in the `CMakeLists.txt` file of this directory.

With this done, find the `CMakeLists.txt` file in the `vendors\espressif\boards\esp32` directory of Amazon FreeRTOS.  Make a back-up of it and then overwrite it with the `CMakeLists.txt` file from this directory.

To define which cellular module you are using (e.g. one of `CELLULAR_CFG_MODULE_SARA_R4` or `CELLULAR_CFG_MODULE_SARA_R5`).  Create an environment variaable called `CELLULAR_FLAGS` and set it to be that module name in the form:

```
set CELLULAR_FLAGS=-DCELLULAR_CFG_MODULE_SARA_R5
```

Clumsy, I know, but it was the only way I could find to pass adhoc conditional compilation flags into CMake via the command-line.  You can overried any other parameters in there, it's just a list, so for instance if you wanted to sat  that there's no "enable power" capability on your board you might use:

```
set CELLULAR_FLAGS=-DCELLULAR_CFG_MODULE_SARA_R5 -DCELLULAR_CFG_PIN_ENABLE_POWER=-1
```

Follow the Amazon FreeRTOS build instructions for your platform but add the parameter `-DAFR_ESP_LWIP=1` to the CMake build line, i.e. so that the environment variable `AFR_ESP_LWIP` is defined for the compiler and set to 1.  On Windows this would be something like:

```
cmake -DVENDOR=espressif -DBOARD=esp32_wrover_kit -DCOMPILER=xtensa-esp32 -DAFR_ESP_LWIP=1 -GNinja -S . -B build
```

This tells Amazon FreeRTOS to link in the LWIP API, through which the code in this repository provides a cellular driver.

The SSID field of the Wifi API is re-used to provide the APN for cellular.  A default APN will be supplied by the cellular network and hence this field must usually be left empty.  You will have followed the Amazon FreeRTOS instructions to edit the file `tools\aws_config_quick_start\configure.json` to contain your Wifi network SSID etc.  and hence the field `wifi_ssid` must be changed to an empty string once more.  You can change this in `configure.json` however it won't have any effect, the value will have already been written into `demos\include\aws_clientcredential.h` by the quick-start script so you need to go into `aws_clientcredential.h` and change it there.

If you do have a specific APN, e.g. for some specific/feature from the network, then enter that APN in the field instead.  The other fields in `configure.json` can be left as they are.

Continue following the Amazon FreeRTOS instructions to generate the binary and download it to the ESP32 platform. When you run the "Hello MQTT" demo it will now run over cellular instead of Wifi.