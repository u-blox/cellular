# Introduction
These directories provide a driver which implements the control interface to a cellular module.

# Usage
The directories includes only the API and C source files.  They rely upon the `port` directory to map to a target platform and provide the necessary build infrastructure for that target platform; see the relevant platform directory under `port` for build and usage information.

# Testing
The tests for these directories are carried out on your chosen platform and hence can be found in the relevant platform directory of the `port` component along with instructions on how to run the tests.