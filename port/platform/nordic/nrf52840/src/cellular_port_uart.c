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

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "nrf.h"
#include "nrf_uarte.h"
#include "nrf_gpio.h"

/* Note: in order to implement the API we require, where receipt
 * of data is signalled by an event queue and other things can 
 * send to that same event queue, this code is implemented on top of
 * the nrf_uarte.h HAL and replaces the nrfx_uarte.h default driver
 * from Nordic.  It steals from the code in nrfx_uarte.c, Nordic's
 * implementation.
 *
 * So that users can continue to use the Nordic UARTE driver this
 * code uses only the UART port that the Nordic UARTE driver is NOT
 * using: for instance, to use UARTE1 in this driver then
 * NRFX_UARTE1_ENABLED should be set to 0 in sdk_config to free it
 * up.
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// The maximum number of UARTs supported, which is the range of the
// "uart" parameter on this platform
#ifndef NRFX_UARTE_ENABLED
// NRFX_UARTE is not enabled, we can have both
# define CELLULAR_PORT_UART_MAX_NUM 2
#else 
# if !NRFX_UARTE0_ENABLED && !NRFX_UARTE1_ENABLED
// NRFX_UARTE is enabled but neither UARTEx is enabled so we can
// have both
#  define CELLULAR_PORT_UART_MAX_NUM 2
# else
#  if !NRFX_UARTE0_ENABLED || !NRFX_UARTE1_ENABLED
#    define CELLULAR_PORT_UART_MAX_NUM 1
#  else
#    error No UARTs available, both are being used by the Nordic NRFX_UARTE driver; to use this code at least one of NRFX_UARTE0_ENABLED or NRFX_UARTE1_ENABLED must be set to 0.
#  endif
# endif
#endif

// The maximum length that can be EasyDMAed, dictated
// by the NRF52840 HW
#if CELLULAR_PORT_UART_RX_BUFFER_SIZE < 256
# define DMA_MAX_LEN CELLULAR_PORT_UART_RX_BUFFER_SIZE
#else 
# define DMA_MAX_LEN 256 // Best if CELLULAR_PORT_UART_RX_BUFFER_SIZE is a multiple of this
#endif

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/** Structure to hold a UART event.
 */
typedef struct {
    int32_t type;
    size_t size;
} CellularPortUartEventData_t;

/** UART buffer structure.
 */
typedef struct {
    char *pStart;
    char *pRead;
    volatile char *pWrite;
    volatile size_t toRead;
} CellularPortUartBuffer_t;

/** Structure of the things we need to keep track of per UART.
 */
typedef struct {
    NRF_UARTE_Type *pReg;
    CellularPortMutexHandle_t mutex;
    CellularPortQueueHandle_t queue;
    CellularPortUartBuffer_t rxBuffer;
} CellularPortUartData_t;

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// UART data and pointer to Rx buffer (the latter needed by the
// interrupt handler)
#if !NRFX_UARTE0_ENABLED && !NRFX_UARTE1_ENABLED
static CellularPortUartData_t gUartData[] = {{NRF_UARTE0, NULL, NULL, {NULL, 0}},
                                             {NRF_UARTE1, NULL, NULL, {NULL, 0}}};
# else
#  if !NRFX_UARTE0_ENABLED 
static CellularPortUartData_t gUartData[] = {NRF_UARTE0, NULL, NULL, {NULL, 0}};
#  else
static CellularPortUartData_t gUartData[] = {NRF_UARTE1, NULL, NULL, {NULL, 0}};
#  endif
#endif

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

// The interrupt handler: only handles Rx events as Tx is blocking.
static void rxIrqHandler(CellularPortUartData_t *pUartData)
{
    NRF_UARTE_Type *pReg = pUartData->pReg;
    BaseType_t higherPriorityTaskWoken = 0;
    CellularPortUartBuffer_t *pRxBuffer = &(pUartData->rxBuffer);

    if (nrf_uarte_event_check(pReg, NRF_UARTE_EVENT_ERROR)) {
        // Clear any errors: no need to do anything, they
        // have no effect upon reception
        nrf_uarte_event_clear(pReg, NRF_UARTE_EVENT_ERROR);
        nrf_uarte_errorsrc_get_and_clear(pReg);
    } else if (nrf_uarte_event_check(pReg, NRF_UARTE_EVENT_RXDRDY)) {
        nrf_uarte_event_clear(pReg, NRF_UARTE_EVENT_RXDRDY);
        // Note: no need to check for overrun here, the
        // initialisation of the pointer and the DMA length
        // should ensure that it will never overrun
        (pRxBuffer->toRead)++;
        if (pRxBuffer->toRead == 1) {
            // Got a new byte into a buffer that the user had
            // previously fully read: let the user know
            // that there's more to read now
            CellularPortUartEventData_t uartEvent;
            // Inform the application of the receive event,
            // which also gives time for the data to get into RAM
            uartEvent.type = 0;
            uartEvent.size = pRxBuffer->toRead;
            xQueueSendFromISR((QueueHandle_t *) (pUartData->queue),
                              &uartEvent, &higherPriorityTaskWoken);
        }
    } else if (nrf_uarte_event_check(pReg, NRF_UARTE_EVENT_ENDRX)) {
        // Clear the event and move the buffer on
        nrf_uarte_event_clear(pReg, NRF_UARTE_EVENT_ENDRX);
        pRxBuffer->pWrite += DMA_MAX_LEN;
        // Start the next receive
        if (pRxBuffer->pWrite + DMA_MAX_LEN > pRxBuffer->pStart +
                                              CELLULAR_PORT_UART_RX_BUFFER_SIZE) {
            pRxBuffer->pWrite = pRxBuffer->pStart;
        }
        nrf_uarte_rx_buffer_set(pReg, (uint8_t *) (pRxBuffer->pWrite), DMA_MAX_LEN);
        nrf_uarte_task_trigger(pReg, NRF_UARTE_TASK_STARTRX);
    } else if (nrf_uarte_event_check(pReg, NRF_UARTE_EVENT_RXTO)) {
        // We've been stopped, so just clear the event
        nrf_uarte_event_clear(pReg, NRF_UARTE_EVENT_RXTO);
    }

    // Required for FreeRTOS task scheduling to work
    if (higherPriorityTaskWoken) {
        taskYIELD();
    }
}

// Convert a baud rate into an NRF52840 baud rate.
static int32_t baudRateToNrfBaudRate(int32_t baudRate)
{
    int32_t baudRateNrf = -1;

    switch (baudRate) {
        case 1200:
            baudRateNrf = UARTE_BAUDRATE_BAUDRATE_Baud1200;
        break;
        case 2400:
            baudRateNrf = UARTE_BAUDRATE_BAUDRATE_Baud2400;
        break;
        case 9600:
            baudRateNrf = UARTE_BAUDRATE_BAUDRATE_Baud9600;
        break;
        case 14400:
            baudRateNrf = UARTE_BAUDRATE_BAUDRATE_Baud14400;
        break;
        case 19200:
            baudRateNrf = UARTE_BAUDRATE_BAUDRATE_Baud19200;
        break;
        case 28800:
            baudRateNrf = UARTE_BAUDRATE_BAUDRATE_Baud28800;
        break;
        case 31250:
            baudRateNrf = UARTE_BAUDRATE_BAUDRATE_Baud31250;
        break;
        case 38400:
            baudRateNrf = UARTE_BAUDRATE_BAUDRATE_Baud38400;
        break;
        case 56000:
            baudRateNrf = UARTE_BAUDRATE_BAUDRATE_Baud56000;
        break;
        case 57600:
            baudRateNrf = UARTE_BAUDRATE_BAUDRATE_Baud57600;
        break;
        case 76800:
            baudRateNrf = UARTE_BAUDRATE_BAUDRATE_Baud76800;
        break;
        case 115200:
            baudRateNrf = UARTE_BAUDRATE_BAUDRATE_Baud115200;
        break;
        case 230400:
            baudRateNrf = UARTE_BAUDRATE_BAUDRATE_Baud230400;
        break;
        case 250000:
            baudRateNrf = UARTE_BAUDRATE_BAUDRATE_Baud250000;
        break;
        case 460800:
            baudRateNrf = UARTE_BAUDRATE_BAUDRATE_Baud460800;
        break;
        case 921600:
            baudRateNrf = UARTE_BAUDRATE_BAUDRATE_Baud921600;
        break;
        case 1000000:
            baudRateNrf = UARTE_BAUDRATE_BAUDRATE_Baud1M;
        break;
        default:
        break;
    }

    return baudRateNrf;
}

// Derived from the NRFX functions nrfx_is_in_ram() and
// nrfx_is_word_aligned(), check if a buffer pointer is good
// for DMA
__STATIC_INLINE bool isGoodForDma(void const *pBuffer)
{
    return (((((uint32_t) pBuffer) & 0x3u) == 0u) && 
            ((((uint32_t) pBuffer) & 0xE0000000u) == 0x20000000u));
}

// Derived from the NRFX function nrfx_get_irq_number()
__STATIC_INLINE IRQn_Type getIrqNumber(void const *pReg)
{
    return (IRQn_Type) (uint8_t)((uint32_t)(pReg) >> 12);
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: INTERRUPT HANDLERS
 * -------------------------------------------------------------- */

#if !NRFX_UARTE0_ENABLED
void nrfx_uarte_0_irq_handler(void)
{
    rxIrqHandler(&(gUartData[0]));
}
#endif

#if !NRFX_UARTE1_ENABLED
void nrfx_uarte_1_irq_handler(void)
{
    rxIrqHandler(&(gUartData[1]));
}
#endif

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
    CellularPortErrorCode_t errorCode = CELLULAR_PORT_INVALID_PARAMETER;
    int32_t baudRateNrf = baudRateToNrfBaudRate(baudRate);
    uint32_t pinCtsNrf = NRF_UARTE_PSEL_DISCONNECTED;
    uint32_t pinRtsNrf = NRF_UARTE_PSEL_DISCONNECTED;
    nrf_uarte_hwfc_t hwfc = NRF_UARTE_HWFC_DISABLED;
    NRF_UARTE_Type *pReg;

    // TODO: use this
    (void) rtsThreshold;

    if ((pUartQueue != NULL) && (pinRx >= 0) && (pinTx >= 0) &&
        (uart < sizeof(gUartData) / sizeof(gUartData[0])) && 
        (baudRateNrf >= 0)) {
        errorCode = CELLULAR_PORT_SUCCESS;
        if (gUartData[uart].mutex == NULL) {
            pReg = gUartData[uart].pReg;

#if NRFX_PRS_ENABLED
            static nrfx_irq_handler_t const irq_handlers[NRFX_UARTE_ENABLED_COUNT] = {
# if !NRFX_UARTE0_ENABLED
                nrfx_uarte_0_irq_handler,
# endif
# if !NRFX_UARTE1_ENABLED
                nrfx_uarte_1_irq_handler,
# endif
            };
            if (nrfx_prs_acquire(pReg, irq_handlers[pReg) != NRFX_SUCCESS) {
                errorCode = CELLULAR_PORT_PLATFORM_ERROR;
            }
#endif
            if (errorCode == 0) {
                // Create the mutex
                errorCode = cellularPortMutexCreate(&(gUartData[uart].mutex));
                if (errorCode == 0) {

                    CELLULAR_PORT_MUTEX_LOCK(gUartData[uart].mutex);

                    errorCode = CELLULAR_PORT_OUT_OF_MEMORY;
                    // Malloc memory for the read buffer
                    gUartData[uart].rxBuffer.pStart = pCellularPort_malloc(CELLULAR_PORT_UART_RX_BUFFER_SIZE);
                    if (gUartData[uart].rxBuffer.pStart != NULL) {
                        gUartData[uart].rxBuffer.pWrite = gUartData[uart].rxBuffer.pStart;
                        gUartData[uart].rxBuffer.pRead = gUartData[uart].rxBuffer.pStart;
                        gUartData[uart].rxBuffer.toRead = 0;
                        // Create the queue
                        errorCode = cellularPortQueueCreate(CELLULAR_PORT_UART_EVENT_QUEUE_SIZE,
                                                            sizeof(CellularPortUartEventData_t),
                                                            pUartQueue);
                        if (errorCode == 0) {
                            gUartData[uart].queue = *pUartQueue;

                            // Set baud rate
                            nrf_uarte_baudrate_set(pReg, baudRateNrf);

                            // Set Tx/Rx pins
                            nrf_gpio_pin_set(pinTx);
                            nrf_gpio_cfg_output(pinTx);
                            nrf_gpio_cfg_input(pinRx, NRF_GPIO_PIN_NOPULL);
                            nrf_uarte_txrx_pins_set(pReg, pinTx, pinRx);

                            // Set flow control
                            if (pinCts >= 0) {
                                pinCtsNrf = pinCts;
                                nrf_gpio_cfg_input(pinCtsNrf,
                                                   NRF_GPIO_PIN_NOPULL);
                                hwfc = NRF_UARTE_HWFC_ENABLED;
                            }
                            if (pinRts >= 0) {
                                pinRtsNrf = pinRts;
                                nrf_gpio_pin_set(pinRtsNrf);
                                nrf_gpio_cfg_output(pinRtsNrf);
                                hwfc = NRF_UARTE_HWFC_ENABLED;
                            }

                            if (hwfc == NRF_UARTE_HWFC_ENABLED) {
                                nrf_uarte_hwfc_pins_set(pReg, pinRtsNrf, pinCtsNrf);
                            }

                            // Configure the UART
                            nrf_uarte_configure(pReg, NRF_UARTE_PARITY_EXCLUDED, hwfc);

                            // Enable the UART
                            nrf_uarte_enable(pReg);

                            // Clear flags, set Rx interrupt and buffer and let it go
                            nrf_uarte_event_clear(pReg, NRF_UARTE_EVENT_ENDRX);
                            nrf_uarte_event_clear(pReg, NRF_UARTE_EVENT_ENDTX);
                            nrf_uarte_event_clear(pReg, NRF_UARTE_EVENT_ERROR);
                            nrf_uarte_event_clear(pReg, NRF_UARTE_EVENT_RXTO);
                            nrf_uarte_event_clear(pReg, NRF_UARTE_EVENT_RXDRDY);
                            nrf_uarte_event_clear(pReg, NRF_UARTE_EVENT_TXSTOPPED);

                            nrf_uarte_rx_buffer_set(pReg,
                                                    (uint8_t *) (gUartData[uart].rxBuffer.pWrite),
                                                    DMA_MAX_LEN);
                            nrf_uarte_task_trigger(pReg, NRF_UARTE_TASK_STARTRX);
                            nrf_uarte_int_enable(pReg, NRF_UARTE_INT_ENDRX_MASK |
                                                       NRF_UARTE_INT_ERROR_MASK |
                                                       NRF_UARTE_INT_RXTO_MASK  |
                                                       NRF_UARTE_INT_RXDRDY_MASK);
                            NRFX_IRQ_PRIORITY_SET(getIrqNumber((void *) pReg),
                                                  NRFX_UARTE_DEFAULT_CONFIG_IRQ_PRIORITY);
                            NRFX_IRQ_ENABLE(getIrqNumber((void *) (pReg)));
                        }
                    }

                    CELLULAR_PORT_MUTEX_UNLOCK(gUartData[uart].mutex);

                    // If we failed to create the queue or get memory for the buffer,
                    // delete the mutex, free memory and put the uart's
                    // mutex back to NULL
                    if ((errorCode != 0) ||
                        (gUartData[uart].rxBuffer.pStart == NULL)) {
                        cellularPortMutexDelete(gUartData[uart].mutex);
                        gUartData[uart].mutex = NULL;
                        cellularPort_free(gUartData[uart].rxBuffer.pStart);
                        gUartData[uart].rxBuffer.pStart = NULL;
                    }
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
    uint32_t pinRtsNrf;
    uint32_t pinCtsNrf;
    NRF_UARTE_Type *pReg;

    if (uart < sizeof(gUartData) / sizeof(gUartData[0])) {
        errorCode = CELLULAR_PORT_SUCCESS;
        if (gUartData[uart].mutex != NULL) {
            errorCode = CELLULAR_PORT_PLATFORM_ERROR;
            pReg = gUartData[uart].pReg;

            // This function should not be called if another task
            // already has the mutex, do a quick check here
            cellularPort_assert(cellularPortMutexGetLocker(gUartData[uart].mutex) == NULL);

            // No need to lock the mutex, we need to delete it
            // and we're not allowed to delete a locked mutex.
            // The caller needs to make sure that no read/write
            // is in progress when this function is called.

            // Disable Rx interrupts
            nrf_uarte_int_disable(pReg, NRF_UARTE_INT_ENDRX_MASK |
                                        NRF_UARTE_INT_ERROR_MASK |
                                        NRF_UARTE_INT_RXTO_MASK  |
                                        NRF_UARTE_INT_RXDRDY_MASK);
            NRFX_IRQ_DISABLE(nrfx_get_irq_number((void *) (pReg)));

            // Make sure all transfers are finished before UARTE is
            // disabled to achieve the lowest power consumption
            nrf_uarte_shorts_disable(pReg, NRF_UARTE_SHORT_ENDRX_STARTRX);
            nrf_uarte_event_clear(pReg, NRF_UARTE_EVENT_RXTO);
            nrf_uarte_task_trigger(pReg, NRF_UARTE_TASK_STOPRX);
            nrf_uarte_event_clear(pReg, NRF_UARTE_EVENT_TXSTOPPED);
            nrf_uarte_task_trigger(pReg, NRF_UARTE_TASK_STOPTX);
            while (!nrf_uarte_event_check(pReg, NRF_UARTE_EVENT_TXSTOPPED) ||
                   !nrf_uarte_event_check(pReg, NRF_UARTE_EVENT_RXTO)) {}

            // Disable the UARTE
            nrf_uarte_disable(pReg);

            // Put the pins back
            nrf_gpio_cfg_default(nrf_uarte_tx_pin_get(pReg));
            nrf_gpio_cfg_default(nrf_uarte_rx_pin_get(pReg));
            nrf_uarte_txrx_pins_disconnect(pReg);
            pinRtsNrf = nrf_uarte_rts_pin_get(pReg);
            pinCtsNrf = nrf_uarte_cts_pin_get(pReg);
            nrf_uarte_hwfc_pins_disconnect(pReg);
            if (pinCtsNrf != NRF_UARTE_PSEL_DISCONNECTED) {
                nrf_gpio_cfg_default(pinCtsNrf);
            }
            if (pinRtsNrf != NRF_UARTE_PSEL_DISCONNECTED) {
                nrf_gpio_cfg_default(pinRtsNrf);
            }

            // Delete the queue
            cellularPortQueueDelete(gUartData[uart].queue);
            gUartData[uart].queue = NULL;
            // Free the buffer
            cellularPort_free(gUartData[uart].rxBuffer.pStart);
            gUartData[uart].rxBuffer.pStart = NULL;
            // Delete the mutex
            cellularPortMutexDelete(gUartData[uart].mutex);
            gUartData[uart].mutex = NULL;
            errorCode = CELLULAR_PORT_SUCCESS;
        }
    }

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
    CellularPortErrorCode_t sizeOrErrorCode = CELLULAR_PORT_INVALID_PARAMETER;

    if (uart < sizeof(gUartData) / sizeof(gUartData[0])) {
        sizeOrErrorCode = CELLULAR_PORT_NOT_INITIALISED;
        if (gUartData[uart].mutex != NULL) {
            // No need to lock the mutex, this is atomic
            sizeOrErrorCode = gUartData[uart].rxBuffer.toRead;
        }
    }

    return (int32_t) sizeOrErrorCode;
}

// Read from the given UART interface.
int32_t cellularPortUartRead(int32_t uart, char *pBuffer,
                             size_t sizeBytes)
{
    CellularPortErrorCode_t sizeOrErrorCode = CELLULAR_PORT_INVALID_PARAMETER;
    CellularPortUartBuffer_t *pRxBuffer;
    size_t leftOvers = 0;

    if ((pBuffer != NULL) && (uart < sizeof(gUartData) / sizeof(gUartData[0]))) {
        sizeOrErrorCode = CELLULAR_PORT_NOT_INITIALISED;
        if (gUartData[uart].mutex != NULL) {

            CELLULAR_PORT_MUTEX_LOCK(gUartData[uart].mutex);

            // Copy out the data
            pRxBuffer = &(gUartData[uart].rxBuffer);
            sizeOrErrorCode = pRxBuffer->toRead;
            if (sizeOrErrorCode > sizeBytes) {
                sizeOrErrorCode = sizeBytes;
            }
            // Deal with wrapping around the end of the buffer
            if (pRxBuffer->pRead + sizeOrErrorCode > pRxBuffer->pStart +
                                                     CELLULAR_PORT_UART_RX_BUFFER_SIZE) {
                leftOvers = pRxBuffer->pRead + sizeOrErrorCode - pRxBuffer->pStart -
                            CELLULAR_PORT_UART_RX_BUFFER_SIZE;
                sizeOrErrorCode -= leftOvers;
            }
            // Copy up to the end of the buffer
            pCellularPort_memcpy(pBuffer, pRxBuffer->pRead,
                                 sizeOrErrorCode);
            pRxBuffer->toRead -= sizeOrErrorCode; 
            pRxBuffer->pRead += sizeOrErrorCode;
            // Copy any leftovers
            if (leftOvers > 0) {
                pRxBuffer->pRead = pRxBuffer->pStart;
                pCellularPort_memcpy(pBuffer + sizeOrErrorCode,
                                     pRxBuffer->pRead, leftOvers);
                pRxBuffer->toRead -= leftOvers; 
                pRxBuffer->pRead += leftOvers;
                sizeOrErrorCode += leftOvers;
            }

            CELLULAR_PORT_MUTEX_UNLOCK(gUartData[uart].mutex);
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
    const char *pTxBuffer = NULL;
    char *pTmpBuffer = NULL;
    NRF_UARTE_Type *pReg;

    if ((pBuffer != NULL) &&
        (uart < sizeof(gUartData) / sizeof(gUartData[0]))) {
        sizeOrErrorCode = CELLULAR_PORT_NOT_INITIALISED;
        if (gUartData[uart].mutex != NULL) {

            CELLULAR_PORT_MUTEX_LOCK(gUartData[uart].mutex);

            sizeOrErrorCode = CELLULAR_PORT_OUT_OF_MEMORY;
            pReg = gUartData[uart].pReg;

            // If the provided buffer is not good for
            // DMA (e.g. if it's in flash) then copy
            // it to somewhere that is
            if (!isGoodForDma(pBuffer)) {
                pTmpBuffer = pCellularPort_malloc(sizeBytes);
                if (pTmpBuffer != NULL) {
                    pCellularPort_memcpy(pTmpBuffer, pBuffer, sizeBytes);
                    pTxBuffer = pTmpBuffer;
                }
            } else {
                pTxBuffer = pBuffer;
            }

            if (pTxBuffer != NULL) {
                // Set up the flags
                nrf_uarte_event_clear(pReg, NRF_UARTE_EVENT_ENDTX);
                nrf_uarte_event_clear(pReg, NRF_UARTE_EVENT_TXSTOPPED);
                nrf_uarte_tx_buffer_set(pReg, (uint8_t const *) pTxBuffer, sizeBytes);
                nrf_uarte_task_trigger(pReg, NRF_UARTE_TASK_STARTTX);

                // Wait for the transmission to complete
                while (!nrf_uarte_event_check(pReg, NRF_UARTE_EVENT_ENDTX)) {}

                // Put UARTE into lowest power state.
                nrf_uarte_task_trigger(pReg, NRF_UARTE_TASK_STOPTX);
                while (!nrf_uarte_event_check(pReg, NRF_UARTE_EVENT_TXSTOPPED)) {}

                sizeOrErrorCode = sizeBytes;
            }

            // Free memory (it is valid C to free a NULL buffer)
            cellularPort_free(pTmpBuffer);

            CELLULAR_PORT_MUTEX_UNLOCK(gUartData[uart].mutex);
        }
    }

    return (int32_t) sizeOrErrorCode;
}

// Determine if RTS flow control is enabled.
bool cellularPortIsRtsFlowControlEnabled(int32_t uart)
{
    bool rtsFlowControlIsEnabled = false;
    NRF_UARTE_Type *pReg;

    if ((uart < sizeof(gUartData) / sizeof(gUartData[0])) &&
        (gUartData[uart].mutex != NULL)) {

        CELLULAR_PORT_MUTEX_LOCK(gUartData[uart].mutex);

        pReg = gUartData[uart].pReg;

        if (nrf_uarte_rts_pin_get(pReg) != NRF_UARTE_PSEL_DISCONNECTED) {
            rtsFlowControlIsEnabled = true;
        }

        CELLULAR_PORT_MUTEX_UNLOCK(gUartData[uart].mutex);
    }

    return rtsFlowControlIsEnabled;
}

// Determine if CTS flow control is enabled.
bool cellularPortIsCtsFlowControlEnabled(int32_t uart)
{
    bool ctsFlowControlIsEnabled = false;
    NRF_UARTE_Type *pReg;

    if ((uart < sizeof(gUartData) / sizeof(gUartData[0])) &&
        (gUartData[uart].mutex != NULL)) {

        CELLULAR_PORT_MUTEX_LOCK(gUartData[uart].mutex);

        pReg = gUartData[uart].pReg;

        if (nrf_uarte_cts_pin_get(pReg) != NRF_UARTE_PSEL_DISCONNECTED) {
            ctsFlowControlIsEnabled = true;
        }

        CELLULAR_PORT_MUTEX_UNLOCK(gUartData[uart].mutex);
    }

    return ctsFlowControlIsEnabled;
}

// End of file
