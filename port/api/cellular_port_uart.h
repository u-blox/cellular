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
 
#ifndef _CELLULAR_PORT_UART_H_
#define _CELLULAR_PORT_UART_H_

/** Porting layer for UART access functions.  These functions
 * are threadsafe.
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

#ifndef CELLULAR_PORT_UART_RX_BUFFER_SIZE
/** The size of ring buffer to use for receive.
 */
# define CELLULAR_PORT_UART_RX_BUFFER_SIZE 1024
#endif

#ifndef CELLULAR_PORT_UART_TX_BUFFER_SIZE
/** The size of ring buffer to use for transmit.
 * 0 means blocking.
 */
# define CELLULAR_PORT_UART_TX_BUFFER_SIZE 0
#endif

#ifndef CELLULAR_PORT_UART_EVENT_QUEUE_SIZE
/** The event queue size.
 */
# define CELLULAR_PORT_UART_EVENT_QUEUE_SIZE 20
#endif

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/** Structure to hold a UART event.
 */
typedef struct {
    int32_t eventType;
    size_t size;
} CellularPortUartEventData_t;

/* ----------------------------------------------------------------
 * FUNCTIONS
 * -------------------------------------------------------------- */

/** Initialise a UART.  If the UART has already been initialised
 * this function just returns.
 *
 * @param pinTx           the transmit (output) pin.
 * @param pinRx           the receive (input) pin.
 * @param pinCts          the CTS (input) flow control pin, asserted
 *                        by the modem when it is ready to receive
 *                        data; use -1 for none.
 * @param pinRts          the RTS (output) flow control pin, asserted
 *                        when we are ready to receive data from the
 *                        modem; use -1 for none.
 * @param baudRate        the baud rate to use.
 * @param rtsThreshold    the buffer length at which pinRts is
 *                        de-asserted.  Ignored if pinRts is -1.
 * @param uart            the UART number to use.
 * @param pUartQueue      a place to put the UART event queue.
 * @return                0 on success, otherwise negative error code.
 */
int32_t cellularPortUartInit(int32_t pinTx, int32_t pinRx,
                             int32_t pinCts, int32_t pinRts,
                             int32_t baudRate,
                             size_t rtsThreshold,
                             int32_t uart,
                             CellularPortQueueHandle_t *pUartQueue);

/** Shutdown a UART.  Note that this should NOT be called if a UART
 * read or write might be in progress.
 *
 * @param uart the UART number to shut down.
 * @return     0 on success, otherwise negative error code.
 */
int32_t cellularPortUartDeinit(int32_t uart);

/** Send a data event to the UART event queue.  This is not
 * normally required, it is done by the UART driver, however
 * there are occasions when a receive event is handled but
 * the data is then only partially read.  This can be used to
 * generate a new event so that the remaining data can be
 * processed naturally by the receive thread.
 * Also, a negative value of send data length will cause
 * an invalid receive event to be sent which can be detected
 * by the receive thread and used to shut it down cleanly.
 *
 * @param queueHandle the handle for the UART event queue.
 * @param sizeBytes   the number of bytes of received data
 *                    to be signalled
 * @return            zero on success else negative error code.
 */
int32_t cellularPortUartEventSend(const CellularPortQueueHandle_t queueHandle,
                                  int32_t sizeBytes);

/** Receive a UART event, blocking until one turns up.
 *
 * @param queueHandle the handle for the UART event queue.
 * @return            if the event was a receive event then
 *                    the length of the data received by the`
 *                    UART, else a negative number.
 */
int32_t cellularPortUartEventReceive(const CellularPortQueueHandle_t queueHandle);

/** Get the number of bytes waiting in the receive buffer.
 *
 * @param uart      the UART number to use.
 * @return          the number of bytes in the receive buffer
 *                  or negative error code.
 */
int32_t cellularPortUartGetReceiveSize(int32_t uart);

/** Read from the given UART interface.  Any characters
 * already available will be returned; no waiting around.
 *
 * @param uart      the UART number to use.
 * @param pBuffer   a pointer to a buffer in which to store
 *                  received bytes.
 * @param sizeBytes the size of buffer pointed to by pBuffer.
 * @return          the number of bytes received or negative
 *                  error code.
 */
int32_t cellularPortUartRead(int32_t uart, char *pBuffer,
                             size_t sizeBytes);

/** Write to the given UART interface.  The function will
 * block until all the data has been written.
 *
 * @param uart      the UART number to use.
 * @param pBuffer   a pointer to a buffer of data to send.
 * @param sizeBytes the number of bytes in pBuffer.
 * @return          the number of bytes sent or negative
 *                  error code.
 */
int32_t cellularPortUartWrite(int32_t uart,
                              const char *pBuffer,
                              size_t sizeBytes);

#endif // _CELLULAR_PORT_UART_H_

// End of file
