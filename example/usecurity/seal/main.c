/*
 * Copyright 2020 u-blox Cambourne Ltd
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
    http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/* This file demonstrates security sealing of a u-blox module,
 * effecively "claiming" the module for the customer.
 * The choice of module and the choice of platform on which this
 * code runs is made at build time, see the README.md for
 * instructions.
 */

#ifdef CELLULAR_CFG_OVERRIDE
# include "cellular_cfg_override.h" // For a customer's configuration override
#endif
#include "cellular_cfg_hw_platform_specific.h"
#include "cellular_cfg_os_platform_specific.h"
#include "cellular_cfg_sw.h"
#include "cellular_cfg_module.h"
#include "cellular_port_clib.h"
#include "cellular_port.h"
#include "cellular_port_debug.h"
#include "cellular_port_os.h"
#include "cellular_port_uart.h"
#include "cellular_ctrl.h"
#ifdef CELLULAR_CFG_UBLOX_TEST
// This purely for internal u-blox testing
# include "cellular_port_test_platform_specific.h"
# include "cellular_cfg_test.h"
#endif

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// Please uncomment the line below and set its value to
// the device information you received from u-blox.
// e.g. #define MY_SECURITY_DEVICE_INFO "ZHlN70dVgUWCdfNeXHkQRg"
//#define MY_SECURITY_DEVICE_INFO

// If you want this example to run in a particular RAT
// e.g. CatM1, NB1, etc., define it either by replacing
// the lines below with, for instance:
//
// #define MY_RAT CELLULAR_CTRL_RAT_CATM1
//
// or by setting MY_RAT to your chosen RAT outside this
// build.  The CELLULAR_CFG setting below is simply
// used during internal u-blox testing of this example.
#if !defined(MY_RAT) && defined(CELLULAR_CFG_TEST_RAT)
# define MY_RAT CELLULAR_CFG_TEST_RAT
#endif

// If you chose CATM1 or NB1 as your RAT, you may
// wish to chose the bands to use by editing
// the lines below to set both MY_BANDMASK1 and
// MY_BANDMASK2 (both must be set) or by setting
// both MY_BANDMASK1 and MY_BANDMASK2 outside this build.
// The CELLULAR_CFG setting below is simply used during
// internal u-blox testing of this example.
#if !defined(MY_BANDMASK1) && defined(CELLULAR_CFG_TEST_BANDMASK1)
# define MY_BANDMASK1 CELLULAR_CFG_TEST_BANDMASK1
#endif
#if !defined(MY_BANDMASK2) && defined(CELLULAR_CFG_TEST_BANDMASK2)
# define MY_BANDMASK2 CELLULAR_CFG_TEST_BANDMASK2
#endif

// If your cellular subscription needs an APN, possibly
// with an associated username or password, then set it here.
#ifndef MY_APN
# define MY_APN      NULL
#endif
#ifndef MY_USERNAME
# define MY_USERNAME NULL
#endif
#ifndef MY_PASSWORD
# define MY_PASSWORD NULL
#endif

// Only include this example if root of trust
// is supported by the cellular module.
#if CELLULAR_CTRL_SECURITY_ROOT_OF_TRUST

#ifndef CELLULAR_PORT_TEST_FUNCTION
# error if you are not using the unit test framework to run this code you must ensure that the platform clocks/RTOS are set up and either define CELLULAR_PORT_TEST_FUNCTION yourself or replace it as necessary.
#endif

// For u-blox internal testing only
#ifdef CELLULAR_PORT_TEST_ASSERT
# define EXAMPLE_FINAL_STATE(x) CELLULAR_PORT_TEST_ASSERT(x);
#else 
# define EXAMPLE_FINAL_STATE(x)
#endif

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// Used for keepGoingCallback() timeout.
static int64_t gStopTimeMs;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

// Callback function for the cellular connection process,
// return true while the connection process should continue,
// false to stop it.
static bool keepGoingCallback()
{
    bool keepGoing = true;

    if (cellularPortGetTickTimeMs() > gStopTimeMs) {
        keepGoing = false;
    }

    return keepGoing;
}

// Print a buffer in hex
static void printHex(char *pBuffer, char bufferLength)
{
    for (size_t x = 0; x < bufferLength; x++) {
        cellularPortLog("%02x", *pBuffer);
        pBuffer++;
    }
}

// Connect to the cellular network, returning 0 on success
// else negative error code.
static int32_t connect()
{
#if defined(MY_RAT) && defined(MY_BANDMASK1) && defined(MY_BANDMASK2)
    bool rebootRequired = false;
    uint64_t bandMask1;
    uint64_t bandMask2;
#endif

#ifdef MY_RAT
    // Check if we're on the correct RAT
    if (cellularCtrlGetRat(0) != MY_RAT) {
        cellularCtrlSetRatRank(MY_RAT, 0);
        rebootRequired = true;
    }
#endif

#if defined(MY_RAT) && defined(MY_BANDMASK1) && defined(MY_BANDMASK2)
    // If we're using CAT-M1 or NB1 RAT make sure that the band mask is correct
    if ((MY_RAT == CELLULAR_CTRL_RAT_CATM1) || (MY_RAT == CELLULAR_CTRL_RAT_NB1)) {
        cellularCtrlGetBandMask(MY_RAT, &bandMask1, &bandMask2);
        if ((bandMask1 != MY_BANDMASK1) || (bandMask2 != MY_BANDMASK2)) {
            cellularCtrlSetBandMask(MY_RAT, bandMask1, bandMask2);
            rebootRequired = true;
        }
    }
#endif

#ifdef MY_RAT
    // If we made a change above, the cellular module must be rebooted
    // for it to take effect
    if (rebootRequired) {
        cellularCtrlReboot();
    }
#endif

    // Now connect to the network
    gStopTimeMs = cellularPortGetTickTimeMs() + (180 * 1000);
    return cellularCtrlConnect(keepGoingCallback, MY_APN, MY_USERNAME, MY_PASSWORD);
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: THE EXAMPLE_USECURITY
 * -------------------------------------------------------------- */

// The entry point: before this is called the system clocks have
// been started and the RTOS is running; we are in task space.
CELLULAR_PORT_TEST_FUNCTION(void cellularExampleUsecuritySeal(),
                            "exampleUsecuritySeal",
                            "example")
{
    CellularPortQueueHandle_t uartQueueHandle;
    imei[CELLULAR_CTRL_IMEI_SIZE + 1]; // +1 to allow room to make this a string
    bool success = false;

    // Initialise the underlying cellular module drivers
    cellularPortInit();
    cellularPortUartInit(CELLULAR_CFG_PIN_TXD, CELLULAR_CFG_PIN_RXD,
                         CELLULAR_CFG_PIN_CTS, CELLULAR_CFG_PIN_RTS,
                         CELLULAR_CFG_BAUD_RATE,
                         CELLULAR_CFG_RTS_THRESHOLD,
                         CELLULAR_CFG_UART, &uartQueueHandle);
    cellularCtrlInit(CELLULAR_CFG_PIN_ENABLE_POWER,
                     CELLULAR_CFG_PIN_PWR_ON, CELLULAR_CFG_PIN_VINT,
                     false, CELLULAR_CFG_UART, uartQueueHandle);

    cellularPortLog("EXAMPLE_USECURITY: powering on cellular module...\n");
    if (cellularCtrlPowerOn(NULL) == 0) {

        // Continue only if we don't already have a security seal
        cellularPortLog("EXAMPLE_USECURITY: checking security seal of "
                        " cellular module...\n");
        if (cellularCtrlGetSecuritySeal() < 0) {

            // Connect to the network
            cellularPortLog("EXAMPLE_USECURITY: connecting to the cellular network...\n");
            if (connect() == 0) {
#ifdef MY_SECURITY_DEVICE_INFO
                cellularPortLog("EXAMPLE_USECURITY: MY_SECURITY_DEVICE_INFO is "
                                "defined, attempting security seal...\n");
                // Get the device IMEI to use during sealing
                if (cellularCtrlGetImei(imei) == 0) {
                    imei[sizeof(imei) - 1] = '\0';
                    cellularPortLog("EXAMPLE_USECURITY: using IMEI %s as the device serial number...",
                                    imei);
                    gStopTimeMs = cellularPortGetTickTimeMs() + (180 * 1000));
                    if (cellularCtrlSetSecuritySeal(MY_SECURITY_DEVICE_INFORMATION,
                                                    imei, keepGoingCallback) == 0) {
                        cellularPortLog("EXAMPLE_USECURITY: module is security sealed.\n");
                        success = true;
                    } else {
                        cellularPortLog("EXAMPLE_USECURITY ERROR: unable to complete security seal.\n");
                    }
                } else {
                    cellularPortLog("EXAMPLE_USECURITY ERROR: unable to obtain IMEI from module.\n");
                }
#else
                cellularPortLog("EXAMPLE_USECURITY: MY_SECURITY_DEVICE_INFO is NOT "
                                "defined, exiting...\n");
#endif
                // Disconnect from the cellular network
                cellularCtrlDisconnect();
            } else {
                cellularPortLog("EXAMPLE_USECURITY ERROR: unable to connect to the cellular network.\n");
            }
        } else {
            cellularPortLog("EXAMPLE_USECURITY: unable to run this example of end to"
                            " end security as this cellular module is already"
                            " security sealed.\n");
            success = true;
        }
        cellularCtrlPowerOff(NULL);
    } else {
        cellularPortLog("EXAMPLE_USECURITY ERROR: unable to power-up the cellular module.\n");
    }

    cellularPortLog("EXAMPLE_USECURITY: finished.\n");

    cellularCtrlDeinit();
    cellularPortUartDeinit(CELLULAR_CFG_UART);
    cellularPortDeinit();

    // For u-blox internal testing only
    EXAMPLE_FINAL_STATE(success);
}

#endif // CELLULAR_CTRL_SECURITY_ROOT_OF_TRUST

// End of file
