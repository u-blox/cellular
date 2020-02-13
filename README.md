# Introduction
In this repository you will find the implementation of a cellular transport in C for various embedded target platforms.  The implementation consists of:

- `ctrl`: a driver that allows control of a cellular module over an AT interface, e.g. make connection, read IMEI, read signal strength, etc.
- `sock`: a driver that allows data transfer through a cellular module, presenting the same TCP/UDP sockets API as `lwip` and building on top of the AT parser in `ctrl`.
- `port`: a limited porting layer which allows `ctrl` and `sock` to be compiled and tested on various platforms.

![Architecture](pics-for-readme/architecture.jpg)

# Usage
The C API for `ctrl`, `sock` and `port` can be found in their respective `api` sub-directories and are documented in the header files there.  Information on how to build run tests on a given target platform can be found in the `README.md` files of the relevant platform sub-directory of `port`.

# License
The software in this repository is Apache 2.0 licensed.  The AT parsing code in `ctrl` is derived from the Apache 2.0 licensed AT parser of mbed-os; copyright (and our thanks) remain with the original authors.