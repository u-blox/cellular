# Introduction
These directories provide a driver which implements the user data (sockets) interface to a cellular module.

The files under the `ctrl` directory provide the AT interface to the cellular module and hence must be linked with this directory to achieve a usable binary image.

# Usage
The directories includes only the API and C source files.  They rely upon the `port` directory to map to a target platform and provide the necessary build infrastructure for that target platform; see the relevant platform directory under `port` for build and usage information.

# Testing
The tests for these directories are carried out on your chosen platform and hence can be found in the relevant platform directory of the `port` component along with instructions on how to run the tests.