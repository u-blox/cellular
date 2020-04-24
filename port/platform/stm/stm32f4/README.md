#Introduction
These directories provide the implementation of the porting layer on the STM32F4 platform plus the associated build and board configuration information:

- `cfg`: contains the file `cellular_cfg_hw_platform_specific.h` which provides default configuration for a u-blox C030-R412M board that includes a SARA-R412M cellular module.  Note that in order to retain the flexibility to use the same STM32F4 code with other u-blox cellular modules the module type is NOT specified in this configuration file, you must do that when you perform your build.  Also in here you will find the FreeRTOS configuration header file.
- `sdk/cube`: contains the files to build/test for the STM32F4 platform using the STM32Cube IDE.
- `src`: contains the implementation of the porting layers for STM32F4.

#Hardware Requirements
This code was developed to run on the u-blox C030-R412M board, which includes an STM32F437VG chip, a SARA-R412M cellular module and a separate ST debug chip.  However there is no reason why it would not work on any STM32F4 chip talking to another supported u-blox cellular module, see `cfg/cellular_cfg_module.h`.

#Chip Resource Requirements
TBD.

#Downloading To The Board
When the C030-R412M board is plugged into a USB port a mapped drive should appear, e.g. `D:`.  Builds are downloaded by copying your compiled `.bin` file to this drive.  Builds can also be downloaded using ST's (ST-Link utility)[https://www.st.com/en/development-tools/stsw-link004.html] and through the STM32Cube IDE.

#Trace Output
The trace output from this build is sent over SWD.  Instructions for how to view the trace output in the STM32Cube IDE can be found in the `cube` sub-directory below.

Alternatively, if you just want to run the target without the debugger and simply view the SWO output, the (ST-Link utility)[https://www.st.com/en/development-tools/stsw-link004.html] utility includes a "Printf via SWO Viewer" option under its "ST-LINK" menu.  Set the clock to the same value as that used in the configuration above (by default 16 MHz), press "Start" and your debug printf()s will appear in that window.  You can also run this from the command line with something like:

```
"C:\Program Files (x86)\STMicroelectronics\STM32 ST-LINK Utility\ST-LINK Utility\ST-LINK_CLI.exe" SWD Freq=2 UR
```

*** THE REST OF THIS A WORK IN PROGRESS ***

https://arm-stm.blogspot.com/2014/12/debug-mcu-configuration-register.html

(DBGMCU_CR)
DBGMCU_CR is mapped on the External PPB bus at address 0xE0042004

 TRACE_IOEN (bit 5) and
TRACE_MODE



There are several steps required for enabling the SWV trace.

Set DEMCR.TRCENA = 1 to enable access to the trace components' registers.

Select the required pin protocol in the TPIU_SPPR register:

0 - parallel synchronous trace port (not SWV)

1 - Serial Wire Viewer, Manchester encoding

2 - Serial Wire Viewer, UART Non-Return to Zero encoding

Write 0xC5ACCE55 to the ITM's CoreSight Lock Access Register (ITM_LAR) to unlock the ITM. This register is missing from some editions of the processor's Technical Reference Manual (TRM).

Set ITM_TCR.ITMENA = 1 to enable the ITM, together with additional bit fields in this register to enable timestamps, synchronization packets, and others as required.

Enable the individual channels as required by setting bits in ITM_TER0. You might also wish to control the privilege level of the stimulus channels by writing to the four least significant bits of ITM_TPR.

Write values to your enabled stimulus channels (ITM_STIM0 to ITM_STIM31) to generate SWV trace activity.


https://developer.arm.com/docs/128795835/10/enabling-itm-trace-on-serial-wire-viewer

Set DEMCR (0xE000EDFC) TRCENA (bit 24) = 1 to enable access to the trace components' register: set {int}0xE000EDFC=0x01000000
Select protocol "UART Non-Return to Zero encoding" in TPIU_SPPR (0xE00400F0) register:         set {int}0xE00400F0=2
Initiate access by writing 0xC5ACCE55 to ITM_LAR (0xE0000FB0):                                 set {int}0xE0000FB0=0xC5ACCE55
Set ITM_TCR (0xE0000E80) ITMENA (bit) and others:                                              set {int}0xE0000E80=0x0001000f
Enable channel 0 by writing 1 to ITM_TER (0xE0000E00):                                         set {int}0xE0000E00=1



Set DBGMCU_CR.TRACE_IOEN (bit 5):  set {int}0xE0042004={int}0xE0042004 | 0x20
Enable trace channel 0:            set {int}0xE0000E00={int}0xE0000E00 | 1
Set trace control: set {int}0xE0000E80={int}0xE0000E80 | 1


0xE0000FB0	ITM_LAR
