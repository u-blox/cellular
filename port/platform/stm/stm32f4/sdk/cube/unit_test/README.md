# Introduction
This directory contains the unit test build for STM32F4 under the STM32Cube IDE.

IMPORTANT: the STM32Cube IDE creates lots of sub-directories in here for some reason.  Please ignore them, NONE of the source files/header files are down here, they are all up on the `test` sub-directory for this platform.

# Usage
Make sure you have followed the instructions in the directory above this to install the STM32Cube IDE toolchain and STM32F4 MCU support files.

You will also need a copy of Unity, the unit test framework, which can be Git cloned from here:

https://github.com/ThrowTheSwitch/Unity

Clone it to the same directory level as `cellular`, i.e.:

```
..
.
Unity
cellular
```


Note: you may put this repo in a different location but if you do so, when open the project in the STM32Cube IDE you must go to `Project` -> `Properties` -> `Resource` -> `Linked Resources`, modify the path variable `UNITY_PATH` to point to the correct location and then refresh the project.

BEFORE running the STM32Cube IDE you first need to define which module you are using (e.g. one of `CELLULAR_CFG_MODULE_SARA_R4` or `CELLULAR_CFG_MODULE_SARA_R5`).  To do this, create an environment variable called `CELLULAR_FLAGS` and set it to be that module name in the form:

```
set CELLULAR_FLAGS=-DCELLULAR_CFG_MODULE_SARA_R5
```

You can override any other parameters in there, it's just a list, so for instance if you wanted to say that there's no "enable power" capability on your board you might use:

```
set CELLULAR_FLAGS=-DCELLULAR_CFG_MODULE_SARA_R5 -DCELLULAR_CFG_PIN_ENABLE_POWER=-1
```

With that done load the project into the STM32Cube IDE and run it.

TODO: describe testing process.