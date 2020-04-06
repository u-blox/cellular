# Introduction
This directory contains the build infrastructure for Nordic NRF52840 using the Segger Embedded Studio SDK.

# Usage
Follow the instructions to install the development tools:

https://infocenter.nordicsemi.com/topic/ug_nrf52840_dk/UG/common/nordic_tools.html

Make sure you install the Segger Embedded Studio SDK, the Nordic NRF5 SDK and the Nordic command-line tools.  Ensure NO SPACES in the install location of the NRF5 SDK.

When you install the NRF5 SDK, if you install it to the same directory as you cloned this repo with the name `nrf5`, i.e.:

```
..
.
nrf5
cellular
```

...then the builds here will find it, otherwise you will need to tell the builds where you have installed it (see below).

Segger Embedded Studio (SES), as far as I can tell, is unable to adopt the value of environment variables.  To get configuration values into SES you must either open the IDE and set them in the `Tools`->`Options`->`Building`->`Build`->`Global Macros` box (e.g. enter `SOME_PATH=c:/my_thing`) or, more flexibly, you can start SES from the command line and specify them with a `-D` prefix e.g.:

```
"C:\Program Files\Segger\SEGGER Embedded Studio for ARM 4.50\bin\emstudio" -D SOME_PATH=c:/my_thing
```

Note that these `-D` configuration items are NOT passed on to the compilation tools like `make` would do, they only get into SES itself as these "global macros".  In order to make that possible the `cellular_pca10056` project file has 10 project variables named `EXTRA0` to `EXTRA9` that *do* get passed on to the compilation tools which you can define for yourself on the command line.  For instance, to effect `#define CELLULAR_CFG_PIN_ENABLE_POWER=-1`, you would do the following:

```
"C:\Program Files\Segger\SEGGER Embedded Studio for ARM 4.50\bin\emstudio" -D EXTRA0="CELLULAR_CFG_PIN_ENABLE_POWER=-1"
```

If you have installed the NRF5 SDK to somewhere other than the location mentioned above, you must set the variable `NRF5_PATH` in the command line to SES, e.g. something like:

```
"C:\Program Files\Segger\SEGGER Embedded Studio for ARM 4.50\bin\emstudio" -D NRF5_PATH=c:/nrf5
```

You must also set the type of cellular module you are using by setting the SES project variable `MODULE_TYPE`, e.g. 

```
"C:\Program Files\Segger\SEGGER Embedded Studio for ARM 4.50\bin\emstudio" -D MODULE_TYPE=CELLULAR_CFG_MODULE_SARA_R5
```

So, `cd` to your chosen build under this sub-directory and load the project file with a line something like:

```
"C:\Program Files\Segger\SEGGER Embedded Studio for ARM 4.50\bin\emstudio" -D NRF5_PATH=c:/nrf5 -D MODULE_TYPE=CELLULAR_CFG_MODULE_SARA_R5 cellular_pca10056.emProject
```