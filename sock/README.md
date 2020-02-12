# Introduction
These directories provide a driver which implements the user data (sockets) interface to a cellular module.  The API presented is the same as that of `lwip`, allowing the files here to take the place of `lwip`, providing a cellular-based sockets interface instead. 

The files under the `ctrl` directory provide the actual AT interface to the cellular module and hence are required by the files in this directory (and those of `port`, see next section) to achieve a usable binary image.

# Usage
The directories include only the API and pure C source files which make no reference to a platform, a C library or an operating system.  They rely upon the `port` directory to map to a target platform and provide the necessary build infrastructure for that target platform; see the relevant platform directory under `port` for build and usage information.

# Testing
The `test` directory contains generic tests for the `sock` API. Please refer to the platform directory of the `port` component for instructions on how to run the tests.