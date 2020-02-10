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

#include "cellular_port.h"
#include "cellular_port_clib.h"
#include "cellular_port_debug.h"
#include "cellular_port_os.h"
#include "cellular_port_gpio.h"
#include "cellular_port_uart.h"
#include "cellular_ctrl_at.h"
#include "cellular_ctrl_apn_db.h"
#include "cellular_ctrl.h"

#include "unity.h"

static int64_t gStopTimeMS;

// Callback function for the cellular connect process
static bool keepGoingCallback()
{
    bool keepGoing = true;

    if (cellularPortTimeMs() > gStopTimeMS) {
        keepGoing = false;
    }

    return keepGoing;
}

TEST_CASE("initialisation", "[cellular]")
{
    CellularPortQueueHandle_t queueHandle;

    TEST_ASSERT(cellularPortInit() == 0);
    TEST_ASSERT(cellularPortUartInit(CONFIG_PIN_TXD,
                                     CONFIG_PIN_RXD,
                                     CONFIG_BAUD_RATE,
                                     CONFIG_PIN_CTS,
                                     CONFIG_PIN_RTS,
                                     CONFIG_RTS_THRESHOLD,
                                     CONFIG_UART,
                                     &queueHandle) == 0);
    TEST_ASSERT(cellularCtrlInit(CONFIG_PIN_CELLULAR_ENABLE_POWER,
                                 CONFIG_PIN_CELLULAR_CP_ON,
                                 CONFIG_PIN_CELLULAR_VINT,
                                 CONFIG_UART,
                                 queueHandle) == 0);
    cellularCtrlDeinit();
    TEST_ASSERT(cellularPortUartDeinit(CONFIG_UART) == 0);

    // Allow idle task to run so that any deleted
    // tasks are actually deleted, otherwise
    // FreeRTOS can throw exceptions
    cellularPortTaskBlock(100);
}
