# Introduction
This directory contains the build infrastructure for the Nordic NRF52840 using the Segger Embedded Studio SDK.

# Usage
Follow the instructions to install the development tools:

https://infocenter.nordicsemi.com/topic/ug_nrf52840_dk/UG/common/nordic_tools.html

Make sure you install the Segger Embedded Studio SDK, the Nordic NRF5 SDK and the Nordic command-line tools.  Ensure NO SPACES in the install location of the NRF5 SDK.

Segger Embedded Studio (SES), as far as I can tell, is unable to adopt the value of environment variables.  To get configuration values into SES you must either open the IDE and set them in the `Tools`->`Options`->`Building`->`Build`->`Global Macros` box (e.g. enter `SOME_PATH=c:/my_thing`) or, more flexibly, you can start SES from the command line and specify them with a `-D` prefix e.g.:

```
"C:\Program Files\Segger\SEGGER Embedded Studio for ARM 4.50\bin\emstudio" -D SOME_PATH=c:/my_thing
```

The instructions that follow require you to pass the path of the NRF5 SDK installation into SES by setting the environment variable `NRF5_PATH` in SES, e.g. something like:

```
"C:\Program Files\Segger\SEGGER Embedded Studio for ARM 4.50\bin\emstudio" -D NRF5_PATH=c:/nrf5
```

Do not use quotes around the path and make sure you use `/` and not `\`.

You must also set the type of module you are using, e.g. 

```
"C:\Program Files\Segger\SEGGER Embedded Studio for ARM 4.50\bin\emstudio" -D NRF5_PATH=c:/nrf5 -D CELLULAR_CFG_MODULE_SARA_R5
```

The rest TODO, only building/running the unit tests for now (which test absolutely everything).

# Testing
Before starting SES, add any additional environment variables to your command line, e.g.:

```
"C:\Program Files\Segger\SEGGER Embedded Studio for ARM 4.50\bin\emstudio" -D NRF5_PATH=c:/nrf5 -D CELLULAR_CFG_MODULE_SARA_R5 -D CELLULAR_CFG_PIN_ENABLE_POWER=-1
```

...if your board does not have the ability to control the power supply to the module.


With this done, `cd` to the `unit_test` directory and execute the following:

