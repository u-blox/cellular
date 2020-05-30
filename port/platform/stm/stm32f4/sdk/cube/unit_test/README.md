# Introduction
This directory contains the examples and unit tests build for STM32F4 under the STM32Cube IDE.

IMPORTANT: the STM32Cube IDE creates lots of sub-directories in here.  Please ignore them, NONE of the source files/header files are down here, they are all up in the `test` sub-directory for this platform so that they can be built with other SDKs as required.  Don't delete the empty directories though: if you do the STM32Cube IDE will then delete the links to the files in the `.project` file.  Weird, but the only way I could find to keep the source code independent of the SDK that happens to build it.

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


Note: you may put this repo in a different location but, if you do so, when you open the project in the STM32Cube IDE you must go to `Project` -> `Properties` -> `Resource` -> `Linked Resources`, modify the path variable `UNITY_PATH` to point to the correct location and then refresh the project.

With that done load the project into the STM32Cube IDE to build, download and run it.

TODO: describe running/testing process.