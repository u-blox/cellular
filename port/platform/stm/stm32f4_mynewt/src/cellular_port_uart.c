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

#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_dma.h"
#include "stm32f4xx_ll_usart.h"

#include "cellular_port_private.h"  // Down here 'cos it needs GPIO_TypeDef

/* WWW */
//#include <string.h>
/* WWW */

/* The code here was written using the really useful information
 * here:
 * *
 * https://stm32f4-discovery.net/2017/07/stm32-tutorial-efficiently-receive-uart-data-using-dma/
 * *
 * This code uses the LL API, as that tutorial does, and sticks
 * to it exactly, hence where the LL API has a series of
 * named functions rather than taking a parameter (e.g.
 * LL_DMA_ClearFlag_HT0(), LL_DMA_ClearFlag_HT1(), etc.)
 * the correct function is accessed through a jump table,
 * making it possible to use it in a parameterised manner
 * again.
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// The maximum number of UART HW blocks on an STM32F4.
#define CELLULAR_PORT_MAX_NUM_UARTS 8

// The maximum number of DMA engines on an STM32F4.
#define CELLULAR_PORT_MAX_NUM_DMA_ENGINES 2

// The maximum number of DMA streams on an STM32F4.
#define CELLULAR_PORT_MAX_NUM_DMA_STREAMS 8

// Determine if the given DMA engine/stream interrupt is in use
#define CELLULAR_PORT_DMA_INTERRUPT_IN_USE(x, y) (((CELLULAR_CFG_UART1_AVAILABLE != 0) && (CELLULAR_CFG_UART1_DMA_ENGINE == x) && (CELLULAR_CFG_UART1_DMA_STREAM == y)) || \
                                                  ((CELLULAR_CFG_UART2_AVAILABLE != 0) && (CELLULAR_CFG_UART2_DMA_ENGINE == x) && (CELLULAR_CFG_UART2_DMA_STREAM == y)) || \
                                                  ((CELLULAR_CFG_UART3_AVAILABLE != 0) && (CELLULAR_CFG_UART3_DMA_ENGINE == x) && (CELLULAR_CFG_UART3_DMA_STREAM == y)) || \
                                                  ((CELLULAR_CFG_UART4_AVAILABLE != 0) && (CELLULAR_CFG_UART4_DMA_ENGINE == x) && (CELLULAR_CFG_UART4_DMA_STREAM == y)) || \
                                                  ((CELLULAR_CFG_UART5_AVAILABLE != 0) && (CELLULAR_CFG_UART5_DMA_ENGINE == x) && (CELLULAR_CFG_UART5_DMA_STREAM == y)) || \
                                                  ((CELLULAR_CFG_UART6_AVAILABLE != 0) && (CELLULAR_CFG_UART6_DMA_ENGINE == x) && (CELLULAR_CFG_UART6_DMA_STREAM == y)) || \
                                                  ((CELLULAR_CFG_UART7_AVAILABLE != 0) && (CELLULAR_CFG_UART7_DMA_ENGINE == x) && (CELLULAR_CFG_UART7_DMA_STREAM == y)) || \
                                                  ((CELLULAR_CFG_UART8_AVAILABLE != 0) && (CELLULAR_CFG_UART8_DMA_ENGINE == x) && (CELLULAR_CFG_UART8_DMA_STREAM == y)))

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/** A UART event.  Since we only ever need to signal
 * size or error then on this platform the
 * CellularPortUartEventData_t can simply be an int32_t.
 */
typedef int32_t CellularPortUartEventData_t;

/** Structure of the constant data per UART.
 */
typedef struct CellularPortUartConstData_t {
    USART_TypeDef *pReg;
    uint32_t dmaEngine;
    uint32_t dmaStream;
    uint32_t dmaChannel;
    IRQn_Type irq;
} CellularPortUartConstData_t;

/** Structure of the data per UART.
 */
typedef struct CellularPortUartData_t {
    int32_t number;
    const CellularPortUartConstData_t * pConstData;
    CellularPortMutexHandle_t mutex;
    CellularPortQueueHandle_t queue;
    char *pRxBufferStart;
    char *pRxBufferRead;
    volatile char *pRxBufferWrite;
    bool userNeedsNotify; //!< set this if toRead has hit zero and
                          // hence the user would like a notification
                          // when new data arrives.
    struct CellularPortUartData_t *pNext;
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
static const uint32_t gLlApbGrpPeriphUart[] = {0, // This to avoid having to -1 all the time
                                               LL_APB2_GRP1_PERIPH_USART1,
                                               LL_APB1_GRP1_PERIPH_USART2,
                                               0,
                                               0,
                                               0,
                                               LL_APB2_GRP1_PERIPH_USART6,
                                               0,
                                               0};

// Get the LL driver peripheral number for a given DMA engine.
static const uint32_t gLlApbGrpPeriphDma[] = {0, // This to avoid having to -1 all the time
                                              LL_AHB1_GRP1_PERIPH_DMA1,
                                              LL_AHB1_GRP1_PERIPH_DMA2};

// Get the DMA base address for a given DMA engine
static DMA_TypeDef * const gpDmaReg[] =  {0, // This to avoid having to -1 all the time
                                          DMA1,
                                          DMA2};

// Get the alternate function required on a GPIO line for a given UART.
// Note: which function a GPIO line actually performs on that UART is
// hard coded in the chip; for instance see table 12 of the STM32F437 data sheet.
static const uint32_t gGpioAf[] = {0, // This to avoid having to -1 all the time
                                   LL_GPIO_AF_7,  // USART 1
                                   LL_GPIO_AF_7,  // USART 2
                                   LL_GPIO_AF_7,  // USART 3
                                   LL_GPIO_AF_8,  // UART 4
                                   LL_GPIO_AF_8,  // UART 5
                                   LL_GPIO_AF_8,  // USART 6
                                   LL_GPIO_AF_8,  // USART 7
                                   LL_GPIO_AF_8}; // UART 8

// Table of stream IRQn for DMA engine 1
static const IRQn_Type gDma1StreamIrq[] = {DMA1_Stream0_IRQn,
                                           DMA1_Stream1_IRQn,
                                           DMA1_Stream2_IRQn,
                                           DMA1_Stream3_IRQn,
                                           DMA1_Stream4_IRQn,
                                           DMA1_Stream5_IRQn,
                                           DMA1_Stream6_IRQn,
                                           DMA1_Stream7_IRQn};

// Table of stream IRQn for DMA engine 2
static const IRQn_Type gDma2StreamIrq[] = {DMA2_Stream0_IRQn,
                                           DMA2_Stream1_IRQn,
                                           DMA2_Stream2_IRQn,
                                           DMA2_Stream3_IRQn,
                                           DMA2_Stream4_IRQn,
                                           DMA2_Stream5_IRQn,
                                           DMA2_Stream6_IRQn,
                                           DMA2_Stream7_IRQn};

// Table of DMAx_Stream_IRQn per DMA engine
static const IRQn_Type *gpDmaStreamIrq[] = {NULL, // This to avoid having to -1 all the time
                                            gDma1StreamIrq,
                                            gDma2StreamIrq};

// Table of LL_DMA_CHANNEL_x per channel
static const int32_t gLlDmaChannel[] = {LL_DMA_CHANNEL_0,
                                        LL_DMA_CHANNEL_1,
                                        LL_DMA_CHANNEL_2,
                                        LL_DMA_CHANNEL_3,
                                        LL_DMA_CHANNEL_4,
                                        LL_DMA_CHANNEL_5,
                                        LL_DMA_CHANNEL_6,
                                        LL_DMA_CHANNEL_7};

// Table of functions LL_DMA_ClearFlag_HTx(DMA_TypeDef *DMAx) for each stream.
static const void (*gpLlDmaClearFlagHt[]) (DMA_TypeDef *)  = {LL_DMA_ClearFlag_HT0,
                                                              LL_DMA_ClearFlag_HT1,
                                                              LL_DMA_ClearFlag_HT2,
                                                              LL_DMA_ClearFlag_HT3,
                                                              LL_DMA_ClearFlag_HT4,
                                                              LL_DMA_ClearFlag_HT5,
                                                              LL_DMA_ClearFlag_HT6,
                                                              LL_DMA_ClearFlag_HT7};

// Table of functions LL_DMA_ClearFlag_TCx(DMA_TypeDef *DMAx) for each stream.
static const void (*gpLlDmaClearFlagTc[]) (DMA_TypeDef *)  = {LL_DMA_ClearFlag_TC0,
                                                              LL_DMA_ClearFlag_TC1,
                                                              LL_DMA_ClearFlag_TC2,
                                                              LL_DMA_ClearFlag_TC3,
                                                              LL_DMA_ClearFlag_TC4,
                                                              LL_DMA_ClearFlag_TC5,
                                                              LL_DMA_ClearFlag_TC6,
                                                              LL_DMA_ClearFlag_TC7};

// Table of functions LL_DMA_ClearFlag_TEx(DMA_TypeDef *DMAx) for each stream.
static const void (*gpLlDmaClearFlagTe[]) (DMA_TypeDef *)  = {LL_DMA_ClearFlag_TE0,
                                                              LL_DMA_ClearFlag_TE1,
                                                              LL_DMA_ClearFlag_TE2,
                                                              LL_DMA_ClearFlag_TE3,
                                                              LL_DMA_ClearFlag_TE4,
                                                              LL_DMA_ClearFlag_TE5,
                                                              LL_DMA_ClearFlag_TE6,
                                                              LL_DMA_ClearFlag_TE7};

// Table of functions LL_DMA_ClearFlag_DMEx(DMA_TypeDef *DMAx) for each stream.
static const void (*gpLlDmaClearFlagDme[]) (DMA_TypeDef *)  = {LL_DMA_ClearFlag_DME0,
                                                               LL_DMA_ClearFlag_DME1,
                                                               LL_DMA_ClearFlag_DME2,
                                                               LL_DMA_ClearFlag_DME3,
                                                               LL_DMA_ClearFlag_DME4,
                                                               LL_DMA_ClearFlag_DME5,
                                                               LL_DMA_ClearFlag_DME6,
                                                               LL_DMA_ClearFlag_DME7};

// Table of functions LL_DMA_ClearFlag_FEx(DMA_TypeDef *DMAx) for each stream.
static const void (*gpLlDmaClearFlagFe[]) (DMA_TypeDef *)  = {LL_DMA_ClearFlag_FE0,
                                                              LL_DMA_ClearFlag_FE1,
                                                              LL_DMA_ClearFlag_FE2,
                                                              LL_DMA_ClearFlag_FE3,
                                                              LL_DMA_ClearFlag_FE4,
                                                              LL_DMA_ClearFlag_FE5,
                                                              LL_DMA_ClearFlag_FE6,
                                                              LL_DMA_ClearFlag_FE7};

// Table of functions LL_DMA_IsActiveFlag_HTx(DMA_TypeDef *DMAx) for each stream.
static const uint32_t (*gpLlDmaIsActiveFlagHt[]) (DMA_TypeDef *)  = {LL_DMA_IsActiveFlag_HT0,
                                                                     LL_DMA_IsActiveFlag_HT1,
                                                                     LL_DMA_IsActiveFlag_HT2,
                                                                     LL_DMA_IsActiveFlag_HT3,
                                                                     LL_DMA_IsActiveFlag_HT4,
                                                                     LL_DMA_IsActiveFlag_HT5,
                                                                     LL_DMA_IsActiveFlag_HT6,
                                                                     LL_DMA_IsActiveFlag_HT7};

// Table of functions LL_DMA_IsActiveFlag_TCx(DMA_TypeDef *DMAx) for each stream.
static const uint32_t (*gpLlDmaIsActiveFlagTc[]) (DMA_TypeDef *)  = {LL_DMA_IsActiveFlag_TC0,
                                                                     LL_DMA_IsActiveFlag_TC1,
                                                                     LL_DMA_IsActiveFlag_TC2,
                                                                     LL_DMA_IsActiveFlag_TC3,
                                                                     LL_DMA_IsActiveFlag_TC4,
                                                                     LL_DMA_IsActiveFlag_TC5,
                                                                     LL_DMA_IsActiveFlag_TC6,
                                                                     LL_DMA_IsActiveFlag_TC7};

// Table of the constant data per UART.
static const CellularPortUartConstData_t gUartCfg[] = {{}, // This to avoid having to -1 all the time
                                                       {USART1,
                                                        CELLULAR_CFG_UART1_DMA_ENGINE,
                                                        CELLULAR_CFG_UART1_DMA_STREAM,
                                                        CELLULAR_CFG_UART1_DMA_CHANNEL,
                                                        USART1_IRQn},
                                                       {USART2,
                                                        CELLULAR_CFG_UART2_DMA_ENGINE,
                                                        CELLULAR_CFG_UART2_DMA_STREAM,
                                                        CELLULAR_CFG_UART2_DMA_CHANNEL,
                                                        USART2_IRQn},
                                                       {0,
                                                        0,
                                                        0,
                                                        0,
                                                        0},
                                                       {0,
                                                        0,
                                                        0,
                                                        0,
                                                        0},
                                                       {0,
                                                        0,
                                                        0,
                                                        0,
                                                        0},
                                                       {USART6,
                                                        CELLULAR_CFG_UART6_DMA_ENGINE,
                                                        CELLULAR_CFG_UART6_DMA_STREAM,
                                                        CELLULAR_CFG_UART6_DMA_CHANNEL,
                                                        USART6_IRQn},
                                                       {0,
                                                        0,
                                                        0,
                                                        0,
                                                        0},
                                                       {0,
                                                        0,
                                                        0,
                                                        0,
                                                        0}};

// Table to make it possible for UART interrupts to get to the UART data
// without having to trawl through a list.  +1 is for the usual reason.
static CellularPortUartData_t *gpUart[CELLULAR_PORT_MAX_NUM_UARTS + 1] = {NULL};

// Table to make it possible for a DMA interrupt to
// get to the UART data.  +1 is for the usual reason.
static CellularPortUartData_t *gpDmaUart[(CELLULAR_PORT_MAX_NUM_DMA_ENGINES + 1) *
                                          CELLULAR_PORT_MAX_NUM_DMA_STREAMS] = {NULL};

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

                                          // Add a UART data structure to the list.
// The required memory is malloc()ed.
static CellularPortUartData_t *pAddUart(int32_t uart,
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
        // Set the UART table up to point to it
        // so that the UART interrupt can find it
        gpUart[uart] = *ppUartData;
        // And set the other table up so that the
        // DMA interrupt can find the UART data as well
        gpDmaUart[pUartData->pConstData->dmaEngine +
                  pUartData->pConstData->dmaStream] = *ppUartData;
    }

    return *ppUartData;
}

// Find the UART data structure for a given UART.
static CellularPortUartData_t *pGetUart(int32_t uart)
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
static bool removeUart(int32_t uart)
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
        // NULL the entries in the two tables
        gpUart[uart] = NULL;
        gpDmaUart[(*ppUartData)->pConstData->dmaEngine +
                  (*ppUartData)->pConstData->dmaStream] = NULL;
        // Free memory and NULL the pointer
        cellularPort_free(*ppUartData);
        *ppUartData = NULL;
    }

    return found;
}

// Deal with data already received by the DMA; this
// code is run in INTERRUPT CONTEXT.
static inline void dataIrqHandler(CellularPortUartData_t *pUartData,
                                  char *pRxBufferWriteDma)
{
    CellularPortErrorCode_t rc;
    CellularPortUartEventData_t uartSizeOrError = 0;

    // Work out how much new data there is
    if (pUartData->pRxBufferWrite < pRxBufferWriteDma) {
        // The current write pointer is behind the DMA write pointer,
        // the number of bytes received is simply the difference
        uartSizeOrError = pRxBufferWriteDma - pUartData->pRxBufferWrite;
    } else if (pUartData->pRxBufferWrite > pRxBufferWriteDma) {
        // The current write pointer is ahead of the DMA
        // write pointer, the number of bytes received
        // is up to the end of the buffer then wrap
        // around to the DMA write pointer pointer
        uartSizeOrError = (pUartData->pRxBufferStart +
                           CELLULAR_PORT_UART_RX_BUFFER_SIZE -
                           pUartData->pRxBufferWrite) +
                          (pRxBufferWriteDma - pUartData->pRxBufferStart);
    }

    // Move the write pointer on
    pUartData->pRxBufferWrite += uartSizeOrError;
    if (pUartData->pRxBufferWrite >= pUartData->pRxBufferStart +
                                     CELLULAR_PORT_UART_RX_BUFFER_SIZE) {
        pUartData->pRxBufferWrite = pUartData->pRxBufferWrite -
                                    CELLULAR_PORT_UART_RX_BUFFER_SIZE;
    }

    // If there is new data and the user wanted to know
    // then send a message to let them know.
    if ((uartSizeOrError > 0) && pUartData->userNeedsNotify) {
        rc = cellularPortQueueSendFromISR(pUartData->queue, &uartSizeOrError);
        if (rc == CELLULAR_PORT_SUCCESS) {
            pUartData->userNeedsNotify = false;
        }
    }
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: INTERRUPT HANDLERS
 * -------------------------------------------------------------- */

// DMA interrupt handler
void dmaIrqHandler(uint32_t dmaEngine, uint32_t dmaStream)
{
    DMA_TypeDef * const pDmaReg = gpDmaReg[dmaEngine];
    CellularPortUartData_t *pUartData = NULL;

    // Check half-transfer complete interrupt
    if (LL_DMA_IsEnabledIT_HT(pDmaReg, dmaStream) &&
        gpLlDmaIsActiveFlagHt[dmaStream](pDmaReg)) {
        // Clear the flag
        gpLlDmaClearFlagHt[dmaStream](pDmaReg);
        pUartData = gpDmaUart[dmaEngine + dmaStream];
    }

    // Check transfer complete interrupt
    if (LL_DMA_IsEnabledIT_TC(pDmaReg, dmaStream) &&
        gpLlDmaIsActiveFlagTc[dmaStream](pDmaReg)) {
        // Clear the flag
        gpLlDmaClearFlagTc[dmaStream](pDmaReg);
        pUartData = gpDmaUart[dmaEngine + dmaStream];
    }

    if (pUartData != NULL) {
        char *pRxBufferWriteDma;

        // Stuff has arrived: how much?
        // Get the new DMA pointer
        // LL_DMA_GetDataLength() returns a value in the sense
        // of "number of bytes left to be transmitted", so for
        // an Rx DMA we have to subtract the number from
        // the Rx buffer size
        pRxBufferWriteDma = pUartData->pRxBufferStart +
                            CELLULAR_PORT_UART_RX_BUFFER_SIZE -
                            LL_DMA_GetDataLength(pDmaReg, dmaStream);
        // Deal with the data
        dataIrqHandler(pUartData, pRxBufferWriteDma);
    }
}

// UART interrupt handler
void uartIrqHandler(CellularPortUartData_t *pUartData)
{
    const CellularPortUartConstData_t *pUartCfg = pUartData->pConstData;
    USART_TypeDef * const pUartReg = pUartCfg->pReg;

    // Check for IDLE line interrupt
    if (LL_USART_IsEnabledIT_IDLE(pUartReg) &&
        LL_USART_IsActiveFlag_IDLE(pUartReg)) {
        char *pRxBufferWriteDma;

        // Clear flag
        LL_USART_ClearFlag_IDLE(pUartReg);

        // Get the new DMA pointer
        // LL_DMA_GetDataLength() returns a value in the sense
        // of "number of bytes left to be transmitted", so for
        // an Rx DMA we have to subtract the number from
        // the Rx buffer size
        pRxBufferWriteDma = pUartData->pRxBufferStart +
                            CELLULAR_PORT_UART_RX_BUFFER_SIZE -
                            LL_DMA_GetDataLength(gpDmaReg[pUartCfg->dmaEngine],
                                                 pUartCfg->dmaStream);
        // Deal with the data
        dataIrqHandler(pUartData, pRxBufferWriteDma);
    }
}

#if CELLULAR_CFG_UART1_AVAILABLE
// USART 1 interrupt handler.
void USART1_IRQHandler()
{
    if (gpUart[1] != NULL) {
        uartIrqHandler(gpUart[1]);
    }
}
#endif

#if CELLULAR_CFG_UART2_AVAILABLE
// USART 2 interrupt handler.
void USART2_IRQHandler()
{
    if (gpUart[2] != NULL) {
        uartIrqHandler(gpUart[2]);
    }
}
#endif

#if CELLULAR_CFG_UART3_AVAILABLE
// USART 3 interrupt handler.
void USART3_IRQHandler()
{
    if (gpUart[3] != NULL) {
        uartIrqHandler(gpUart[3]);
    }
}
#endif

#if CELLULAR_CFG_UART4_AVAILABLE
// UART 4 interrupt handler.
void UART4_IRQHandler()
{
    if (gpUart[4] != NULL) {
        uartIrqHandler(gpUart[4]);
    }
}
#endif

#if CELLULAR_CFG_UART5_AVAILABLE
// UART 5 interrupt handler.
void UART5_IRQHandler()
{
    if (gpUart[5] != NULL) {
        uartIrqHandler(gpUart[5]);
    }
}
#endif

#if CELLULAR_CFG_UART6_AVAILABLE
// USART 6 interrupt handler.
void USART6_IRQHandler()
{
    if (gpUart[6] != NULL) {
        uartIrqHandler(gpUart[6]);
    }
}
#endif

#if CELLULAR_CFG_UART7_AVAILABLE
// UART 7 interrupt handler.
void UART7_IRQHandler()
{
    if (gpUart[7] != NULL) {
        uartIrqHandler(gpUart[7]);
    }
}
#endif

#if CELLULAR_CFG_UART8_AVAILABLE
// UART 8 interrupt handler.
void UART8_IRQHandler()
{
    if (gpUart[8] != NULL) {
        uartIrqHandler(gpUart[8]);
    }
}
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(1, 0)
void DMA1_Stream0_IRQHandler()
{
    dmaIrqHandler(1, 0);
}
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(1, 1)
void DMA1_Stream1_IRQHandler()
{
    dmaIrqHandler(1, 1);
}
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(1, 2)
void DMA1_Stream2_IRQHandler()
{
    dmaIrqHandler(1, 2);
}
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(1, 3)
void DMA1_Stream3_IRQHandler()
{
    dmaIrqHandler(1, 3);
}
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(1, 4)
void DMA1_Stream4_IRQHandler()
{
    dmaIrqHandler(1, 4);
}
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(1, 5)
void DMA1_Stream5_IRQHandler()
{
    dmaIrqHandler(1, 5);
}
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(1, 6)
void DMA1_Stream6_IRQHandler()
{
    dmaIrqHandler(1, 6);
}
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(1, 7)
void DMA1_Stream7_IRQHandler()
{
    dmaIrqHandler(1, 7);
}
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(2, 0)
void DMA2_Stream0_IRQHandler()
{
    dmaIrqHandler(2, 0);
}
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(2, 1)
void DMA2_Stream1_IRQHandler()
{
    dmaIrqHandler(2, 1);
}
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(2, 2)
void DMA2_Stream2_IRQHandler()
{
    dmaIrqHandler(2, 2);
}
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(2, 3)
void DMA2_Stream3_IRQHandler()
{
    dmaIrqHandler(2, 3);
}
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(2, 4)
void DMA2_Stream4_IRQHandler()
{
    dmaIrqHandler(2, 4);
}
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(2, 5)
void DMA2_Stream5_IRQHandler()
{
    dmaIrqHandler(2, 5);
}
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(2, 6)
void DMA2_Stream6_IRQHandler()
{
    dmaIrqHandler(2, 6);
}
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(2, 7)
void DMA2_Stream7_IRQHandler()
{
    dmaIrqHandler(2, 7);
}
#endif

static void
cellular_port_uart_set_vectors(void)
{
#if CELLULAR_CFG_UART1_AVAILABLE
    NVIC_SetVector(USART1_IRQn, (uint32_t)USART1_IRQHandler);
#endif

#if CELLULAR_CFG_UART2_AVAILABLE
    NVIC_SetVector(USART2_IRQn, (uint32_t)USART2_IRQHandler);
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(1, 0)
    NVIC_SetVector(DMA1_Stream0_IRQn, (uint32_t)DMA1_Stream0_IRQHandler);
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(1, 1)
    NVIC_SetVector(DMA1_Stream1_IRQn, (uint32_t)DMA1_Stream1_IRQHandler);
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(1, 2)
    NVIC_SetVector(DMA1_Stream2_IRQn, (uint32_t)DMA1_Stream2_IRQHandler);
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(1, 3)
    NVIC_SetVector(DMA1_Stream3_IRQn, (uint32_t)DMA1_Stream3_IRQHandler);
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(1, 4)
    NVIC_SetVector(DMA1_Stream4_IRQn, (uint32_t)DMA1_Stream4_IRQHandler);
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(1, 5)
    NVIC_SetVector(DMA1_Stream5_IRQn, (uint32_t)DMA1_Stream5_IRQHandler);
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(1, 6)
    NVIC_SetVector(DMA1_Stream6_IRQn, (uint32_t)DMA1_Stream6_IRQHandler);
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(1, 7)
    NVIC_SetVector(DMA1_Stream7_IRQn, (uint32_t)DMA1_Stream7_IRQHandler);
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(2, 0)
    NVIC_SetVector(DMA2_Stream0_IRQn, (uint32_t)DMA2_Stream0_IRQHandler);
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(2, 1)
    NVIC_SetVector(DMA2_Stream1_IRQn, (uint32_t)DMA2_Stream1_IRQHandler);
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(2, 2)
    NVIC_SetVector(DMA2_Stream2_IRQn, (uint32_t)DMA2_Stream2_IRQHandler);
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(2, 3)
    NVIC_SetVector(DMA2_Stream3_IRQn, (uint32_t)DMA2_Stream3_IRQHandler);
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(2, 4)
    NVIC_SetVector(DMA2_Stream4_IRQn, (uint32_t)DMA2_Stream4_IRQHandler);
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(2, 5)
    NVIC_SetVector(DMA2_Stream5_IRQn, (uint32_t)DMA2_Stream5_IRQHandler);
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(2, 6)
    NVIC_SetVector(DMA2_Stream6_IRQn, (uint32_t)DMA2_Stream6_IRQHandler);
#endif

#if CELLULAR_PORT_DMA_INTERRUPT_IN_USE(2, 7)
    NVIC_SetVector(DMA2_Stream7_IRQn, (uint32_t)DMA2_Stream7_IRQHandler);
#endif
}

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
    USART_TypeDef *pUartReg;
    uint32_t dmaEngine;
    DMA_TypeDef *pDmaReg;
    uint32_t dmaStream;
    uint32_t dmaChannel;
    IRQn_Type uartIrq;
    IRQn_Type dmaIrq;

    // TODO: use this
    (void) rtsThreshold;

    /* XXX: ugly, but just do this for now */
    cellular_port_uart_set_vectors();

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
                    uartData.pConstData = &(gUartCfg[uart]);
                    uartData.pRxBufferRead = uartData.pRxBufferStart;
                    uartData.pRxBufferWrite = uartData.pRxBufferStart;
                    uartData.userNeedsNotify = true;

                    // Create the queue
                    errorCode = cellularPortQueueCreate(CELLULAR_PORT_UART_EVENT_QUEUE_SIZE,
                                                        sizeof(CellularPortUartEventData_t),
                                                        pUartQueue);
                    if (errorCode == 0) {
                        uartData.queue = *pUartQueue;

                        pUartReg = gUartCfg[uart].pReg;
                        dmaEngine = gUartCfg[uart].dmaEngine;
                        pDmaReg = gpDmaReg[dmaEngine];
                        dmaStream = gUartCfg[uart].dmaStream;
                        dmaChannel = gUartCfg[uart].dmaChannel;
                        uartIrq = gUartCfg[uart].irq;
                        dmaIrq = gpDmaStreamIrq[dmaEngine][dmaStream];

                        // Now do the platform stuff
                        errorCode = CELLULAR_PORT_PLATFORM_ERROR;

                        // Enable clock to the UART/USART HW block
                        gLlApbClkEnable[uart](gLlApbGrpPeriphUart[uart]);

                        // Enable clock to the DMA HW block (all DMAs
                        // are on bus 1)
                        LL_AHB1_GRP1_EnableClock(gLlApbGrpPeriphDma[dmaEngine]);

                        // Configure the GPIOs
                        // Note, using the LL driver rather than our
                        // driver or the HAL driver here partly because
                        // the example code does that and also
                        // because we need to enable the alternate
                        // function for these pins.

                        // Enable clock to the registers for the Tx/Rx pins
                        cellularPortPrivateGpioEnableClock(pinTx);
                        cellularPortPrivateGpioEnableClock(pinRx);
                        // The Pin field is a bitmap so we can do Tx and Rx
                        // at the same time as they are always on the same port
                        gpioInitStruct.Pin = (1U << CELLULAR_PORT_STM32F4_GPIO_PIN(pinTx)) |
                                             (1U << CELLULAR_PORT_STM32F4_GPIO_PIN(pinRx));
                        gpioInitStruct.Mode = LL_GPIO_MODE_ALTERNATE;
                        gpioInitStruct.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
                        // Output type doesn't matter, it is overridden by
                        // the alternate function
                        gpioInitStruct.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
                        gpioInitStruct.Pull = LL_GPIO_PULL_UP;
                        gpioInitStruct.Alternate = gGpioAf[uart];
                        platformError = LL_GPIO_Init(pCellularPortPrivateGpioGetReg(pinTx),
                                                     &gpioInitStruct);

                        //  Configure RTS if present
                        if ((pinRts >= 0) && (platformError == SUCCESS)) {
                            cellularPortPrivateGpioEnableClock(pinRts);
                            gpioInitStruct.Pin = 1U << CELLULAR_PORT_STM32F4_GPIO_PIN(pinRts);
                            platformError = LL_GPIO_Init(pCellularPortPrivateGpioGetReg(pinRts),
                                                         &gpioInitStruct);
                        }

                        //  Configure CTS if present
                        if ((pinCts >= 0) && (platformError == SUCCESS)) {
                            cellularPortPrivateGpioEnableClock(pinCts);
                            gpioInitStruct.Pin = 1U << CELLULAR_PORT_STM32F4_GPIO_PIN(pinCts);
                            // TODO: the u-blox C030-R412M board requires a pull-down here
                            gpioInitStruct.Pull = LL_GPIO_PULL_DOWN;
                            platformError = LL_GPIO_Init(pCellularPortPrivateGpioGetReg(pinCts),
                                                         &gpioInitStruct);
                        }

                        // Configure DMA
                        if (platformError == SUCCESS) {
                            // Set the channel on our DMA/Stream
                            LL_DMA_SetChannelSelection(pDmaReg, dmaStream,
                                                       gLlDmaChannel[dmaChannel]);
                            // Towards RAM
                            LL_DMA_SetDataTransferDirection(pDmaReg, dmaStream,
                                                            LL_DMA_DIRECTION_PERIPH_TO_MEMORY);
                            // Low priority
                            LL_DMA_SetStreamPriorityLevel(pDmaReg, dmaStream,
                                                          LL_DMA_PRIORITY_LOW);
                            // Circular
                            LL_DMA_SetMode(pDmaReg, dmaStream, LL_DMA_MODE_CIRCULAR);
                            // Byte-wise transfers from a fixed
                            // register in a peripheral to an
                            // incrementing location in memory
                            LL_DMA_SetPeriphIncMode(pDmaReg, dmaStream,
                                                    LL_DMA_PERIPH_NOINCREMENT);
                            LL_DMA_SetMemoryIncMode(pDmaReg, dmaStream,
                                                    LL_DMA_MEMORY_INCREMENT);
                            LL_DMA_SetPeriphSize(pDmaReg, dmaStream,
                                                 LL_DMA_PDATAALIGN_BYTE);
                            LL_DMA_SetMemorySize(pDmaReg, dmaStream,
                                                 LL_DMA_MDATAALIGN_BYTE);
                            // Not FIFO mode, whatever that is
                            LL_DMA_DisableFifoMode(pDmaReg, dmaStream);

                            // Attach the DMA to the UART at one end
                            LL_DMA_SetPeriphAddress(pDmaReg, dmaStream,
                                                    (uint32_t) &(pUartReg->DR));

                            // ...and to the RAM buffer at the other end
                            LL_DMA_SetMemoryAddress(pDmaReg, dmaStream,
                                                    (uint32_t) (uartData.pRxBufferStart));
                            LL_DMA_SetDataLength(pDmaReg, dmaStream,
                                                 CELLULAR_PORT_UART_RX_BUFFER_SIZE);

                            // Clear all the DMA flags and the DMA pending IRQ from any previous
                            // session first, or an unexpected interrupt may result
                            gpLlDmaClearFlagHt[dmaStream](pDmaReg);
                            gpLlDmaClearFlagTc[dmaStream](pDmaReg);
                            gpLlDmaClearFlagTe[dmaStream](pDmaReg);
                            gpLlDmaClearFlagDme[dmaStream](pDmaReg);
                            gpLlDmaClearFlagFe[dmaStream](pDmaReg);
                            NVIC_ClearPendingIRQ(dmaIrq);

                            // Enable half full and transmit complete DMA interrupts
                            LL_DMA_EnableIT_HT(pDmaReg, dmaStream);
                            LL_DMA_EnableIT_TC(pDmaReg, dmaStream);

                            // Set DMA priority
                            NVIC_SetPriority(dmaIrq,
                                             NVIC_EncodePriority(NVIC_GetPriorityGrouping(), 5, 0));

                            // Go!
                            NVIC_EnableIRQ(dmaIrq);

                            // Initialise the UART/USART
                            usartInitStruct.BaudRate = baudRate;
                            usartInitStruct.DataWidth = LL_USART_DATAWIDTH_8B;
                            usartInitStruct.StopBits = LL_USART_STOPBITS_1;
                            usartInitStruct.Parity = LL_USART_PARITY_NONE;
                            // Both transmit and received enabled
                            usartInitStruct.TransferDirection = LL_USART_DIRECTION_TX_RX;
                            // TODO: need to connect flow control to DMA?
                            usartInitStruct.HardwareFlowControl = LL_USART_HWCONTROL_NONE;
/* WWW */
#if 1
                            if ((pinRts >= 0) && (pinCts >= 0)) {
                                usartInitStruct.HardwareFlowControl = LL_USART_HWCONTROL_RTS_CTS;
                            } else {
                                if (pinRts >= 0) {
                                    usartInitStruct.HardwareFlowControl = LL_USART_HWCONTROL_RTS;
                                } else {
                                    usartInitStruct.HardwareFlowControl = LL_USART_HWCONTROL_CTS;
                                }
                            }
#endif
/* WWW */
                            usartInitStruct.OverSampling = LL_USART_OVERSAMPLING_16;
                            platformError = LL_USART_Init(pUartReg, &usartInitStruct);
                        }

                        // Connect it all together
                        if (platformError == SUCCESS) {
                            // Asynchronous UART/USART with DMA on the receive
                            // and include only the idle line interrupt,
                            // DMA does the rest
                            LL_USART_ConfigAsyncMode(pUartReg);
                            LL_USART_EnableDMAReq_RX(pUartReg);
                            LL_USART_EnableIT_IDLE(pUartReg);

                            // Enable the UART/USART interrupt
                            NVIC_SetPriority(uartIrq,
                                             NVIC_EncodePriority(NVIC_GetPriorityGrouping(),
                                                                 5, 1));
                            LL_USART_ClearFlag_IDLE(pUartReg);
                            NVIC_ClearPendingIRQ(uartIrq);
                            NVIC_EnableIRQ(uartIrq);

                            // Enable DMA and UART/USART
                            LL_DMA_EnableStream(pDmaReg, dmaStream);
                            LL_USART_Enable(pUartReg);
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
    CellularPortUartData_t *pUartData;
    USART_TypeDef *pUartReg;
    uint32_t dmaEngine;
    uint32_t dmaStream;

    if ((uart > 0) && (uart <= CELLULAR_PORT_MAX_NUM_UARTS)) {
        pUartData = pGetUart(uart);
        errorCode = CELLULAR_PORT_SUCCESS;
        if (pUartData != NULL) {

            pUartReg = gUartCfg[uart].pReg;
            dmaEngine = gUartCfg[uart].dmaEngine;
            dmaStream = gUartCfg[uart].dmaStream;
            // No need to lock the mutex, we need to delete it
            // and we're not allowed to delete a locked mutex.
            // The caller needs to make sure that no read/write
            // is in progress when this function is called.

            // TODO check this

            // Disable DMA and UART/USART interrupts
            NVIC_DisableIRQ(gpDmaStreamIrq[dmaEngine][dmaStream]);
            NVIC_DisableIRQ(gUartCfg[uart].irq);

            // Disable DMA and USART, waiting for DMA to be
            // disabled first according to the note in
            // section 10.3.17 of ST's RM0090.
            LL_DMA_DisableStream(gpDmaReg[dmaEngine], dmaStream);
            while (LL_DMA_IsEnabledStream(gpDmaReg[dmaEngine], dmaStream)) {}
            LL_USART_Disable(pUartReg);
            LL_USART_DeInit(pUartReg);

            // Delete the queue
            cellularPortQueueDelete(pUartData->queue);
            // Free the buffer
            cellularPort_free(pUartData->pRxBufferStart);
            // Delete the mutex
            cellularPortMutexDelete(pUartData->mutex);
            // And finally remove the UART from the list
            removeUart(uart);
            errorCode = CELLULAR_PORT_SUCCESS;
        }
    }

    return (int32_t) errorCode;
}

// Push a UART event onto the UART event queue.
int32_t cellularPortUartEventSend(const CellularPortQueueHandle_t queueHandle,
                                  int32_t sizeBytesOrError)
{
    int32_t errorCode = CELLULAR_PORT_INVALID_PARAMETER;
    CellularPortUartEventData_t uartSizeOrError;

    if (queueHandle != NULL) {
        uartSizeOrError = sizeBytesOrError;
        errorCode = cellularPortQueueSend(queueHandle, (void *) &uartSizeOrError);
    }

    return errorCode;
}

// Receive a UART event, blocking until one turns up.
int32_t cellularPortUartEventReceive(const CellularPortQueueHandle_t queueHandle)
{
    int32_t sizeOrErrorCode = CELLULAR_PORT_INVALID_PARAMETER;
    CellularPortUartEventData_t uartSizeOrError;

    if (queueHandle != NULL) {
        sizeOrErrorCode = CELLULAR_PORT_PLATFORM_ERROR;
        if (cellularPortQueueReceive(queueHandle, &uartSizeOrError) == 0) {
            sizeOrErrorCode = uartSizeOrError;
        }
    }

    return sizeOrErrorCode;
}

// Receive a UART event with a timeout.
int32_t cellularPortUartEventTryReceive(const CellularPortQueueHandle_t queueHandle,
                                        int32_t waitMs)
{
    int32_t sizeOrErrorCode = CELLULAR_PORT_INVALID_PARAMETER;
    CellularPortUartEventData_t uartSizeOrError;

    if (queueHandle != NULL) {
        sizeOrErrorCode = CELLULAR_PORT_TIMEOUT;
        if (cellularPortQueueTryReceive(queueHandle, waitMs, &uartSizeOrError) == 0) {
            sizeOrErrorCode = uartSizeOrError;
        }
    }

    return sizeOrErrorCode;
}

// Get the number of bytes waiting in the receive buffer.
int32_t cellularPortUartGetReceiveSize(int32_t uart)
{
    CellularPortErrorCode_t sizeOrErrorCode = CELLULAR_PORT_INVALID_PARAMETER;
    CellularPortUartData_t *pUartData = pGetUart(uart);
    const volatile char *pRxBufferWrite;

    if (pUartData != NULL) {

        CELLULAR_PORT_MUTEX_LOCK(pUartData->mutex);

        pRxBufferWrite = pUartData->pRxBufferWrite;
        sizeOrErrorCode = 0;
        if (pUartData->pRxBufferRead < pRxBufferWrite) {
            // Read pointer is behind write, bytes
            // received is simply the difference
            sizeOrErrorCode = pRxBufferWrite - pUartData->pRxBufferRead;
        } else if (pUartData->pRxBufferRead > pRxBufferWrite) {
            // Read pointer is ahead of write, bytes received
            // is from the read pointer up to the end of the buffer
            // then wrap around to the write pointer
            sizeOrErrorCode = (pUartData->pRxBufferStart +
                               CELLULAR_PORT_UART_RX_BUFFER_SIZE -
                               pUartData->pRxBufferRead) +
                              (pRxBufferWrite - pUartData->pRxBufferStart);
        }

        CELLULAR_PORT_MUTEX_UNLOCK(pUartData->mutex);

    }

    return (int32_t) sizeOrErrorCode;
}

// Read from the given UART interface.
int32_t cellularPortUartRead(int32_t uart, char *pBuffer,
                             size_t sizeBytes)
{
    CellularPortErrorCode_t sizeOrErrorCode = CELLULAR_PORT_INVALID_PARAMETER;
    size_t thisSize;
    CellularPortUartData_t *pUartData = pGetUart(uart);
    const volatile char *pRxBufferWrite;

    if (pUartData != NULL) {

        CELLULAR_PORT_MUTEX_LOCK(pUartData->mutex);

        sizeOrErrorCode = 0;
        pRxBufferWrite = pUartData->pRxBufferWrite;
        if (pUartData->pRxBufferRead < pRxBufferWrite) {
            // Read pointer is behind write, just take as much
            // of the difference as the user allows
            sizeOrErrorCode = pRxBufferWrite - pUartData->pRxBufferRead;
            if (sizeOrErrorCode > sizeBytes) {
                sizeOrErrorCode = sizeBytes;
            }
            pCellularPort_memcpy(pBuffer, pUartData->pRxBufferRead,
                                 sizeOrErrorCode);
            // Move the pointer on
            pUartData->pRxBufferRead += sizeOrErrorCode;
        } else if (pUartData->pRxBufferRead > pRxBufferWrite) {
            // Read pointer is ahead of write, first take up to the
            // end of the buffer as far as the user allows
            thisSize = pUartData->pRxBufferStart +
                       CELLULAR_PORT_UART_RX_BUFFER_SIZE -
                       pUartData->pRxBufferRead;
            if (thisSize > sizeBytes) {
                thisSize = sizeBytes;
            }
            pCellularPort_memcpy(pBuffer, pUartData->pRxBufferRead,
                                 thisSize);
            pBuffer += thisSize;
            sizeBytes -= thisSize;
            sizeOrErrorCode = thisSize;
            // Move the read pointer on, wrapping as necessary
            pUartData->pRxBufferRead += thisSize;
            if (pUartData->pRxBufferRead >= pUartData->pRxBufferStart +
                                            CELLULAR_PORT_UART_RX_BUFFER_SIZE) {
                pUartData->pRxBufferRead = pUartData->pRxBufferStart;
            }
            // If there is still room in the user buffer then
            // carry on taking up to the write pointer
            if (sizeBytes > 0) {
                thisSize = pRxBufferWrite - pUartData->pRxBufferRead;
                if (thisSize > sizeBytes) {
                    thisSize = sizeBytes;
                }
                pCellularPort_memcpy(pBuffer, pUartData->pRxBufferRead,
                                     thisSize);
                pBuffer += thisSize;
                sizeBytes -= thisSize;
                sizeOrErrorCode += thisSize;
                // Move the read pointer on
                pUartData->pRxBufferRead += thisSize;
            }
        }

        // If everything has been read, a notification
        // is needed for the next one
        if (pUartData->pRxBufferRead == pRxBufferWrite) {
            pUartData->userNeedsNotify = true;
        }

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
    USART_TypeDef *pReg;

    if (pUartData != NULL) {

        CELLULAR_PORT_MUTEX_LOCK(pUartData->mutex);

        pReg = gUartCfg[uart].pReg;
        sizeOrErrorCode = sizeBytes;

        // Do the blocking send
        while (sizeBytes > 0) {
            LL_USART_TransmitData8(pReg, (uint8_t) *pBuffer);
            while (!LL_USART_IsActiveFlag_TXE(pReg)) {}
            pBuffer++;
            sizeBytes--;
        }
        while (!LL_USART_IsActiveFlag_TC(pReg)) {}

        CELLULAR_PORT_MUTEX_UNLOCK(pUartData->mutex);

    }

    return (int32_t) sizeOrErrorCode;
}

// Determine if RTS flow control is enabled.
bool cellularPortIsRtsFlowControlEnabled(int32_t uart)
{
    CellularPortUartData_t *pUartData = pGetUart(uart);
    bool rtsFlowControlIsEnabled = false;
    uint32_t flowControlStatus;

    if (pUartData != NULL) {
        // No need to lock the mutex, this is atomic
        flowControlStatus = LL_USART_GetHWFlowCtrl(gUartCfg[uart].pReg);
        if ((flowControlStatus == LL_USART_HWCONTROL_RTS) ||
            (flowControlStatus == LL_USART_HWCONTROL_RTS_CTS)) {
            rtsFlowControlIsEnabled = true;
        }
    }

    return rtsFlowControlIsEnabled;
}

// Determine if CTS flow control is enabled.
bool cellularPortIsCtsFlowControlEnabled(int32_t uart)
{
    CellularPortUartData_t *pUartData = pGetUart(uart);
    bool ctsFlowControlIsEnabled = false;
    uint32_t flowControlStatus;

    if (pUartData != NULL) {
        // No need to lock the mutex, this is atomic
        flowControlStatus = LL_USART_GetHWFlowCtrl(gUartCfg[uart].pReg);
        if ((flowControlStatus == LL_USART_HWCONTROL_CTS) ||
            (flowControlStatus == LL_USART_HWCONTROL_RTS_CTS)) {
            ctsFlowControlIsEnabled = true;
        }
    }

    return ctsFlowControlIsEnabled;
}

// End of filed
