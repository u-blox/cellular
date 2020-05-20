# Introduction
This directory contains the unit test build for Nordic NRF52840 under GCC with Make.

# Usage
Make sure you have followed the instructions in the directory above this to install the GCC toolchain, Make and the Nordic command-line tools.

You will also need a copy of Unity, the unit test framework, which can be Git cloned from here:

https://github.com/ThrowTheSwitch/Unity

Clone it to the same directory level as `cellular`, i.e.:

```
..
.
Unity
cellular
```

Note: you may put this repo in a different location but if you do so you will need to add, for instance, `UNITY_PATH=c:/Unity` on the command-line to `make`.

Likewise, if you haven't installed the NRF5 SDK at that same directory level and named it `nrf5` then you will need to add, for instance, `NRF5_PATH=c:/nrf5` on the command-line to `make`.

With that done `cd` to this directory and enter:

`make flash CFLAGS=-DCELLULAR_CFG_MODULE_SARA_R5`

This will build the code assuming a SARA-R5 module and download it to a connected NRF52840 development board.  If the pins you have connected between the NRF52840 and the cellular module are different to the defaults, just add the necessary overrides to the `CFLAGS` line, e.g.:

`make flash CFLAGS="-DCELLULAR_CFG_MODULE_SARA_R5 -DCELLULAR_CFG_PIN_VINT=-1"`

...noting the use of quotation marks (otherwise the second `-D` will appear as another parameter to `make` which will just cause it to pause for 30 seconds). 
