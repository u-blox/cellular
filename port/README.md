# Introduction
These directories provide the porting layer that allows the files in the `ctrl` and `sock` directories to be linked and run on a target platform.

No attempt is made to create a full platform porting API; only the APIs necessay for `ctrl` and `sock` to run are implemented here.

# Usage
The API directory defines the platform API, as required by `ctrl` and `sock`.  The other directories provide the implementation of that API on various target platforms and the necessary instructions to create a working binary, plus tests, on each of those target platform.  Please refer to the README.md file in your chosen target platform directory for more information.