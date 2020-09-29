# Introduction
These directories provide the source code examples that show how to use the u-blox security features.  To build and run these source files you must first follow the instructions in the directory above.

All of the examples in this folder require a u-blox module that supports u-blox root of trust (e.g. SARA-R5).

# Overview

[Hamed to complete this with relevant links etc. in Markdown format]

# Examples

- `seal` demonstrates how to security-seal the module, claiming it for yourself.  Your module *must* have been security sealed before any of the other examples can be run.
- `e2e_data_protection` demonstrates how messages can be protected in preparation for secure end to end transmission.
- `e2e_symmetric_kms` [Hamed to complete].
- `local_c2c_security` demonstrates how communications between the module and the host processor can be secured.
- `local_data_protection` [Hamed to complete].
- `secure_communication` demonstrates how to use end to end encryption in a supported u-blox module (e.g. SARA-R5) and exchange these end-to-end encrypted messages using the Thingstream service.