# Introduction
This directory contains the build infrastructure for the native Espressif ESP32 platform build system, AKA ESP-IDF.

# SDK Installation
Follow the instructions to build for the ESP32 platform.  Note that this will use the very latest v4 Espressif environment, rather than the old v3.3 stuff used by the Amazon FreeRTOS SDK.

https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html#get-started-step-by-step

Only building/running the unit tests (which test absolutely everything) are supported at this moment.

# Testing
To build the unit tests, you first need to define which module you are using (e.g. one of `CELLULAR_CFG_MODULE_SARA_R4` or `CELLULAR_CFG_MODULE_SARA_R5`).  To do this, create an environment variable called `CELLULAR_FLAGS` and set it to be that module name in the form:

```
set CELLULAR_FLAGS=-DCELLULAR_CFG_MODULE_SARA_R5
```

Clumsy, I know, but it was the only way I could find to pass adhoc conditional compilation flags into CMake via the command-line.  You can overried any other parameters in there, it's just a list, so for instance if you wanted to sat  that there's no "enable power" capability on your board you might use:

```
set CELLULAR_FLAGS=-DCELLULAR_CFG_MODULE_SARA_R5 -DCELLULAR_CFG_PIN_ENABLE_POWER=-1
```

With this done, `cd` to the `unit_test` directory and execute the following:

```
idf.py  flash monitor -p COMx -D TEST_COMPONENTS="cellular_tests"
```

...where `COMx` is replaced by the COM port to which your ESP32 board is attached. The command adds this directory to ESP-IDF as an ESP-IDF component and requests that the tests for this component are built, downloaded to the board and run.

When the code has built and downloaded, the Espressif monitor terminal will be launced on the same `COMx` port at 115200 baud and the board will be reset.  If you prefer to use your own serial terminal program then omit `monitor` from the command line above and launch your own serial terminal program instead.  You should see a prompt:

```
Press ENTER to see the list of tests.
```

Press ENTER and the tests will be listed, something like:

```
Here's the test menu, pick your combo:
(1)     "initialisation" [ctrl]
(2)     "initialisation" [port]

Enter test for running.
```

Press 1 followed by ENTER to test number 1, \* to run all tests, etc.

# Tracing Guru Meditation Errors (Processor Exceptions)
Good advice on tracing the cause of processor exceptions can be found [here](https://docs.espressif.com/projects/esp-idf/en/latest/api-guides/fatal-errors.html).  Note that viewing the debug stream from the target in the ESP-IDF monitor program (i.e. with `monitor` as one of the parameters to `idf.py`) will give you  file/line numbers for the back-trace immediately.  If you don't happen to be running the ESP-IDF monitor program when the error occurs, you can decode addresses to file/line numbers with:

`xtensa-esp32-elf-addr2line -pfiaC -e <.elf file> ADDRESS`

...where `<.elf file>` is replaced by the path to the `.elf` file created by the build process; for example, if you are running unit tests this will be `build/unit-test-app.elf`.

If you're getting nowhere, you can compile your code with GDB stub switched on (under `ESP32 Specific` in `menuconfig`).  When an exception is hit the target halts and waits for you to connect GDB: if you're in the `monitor` this should happen automatically, otherwise exit any terminal program you are using and invoke ESP32 GDB instead on the same port with:

`xtensa-esp32-elf-gdb -ex "set serial baud 912600" -ex "target remote COMx" -ex interrupt <.elf file>`

...where `COMx` is replaced by the COM port you are using (e.g. in my case `\\.\COM17`) and `<.elf file>` is replaced by the path to the `.elf` file created by the build process.  IMPORTANT: if your COM port is higher than 9 then when invoking `xtensa-esp32-elf-gdb`, or when invoking `monitor` to invoke subsequently invoke GDB correctly, you must put `\\.\` in front of the COM port name; this is because `xtensa-esp32-elf-gdb` uses the Windows serial port API and that's just the way it works.  IMPORTANT: if you are working from Windows and with a WHRE demo board in a development carrier which has RTS/CTS wired to reset `NINA-W1` you will need to have this board modified to NOT do that for this GDB stub thing to work.  This is because, between running applications, Windows sets the flow control lines to a state which resets `NINA-W1`, so between switching from `monitor` to `xtensa-esp32-elf-gdb` the device will have been reset.

Anyway, once at the GDB prompt you can enter any [GDB command](https://darkdust.net/files/GDB%20Cheat%20Sheet.pdf) to look around at memory, though you can't set breakpoints/watchpoints or continue execution.  HOWEVER, note that I found that `xtensa-esp32-elf-gdb` would just exit immediately under Windows 10; I had to run the Windows compatiblity troubleshooter and get that to save working settings for it (Windows 8) first.  Anyway, commands like `x/ 0x400C0000` will print the contents of the first location in RTC fast memory in decimal, or `x/x 0x400C0000` will print it in hex, or `print thingy` will print the value of the variable `thingy`, or `print &thingy` will print the address of the variable `thingy`, etc.