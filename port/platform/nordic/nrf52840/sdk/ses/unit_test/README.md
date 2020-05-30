# Introduction
This directory contains the example and unit test builds for Nordic NRF52840 under GCC with Segger Embedded Studio (SES).

# Usage
Make sure you have followed the instructions in the directory above this to install the SES and the Nordic command-line tools.

You will also need a copy of Unity, the unit test framework, which can be Git cloned from here:

https://github.com/ThrowTheSwitch/Unity

Clone it to the same directory level as `cellular`, i.e.:

```
..
.
Unity
cellular
```

Note: you may put this repo in a different location but if you do so you will need to specify the path of your `Unity` directory on the command-line to SES (using `/` instead of `\`), e.g.:

```
"C:\Program Files\Segger\SEGGER Embedded Studio for ARM 4.50\bin\emstudio" -D UNITY_PATH=c:/Unity
```

Otherwise follow the instructions in the directory above this to start SES and build/run the examples or unit tests on NRF52840.