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

#include "cellular_port_clib.h"
#include "cellular_cfg_hw.h"
#include "cellular_cfg_sw.h"
#include "cellular_port.h"
#include "cellular_port_debug.h"
#include "cellular_port_os.h"
#include "cellular_port_uart.h"
#include "cellular_ctrl.h"

#include "unity.h"

static int64_t gStopTimeMS;

// Callback function for the cellular connect process
static bool keepGoingCallback()
{
    bool keepGoing = true;

    if (cellularPortGetTimeMs() > gStopTimeMS) {
        keepGoing = false;
    }

    return keepGoing;
}

TEST_CASE("initialisation", "[cellular]")
{
    CellularPortQueueHandle_t queueHandle;

    TEST_ASSERT(cellularPortInit() == 0);
    TEST_ASSERT(cellularPortUartInit(CELLULAR_CFG_WHRE_PIN_TXD,
                                     CELLULAR_CFG_WHRE_PIN_RXD,
                                     CELLULAR_CFG_WHRE_PIN_CTS,
                                     CELLULAR_CFG_WHRE_PIN_RTS,
                                     CELLULAR_CFG_BAUD_RATE,
                                     CELLULAR_CFG_RTS_THRESHOLD,
                                     CELLULAR_CFG_UART,
                                     &queueHandle) == 0);
    TEST_ASSERT(cellularCtrlInit(CELLULAR_CFG_WHRE_PIN_ENABLE_POWER,
                                 CELLULAR_CFG_WHRE_PIN_CP_ON,
                                 CELLULAR_CFG_WHRE_PIN_VINT,
                                 false,
                                 CELLULAR_CFG_UART,
                                 queueHandle) == 0);
    cellularCtrlDeinit();
    TEST_ASSERT(cellularPortUartDeinit(CELLULAR_CFG_UART) == 0);

    // Allow idle task to run so that any deleted
    // tasks are actually deleted, otherwise
    // FreeRTOS can throw exceptions
    cellularPortTaskBlock(100);
}
