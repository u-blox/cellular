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

// Uncomment the following line to enable detailed UART logging.
//#define CELLULAR_PORT_UART_DETAILED_DEBUG

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
#include "cellular_port_private.h"

#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"

#include "nrf.h"
#include "nrf_uarte.h"
#include "nrf_gpio.h"
#include "nrfx_ppi.h"
#include "nrfx_timer.h"

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
 * 
 * Design note: it took ages to get this to work.  The issue
 * is with handling continuous reception that has gaps, i.e. running
 * DMA and also having a timer of some sort to push up to the
 * application any data left in a buffer when the incoming data
 * stream happens to pauuse. The key is NEVER to stop the UARTE HW,
 * to always have the ENDRX event shorted to a STARTRX task with at
 * least two buffers.  Any attempt to stop and restart
 * the UARTE ends up with character loss; believe me I've tried them
 * all.
 */

#ifdef CELLULAR_PORT_UART_DETAILED_DEBUG
/* Since I had so much trouble getting the UART code working,
 * I've left in this detailed debug code under a compilation
 * switch in case it's needed later.
 */
#endif

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


// Length of DMA on NRF52840 HW.
// Note that the maximum length is 256 however
// the cost of starting a new DMA buffer is
// zero (since the pointer is double-buffered
// buffered in HW) so setting this to a smaller
// value so that the user can set
// CELLULAR_PORT_UART_RX_BUFFER_SIZE to a smaller
// value and still have at least two buffers.
#ifndef CELLULAR_PORT_UART_SUB_BUFFER_SIZE
# define CELLULAR_PORT_UART_SUB_BUFFER_SIZE 128
#endif

// The number of sub-buffers.
#define CELLULAR_PORT_UART_NUM_SUB_BUFFERS (CELLULAR_PORT_UART_RX_BUFFER_SIZE / \
                                            CELLULAR_PORT_UART_SUB_BUFFER_SIZE)

#if CELLULAR_PORT_UART_NUM_SUB_BUFFERS < 2
# error Cannot accommodate two sub-buffers, either increase CELLULAR_PORT_UART_RX_BUFFER_SIZE to a larger multiple of CELLULAR_PORT_UART_SUB_BUFFER_SIZE or reduce CELLULAR_PORT_UART_SUB_BUFFER_SIZE.
#endif


#ifdef CELLULAR_PORT_UART_DETAILED_DEBUG
// To do detailed UART logging we can't afford to do time calculations on each call,
// instead record the raw data and work it out afterwards
extern nrfx_timer_t gTickTimer;
extern int64_t gTickTimerOverflowCount;
extern int32_t gTickTimerOffset;
extern bool gTickTimerUartMode;
// Macro for detailed UART logging.
# define UART_DETAILED_LOG(_event, _data) NRFX_CRITICAL_SECTION_ENTER();                                     \
                                        gLog[gLogIndex].tick = nrfx_timer_capture(&gTickTimer, 1);           \
                                        gLog[gLogIndex].overflowCount = (int32_t) gTickTimerOverflowCount;   \
                                        gLog[gLogIndex].offset = gTickTimerOffset;                           \
                                        gLog[gLogIndex].tickIsUartMode = gTickTimerUartMode;                 \
                                        gLog[gLogIndex].event = _event;                                      \
                                        gLog[gLogIndex].data = (int32_t) _data;                              \
                                        gLogIndex++;                                                         \
                                        if (gLogIndex >= sizeof(gLog) / sizeof(gLog[0])) {                   \
                                            gLogIndex = 0;                                                   \
                                        }                                                                    \
                                        NRFX_CRITICAL_SECTION_EXIT(); 
#else
# define UART_DETAILED_LOG(_event, _data)
#endif

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/** A UART event.  Since we only ever need to signal
 * size or error then on this platform the
 * CellularPortUartEventData_t can simply be an int32_t.
 */
typedef int32_t CellularPortUartEventData_t;

/** UART receive buffer structure, which can be used as a list.
 */
typedef struct CellularPortUartBuffer_t {
    char *pStart;
    struct CellularPortUartBuffer_t *pNext;
} CellularPortUartBuffer_t;

/** Structure of the things we need to keep track of per UART.
 */
typedef struct {
    NRF_UARTE_Type *pReg;
    nrfx_timer_t timer;
    nrf_ppi_channel_t ppiChannel;
    CellularPortMutexHandle_t mutex;
    CellularPortQueueHandle_t queue;
    char *pRxStart;
    CellularPortUartBuffer_t *pRxBufferWriteNext;
    char *pRxRead;
    size_t startRxByteCount;
    volatile size_t endRxByteCount;
    bool userNeedsNotify; //!< set this when all the data has
                          // been read and hence the user
                          // would like a notification
                          // when new data arrives.
    CellularPortUartBuffer_t rxBufferList[CELLULAR_PORT_UART_NUM_SUB_BUFFERS];
} CellularPortUartData_t;

#ifdef CELLULAR_PORT_UART_DETAILED_DEBUG

// Events to record for detailed UART debug.
// If you modify this enum then you must also modify
// gLogText to match.
typedef enum {
    UART_LOG_EVENT_NULL,
    UART_LOG_EVENT_API_INIT_START,
    UART_LOG_EVENT_API_INIT_END,
    UART_LOG_EVENT_API_DEINIT_START,
    UART_LOG_EVENT_API_DEINIT_END,
    UART_LOG_EVENT_API_EVENT_SEND_START,
    UART_LOG_EVENT_API_EVENT_SEND_END,
    UART_LOG_EVENT_API_EVENT_RECEIVE_START,
    UART_LOG_EVENT_API_EVENT_RECEIVE_END,
    UART_LOG_EVENT_API_EVENT_TRY_RECEIVE_START,
    UART_LOG_EVENT_API_EVENT_TRY_RECEIVE_END,
    UART_LOG_EVENT_API_GET_RECEIVE_SIZE_START,
    UART_LOG_EVENT_API_GET_RECEIVE_SIZE_END,
    UART_LOG_EVENT_API_READ_START,
    UART_LOG_EVENT_API_READ_END,
    UART_LOG_EVENT_API_WRITE_START,
    UART_LOG_EVENT_API_WRITE_END,
    UART_LOG_EVENT_API_IS_RTS_FLOW_CONTROL_ENABLED_START,
    UART_LOG_EVENT_API_IS_RTS_FLOW_CONTROL_ENABLED_END,
    UART_LOG_EVENT_API_IS_CTS_FLOW_CONTROL_ENABLED_START,
    UART_LOG_EVENT_API_IS_CTS_FLOW_CONTROL_ENABLED_END,
    UART_LOG_EVENT_INT_TIMER_CALLBACK,
    UART_LOG_EVENT_INT_ENDRX,
    UART_LOG_EVENT_INT_RXSTARTED,
    UART_LOG_EVENT_INT_ERROR,
    UART_LOG_EVENT_MUTEX_HANDLE,
    UART_LOG_EVENT_QUEUE_HANDLE,
    UART_LOG_EVENT_RX_DATA_SIZE,
    UART_LOG_EVENT_DMA_RX_DATA_SIZE,
    UART_LOG_EVENT_TX_DATA_SIZE,
    UART_LOG_EVENT_BAUD_RATE,
    UART_LOG_EVENT_BAUD_RATE_NRF,
    UART_LOG_EVENT_PIN_TXD,
    UART_LOG_EVENT_PIN_RXD,
    UART_LOG_EVENT_PIN_CTS,
    UART_LOG_EVENT_PIN_RTS,
    UART_LOG_EVENT_REG,
    UART_LOG_EVENT_USER_RX_BUFFER,
    UART_LOG_EVENT_USER_TX_BUFFER,
    UART_LOG_EVENT_RX_BUFFER_MALLOC,
    UART_LOG_EVENT_START_PTR,
    UART_LOG_EVENT_READ_PTR,
    UART_LOG_EVENT_WRITE_NEXT_BUFFER_PTR,
    UART_LOG_EVENT_WRITE_NEXT_BUFFER_START_PTR,
    UART_LOG_EVENT_WRITE_NEXT_BUFFER_NEXT_PTR,
    UART_LOG_EVENT_START_RX_BYTE_COUNT,
    UART_LOG_EVENT_END_RX_BYTE_COUNT,
    UART_LOG_EVENT_GET_RX_BYTES,
    UART_LOG_EVENT_USER_NEEDS_NOTIFY,
    UART_LOG_EVENT_YIELD,
    UART_LOG_EVENT_X
} LogEvent_t;

// Log struct.
typedef struct {
    int32_t tick;
    int32_t overflowCount; // Should be int64_t but saving space
    int32_t offset;
    bool tickIsUartMode;
    LogEvent_t event;
    int32_t data;
} LogThing_t;

#endif

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// UART data, where the UARTE register and the
// associated counter and PPI channel are the only
// ones initialised here.
#if !NRFX_UARTE0_ENABLED && !NRFX_UARTE1_ENABLED
static CellularPortUartData_t gUartData[] = {{NRF_UARTE0, 
                                              NRFX_TIMER_INSTANCE(CELLULAR_PORT_UART_COUNTER_INSTANCE_0),
                                              -1},
                                             {NRF_UARTE1, 
                                              NRFX_TIMER_INSTANCE(CELLULAR_PORT_UART_COUNTER_INSTANCE_1),
                                              -1}};
# else
#  if !NRFX_UARTE0_ENABLED 
static CellularPortUartData_t gUartData[] = {NRF_UARTE0,
                                             NRFX_TIMER_INSTANCE(CELLULAR_PORT_UART_COUNTER_INSTANCE_0),
                                             -1};
#  else
static CellularPortUartData_t gUartData[] = {NRF_UARTE1,
                                             NRFX_TIMER_INSTANCE(CELLULAR_PORT_UART_COUNTER_INSTANCE_1),
                                             -1};
#  endif
#endif

#ifdef CELLULAR_PORT_UART_DETAILED_DEBUG
// Logging array for detailed UART logging
static LogThing_t gLog[2048];
// Index into the array
static size_t gLogIndex = 0;
static const char *gLogText[] = {"NULL",
                                 "API_INIT_START",
                                 "API_INIT_END",
                                 "API_DEINIT_START",
                                 "API_DEINIT_END",
                                 "API_EVENT_SEND_START",
                                 "API_EVENT_SEND_END",
                                 "API_EVENT_RECEIVE_START",
                                 "API_EVENT_RECEIVE_END",
                                 "API_EVENT_TRY_RECEIVE_START",
                                 "API_EVENT_TRY_RECEIVE_END",
                                 "API_GET_RECEIVE_SIZE_START",
                                 "API_GET_RECEIVE_SIZE_END",
                                 "API_READ_START",
                                 "API_READ_END",
                                 "API_WRITE_START",
                                 "API_WRITE_END",
                                 "API_IS_RTS_FLOW_CONTROL_ENABLED_START",
                                 "API_IS_RTS_FLOW_CONTROL_ENABLED_END",
                                 "API_IS_CTS_FLOW_CONTROL_ENABLED_START",
                                 "API_IS_CTS_FLOW_CONTROL_ENABLED_END",
                                 "INT_TIMER_CALLBACK",
                                 "INT_ENDRX",
                                 "INT_RXSTARTED",
                                 "INT_ERROR",
                                 "MUTEX_HANDLE",
                                 "QUEUE_HANDLE",
                                 "RX_DATA_SIZE",
                                 "DMA_RX_DATA_SIZE",
                                 "TX_DATA_SIZE",
                                 "BAUD_RATE",
                                 "BAUD_RATE_NRF",
                                 "PIN_TXD",
                                 "PIN_RXD",
                                 "PIN_CTS",
                                 "PIN_RTS",
                                 "REG",
                                 "USER_RX_BUFFER",
                                 "USER_TX_BUFFER",
                                 "RX_BUFFER_MALLOC",
                                 "START_PTR",
                                 "READ_PTR",
                                 "WRITE_NEXT_BUFFER_PTR",
                                 "WRITE_NEXT_BUFFER_START_PTR",
                                 "WRITE_NEXT_BUFFER_NEXT_PTR",
                                 "START_RX_BYTE_COUNT",
                                 "END_RX_BYTE_COUNT",
                                 "GET_RX_BYTES",
                                 "USER_NEEDS_NOTIFY",
                                 "YIELD",
                                 "X"};
#endif

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

// Get the number of received bytes waiting in the buffer.
static size_t uartGetRxBytes(CellularPortUartData_t *pUartData)
{
    size_t x;

    // Read the amount of received data from the timer/counter
    // on CC channel 0
    pUartData->endRxByteCount = nrfx_timer_capture(&(pUartData->timer), 0);
    UART_DETAILED_LOG(UART_LOG_EVENT_END_RX_BYTE_COUNT, pUartData->endRxByteCount);
    UART_DETAILED_LOG(UART_LOG_EVENT_START_RX_BYTE_COUNT, pUartData->startRxByteCount);
    if (pUartData->endRxByteCount >= pUartData->startRxByteCount) {
        x = pUartData->endRxByteCount - pUartData->startRxByteCount;
    } else {
        // Wrapped
        x = INT_MAX - pUartData->startRxByteCount + pUartData->endRxByteCount;
    }
    if (x > CELLULAR_PORT_UART_RX_BUFFER_SIZE) {
        x = CELLULAR_PORT_UART_RX_BUFFER_SIZE;
    }
    UART_DETAILED_LOG(UART_LOG_EVENT_GET_RX_BYTES, x);

    return x;
}

// Callback to be called when the receive check timer has expired.
// pParameter must be a pointer to CellularPortUartData_t.
static void rxCb(void *pParameter)
{
    CellularPortUartData_t *pUartData = (CellularPortUartData_t *) pParameter;
    size_t x;
    BaseType_t yield = false;

    UART_DETAILED_LOG(UART_LOG_EVENT_INT_TIMER_CALLBACK, pUartData->pReg);
    x = uartGetRxBytes(pUartData);
    // If there is at least some data and the user needs to
    // be notified, let them know
    if ((x > 0) && pUartData->userNeedsNotify) {
        CellularPortUartEventData_t uartSizeOrError;
        uartSizeOrError = x;
        UART_DETAILED_LOG(UART_LOG_EVENT_USER_NEEDS_NOTIFY, uartSizeOrError);
        xQueueSendFromISR((QueueHandle_t) (pUartData->queue),
                          &uartSizeOrError, &yield);
        pUartData->userNeedsNotify = false;
        UART_DETAILED_LOG(UART_LOG_EVENT_YIELD, yield);
    }

    // Required for FreeRTOS task scheduling to work
    if (yield) {
        taskYIELD();
    }
}

// The interrupt handler: only handles Rx events as Tx is blocking.
static void rxIrqHandler(CellularPortUartData_t *pUartData)
{
    NRF_UARTE_Type *pReg = pUartData->pReg;

    if (nrf_uarte_event_check(pReg, NRF_UARTE_EVENT_ENDRX)) {
        UART_DETAILED_LOG(UART_LOG_EVENT_INT_ENDRX, pReg);
        // We arrive here when the DMA has filled a buffer
        // No need to do anything as ENDRX is shorted
        // to start a new RX automagically, just
        // clear the event.
        nrf_uarte_event_clear(pReg, NRF_UARTE_EVENT_ENDRX);
        UART_DETAILED_LOG(UART_LOG_EVENT_DMA_RX_DATA_SIZE,
                          nrf_uarte_rx_amount_get(pReg));
    } else if (nrf_uarte_event_check(pReg, NRF_UARTE_EVENT_RXSTARTED)) {
        // An RX has started so it's OK to update the buffer
        // pointer registers in the hardware for the one that
        // will follow after this one has ended as the
        // Rx buffer register is double-buffered in HW.
        UART_DETAILED_LOG(UART_LOG_EVENT_INT_RXSTARTED, pReg);
        nrf_uarte_event_clear(pReg, NRF_UARTE_EVENT_RXSTARTED);
        UART_DETAILED_LOG(UART_LOG_EVENT_WRITE_NEXT_BUFFER_START_PTR,
                          pUartData->pRxBufferWriteNext->pStart);
        nrf_uarte_rx_buffer_set(pReg,
                                (uint8_t *) (pUartData->pRxBufferWriteNext->pStart),
                                CELLULAR_PORT_UART_SUB_BUFFER_SIZE);
        // Move the write next buffer pointer on
        pUartData->pRxBufferWriteNext = pUartData->pRxBufferWriteNext->pNext;
        UART_DETAILED_LOG(UART_LOG_EVENT_WRITE_NEXT_BUFFER_PTR,
                          pUartData->pRxBufferWriteNext);
    } else if (nrf_uarte_event_check(pReg, NRF_UARTE_EVENT_ERROR)) {
        // Clear any errors: no need to do anything, they
        // have no effect upon reception
        nrf_uarte_event_clear(pReg, NRF_UARTE_EVENT_ERROR);
#ifdef CELLULAR_PORT_UART_DETAILED_DEBUG
        UART_DETAILED_LOG(UART_LOG_EVENT_INT_ERROR, nrf_uarte_errorsrc_get_and_clear(pReg));
#else
        nrf_uarte_errorsrc_get_and_clear(pReg);
#endif
    }
}

// Dummy counter event handler, required by
// nrfx_timer_init().
static void counterEventHandler(nrf_timer_event_t eventType,
                                void *pContext)
{
    (void) eventType;
    (void) pContext;
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

#ifdef CELLULAR_PORT_UART_DETAILED_DEBUG
// What it says.
static void printDetailedDebug()
{
    LogThing_t *pThing = gLog;
    uint64_t tickTimerUSeconds;
    const char *pStr;

    cellularPortLog("------- UART debug begins, %d item(s) --------\n", gLogIndex);
    for (size_t x = 0; x < gLogIndex; x++) {
        // First take the timestamp
        tickTimerUSeconds = pThing->tick;
        // Add any offset
        tickTimerUSeconds += pThing->offset;
        // Convert to microseconds when running at 31.25 kHz, one tick
        // every 32 us, so shift left 5
        tickTimerUSeconds <<= 5;
        if (pThing->tickIsUartMode) {
            // This timer code copied from cellularPortPrivateGetTickTimeMs()
            // The timer is 11 bits wide so each overflow represents
            // ((1 / 31250) * 2048) seconds, 65536 microseconds
            tickTimerUSeconds += ((uint64_t) (pThing->overflowCount)) << 16;
        } else {
            // The timer is 24 bits wide so each overflow represents
            // ((1 / 31250) * (2 ^ 24)) seconds, about very 537 seconds.
            tickTimerUSeconds += ((uint64_t) pThing->overflowCount) * 536870912ULL;
        }
        // Point to the string
        pStr = "Unknown";
        if ((pThing->event >= 0) &&
            (pThing->event <= sizeof(gLogText) / sizeof(gLogText[0]))) {
            pStr = gLogText[pThing->event];
        }
        // Now print it out
        cellularPortLog("%5d (%10d usec): %s (%d) - %d (0x%08x)\n", x, (int32_t) tickTimerUSeconds,
                        pStr, pThing->event, pThing->data, pThing->data);
        pThing++;
    }
    cellularPortLog("-------------- UART debug ends ---------------\n");
}
#endif

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
    char *pRxBuffer = NULL;
    nrfx_timer_config_t timerConfig = NRFX_TIMER_DEFAULT_CONFIG;

#ifdef CELLULAR_PORT_UART_DETAILED_DEBUG
    gLogIndex = 0;
#endif

    UART_DETAILED_LOG(UART_LOG_EVENT_API_INIT_START, uart);
    UART_DETAILED_LOG(UART_LOG_EVENT_BAUD_RATE, baudRate);
    UART_DETAILED_LOG(UART_LOG_EVENT_BAUD_RATE_NRF, baudRateNrf);

    // The RTS threshold is not adjustable on this platform,
    // From the nRF52840 Product Specification, UARTE section:
    // "The RTS signal will be deactivated when the receiver
    // is stopped via the STOPRX task or when the UARTE is
    // only able to receive four more bytes in its internal
    // RX FIFO."
    (void) rtsThreshold;

    if ((pUartQueue != NULL) && (pinRx >= 0) && (pinTx >= 0) &&
        (uart < sizeof(gUartData) / sizeof(gUartData[0])) && 
        (baudRateNrf >= 0)) {
        errorCode = CELLULAR_PORT_SUCCESS;
        if (gUartData[uart].mutex == NULL) {
            pReg = gUartData[uart].pReg;

            UART_DETAILED_LOG(UART_LOG_EVENT_REG, pReg);

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

            // Set up a counter/timer as a counter to count
            // received characters.  This is required because
            // the DMA doesn't let you know until it's done.
            // This is done first because it can return error
            // codes and there's no point in continuing without
            // it.
            timerConfig.mode = NRF_TIMER_MODE_COUNTER;
            // Has to be 32 bit for overflow to work correctly
            timerConfig.bit_width = NRF_TIMER_BIT_WIDTH_32;
            if (nrfx_timer_init(&(gUartData[uart].timer),
                                &timerConfig,
                                counterEventHandler) == NRFX_SUCCESS) {
                // Attach the timer/counter to the RXDRDY event
                // of the UARTE using PPI
                if (nrfx_ppi_channel_alloc(&(gUartData[uart].ppiChannel)) == NRFX_SUCCESS) {
                    if ((nrfx_ppi_channel_assign(gUartData[uart].ppiChannel,
                                                 nrf_uarte_event_address_get(pReg,
                                                                             NRF_UARTE_EVENT_RXDRDY),
                                                 nrfx_timer_task_address_get(&(gUartData[uart].timer),
                                                                             NRF_TIMER_TASK_COUNT)) != NRFX_SUCCESS) ||
                        (nrfx_ppi_channel_enable(gUartData[uart].ppiChannel) != NRFX_SUCCESS)) {
                        nrfx_ppi_channel_free(gUartData[uart].ppiChannel);
                        gUartData[uart].ppiChannel = -1;
                        errorCode = CELLULAR_PORT_PLATFORM_ERROR;
                    }
                } else {
                    errorCode = CELLULAR_PORT_PLATFORM_ERROR;
                }
            } else {
                errorCode = CELLULAR_PORT_PLATFORM_ERROR;
            }

            if (errorCode == 0) {
                // Create the mutex
                errorCode = cellularPortMutexCreate(&(gUartData[uart].mutex));
                if (errorCode == 0) {

                    CELLULAR_PORT_MUTEX_LOCK(gUartData[uart].mutex);

                    UART_DETAILED_LOG(UART_LOG_EVENT_MUTEX_HANDLE, gUartData[uart].mutex);
                    errorCode = CELLULAR_PORT_OUT_OF_MEMORY;

                    // Malloc memory for the read buffer
                    pRxBuffer = pCellularPort_malloc(CELLULAR_PORT_UART_RX_BUFFER_SIZE);
                    if (pRxBuffer != NULL) {
                        UART_DETAILED_LOG(UART_LOG_EVENT_RX_BUFFER_MALLOC,
                                          pRxBuffer);
                        gUartData[uart].pRxStart = pRxBuffer;
                        UART_DETAILED_LOG(UART_LOG_EVENT_START_PTR,
                                          gUartData[uart].pRxStart);
                        // Set up the read pointer
                        gUartData[uart].pRxRead = pRxBuffer;
                        UART_DETAILED_LOG(UART_LOG_EVENT_READ_PTR, gUartData[uart].pRxRead);
                        // Set up the buffer list for the DMA write process
                        for (size_t x = 0; x < sizeof(gUartData[uart].rxBufferList) /
                                               sizeof(gUartData[uart].rxBufferList[0]); x++) {
                            UART_DETAILED_LOG(UART_LOG_EVENT_WRITE_NEXT_BUFFER_PTR,
                                              &(gUartData[uart].rxBufferList[x]));
                            gUartData[uart].rxBufferList[x].pStart = pRxBuffer;
                            UART_DETAILED_LOG(UART_LOG_EVENT_WRITE_NEXT_BUFFER_START_PTR,
                                              gUartData[uart].rxBufferList[x].pStart);
                            // Set up the next pointers in a ring
                            if (x < (sizeof(gUartData[uart].rxBufferList) /
                                     sizeof(gUartData[uart].rxBufferList[0])) - 1) {
                                gUartData[uart].rxBufferList[x].pNext =  &(gUartData[uart].rxBufferList[x + 1]);
                            } else {
                                gUartData[uart].rxBufferList[x].pNext = &(gUartData[uart].rxBufferList[0]);
                            }
                            UART_DETAILED_LOG(UART_LOG_EVENT_WRITE_NEXT_BUFFER_NEXT_PTR,
                                              gUartData[uart].rxBufferList[x].pNext);
                            pRxBuffer += CELLULAR_PORT_UART_SUB_BUFFER_SIZE;
                        }
                        // Set up the write buffer pointer etc.
                        gUartData[uart].pRxBufferWriteNext = &(gUartData[uart].rxBufferList[0]);
                        gUartData[uart].startRxByteCount = 0;
                        UART_DETAILED_LOG(UART_LOG_EVENT_START_RX_BYTE_COUNT, gUartData[uart].startRxByteCount);
                        gUartData[uart].endRxByteCount = 0;
                        UART_DETAILED_LOG(UART_LOG_EVENT_END_RX_BYTE_COUNT, gUartData[uart].endRxByteCount);
                        gUartData[uart].userNeedsNotify = true;
                        UART_DETAILED_LOG(UART_LOG_EVENT_USER_NEEDS_NOTIFY, gUartData[uart].userNeedsNotify);

                        // Create the queue
                        errorCode = cellularPortQueueCreate(CELLULAR_PORT_UART_EVENT_QUEUE_SIZE,
                                                            sizeof(CellularPortUartEventData_t),
                                                            pUartQueue);
                        if (errorCode == 0) {
                            gUartData[uart].queue = *pUartQueue;
                            UART_DETAILED_LOG(UART_LOG_EVENT_QUEUE_HANDLE, gUartData[uart].queue);

                            // Set baud rate
                            nrf_uarte_baudrate_set(pReg, baudRateNrf);

                            UART_DETAILED_LOG(UART_LOG_EVENT_PIN_TXD, pinTx);
                            UART_DETAILED_LOG(UART_LOG_EVENT_PIN_RXD, pinRx);

                            // Set Tx/Rx pins
                            nrf_gpio_pin_set(pinTx);
                            nrf_gpio_cfg_output(pinTx);
                            // You might expect this to be "no pull" since
                            // the line is pulled up by the module already but
                            // the problem is that when the module is powered
                            // off and there's a level translator in the way
                            // the pin floats around and can cause the NRF52840
                            // UART to flooed the input buffers with framing
                            // errors that it is impossible to switch off.
                            //nrf_gpio_cfg_input(pinRx, NRF_GPIO_PIN_PULLUP);
                            nrf_uarte_txrx_pins_set(pReg, pinTx, pinRx);

                            UART_DETAILED_LOG(UART_LOG_EVENT_PIN_CTS, pinCts);
                            UART_DETAILED_LOG(UART_LOG_EVENT_PIN_RTS, pinRts);

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
                            nrf_uarte_event_clear(pReg, NRF_UARTE_EVENT_RXSTARTED);
                            nrf_uarte_event_clear(pReg, NRF_UARTE_EVENT_TXSTOPPED);

                            // Let the end of one RX trigger the next immediately
                            nrf_uarte_shorts_enable(pReg, NRF_UARTE_SHORT_ENDRX_STARTRX);

                            // Enable and clear the counter/timer that is counting
                            // received characters.
                            nrfx_timer_enable(&(gUartData[uart].timer));
                            nrfx_timer_clear(&(gUartData[uart].timer));

                            // Off we go
                            nrf_uarte_rx_buffer_set(pReg,
                                                    (uint8_t *) (gUartData[uart].pRxBufferWriteNext->pStart),
                                                    CELLULAR_PORT_UART_SUB_BUFFER_SIZE);
                            gUartData[uart].pRxBufferWriteNext = gUartData[uart].pRxBufferWriteNext->pNext;
                            nrf_uarte_task_trigger(pReg, NRF_UARTE_TASK_STARTRX);
                            nrf_uarte_int_enable(pReg, NRF_UARTE_INT_ENDRX_MASK     |
                                                       NRF_UARTE_INT_ERROR_MASK     |
                                                       NRF_UARTE_INT_RXSTARTED_MASK);
                            NRFX_IRQ_PRIORITY_SET(getIrqNumber((void *) pReg),
                                                  NRFX_UARTE_DEFAULT_CONFIG_IRQ_PRIORITY);
                            NRFX_IRQ_ENABLE(getIrqNumber((void *) (pReg)));

                            // Put the tick timer into UART mode and
                            // register the receive timout callback
                            cellularPortPrivateTickTimeUartMode();
                            cellularPortPrivateTickTimeSetInterruptCb(rxCb,
                                                                      &(gUartData[uart]));
                        }
                    }

                    CELLULAR_PORT_MUTEX_UNLOCK(gUartData[uart].mutex);

                    // If we failed to create the queue or get memory for the buffer,
                    // delete the mutex, free memory, put the uart's
                    // mutex back to NULL and disable the counter/timer, freeing
                    // the PPI channel if it was allocated.
                    if ((errorCode != 0) ||
                        (pRxBuffer == NULL)) {
                        cellularPortMutexDelete(gUartData[uart].mutex);
                        gUartData[uart].mutex = NULL;
                        cellularPort_free(pRxBuffer);
                        nrfx_timer_disable(&(gUartData[uart].timer));
                        nrfx_timer_uninit(&(gUartData[uart].timer));
                        if (gUartData[uart].ppiChannel != -1) {
                            nrfx_ppi_channel_disable(gUartData[uart].ppiChannel);
                            nrfx_ppi_channel_free(gUartData[uart].ppiChannel);
                            gUartData[uart].ppiChannel = -1;
                        }
                    }
                }
            }
        }
    }

    UART_DETAILED_LOG(UART_LOG_EVENT_API_INIT_END, errorCode);

    return (int32_t) errorCode;
}

// Shutdown a UART.
int32_t cellularPortUartDeinit(int32_t uart)
{
    CellularPortErrorCode_t errorCode = CELLULAR_PORT_INVALID_PARAMETER;
    uint32_t pinRtsNrf;
    uint32_t pinCtsNrf;
    NRF_UARTE_Type *pReg;

    UART_DETAILED_LOG(UART_LOG_EVENT_API_DEINIT_START, uart);

    if (uart < sizeof(gUartData) / sizeof(gUartData[0])) {
        errorCode = CELLULAR_PORT_SUCCESS;
        if (gUartData[uart].mutex != NULL) {
            errorCode = CELLULAR_PORT_PLATFORM_ERROR;
            pReg = gUartData[uart].pReg;

            UART_DETAILED_LOG(UART_LOG_EVENT_REG, pReg);

            // No need to lock the mutex, we need to delete it
            // and we're not allowed to delete a locked mutex.
            // The caller needs to make sure that no read/write
            // is in progress when this function is called.

            // Disable the counter/timer and associated PPI
            // channel.
            nrfx_timer_disable(&(gUartData[uart].timer));
            nrfx_timer_uninit(&(gUartData[uart].timer));
            nrfx_ppi_channel_disable(gUartData[uart].ppiChannel);
            nrfx_ppi_channel_free(gUartData[uart].ppiChannel);
            gUartData[uart].ppiChannel = -1;

            // Disable Rx interrupts
            nrf_uarte_int_disable(pReg, NRF_UARTE_INT_ENDRX_MASK     |
                                        NRF_UARTE_INT_ERROR_MASK     |
                                        NRF_UARTE_INT_RXSTARTED_MASK);
            NRFX_IRQ_DISABLE(nrfx_get_irq_number((void *) (pReg)));

            // Deregister the timer callback and 
            // return the tick timer to normal mode
            cellularPortPrivateTickTimeSetInterruptCb(NULL, NULL);
            cellularPortPrivateTickTimeNormalMode();

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

            UART_DETAILED_LOG(UART_LOG_EVENT_QUEUE_HANDLE,
                              gUartData[uart].queue);
            UART_DETAILED_LOG(UART_LOG_EVENT_MUTEX_HANDLE,
                              gUartData[uart].mutex);
            UART_DETAILED_LOG(UART_LOG_EVENT_START_PTR,
                              gUartData[uart].pRxStart);

            // Delete the queue
            cellularPortQueueDelete(gUartData[uart].queue);
            gUartData[uart].queue = NULL;
            // Free the buffer
            cellularPort_free(gUartData[uart].pRxStart);
            // Delete the mutex
            cellularPortMutexDelete(gUartData[uart].mutex);
            gUartData[uart].mutex = NULL;
            errorCode = CELLULAR_PORT_SUCCESS;
        }
    }

    UART_DETAILED_LOG(UART_LOG_EVENT_API_DEINIT_END, errorCode);

#ifdef CELLULAR_PORT_UART_DETAILED_DEBUG
    printDetailedDebug();
#endif

    return (int32_t) errorCode;
}

// Push a UART event onto the UART event queue.
int32_t cellularPortUartEventSend(const CellularPortQueueHandle_t queueHandle,
                                  int32_t sizeBytesOrError)
{
    int32_t errorCode = CELLULAR_PORT_INVALID_PARAMETER;
    CellularPortUartEventData_t uartSizeOrError;

    UART_DETAILED_LOG(UART_LOG_EVENT_API_EVENT_SEND_START, sizeBytesOrError);
    UART_DETAILED_LOG(UART_LOG_EVENT_QUEUE_HANDLE, queueHandle);

    if (queueHandle != NULL) {
        uartSizeOrError = sizeBytesOrError;
        errorCode = cellularPortQueueSend(queueHandle, (void *) &uartSizeOrError);
    }

    UART_DETAILED_LOG(UART_LOG_EVENT_API_EVENT_SEND_END, errorCode);

    return errorCode;
}

// Receive a UART event, blocking until one turns up.
int32_t cellularPortUartEventReceive(const CellularPortQueueHandle_t queueHandle)
{
    int32_t sizeOrErrorCode = CELLULAR_PORT_INVALID_PARAMETER;
    CellularPortUartEventData_t uartSizeOrError;

    UART_DETAILED_LOG(UART_LOG_EVENT_API_EVENT_RECEIVE_START, queueHandle);

    if (queueHandle != NULL) {
        sizeOrErrorCode = CELLULAR_PORT_PLATFORM_ERROR;
        if (cellularPortQueueReceive(queueHandle, &uartSizeOrError) == 0) {
            sizeOrErrorCode = uartSizeOrError;
        }
    }

    UART_DETAILED_LOG(UART_LOG_EVENT_API_EVENT_RECEIVE_END, sizeOrErrorCode);

    return sizeOrErrorCode;
}

// Receive a UART event with a timeout.
int32_t cellularPortUartEventTryReceive(const CellularPortQueueHandle_t queueHandle,
                                        int32_t waitMs)
{
    int32_t sizeOrErrorCode = CELLULAR_PORT_INVALID_PARAMETER;
    CellularPortUartEventData_t uartSizeOrError;

    UART_DETAILED_LOG(UART_LOG_EVENT_API_EVENT_TRY_RECEIVE_START, queueHandle);

    if (queueHandle != NULL) {
        sizeOrErrorCode = CELLULAR_PORT_TIMEOUT;
        if (cellularPortQueueTryReceive(queueHandle, waitMs, &uartSizeOrError) == 0) {
            sizeOrErrorCode = uartSizeOrError;
        }
    }

    UART_DETAILED_LOG(UART_LOG_EVENT_API_EVENT_TRY_RECEIVE_END, sizeOrErrorCode);

    return sizeOrErrorCode;
}

// Get the number of bytes waiting in the receive buffer.
int32_t cellularPortUartGetReceiveSize(int32_t uart)
{
    CellularPortErrorCode_t sizeOrErrorCode = CELLULAR_PORT_INVALID_PARAMETER;

    UART_DETAILED_LOG(UART_LOG_EVENT_API_GET_RECEIVE_SIZE_START, uart);

    if (uart < sizeof(gUartData) / sizeof(gUartData[0])) {
        sizeOrErrorCode = CELLULAR_PORT_NOT_INITIALISED;
        if (gUartData[uart].mutex != NULL) {

            CELLULAR_PORT_MUTEX_LOCK(gUartData[uart].mutex);

            sizeOrErrorCode = uartGetRxBytes(&(gUartData[uart]));
            UART_DETAILED_LOG(UART_LOG_EVENT_RX_DATA_SIZE, sizeOrErrorCode);
            if (sizeOrErrorCode == 0) {
                gUartData[uart].userNeedsNotify = true;
                UART_DETAILED_LOG(UART_LOG_EVENT_USER_NEEDS_NOTIFY,
                                  gUartData[uart].userNeedsNotify);
            }

            CELLULAR_PORT_MUTEX_UNLOCK(gUartData[uart].mutex);

        }
    }

    UART_DETAILED_LOG(UART_LOG_EVENT_API_GET_RECEIVE_SIZE_END, sizeOrErrorCode);

    return (int32_t) sizeOrErrorCode;
}

// Read from the given UART interface.
int32_t cellularPortUartRead(int32_t uart, char *pBuffer,
                             size_t sizeBytes)
{
    CellularPortErrorCode_t sizeOrErrorCode = CELLULAR_PORT_INVALID_PARAMETER;
    size_t totalRead;
    size_t thisRead;

    UART_DETAILED_LOG(UART_LOG_EVENT_API_READ_START, uart);
    UART_DETAILED_LOG(UART_LOG_EVENT_USER_RX_BUFFER, sizeBytes);

    if ((pBuffer != NULL) && (sizeBytes > 0) &&
        (uart < sizeof(gUartData) / sizeof(gUartData[0]))) {
        sizeOrErrorCode = CELLULAR_PORT_NOT_INITIALISED;
        if (gUartData[uart].mutex != NULL) {

            CELLULAR_PORT_MUTEX_LOCK(gUartData[uart].mutex);

            // The user can't read more than 
            // CELLULAR_PORT_UART_RX_BUFFER_SIZE
            if (sizeBytes > CELLULAR_PORT_UART_RX_BUFFER_SIZE) {
                sizeBytes = CELLULAR_PORT_UART_RX_BUFFER_SIZE;
            }

            // Get the number of bytes available to read
            totalRead = uartGetRxBytes(&(gUartData[uart]));
            if (totalRead > sizeBytes) {
                totalRead = sizeBytes;
            }

            // Copy out from the read pointer onwards,
            // stopping at the end of the buffer or
            // totalRead, whichever comes first
            thisRead = gUartData[uart].pRxStart +
                       CELLULAR_PORT_UART_RX_BUFFER_SIZE
                       - gUartData[uart].pRxRead;
            if (thisRead > totalRead) {
                thisRead = totalRead;
            }
            UART_DETAILED_LOG(UART_LOG_EVENT_READ_PTR,
                              gUartData[uart].pRxRead);
            UART_DETAILED_LOG(UART_LOG_EVENT_RX_DATA_SIZE, thisRead);
            pCellularPort_memcpy(pBuffer,
                                 gUartData[uart].pRxRead,
                                 thisRead);
            gUartData[uart].pRxRead += thisRead;
            if (gUartData[uart].pRxRead >= gUartData[uart].pRxStart +
                                          CELLULAR_PORT_UART_RX_BUFFER_SIZE) {
                gUartData[uart].pRxRead = gUartData[uart].pRxStart;
            }

            // Copy out any remainder
            if (thisRead < totalRead) {
                thisRead = totalRead - thisRead;
                UART_DETAILED_LOG(UART_LOG_EVENT_READ_PTR,
                                  gUartData[uart].pRxRead);
                UART_DETAILED_LOG(UART_LOG_EVENT_RX_DATA_SIZE, thisRead);
                pCellularPort_memcpy(pBuffer,
                                     gUartData[uart].pRxRead,
                                     thisRead);
                gUartData[uart].pRxRead += thisRead;
            }

            // Update the starting number for the byte count
            gUartData[uart].startRxByteCount += totalRead;
            UART_DETAILED_LOG(UART_LOG_EVENT_START_RX_BYTE_COUNT,
                              gUartData[uart].startRxByteCount);

            // Set the return value
            sizeOrErrorCode = totalRead;
            UART_DETAILED_LOG(UART_LOG_EVENT_RX_DATA_SIZE, sizeOrErrorCode);

            // Set the notify flag if we were unable
            // to read anything
            gUartData[uart].userNeedsNotify = false;
            if (sizeOrErrorCode == 0) {
                gUartData[uart].userNeedsNotify = true;
                UART_DETAILED_LOG(UART_LOG_EVENT_USER_NEEDS_NOTIFY,
                                  gUartData[uart].userNeedsNotify);
            }

            CELLULAR_PORT_MUTEX_UNLOCK(gUartData[uart].mutex);
        }
    }

    UART_DETAILED_LOG(UART_LOG_EVENT_API_READ_END, sizeOrErrorCode);

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

    UART_DETAILED_LOG(UART_LOG_EVENT_API_WRITE_START, uart);

    if ((pBuffer != NULL) &&
        (uart < sizeof(gUartData) / sizeof(gUartData[0]))) {
        sizeOrErrorCode = CELLULAR_PORT_NOT_INITIALISED;
        if (gUartData[uart].mutex != NULL) {

            CELLULAR_PORT_MUTEX_LOCK(gUartData[uart].mutex);

            sizeOrErrorCode = CELLULAR_PORT_OUT_OF_MEMORY;
            pReg = gUartData[uart].pReg;

            UART_DETAILED_LOG(UART_LOG_EVENT_REG, pReg);

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
                UART_DETAILED_LOG(UART_LOG_EVENT_USER_TX_BUFFER, pTxBuffer);
                UART_DETAILED_LOG(UART_LOG_EVENT_TX_DATA_SIZE, sizeBytes);
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

    UART_DETAILED_LOG(UART_LOG_EVENT_API_WRITE_END, sizeOrErrorCode);

    return (int32_t) sizeOrErrorCode;
}

// Determine if RTS flow control is enabled.
bool cellularPortIsRtsFlowControlEnabled(int32_t uart)
{
    bool rtsFlowControlIsEnabled = false;
    NRF_UARTE_Type *pReg;

    UART_DETAILED_LOG(UART_LOG_EVENT_API_IS_RTS_FLOW_CONTROL_ENABLED_START, uart);

    if ((uart < sizeof(gUartData) / sizeof(gUartData[0])) &&
        (gUartData[uart].mutex != NULL)) {

        CELLULAR_PORT_MUTEX_LOCK(gUartData[uart].mutex);

        pReg = gUartData[uart].pReg;

        UART_DETAILED_LOG(UART_LOG_EVENT_REG, pReg);

        if (nrf_uarte_rts_pin_get(pReg) != NRF_UARTE_PSEL_DISCONNECTED) {
            rtsFlowControlIsEnabled = true;
        }

        CELLULAR_PORT_MUTEX_UNLOCK(gUartData[uart].mutex);
    }

    UART_DETAILED_LOG(UART_LOG_EVENT_API_IS_RTS_FLOW_CONTROL_ENABLED_END,
                      rtsFlowControlIsEnabled);

    return rtsFlowControlIsEnabled;
}

// Determine if CTS flow control is enabled.
bool cellularPortIsCtsFlowControlEnabled(int32_t uart)
{
    bool ctsFlowControlIsEnabled = false;
    NRF_UARTE_Type *pReg;

    UART_DETAILED_LOG(UART_LOG_EVENT_API_IS_CTS_FLOW_CONTROL_ENABLED_START, uart);

    if ((uart < sizeof(gUartData) / sizeof(gUartData[0])) &&
        (gUartData[uart].mutex != NULL)) {

        CELLULAR_PORT_MUTEX_LOCK(gUartData[uart].mutex);

        pReg = gUartData[uart].pReg;

        UART_DETAILED_LOG(UART_LOG_EVENT_REG, pReg);

        if (nrf_uarte_cts_pin_get(pReg) != NRF_UARTE_PSEL_DISCONNECTED) {
            ctsFlowControlIsEnabled = true;
        }

        CELLULAR_PORT_MUTEX_UNLOCK(gUartData[uart].mutex);
    }

    UART_DETAILED_LOG(UART_LOG_EVENT_API_IS_CTS_FLOW_CONTROL_ENABLED_END,
                      ctsFlowControlIsEnabled);

    return ctsFlowControlIsEnabled;
}

// End of file
