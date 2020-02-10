# Introduction
In these directories you will find the implementation of a cellular transport in C for various embedded target platforms.  The implementation consists of:

- `ctrl`: a C API to control a cellular module over an AT interface.
- `sock`: a C sockets API compatible with `lwip` the builds on top of `ctrl`.
- `port`: a porting layer which allows `ctrl` and `sock` to be compiled and tested on various platforms.

# Usage
The C API for `ctrl`, `sock` and `port` can be found in their respective `api` sub-directories and are documented there.  Information on how to build and test for a given target platform can be found in the relevant sub-directory of `port`.