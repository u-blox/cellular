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

#ifdef CELLULAR_CFG_OVERRIDE
# include "cellular_cfg_override.h" // For a customer's configuration override
#endif
#include "cellular_cfg_sw.h"
#include "cellular_port_debug.h"
#include "cellular_port_clib.h"
#include "cellular_port.h"
#include "cellular_port_os.h"
#include "cellular_port_uart.h"
#include "cellular_port_private.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_uart.h"

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/** Structure to hold a UART event.
 */
typedef struct {
    int32_t type;
    size_t size;
} CellularPortUartEventData_t;

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: INTERRUPT HANDLERS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS
 * -------------------------------------------------------------- */

// Initialise a UARTE.
int32_t cellularPortUartInit(int32_t pinTx, int32_t pinRx,
                             int32_t pinCts, int32_t pinRts,
                             int32_t baudRate,
                             size_t rtsThreshold,
                             int32_t uart,
                             CellularPortQueueHandle_t *pUartQueue)
{
    CellularPortErrorCode_t errorCode = CELLULAR_PORT_NOT_IMPLEMENTED;

    return (int32_t) errorCode;
}

// Shutdown a UART.
int32_t cellularPortUartDeinit(int32_t uart)
{
    CellularPortErrorCode_t errorCode = CELLULAR_PORT_NOT_IMPLEMENTED;

    return (int32_t) errorCode;
}

// Push a UART event onto the UART event queue.
int32_t cellularPortUartEventSend(const CellularPortQueueHandle_t queueHandle,
                                  int32_t sizeBytes)
{
    int32_t errorCode = CELLULAR_PORT_INVALID_PARAMETER;
    CellularPortUartEventData_t uartEvent;

    if (queueHandle != NULL) {
        uartEvent.type = -1;
        uartEvent.size = 0;
        if (sizeBytes >= 0) {
            uartEvent.type = 0;
            uartEvent.size = sizeBytes;
        }
        errorCode = cellularPortQueueSend(queueHandle, (void *) &uartEvent);
    }

    return errorCode;
}

// Receive a UART event, blocking until one turns up.
int32_t cellularPortUartEventReceive(const CellularPortQueueHandle_t queueHandle)
{
    int32_t sizeOrErrorCode = CELLULAR_PORT_INVALID_PARAMETER;
    CellularPortUartEventData_t uartEvent;

    if (queueHandle != NULL) {
        sizeOrErrorCode = CELLULAR_PORT_PLATFORM_ERROR;
        if (cellularPortQueueReceive(queueHandle, &uartEvent) == 0) {
            sizeOrErrorCode = CELLULAR_PORT_UNKNOWN_ERROR;
            if (uartEvent.type >= 0) {
                sizeOrErrorCode = uartEvent.size;
            }
        }
    }

    return sizeOrErrorCode;
}

// Get the number of bytes waiting in the receive buffer.
int32_t cellularPortUartGetReceiveSize(int32_t uart)
{
    CellularPortErrorCode_t sizeOrErrorCode = CELLULAR_PORT_NOT_IMPLEMENTED;

    return (int32_t) sizeOrErrorCode;
}

// Read from the given UART interface.
int32_t cellularPortUartRead(int32_t uart, char *pBuffer,
                             size_t sizeBytes)
{
    CellularPortErrorCode_t sizeOrErrorCode = CELLULAR_PORT_NOT_IMPLEMENTED;

    return (int32_t) sizeOrErrorCode;
}

// Write to the given UART interface.
int32_t cellularPortUartWrite(int32_t uart,
                              const char *pBuffer,
                              size_t sizeBytes)
{
    CellularPortErrorCode_t sizeOrErrorCode = CELLULAR_PORT_NOT_IMPLEMENTED;

    return (int32_t) sizeOrErrorCode;
}

// Determine if RTS flow control is enabled.
bool cellularPortIsRtsFlowControlEnabled(int32_t uart)
{
    bool rtsFlowControlIsEnabled = false;

    return rtsFlowControlIsEnabled;
}

// Determine if CTS flow control is enabled.
bool cellularPortIsCtsFlowControlEnabled(int32_t uart)
{
    bool ctsFlowControlIsEnabled = false;

    return ctsFlowControlIsEnabled;
}

// End of file
