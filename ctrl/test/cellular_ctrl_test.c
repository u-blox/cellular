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

/* Only #includes of cellular_* are allowed here, no C lib,
 * no platform stuff and no OS stuff.  Anything required from
 * the platform/C library/OS must be brought in through
 * cellular_port* to maintain portability.
 */

#include "cellular_port_clib.h"
#include "cellular_cfg_hw.h"
#include "cellular_cfg_sw.h"
#include "cellular_port.h"
#include "cellular_port_debug.h"
#include "cellular_port_os.h"
#include "cellular_port_uart.h"
#include "cellular_port_test.h"
#include "cellular_ctrl.h"

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// The amount of time to allow for cellular power off in milliseconds.
#define CELLULAR_CTRL_TEST_POWER_OFF_TIME_MS 10000

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// Used for keepGoingCallback() timeout.
static int64_t gStopTimeMS;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

// Callback function for the cellular connect process
static bool keepGoingCallback()
{
    bool keepGoing = true;

    if (cellularPortGetTimeMs() > gStopTimeMS) {
        keepGoing = false;
    }

    return keepGoing;
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: TESTS
 * -------------------------------------------------------------- */

/** Basic test: initialise and then deinitialise everything.
 */
CELLULAR_PORT_TEST_FUNCTION(void cellularCtrlTestInitialisation(),
                            "initialisation",
                            "ctrl")
{
    CellularPortQueueHandle_t queueHandle;

    CELLULAR_PORT_TEST_ASSERT(cellularPortInit() == 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPortUartInit(CELLULAR_CFG_WHRE_PIN_TXD,
                                                   CELLULAR_CFG_WHRE_PIN_RXD,
                                                   CELLULAR_CFG_WHRE_PIN_CTS,
                                                   CELLULAR_CFG_WHRE_PIN_RTS,
                                                   CELLULAR_CFG_BAUD_RATE,
                                                   CELLULAR_CFG_RTS_THRESHOLD,
                                                   CELLULAR_CFG_UART,
                                                   &queueHandle) == 0);
    CELLULAR_PORT_TEST_ASSERT(cellularCtrlInit(CELLULAR_CFG_WHRE_PIN_ENABLE_POWER,
                                               CELLULAR_CFG_WHRE_PIN_CP_ON,
                                               CELLULAR_CFG_WHRE_PIN_VINT,
                                               false,
                                               CELLULAR_CFG_UART,
                                               queueHandle) == 0);
    cellularCtrlDeinit();
    CELLULAR_PORT_TEST_ASSERT(cellularPortUartDeinit(CELLULAR_CFG_UART) == 0);
    cellularPortDeinit();

    // Allow idle task to run so that any deleted
    // tasks are actually deleted, required by some
    // operating systems (e.g. freeRTOS)
    cellularPortTaskBlock(100);
}

/** Test power on/off and aliveness, parameterised with the VInt pin.
 */
static void cellularCtrlTestPowerAliveVInt(int32_t pinVint)
{
    CellularPortQueueHandle_t queueHandle;
    bool (*pKeepGoingCallback) (void) = NULL;

    CELLULAR_PORT_TEST_ASSERT(cellularPortInit() == 0);

    if (pinVint >= 0) {
        cellularPortLog("CELLULAR_CTRL_TEST: running power-on and alive tests with VInt on pin %d.\n",
                        pinVint);
    } else {
        cellularPortLog("CELLULAR_CTRL_TEST: running power-on and alive tests without VInt.\n");
    }

    CELLULAR_PORT_TEST_ASSERT(cellularPortUartInit(CELLULAR_CFG_WHRE_PIN_TXD,
                                                   CELLULAR_CFG_WHRE_PIN_RXD,
                                                   CELLULAR_CFG_WHRE_PIN_CTS,
                                                   CELLULAR_CFG_WHRE_PIN_RTS,
                                                   CELLULAR_CFG_BAUD_RATE,
                                                   CELLULAR_CFG_RTS_THRESHOLD,
                                                   CELLULAR_CFG_UART,
                                                   &queueHandle) == 0);

    cellularPortLog("CELLULAR_CTRL_TEST: testing power-on and alive calls before initialisation...\n");
#if (CELLULAR_CFG_WHRE_PIN_ENABLE_POWER) == -1
    // Should always return true if there isn't a power enable pin
    CELLULAR_PORT_TEST_ASSERT(cellularCtrlIsPowered());
#endif
    // Should return false before initialisation
    CELLULAR_PORT_TEST_ASSERT(!cellularCtrlIsAlive());
    // Should fail before initialisation
    CELLULAR_PORT_TEST_ASSERT(cellularCtrlPowerOn(NULL) < 0);
    // Should still return false
    CELLULAR_PORT_TEST_ASSERT(!cellularCtrlIsAlive());

    CELLULAR_PORT_TEST_ASSERT(cellularCtrlInit(CELLULAR_CFG_WHRE_PIN_ENABLE_POWER,
                                               CELLULAR_CFG_WHRE_PIN_CP_ON,
                                               pinVint,
                                               false,
                                               CELLULAR_CFG_UART,
                                               queueHandle) == 0);

    // Do this twice so as to check transiting from
    // a call to cellularCtrlPowerOff() to a call to
    // cellularCtrlPowerOn().
    for (size_t x = 0; x < 2; x++) {
        cellularPortLog("CELLULAR_CTRL_TEST: testing power-on and alive calls");
        if (x > 0) {
           cellularPortLog(" with a callback passed to cellularCtrlPowerOff() and a %d second power-off timer.\n",
                           CELLULAR_CTRL_TEST_POWER_OFF_TIME_MS / 1000);
        } else {
           cellularPortLog(" with cellularCtrlPowerOff(NULL).\n");
        }
        CELLULAR_PORT_TEST_ASSERT(!cellularCtrlIsAlive());
#if (CELLULAR_CFG_WHRE_PIN_ENABLE_POWER) != -1
        CELLULAR_PORT_TEST_ASSERT(!cellularCtrlIsPowered());
#endif
        CELLULAR_PORT_TEST_ASSERT(cellularCtrlPowerOn(NULL) == 0);
        CELLULAR_PORT_TEST_ASSERT(cellularCtrlIsAlive());
        // Test with and without a keep going callback
        if (x > 0) {
            pKeepGoingCallback = keepGoingCallback;
            gStopTimeMS = cellularPortGetTimeMs() + CELLULAR_CTRL_TEST_POWER_OFF_TIME_MS;
        }
        cellularCtrlPowerOff(pKeepGoingCallback);
    }

    // Do this twice so as to check transiting from
    // a call to cellularCtrlHardOff() to a call to
    // cellularCtrlPowerOn().
    for (size_t x = 0; x < 2; x++) {
        cellularPortLog("CELLULAR_CTRL_TEST: testing power-on and alive calls with cellularCtrlHardPowerOff().\n");
        CELLULAR_PORT_TEST_ASSERT(!cellularCtrlIsAlive());
#if (CELLULAR_CFG_WHRE_PIN_ENABLE_POWER) != -1
        CELLULAR_PORT_TEST_ASSERT(!cellularCtrlIsPowered());
#endif
        CELLULAR_PORT_TEST_ASSERT(cellularCtrlPowerOn(NULL) == 0);
        CELLULAR_PORT_TEST_ASSERT(cellularCtrlIsAlive());
        cellularCtrlHardPowerOff();
    }

    cellularPortLog("CELLULAR_CTRL_TEST: testing power-on and alive calls after hard power off.\n");
    CELLULAR_PORT_TEST_ASSERT(!cellularCtrlIsAlive());
#if (CELLULAR_CFG_WHRE_PIN_ENABLE_POWER) != -1
    CELLULAR_PORT_TEST_ASSERT(!cellularCtrlIsPowered());
#endif

    cellularCtrlDeinit();

    cellularPortLog("CELLULAR_CTRL_TEST: testing power-on and alive calls after deinitialisation.\n");
#if (CELLULAR_CFG_WHRE_PIN_ENABLE_POWER) == -1
    // Should always return true if there isn't a power enable pin
    CELLULAR_PORT_TEST_ASSERT(cellularCtrlIsPowered());
#endif
    // Should fail after deinitialisation
    CELLULAR_PORT_TEST_ASSERT(cellularCtrlPowerOn(NULL) < 0);
    // Should return false after deinitialisation
    CELLULAR_PORT_TEST_ASSERT(!cellularCtrlIsAlive());

    CELLULAR_PORT_TEST_ASSERT(cellularPortUartDeinit(CELLULAR_CFG_UART) == 0);

    cellularPortDeinit();

    // Allow idle task to run so that any deleted
    // tasks are actually deleted, required by some
    // operating systems (e.g. freeRTOS)
    cellularPortTaskBlock(100);
}

/** Test power on/off and aliveness.
 */
CELLULAR_PORT_TEST_FUNCTION(void cellularCtrlTestPowerAlive(),
                            "powerAndAliveness",
                            "ctrl")
{

    // Should work with and without a VInt pin connected
    cellularCtrlTestPowerAliveVInt(-1);
#if (CELLULAR_CFG_WHRE_PIN_VINT) != -1
    cellularCtrlTestPowerAliveVInt(CELLULAR_CFG_WHRE_PIN_VINT);
#endif
}

// End of file
