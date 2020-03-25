# Introduction
This directory and its sub-directories contain the build infrastructure for the Nordic NRF52840 building under GCC with Make.

# SDK Installation
The blog post at the link below describes how to install GCC for building the Nordic platform:

https://devzone.nordicsemi.com/nordic/nordic-blog/b/blog/posts/development-with-gcc-and-eclipse

However it expects you to be using Eclipse; the instructions that follow are modified to work wholly from the command-line (and, in my case, on Windows).

First, install the latest version of GCC for ARM from here:

https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/gnu-rm/downloads

Next obtain a version of Make and add it to your path.  I obtained a Windows version from here:

http://gnuwin32.sourceforge.net/packages/make.htm

Install the latest version of the NRF5 SDK from here:

https://www.nordicsemi.com/Software-and-Tools/Software/nRF5-SDK

If you install it to the same directory as you cloned this repo with the name `nrf5`, i.e.:

```
..
.
nrf5
cellular
```

...then the builds here will find it else you will need to set an environment variable `NRF5_PATH` to locate it, or add that on the command-line to `make` (more on this when you get to the sub-directories).

In the `components\toolchain\gcc` sub-directory of the NRF5 installation you will find two makefiles: if you are running on Linux or OS X you need to pay attention to the `.posix` one else pay attention to the `.windows` one.  Edit the appropriate makefile to set the `GNU_INSTALL_ROOT` variable to the location of the `bin` directory of your GCC installation, e.g.:

```
GNU_INSTALL_ROOT := C:/Program Files (x86)/GNU Tools ARM Embedded/9 2019-q4-major/bin/
GNU_VERSION := 9.2.1
GNU_PREFIX := arm-none-eabi
```

Note the use of `/` and not `\`; no quotation marks are required but a final `/` is required.

You will also need to have installed the Nordic command-line tools from here if you haven't done so already, and have these on your path:

https://www.nordicsemi.com/Software-and-Tools/Development-Tools/nRF-Command-Line-Tools

With this done, go to the relevant sub-directory of this directory to actually build something.