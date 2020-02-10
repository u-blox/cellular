/*
 * Copyright (C) u-blox Cambourne Ltd
 * u-blox Cambourne Ltd, Cambourne, UK
 *
 * All rights reserved.
 *
 * This source file is the sole property of u-blox Cambourne Ltd.
 * Reproduction or utilisation of this source in whole or part is
 * forbidden without the written consent of u-blox Cambourne Ltd.
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
