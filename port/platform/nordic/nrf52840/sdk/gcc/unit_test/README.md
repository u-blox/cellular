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

Note: you may put this repo in a different location but if you do so you will need to either create an environment variable named `UNITY_PATH` and set it to the path of your `Unity` directory (using `/` instead of `\`), e.g.:

```
set UNITY_PATH=c:/Unity
```

... or add `UNITY_PATH=c:/Unity` on the command-line to `make`.

Likewise, if you haven't installed the NRF5 SDK at that same directory level and named it `nrf5` then you will need to either create an environment variable named `NRF5_PATH` and set it to the path of your NRF5 SDK installation, e.g.:

```
set NRF5_PATH=c:/nrf5
```

... or add `NRF5_PATH=c:/nrf5` on the command-line to `make`.

With that done `cd` to this directory and enter:

`make flash CFLAGS=-DCELLULAR_CFG_MODULE_SARA_R4`

This will build the code assuming a SARA-R4 module and download it to a connected NRF52840 development board.  If the pins you have connected between the NRF52840 and the cellular module are different to the defaults, just add the necessary overrides to the `CFLAGS` line, e.g.:

`make flash CFLAGS=-DCELLULAR_CFG_MODULE_SARA_R4 -DCELLULAR_CFG_PIN_VINT=-1`

