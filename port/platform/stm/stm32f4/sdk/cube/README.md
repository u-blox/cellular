# Introduction
This directory contains the build infrastructure for STM32F4 using the STM32Cube IDE.

# Usage
Follow the instructions to install the STM32Cube IDE:

https://www.st.com/en/development-tools/stm32cubeide.html

Download a version (this code was tested with version 1.25.0) of the STM32 F4 MCU package ZIP file (containing their HAL etc.) from here:

https://www.st.com/en/embedded-software/stm32cubef4.html

Unzip it to the same directory as you cloned this repo with the name `STM32Cube_FW_F4`, i.e.:

```
..
.
STM32Cube_FW_F4
cellular
```

You may unzip it to a different location but then when you open your chosen build you must go to `Project` -> `Properties` -> `Resource` -> `Linked Resources`, modify the path variable `STM32CUBE_FW_PATH` to point to the correct location and then refresh the project.

So, `cd` to your chosen build under this sub-directory, load the project into the STM32Cube IDE and then follow ST's instructions to build/download the project.

# Trace
To view the SWO trace output in the STM32 Cube IDE, setup up the debugger as normal by pulling down the arrow beside the little button on the toolbar with the "bug" image on it, selecting "STM-Cortex-M C/C++ Application", create a new configuration and then, on the "Debugger" tab, tick the box that enables SWD and click "Apply".

You should then be able to download the Debug build from the IDE and the IDE should launch you into the debugger.  To see the SWD trace output, click on "Window" -> "Show View" -> "SWV" -> "SWV ITM Data Console".  The docked window that appears should have a little "spanner" icon on the far right: click on that icon and, on the set of "ITM Stimulus Ports", tick channel 0 and then press "OK".  Beside the "spanner" icon is a small red button: press that to allow trace output to appear; unfortuantely it seems that this latter step has to be performed every debug session, it is not possible to automate it.