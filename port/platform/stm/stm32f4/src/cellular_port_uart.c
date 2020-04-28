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
#include "cellular_cfg_hw_platform_specific.h"
#include "cellular_port_debug.h"
#include "cellular_port_clib.h"
#include "cellular_port.h"
#include "cellular_port_os.h"
#include "cellular_port_uart.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx_ll_usart.h"

#include "cellular_port_private.h"  // Down here 'cos it needs GPIO_TypeDef

/* The code here was written using the really useful information
 * here:
 * https://stm32f4-discovery.net/2017/07/stm32-tutorial-efficiently-receive-uart-data-using-dma/
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// The maximum number of UART HW blocks on an STM32F4.
#define CELLULAR_PORT_MAX_NUM_UARTS 8

// Two-part macro melder called by all those below.
#define CELLULAR_PORT_UART_MELD2(x, y) x ## y

// Five-part macro melder called by all those below.
#define CELLULAR_PORT_UART_MELD5(v, w, x, y, z) v ## w ## x ## y ## z

// Make DMAx
#define CELLULAR_PORT_DMA(x) CELLULAR_PORT_UART_MELD2(DMA, x)

// Make LL_DMA_STREAM_x
#define CELLULAR_PORT_DMA_STREAM(x) CELLULAR_PORT_UART_MELD2(LL_DMA_STREAM_, x)

// Make LL_DMA_CHANNEL_x
#define CELLULAR_PORT_DMA_CHANNEL(x) CELLULAR_PORT_UART_MELD2(LL_DMA_CHANNEL_, x)

// Make DMAx_Streamy_IRQn
#define CELLULAR_DMA_STREAM_IRQ_N(x, y) CELLULAR_PORT_UART_MELD5(DMA, x, _Stream, y, _IRQn)

// Make LL_DMA_ClearFlag_HTx
#define CELLULAR_DMA_CLEAR_FLAG_HT(x) CELLULAR_PORT_UART_MELD2(LL_DMA_ClearFlag_HT, x)

// Make LL_DMA_ClearFlag_TCx
#define CELLULAR_DMA_CLEAR_FLAG_TC(x) CELLULAR_PORT_UART_MELD2(LL_DMA_ClearFlag_TC, x)

// Make LL_DMA_ClearFlag_TEx
#define CELLULAR_DMA_CLEAR_FLAG_TE(x) CELLULAR_PORT_UART_MELD2(LL_DMA_ClearFlag_TE, x)

// Make LL_DMA_ClearFlag_DMEx
#define CELLULAR_DMA_CLEAR_FLAG_DME(x) CELLULAR_PORT_UART_MELD2(LL_DMA_ClearFlag_DME, x)

// Make LL_DMA_ClearFlag_FEx
#define CELLULAR_DMA_CLEAR_FLAG_FE(x) CELLULAR_PORT_UART_MELD2(LL_DMA_ClearFlag_FE, x)

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/** Structure to hold a UART event.
 */
typedef struct {
    int32_t type;
    size_t size;
} CellularPortUartEventData_t;

/** Structure of the things we need to keep track of per UART.
 */
typedef struct CellularPortUartData_t {
    int32_t number;
    CellularPortMutexHandle_t mutex;
    CellularPortQueueHandle_t queue;
    char *pRxBufferStart;
    char *pRxBufferRead;
    volatile char *pRxBufferWrite;
    volatile size_t toRead;
    bool userNeedsNotify; //!< set this if toRead has hit zero and
                          // hence the user would like a notification
                          // when new data arrives.
    struct CellularPortUartData_t * pNext;
} CellularPortUartData_t;

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// Root of the UART linked list.
static CellularPortUartData_t *gpUartDataHead = NULL;

// Get the bus enable function for the given UART/USART.
static const void (*gLlApbClkEnable[])(uint32_t) = {0, // This to avoid having to -1 all the time
                                                    LL_APB2_GRP1_EnableClock,
                                                    LL_APB1_GRP1_EnableClock,
                                                    LL_APB1_GRP1_EnableClock,
                                                    LL_APB1_GRP1_EnableClock,
                                                    LL_APB1_GRP1_EnableClock,
                                                    LL_APB2_GRP1_EnableClock,
                                                    LL_APB1_GRP1_EnableClock,
                                                    LL_APB1_GRP1_EnableClock};

// Get the LL driver peripheral number for a given UART/USART.
static const int32_t gLlApbGrpPeriphUsart[] = {0, // This to avoid having to -1 all the time
                                               LL_APB2_GRP1_PERIPH_USART1,
                                               LL_APB1_GRP1_PERIPH_USART2,
                                               LL_APB1_GRP1_PERIPH_USART3,
                                               LL_APB1_GRP1_PERIPH_UART4,
                                               LL_APB1_GRP1_PERIPH_UART5,
                                               LL_APB2_GRP1_PERIPH_USART6,
                                               LL_APB1_GRP1_PERIPH_UART7,
                                               LL_APB1_GRP1_PERIPH_UART8};

// Get the LL driver peripheral number for a given DMA engine.
static const int32_t gLlApbGrpPeriphDma[] = {0, // This to avoid having to -1 all the time
                                             LL_AHB1_GRP1_PERIPH_DMA1,
                                             LL_AHB1_GRP1_PERIPH_DMA2};

// Get the LL driver peripheral number for a given GPIO port.
static const int32_t gLlApbGrpPeriphGpioPort[] = {LL_AHB1_GRP1_PERIPH_GPIOA,
                                                  LL_AHB1_GRP1_PERIPH_GPIOB,
                                                  LL_AHB1_GRP1_PERIPH_GPIOC,
                                                  LL_AHB1_GRP1_PERIPH_GPIOD,
                                                  LL_AHB1_GRP1_PERIPH_GPIOE,
                                                  LL_AHB1_GRP1_PERIPH_GPIOF,
                                                  LL_AHB1_GRP1_PERIPH_GPIOG,
                                                  LL_AHB1_GRP1_PERIPH_GPIOH,
                                                  LL_AHB1_GRP1_PERIPH_GPIOI,
                                                  LL_AHB1_GRP1_PERIPH_GPIOJ,
                                                  LL_AHB1_GRP1_PERIPH_GPIOK};

// Get the alternate function required on a GPIO line for a given UART.
// Note: which function a GPIO line actually performs on that UART is
// hard coded in the chip; for instance see table 12 of the STM32F437 data sheet.
static const int32_t gGpioAf[] = {0, // This to avoid having to -1 all the time
                                  LL_GPIO_AF_7,  /* UART 1 */
                                  LL_GPIO_AF_7,  /* UART 2 */
                                  LL_GPIO_AF_7,  /* UART 3 */
                                  LL_GPIO_AF_8,  /* UART 4 */
                                  LL_GPIO_AF_8,  /* UART 5 */
                                  LL_GPIO_AF_8,  /* UART 6 */
                                  LL_GPIO_AF_8,  /* UART 7 */
                                  LL_GPIO_AF_8}; /* UART 8 */

// Get the base address for a given UART/USART.
static USART_TypeDef * const gpUsart[] = {NULL, // This to avoid having to -1 all the time
                                          USART1,
                                          USART2,
                                          USART3,
                                          UART4,
                                          UART5,
                                          USART6,
                                          UART7,
                                          UART8};

// Get the interrupt number, USARTx_IRQn, for a given UART/USART.
static const IRQn_Type gUsartIrqN[] = {0, // This to avoid having to -1 all the time
                                       USART1_IRQn,
                                       USART2_IRQn,
                                       USART3_IRQn,
                                       UART4_IRQn,
                                       UART5_IRQn,
                                       USART6_IRQn,
                                       UART7_IRQn,
                                       UART8_IRQn};

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

// Add a UART data structure to the list.
// The required memory is malloc()ed.
CellularPortUartData_t *pAddUart(int32_t uart,
                                 CellularPortUartData_t *pUartData)
{
    CellularPortUartData_t **ppUartData = &gpUartDataHead;

    // Go to the end of the list
    while (*ppUartData != NULL) {
        ppUartData = &((*ppUartData)->pNext);
    }

    // Malloc memory for the item
    *ppUartData = (CellularPortUartData_t *) pCellularPort_malloc(sizeof(CellularPortUartData_t));
    if (*ppUartData != NULL) {
        // Copy the data in
        pCellularPort_memcpy(*ppUartData, pUartData, sizeof(CellularPortUartData_t));
        (*ppUartData)->pNext = NULL;
    }

    return *ppUartData;
}

// Find the UART data structure for a given UART.
CellularPortUartData_t *pGetUart(int32_t uart)
{
    CellularPortUartData_t *pUartData = gpUartDataHead;
    bool found = false;

    while (!found && (pUartData != NULL)) {
        if (pUartData->number == uart) {
            found = true;
        } else {
            pUartData = pUartData->pNext;
        }
    }

    return pUartData;
}

// Remove a UART from the list.
// The memory occupied is free()ed.
bool removeUart(int32_t uart)
{
    CellularPortUartData_t **ppUartData = &gpUartDataHead;
    CellularPortUartData_t *pTmp = NULL;
    bool found = false;

    // Find it in the list
    while (!found && (*ppUartData != NULL)) {
        if ((*ppUartData)->number == uart) {
            found = true;
        } else {
            pTmp = *ppUartData;
            ppUartData = &((*ppUartData)->pNext);
        }
    }

    // Remove the item
    if (*ppUartData != NULL) {
        // Move the next pointer of the previous
        // entry on
        if (pTmp != NULL) {
            pTmp->pNext = (*ppUartData)->pNext;
        }
        // Free memory and NULL the pointer
        cellularPort_free(*ppUartData);
        *ppUartData = NULL;
    }

    return found;
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: INTERRUPT HANDLERS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS
 * -------------------------------------------------------------- */

// Initialise a UART.
int32_t cellularPortUartInit(int32_t pinTx, int32_t pinRx,
                             int32_t pinCts, int32_t pinRts,
                             int32_t baudRate,
                             size_t rtsThreshold,
                             int32_t uart,
                             CellularPortQueueHandle_t *pUartQueue)
{
    CellularPortErrorCode_t errorCode = CELLULAR_PORT_INVALID_PARAMETER;
    ErrorStatus platformError;
    CellularPortUartData_t uartData = {0};
    LL_USART_InitTypeDef usartInitStruct = {0};
    LL_GPIO_InitTypeDef gpioInitStruct = {0};

    // TODO: use this
    (void) rtsThreshold;

    if ((pUartQueue != NULL) && (pinRx >= 0) && (pinTx >= 0) &&
        (uart > 0) && (uart <= CELLULAR_PORT_MAX_NUM_UARTS) &&
        (baudRate >= 0)) {
        errorCode = CELLULAR_PORT_SUCCESS;
        if (pGetUart(uart) == NULL) {
            // Create the mutex
            errorCode = cellularPortMutexCreate(&(uartData.mutex));
            if (errorCode == 0) {

                CELLULAR_PORT_MUTEX_LOCK(uartData.mutex);

                errorCode = CELLULAR_PORT_OUT_OF_MEMORY;

                uartData.number = uart;
                // Malloc memory for the read buffer
                uartData.pRxBufferStart = (char *) pCellularPort_malloc(CELLULAR_PORT_UART_RX_BUFFER_SIZE);
                if (uartData.pRxBufferStart != NULL) {
                    uartData.pRxBufferRead = uartData.pRxBufferStart;
                    uartData.pRxBufferWrite = uartData.pRxBufferStart;
                    uartData.toRead = 0;
                    uartData.userNeedsNotify = true;

                    // Create the queue
                    errorCode = cellularPortQueueCreate(CELLULAR_PORT_UART_EVENT_QUEUE_SIZE,
                                                        sizeof(CellularPortUartEventData_t),
                                                        pUartQueue);
                    if (errorCode == 0) {
                        uartData.queue = *pUartQueue;

                        // Now do the platform stuff
                        errorCode = CELLULAR_PORT_PLATFORM_ERROR;

                        // Enable UART clock
                        gLlApbClkEnable[uart](gLlApbGrpPeriphUsart[uart]);

                        // Enable DMA clock (all DMAs are on bus 1)
                        LL_AHB1_GRP1_EnableClock(gLlApbGrpPeriphDma[CELLULAR_CFG_DMA]);

                        // Enable GPIO clocks (all on bus 1): note, using the LL driver rather
                        // than our driver or the HAL driver here partly because the
                        // example code does that and also because lower down
                        // we need to enable the UART alternate function for
                        // these pins.
                        LL_AHB1_GRP1_EnableClock(gLlApbGrpPeriphGpioPort[CELLULAR_PORT_STM32F4_GPIO_PORT(pinTx)]);
                        LL_AHB1_GRP1_EnableClock(gLlApbGrpPeriphGpioPort[CELLULAR_PORT_STM32F4_GPIO_PORT(pinRx)]);
                        if (pinCts >= 0) {
                            LL_AHB1_GRP1_EnableClock(gLlApbGrpPeriphGpioPort[CELLULAR_PORT_STM32F4_GPIO_PORT(pinCts)]);
                        }
                        if (pinRts >= 0) {
                            LL_AHB1_GRP1_EnableClock(gLlApbGrpPeriphGpioPort[CELLULAR_PORT_STM32F4_GPIO_PORT(pinRts)]);
                        }

                        //  Configure the GPIOs, start with Tx
                        gpioInitStruct.Pin = CELLULAR_PORT_STM32F4_GPIO_PIN(pinTx);
                        gpioInitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
                        gpioInitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
                        // Output type doesn't matter, it is overridden by
                        // the alternate function
                        gpioInitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
                        gpioInitStruct.Pull = LL_GPIO_PULL_UP;
                        gpioInitStruct.Alternate = gGpioAf[uart];
                        platformError = LL_GPIO_Init(pCellularPortPrivateGpioGetReg(pinTx),
                                                     &gpioInitStruct);

                        //  Configure Rx
                        if (platformError == SUCCESS) {
                            gpioInitStruct.Pin = CELLULAR_PORT_STM32F4_GPIO_PIN(pinRx);
                            platformError = LL_GPIO_Init(pCellularPortPrivateGpioGetReg(pinRx),
                                                         &gpioInitStruct);
                        }

                        //  Configure RTS if present
                        if ((pinRts >= 0) && (platformError == SUCCESS)) {
                            gpioInitStruct.Pin = CELLULAR_PORT_STM32F4_GPIO_PIN(pinRts);
                            platformError = LL_GPIO_Init(pCellularPortPrivateGpioGetReg(pinRts),
                                                         &gpioInitStruct);
                        }

                        //  Configure CTS if present
                        if ((pinCts >= 0) && (platformError == SUCCESS)) {
                            gpioInitStruct.Pin = CELLULAR_PORT_STM32F4_GPIO_PIN(pinCts);
                            platformError = LL_GPIO_Init(pCellularPortPrivateGpioGetReg(pinCts),
                                                         &gpioInitStruct);
                        }

                        // Configure DMA
                        if (platformError == SUCCESS) {
                            // Channel CELLULAR_CFG_DMA_CHANNEL on our DMA/Stream
                            LL_DMA_SetChannelSelection(CELLULAR_PORT_DMA(CELLULAR_CFG_DMA),
                                                       CELLULAR_PORT_DMA_STREAM(CELLULAR_CFG_DMA_STREAM),
                                                       CELLULAR_PORT_DMA_CHANNEL(CELLULAR_CFG_DMA_CHANNEL));
                            // Towards RAM
                            LL_DMA_SetDataTransferDirection(CELLULAR_PORT_DMA(CELLULAR_CFG_DMA),
                                                            CELLULAR_PORT_DMA_STREAM(CELLULAR_CFG_DMA_STREAM),
                                                            LL_DMA_DIRECTION_PERIPH_TO_MEMORY);
                            // Low priority
                            LL_DMA_SetStreamPriorityLevel(CELLULAR_PORT_DMA(CELLULAR_CFG_DMA),
                                                          CELLULAR_PORT_DMA_STREAM(CELLULAR_CFG_DMA_STREAM),
                                                          LL_DMA_PRIORITY_LOW);
                            // Circular
                            LL_DMA_SetMode(CELLULAR_PORT_DMA(CELLULAR_CFG_DMA),
                                           CELLULAR_PORT_DMA_STREAM(CELLULAR_CFG_DMA_STREAM),
                                           LL_DMA_MODE_CIRCULAR);
                            // Byte-wise transfers
                            LL_DMA_SetPeriphIncMode(CELLULAR_PORT_DMA(CELLULAR_CFG_DMA),
                                                    CELLULAR_PORT_DMA_STREAM(CELLULAR_CFG_DMA_STREAM),
                                                    LL_DMA_PERIPH_NOINCREMENT);
                            LL_DMA_SetMemoryIncMode(CELLULAR_PORT_DMA(CELLULAR_CFG_DMA),
                                                    CELLULAR_PORT_DMA_STREAM(CELLULAR_CFG_DMA_STREAM),
                                                    LL_DMA_MEMORY_INCREMENT);
                            LL_DMA_SetPeriphSize(CELLULAR_PORT_DMA(CELLULAR_CFG_DMA),
                                                 CELLULAR_PORT_DMA_STREAM(CELLULAR_CFG_DMA_STREAM),
                                                 LL_DMA_PDATAALIGN_BYTE);
                            LL_DMA_SetMemorySize(CELLULAR_PORT_DMA(CELLULAR_CFG_DMA),
                                                 CELLULAR_PORT_DMA_STREAM(CELLULAR_CFG_DMA_STREAM),
                                                 LL_DMA_MDATAALIGN_BYTE);
                            // Not FIFO mode, whatever that is
                            LL_DMA_DisableFifoMode(CELLULAR_PORT_DMA(CELLULAR_CFG_DMA),
                                                   CELLULAR_PORT_DMA_STREAM(CELLULAR_CFG_DMA_STREAM));

                            // Attach the DMA to the UART at one end
                            LL_DMA_SetPeriphAddress(CELLULAR_PORT_DMA(CELLULAR_CFG_DMA),
                                                    CELLULAR_PORT_DMA_STREAM(CELLULAR_CFG_DMA_STREAM),
                                                    (uint32_t) &((gpUsart[uart])->DR));

                            // ...and to the RAM buffer at the other end
                            LL_DMA_SetMemoryAddress(CELLULAR_PORT_DMA(CELLULAR_CFG_DMA),
                                                    CELLULAR_PORT_DMA_STREAM(CELLULAR_CFG_DMA_STREAM),
                                                    (uint32_t) (uartData.pRxBufferStart));
                            LL_DMA_SetDataLength(CELLULAR_PORT_DMA(CELLULAR_CFG_DMA),
                                                 CELLULAR_PORT_DMA_STREAM(CELLULAR_CFG_DMA_STREAM),
                                                 CELLULAR_PORT_UART_RX_BUFFER_SIZE);

                            // Set DMA priority
                            NVIC_SetPriority(CELLULAR_DMA_STREAM_IRQ_N(CELLULAR_CFG_DMA, CELLULAR_CFG_DMA_STREAM),
                                             NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 5, 0));
                            // Clear all the DMA flags and DMA pending IRQ first
                            CELLULAR_DMA_CLEAR_FLAG_HT(CELLULAR_CFG_DMA_STREAM)(CELLULAR_PORT_DMA(CELLULAR_CFG_DMA));
                            CELLULAR_DMA_CLEAR_FLAG_TC(CELLULAR_CFG_DMA_STREAM)(CELLULAR_PORT_DMA(CELLULAR_CFG_DMA));
                            CELLULAR_DMA_CLEAR_FLAG_TE(CELLULAR_CFG_DMA_STREAM)(CELLULAR_PORT_DMA(CELLULAR_CFG_DMA));
                            CELLULAR_DMA_CLEAR_FLAG_DME(CELLULAR_CFG_DMA_STREAM)(CELLULAR_PORT_DMA(CELLULAR_CFG_DMA));
                            CELLULAR_DMA_CLEAR_FLAG_FE(CELLULAR_CFG_DMA_STREAM)(CELLULAR_PORT_DMA(CELLULAR_CFG_DMA));
                            NVIC_ClearPendingIRQ(CELLULAR_DMA_STREAM_IRQ_N(CELLULAR_CFG_DMA,
                                                                           CELLULAR_CFG_DMA_STREAM));

                            // Enable half full and transmit complete interrupts
                            LL_DMA_EnableIT_HT(CELLULAR_PORT_DMA(CELLULAR_CFG_DMA),
                                               CELLULAR_PORT_DMA_STREAM(CELLULAR_CFG_DMA_STREAM));
                            LL_DMA_EnableIT_TC(CELLULAR_PORT_DMA(CELLULAR_CFG_DMA),
                                               CELLULAR_PORT_DMA_STREAM(CELLULAR_CFG_DMA_STREAM));

                            // Go!
                            NVIC_EnableIRQ(CELLULAR_DMA_STREAM_IRQ_N(CELLULAR_CFG_DMA, CELLULAR_CFG_DMA_STREAM));

                            // Initialise the USART
                            usartInitStruct.BaudRate = baudRate;
                            usartInitStruct.DataWidth = LL_USART_DATAWIDTH_8B;
                            usartInitStruct.StopBits = LL_USART_STOPBITS_1;
                            usartInitStruct.Parity = LL_USART_PARITY_NONE;
                            usartInitStruct.TransferDirection = LL_USART_DIRECTION_TX_RX;
                            // TODO: need to connect flow control to DMA?
                            usartInitStruct.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
                            if ((pinRts >= 0) && (pinCts >= 0)) {
                                usartInitStruct.HardwareFlowControl = LL_USART_HWCONTROL_RTS_CTS;
                            } else {
                                if (pinRts >= 0) {
                                    usartInitStruct.HardwareFlowControl = LL_USART_HWCONTROL_RTS;
                                } else {
                                    usartInitStruct.HardwareFlowControl = LL_USART_HWCONTROL_CTS;
                                }
                            }
                            usartInitStruct.OverSampling = LL_USART_OVERSAMPLING_16;
                            platformError = LL_USART_Init(gpUsart[uart], &usartInitStruct);
                        }

                        // STILL more stuff to configure...
                        if (platformError == SUCCESS) {
                            LL_USART_ConfigAsyncMode(gpUsart[uart]);
                            LL_USART_EnableDMAReq_RX(gpUsart[uart]);
                            LL_USART_EnableIT_IDLE(gpUsart[uart]);

                            // Enable USART interrupt
                            NVIC_SetPriority(gUsartIrqN[uart],
                                             NVIC_EncodePriority(NVIC_GetPriorityGrouping(),
                                                                 5, 1));
                            NVIC_ClearPendingIRQ(gUsartIrqN[uart]);
                            NVIC_EnableIRQ(gUsartIrqN[uart]);

                            // Enable USART and DMA
                            LL_DMA_EnableStream(CELLULAR_PORT_DMA(CELLULAR_CFG_DMA),
                                                CELLULAR_PORT_DMA_STREAM(CELLULAR_CFG_DMA_STREAM));
                            LL_USART_Enable(gpUsart[uart]);
                        }

                        // Finally, add the UART to the list
                        if (platformError == SUCCESS) {
                            errorCode = CELLULAR_PORT_OUT_OF_MEMORY;
                            if (pAddUart(uart, &uartData) != NULL) {
                                errorCode = CELLULAR_PORT_SUCCESS;
                            }
                        }
                    }
                }

                CELLULAR_PORT_MUTEX_UNLOCK(uartData.mutex);

                // If we failed, clean up
                if (errorCode != 0) {
                    cellularPortMutexDelete(uartData.mutex);
                    cellularPort_free(uartData.pRxBufferStart);
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
    CellularPortUartData_t *pUart;

    if ((uart > 0) && (uart <= CELLULAR_PORT_MAX_NUM_UARTS)) {
        pUart = pGetUart(uart);
        errorCode = CELLULAR_PORT_SUCCESS;
        if (pUart != NULL) {

            errorCode = CELLULAR_PORT_PLATFORM_ERROR;

            // This function should not be called if another task
            // already has the mutex, do a quick check here
            cellularPort_assert(cellularPortMutexGetLocker(pUart->mutex) == NULL);

            // No need to lock the mutex, we need to delete it
            // and we're not allowed to delete a locked mutex.
            // The caller needs to make sure that no read/write
            // is in progress when this function is called.

            // TODO check this

            // Disable DMA interrupt
            NVIC_DisableIRQ(CELLULAR_DMA_STREAM_IRQ_N(CELLULAR_CFG_DMA,
                                                      CELLULAR_CFG_DMA_STREAM));

            // Disable USART interrupt
            NVIC_DisableIRQ(gUsartIrqN[uart]);
            // Disable DMA and USART
            LL_DMA_DisableStream(CELLULAR_PORT_DMA(CELLULAR_CFG_DMA),
                                 CELLULAR_PORT_DMA_STREAM(CELLULAR_CFG_DMA_STREAM));
            LL_USART_Disable(gpUsart[uart]);
            LL_USART_DeInit(gpUsart[uart]);

            // Delete the queue
            cellularPortQueueDelete(pUart->queue);
            // Free the buffer
            cellularPort_free(pUart->pRxBufferStart);
            // Delete the mutex
            cellularPortMutexDelete(pUart->mutex);
            // And finally remove the UART from the list
            removeUart(uart);
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
    CellularPortUartData_t *pUartData = pGetUart(uart);

    if (pUartData != NULL) {

        CELLULAR_PORT_MUTEX_LOCK(pUartData->mutex);

        // TODO
        sizeOrErrorCode = CELLULAR_PORT_NOT_IMPLEMENTED;

        CELLULAR_PORT_MUTEX_UNLOCK(pUartData->mutex);

    }

    return (int32_t) sizeOrErrorCode;
}

// Read from the given UART interface.
int32_t cellularPortUartRead(int32_t uart, char *pBuffer,
                             size_t sizeBytes)
{
    CellularPortErrorCode_t sizeOrErrorCode = CELLULAR_PORT_INVALID_PARAMETER;
    CellularPortUartData_t *pUartData = pGetUart(uart);

    if (pUartData != NULL) {

        CELLULAR_PORT_MUTEX_LOCK(pUartData->mutex);

        // TODO
        sizeOrErrorCode = CELLULAR_PORT_NOT_IMPLEMENTED;

        CELLULAR_PORT_MUTEX_UNLOCK(pUartData->mutex);

    }

    return (int32_t) sizeOrErrorCode;
}

// Write to the given UART interface.
int32_t cellularPortUartWrite(int32_t uart,
                              const char *pBuffer,
                              size_t sizeBytes)
{
    CellularPortErrorCode_t sizeOrErrorCode = CELLULAR_PORT_INVALID_PARAMETER;
    CellularPortUartData_t *pUartData = pGetUart(uart);

    if (pUartData != NULL) {

        CELLULAR_PORT_MUTEX_LOCK(pUartData->mutex);

        // TODO

        CELLULAR_PORT_MUTEX_UNLOCK(pUartData->mutex);

    }

    return (int32_t) sizeOrErrorCode;
}

// Determine if RTS flow control is enabled.
bool cellularPortIsRtsFlowControlEnabled(int32_t uart)
{
    bool rtsFlowControlIsEnabled = false;

    CellularPortUartData_t *pUartData = pGetUart(uart);

    if (pUartData != NULL) {

        CELLULAR_PORT_MUTEX_LOCK(pUartData->mutex);

        // TODO

        CELLULAR_PORT_MUTEX_UNLOCK(pUartData->mutex);

    }

    return rtsFlowControlIsEnabled;
}

// Determine if CTS flow control is enabled.
bool cellularPortIsCtsFlowControlEnabled(int32_t uart)
{
    bool ctsFlowControlIsEnabled = false;

    CellularPortUartData_t *pUartData = pGetUart(uart);

    if (pUartData != NULL) {

        CELLULAR_PORT_MUTEX_LOCK(pUartData->mutex);

        // TODO

        CELLULAR_PORT_MUTEX_UNLOCK(pUartData->mutex);

    }

    return ctsFlowControlIsEnabled;
}

// End of file
