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

/* This file demonstrates the use of u-blox end-to-end security.
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

// A message to protect.
#ifndef MY_MESSAGE
# define MY_MESSAGE "The quick brown fox jumps over the lazy dog"
#endif

// Only include this example if root of trust is
// supported by the cellular module.
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

// Buffer in which to store the message.
static char gMessage[sizeof(MY_MESSAGE) - 1 +
                     CELLULAR_CTRL_END_TO_END_ENCRYPT_HEADER_SIZE_BYTES];

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

// Print a buffer in hex
static void printHex(char *pBuffer, char bufferLength)
{
    for (size_t x = 0; x < bufferLength; x++) {
        cellularPortLog("%02x", *pBuffer);
        pBuffer++;
    }
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: THE EXAMPLE_USECURITY
 * -------------------------------------------------------------- */

// The entry point: before this is called the system clocks have
// been started and the RTOS is running; we are in task space.
CELLULAR_PORT_TEST_FUNCTION(void cellularExampleUsecurityEnd2EndDataProtection(),
                            "exampleUsecurityEnd2EndDataProtection",
                            "example")
{
    CellularPortQueueHandle_t uartQueueHandle;
    int32_t x;
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

        // Continue only if we have a security seal
        cellularPortLog("EXAMPLE_USECURITY: checking security seal of "
                        " cellular module...\n");
        if (cellularCtrlGetSecuritySeal() == 0) {

            // Have our message encrypted by the cellular module
            // and returned to us in gMessage.
            cellularPortLog("EXAMPLE_USECURITY: encrypting %d byte message \"%s\"...\n",
                           cellularPort_strlen(MY_MESSAGE), MY_MESSAGE);
            x = cellularSecurityEndToEndEncrypt(MY_MESSAGE, gMessage,
                                                cellularPort_strlen(MY_MESSAGE));
            cellularPortLog("EXAMPLE_USECURITY: got back %d byte(s) of encrypted"
                            " message: 0x", x);
            printHex(gMessage, x);
            cellularPortLog("\n");

            // TODO: fill the instructive text in
            cellularPortLog("EXAMPLE_USECURITY: [fill in here what to do with above].\n");

        } else {
            cellularPortLog("EXAMPLE_USECURITY: unable to run this example of end to"
                            " end security as this cellular module is not"
                            " security sealed, see exampleSecuritySeal for"
                            " that.\n");
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
