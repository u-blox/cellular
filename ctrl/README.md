# Introduction
These directories provide a driver that implements the control interface to a cellular module.

# Usage
The directories include only the API and pure C source files that make no reference to a platform, a C library or an operating system.  They rely upon the `port` directory to map to a target platform and provide the necessary build/test infrastructure for that target platform; see the relevant platform directory under `port` for build and usage information.

# Testing
The `test` directory contains generic tests for the `ctrl` API. Please refer to the relevant platform directory of the `port` component for instructions on how to run the tests.