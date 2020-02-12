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
#include "cellular_port_os.h"
#include "cellular_port_uart.h"

#include "driver/uart.h"

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// The maximum number of UARTs supported, which is the range of the
// "uart" parameter on this platform
#define CELLULAR_PORT_UART_MAX_NUM 3

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// Mutexes to protect the UART hardware.
static CellularPortMutexHandle_t gMutex[CELLULAR_PORT_UART_MAX_NUM] = {NULL};

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS
 * -------------------------------------------------------------- */

//** Initialise a UART.
int32_t cellularPortUartInit(int32_t pinTx, int32_t pinRx,
                             int32_t pinCts, int32_t pinRts,
                             int32_t baudRate,
                             size_t rtsThreshold,
                             int32_t uart,
                             CellularPortQueueHandle_t *pUartQueue)
{
    CellularPortErrorCode_t errorCode = CELLULAR_PORT_INVALID_PARAMETER;
    uart_config_t config;
    esp_err_t espError;

    if ((pUartQueue != NULL) && (pinRx >= 0) && (pinTx >= 0) &&
        (uart < sizeof(gMutex) / sizeof(gMutex[0]))) {
        errorCode = CELLULAR_PORT_SUCCESS;
        if (gMutex[uart] == NULL) {
            errorCode = cellularPortMutexCreate(&(gMutex[uart]));
            if (errorCode == ESP_OK) {
                errorCode = CELLULAR_PORT_PLATFORM_ERROR;

                CELLULAR_PORT_MUTEX_LOCK(gMutex[uart]);

                // Set the things that won't change
                config.data_bits = UART_DATA_8_BITS,
                config.stop_bits = UART_STOP_BITS_1,
                config.parity    = UART_PARITY_DISABLE,
                config.use_ref_tick = false;

                // Set the baud rate
                config.baud_rate = baudRate;

                // Set flow control
                config.flow_ctrl = UART_HW_FLOWCTRL_DISABLE;
                config.rx_flow_ctrl_thresh = 0;
                if ((pinCts >= 0) && (pinRts >= 0)) {
                    config.flow_ctrl = UART_HW_FLOWCTRL_CTS_RTS;
                    config.rx_flow_ctrl_thresh = rtsThreshold;
                } else if (pinCts >= 0) {
                    config.flow_ctrl = UART_HW_FLOWCTRL_CTS;
                } else if (pinRts >= 0) {
                    config.flow_ctrl = UART_HW_FLOWCTRL_RTS;
                    config.rx_flow_ctrl_thresh = rtsThreshold;
                }

                // Do the UART configuration
                espError = uart_param_config(uart, &config);
                if (espError == ESP_OK) {
                    // Set up the UART pins
                    if (pinCts < 0) {
                        pinCts = UART_PIN_NO_CHANGE;
                    }
                    if (pinRts < 0) {
                        pinRts = UART_PIN_NO_CHANGE;
                    }
                    espError = uart_set_pin(uart, pinTx, pinRx,
                                            pinRts, pinCts);
                    if (espError == ESP_OK) {
                        // Switch off SW flow control
                        espError = uart_set_sw_flow_ctrl(uart, false, 0, 0);
                        if (espError == ESP_OK) {
                            // Install the driver
                            espError = uart_driver_install(uart,
                                                           CELLULAR_PORT_UART_RX_BUFFER_SIZE,
                                                           CELLULAR_PORT_UART_TX_BUFFER_SIZE,
                                                           CELLULAR_PORT_UART_EVENT_QUEUE_SIZE,
                                                           pUartQueue, 0);
                            if (espError == ESP_OK) {
                                errorCode = CELLULAR_PORT_SUCCESS;
                            }
                        }
                    }
                }

                CELLULAR_PORT_MUTEX_UNLOCK(gMutex[uart]);

                // If we failed to initialise the UART,
                // delete the mutex and put the uart's gMutex[]
                // state back to NULL
                if (errorCode != CELLULAR_PORT_SUCCESS) {
                    cellularPortMutexDelete(gMutex[uart]);
                    gMutex[uart] = NULL;
                }
            }
        }
    }

    return (int32_t) errorCode;
}

// Shutdown a UART.
int32_t cellularPortUartDeinit(int32_t uart)
{
    CellularPortErrorCode_t errorCode = CELLULAR_PORT_INVALID_PARAMETER;
    esp_err_t espError;

    if (uart < sizeof(gMutex) / sizeof(gMutex[0])) {
        errorCode = CELLULAR_PORT_SUCCESS;
        if (gMutex[uart] != NULL) {
            errorCode = CELLULAR_PORT_PLATFORM_ERROR;
            // This function should not be called if another task
            // already has the mutex, do a quick check here
            cellularPort_assert(cellularPortMutexGetLocker(gMutex[uart]) == NULL);

            // No need to lock the mutex, we need to delete it
            // and we're not allowed to delete a locked mutex.
            // The caller needs to make sure that no read/write
            // is in progress when this function is called.
            espError = uart_driver_delete(uart);
            if (espError == ESP_OK) {
                cellularPortMutexDelete(gMutex[uart]);
                gMutex[uart] = NULL;
                errorCode = CELLULAR_PORT_SUCCESS;
            }
        }
    }

    return (int32_t) errorCode;
}

// Push an invalid UART event onto the UART event queue.
int32_t cellularPortUartEventSend(const CellularPortQueueHandle_t queueHandle,
                                  int32_t sizeBytes)
{
    int32_t errorCode = CELLULAR_PORT_INVALID_PARAMETER;
    uart_event_t uartEvent;

    if (queueHandle != NULL) {
        uartEvent.type = UART_DATA;
        uartEvent.size = sizeBytes;
        errorCode = cellularPortQueueSend(queueHandle, (void *) &uartEvent);
    }

    return errorCode;
}

// Receive a UART event, blocking until one turns up.
int32_t cellularPortUartEventReceive(const CellularPortQueueHandle_t queueHandle)
{
    int32_t sizeOrErrorCode = CELLULAR_PORT_INVALID_PARAMETER;
    uart_event_t uart_event;

    if (queueHandle != NULL) {
        sizeOrErrorCode = CELLULAR_PORT_PLATFORM_ERROR;
        if (cellularPortQueueReceive(queueHandle, &uart_event) == 0) {
            sizeOrErrorCode = CELLULAR_PORT_UNKNOWN_ERROR;
            if (uart_event.type == UART_DATA) {
                sizeOrErrorCode = uart_event.size;
            }
        }
    }

    return sizeOrErrorCode;
}

// Get the number of bytes waiting in the receive buffer.
int32_t cellularPortUartGetReceiveSize(int32_t uart)
{
    CellularPortErrorCode_t sizeOrErrorCode = CELLULAR_PORT_INVALID_PARAMETER;
    size_t receiveSize;

    if (uart < sizeof(gMutex) / sizeof(gMutex[0])) {
        sizeOrErrorCode = CELLULAR_PORT_NOT_INITIALISED;
        if (gMutex[uart] != NULL) {
            sizeOrErrorCode = CELLULAR_PORT_PLATFORM_ERROR;

            CELLULAR_PORT_MUTEX_LOCK(gMutex[uart]);

            // Will get back either size or -1
            if (uart_get_buffered_data_len(uart, &(receiveSize)) == 0) {
                sizeOrErrorCode = receiveSize;
            }

            CELLULAR_PORT_MUTEX_UNLOCK(gMutex[uart]);
        }
    }

    return (int32_t) sizeOrErrorCode;
}

// Read from the given UART interface.
int32_t cellularPortUartRead(int32_t uart, char *pBuffer,
                             size_t sizeBytes)
{
    CellularPortErrorCode_t sizeOrErrorCode = CELLULAR_PORT_INVALID_PARAMETER;

    if ((pBuffer != NULL) &&
        (uart < sizeof(gMutex) / sizeof(gMutex[0]))) {
        sizeOrErrorCode = CELLULAR_PORT_NOT_INITIALISED;
        if (gMutex[uart] != NULL) {

            CELLULAR_PORT_MUTEX_LOCK(gMutex[uart]);

            // Will get back either size or -1
            sizeOrErrorCode = uart_read_bytes(uart, (uint8_t *) pBuffer, sizeBytes, 0);
            if (sizeOrErrorCode < 0) {
                sizeOrErrorCode = CELLULAR_PORT_PLATFORM_ERROR;
            }

            CELLULAR_PORT_MUTEX_UNLOCK(gMutex[uart]);
        }
    }

    return (int32_t) sizeOrErrorCode;
}

// Write to the given UART interface.
int32_t cellularPortUartWrite(int32_t uart,
                              const char *pBuffer,
                              size_t sizeBytes)
{
    CellularPortErrorCode_t sizeOrErrorCode = CELLULAR_PORT_INVALID_PARAMETER;

    if ((pBuffer != NULL) &&
        (uart < sizeof(gMutex) / sizeof(gMutex[0]))) {
        sizeOrErrorCode = CELLULAR_PORT_NOT_INITIALISED;
        if (gMutex[uart] != NULL) {

            CELLULAR_PORT_MUTEX_LOCK(gMutex[uart]);

            // Will get back either size or -1
            sizeOrErrorCode = uart_write_bytes(uart, (const char *) pBuffer, sizeBytes);
            if (sizeOrErrorCode < 0) {
                sizeOrErrorCode = CELLULAR_PORT_PLATFORM_ERROR;
            }

            CELLULAR_PORT_MUTEX_UNLOCK(gMutex[uart]);
        }
    }

    return (int32_t) sizeOrErrorCode;
}

// End of file
