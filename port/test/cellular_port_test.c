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

/* Only #includes of cellular_* are allowed here, no C lib,
 * no platform stuff and no OS stuff.  Anything required from
 * the platform/C library/OS must be brought in through
 * cellular_port* to maintain portability.
 */

#include "cellular_cfg_hw_platform_specific.h"
#include "cellular_cfg_sw.h"
#include "cellular_cfg_module.h"
#include "cellular_port_clib.h"
#include "cellular_port.h"
#include "cellular_port_debug.h"
#include "cellular_port_os.h"
#include "cellular_port_gpio.h"
#include "cellular_port_uart.h"
#include "cellular_port_test_platform_specific.h"

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// The queue length to create.
#define CELLULAR_PORT_TEST_QUEUE_LENGTH 20

// The size of each item on the queue.
#define CELLULAR_PORT_TEST_QUEUE_ITEM_SIZE sizeof(int32_t)

// The guard time for the OS test.
#define CELLULAR_PORT_TEST_OS_GUARD_DURATION_MS 4200

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

// Type to hold the stuff that the UART test task needs to know about
typedef struct {
    CellularPortMutexHandle_t runningMutexHandle;
    CellularPortQueueHandle_t uartQueueHandle;
    CellularPortQueueHandle_t controlQueueHandle;
} UartTestTaskData_t;

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// OS test mutex handle.
static CellularPortMutexHandle_t gMutexHandle = NULL;

// OS test queue handle.
static CellularPortQueueHandle_t gQueueHandle = NULL;

// OS test task handle.
static CellularPortTaskHandle_t gTaskHandle = NULL;

// OS task parameter.
static const char *gpTaskParameter = "Boo!";

// Stuff to send to OS test task, must all be positive numbers.
static int32_t gStuffToSend[] = {0, 100, 25, 3};

#if (CELLULAR_PORT_TEST_PIN_UART_TXD >= 0) && (CELLULAR_PORT_TEST_PIN_UART_RXD >= 0)

// The number of bytes correctly received during UART testing.
static int32_t gUartBytesReceived = 0;

// The data to send during UART testing.
static const char gUartTestData[] =  "_____0000:0123456789012345678901234567890123456789"
                                     "01234567890123456789012345678901234567890123456789"
                                     "_____0100:0123456789012345678901234567890123456789"
                                     "01234567890123456789012345678901234567890123456789"
                                     "_____0200:0123456789012345678901234567890123456789"
                                     "01234567890123456789012345678901234567890123456789"
                                     "_____0300:0123456789012345678901234567890123456789"
                                     "01234567890123456789012345678901234567890123456789"
                                     "_____0400:0123456789012345678901234567890123456789"
                                     "01234567890123456789012345678901234567890123456789"
                                     "_____0500:0123456789012345678901234567890123456789"
                                     "01234567890123456789012345678901234567890123456789"
                                     "_____0600:0123456789012345678901234567890123456789"
                                     "01234567890123456789012345678901234567890123456789"
                                     "_____0700:0123456789012345678901234567890123456789"
                                     "01234567890123456789012345678901234567890123456789"
                                     "_____0800:0123456789012345678901234567890123456789"
                                     "01234567890123456789012345678901234567890123456789"
                                     "_____0900:0123456789012345678901234567890123456789"
                                     "01234567890123456789012345678901234567890123456789";

// A buffer to receive UART data into
static char gUartBuffer[1024];

#endif // (CELLULAR_PORT_TEST_PIN_UART_TXD >= 0) && (CELLULAR_PORT_TEST_PIN_UART_RXD >= 0)

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

// The test task for OS stuff.
static void osTestTask(void *pParameters)
{
    int32_t queueItem = 0;
    int32_t index = 0;

    // Pause here to let the task that spawned this one
    // run otherwise gTaskHandle won't have been populated.
    cellularPortTaskBlock(10);

    cellularPortLog("CELLULAR_PORT_TEST_TASK: task with handle 0x%08x started, received parameter pointer 0x%08x containing string \"%s\".\n",
                    (int) gTaskHandle, pParameters, (const char *) pParameters);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_strcmp(pParameters,
                                                  gpTaskParameter) == 0);

    CELLULAR_PORT_TEST_ASSERT(cellularPortTaskIsThis(gTaskHandle));

    cellularPortLog("CELLULAR_PORT_TEST_TASK: task trying to lock the mutex.\n");
    CELLULAR_PORT_TEST_ASSERT(gMutexHandle != NULL);
    CELLULAR_PORT_TEST_ASSERT(cellularPortMutexTryLock(gMutexHandle, 10) == 0);
    cellularPortLog("CELLULAR_PORT_TEST_TASK: unlocking it again.\n");
    CELLULAR_PORT_TEST_ASSERT(cellularPortMutexUnlock(gMutexHandle) == 0);

    cellularPortLog("CELLULAR_PORT_TEST_TASK: locking it again (non-try version).\n");
    CELLULAR_PORT_MUTEX_LOCK(gMutexHandle);

    CELLULAR_PORT_TEST_ASSERT(gQueueHandle != NULL);
    cellularPortLog("CELLULAR_PORT_TEST_TASK: task waiting on queue.\n");
    while (queueItem >= 0) {
        CELLULAR_PORT_TEST_ASSERT(cellularPortQueueReceive(gQueueHandle, &queueItem) == 0);
        cellularPortLog("CELLULAR_PORT_TEST_TASK: task received %d.\n",
                        queueItem);
        if (queueItem >= 0) {
            cellularPortLog("                         item %d, expecting %d.\n",
                            index + 1, gStuffToSend[index]);
            CELLULAR_PORT_TEST_ASSERT(gStuffToSend[index] == queueItem);
            index++;
        } 
    }

    cellularPortLog("CELLULAR_PORT_TEST_TASK: task exiting, unlocking mutex.\n");
    CELLULAR_PORT_MUTEX_UNLOCK(gMutexHandle);

    cellularPortLog("CELLULAR_PORT_TEST_TASK: task deleting itself.\n");
    CELLULAR_PORT_TEST_ASSERT(cellularPortTaskDelete(NULL) == 0);
}

// Function to send stuff to a queue.
static int32_t sendToQueue(CellularPortQueueHandle_t gQueueHandle,
                           int32_t thing)
{
    return cellularPortQueueSend(gQueueHandle, &thing);
}

#if (CELLULAR_PORT_TEST_PIN_UART_TXD >= 0) && (CELLULAR_PORT_TEST_PIN_UART_RXD >= 0)

// The test task for UART stuff.
static void uartTestTask(void *pParameters)
{
    UartTestTaskData_t *pUartTaskData = (UartTestTaskData_t *) pParameters;
    int32_t control = 0;
    int32_t receiveSize;
    int32_t dataSize;
    int32_t blockNumber = 0;
    int32_t indexInBlock = 0;
    char *pReceive = gUartBuffer;

    CELLULAR_PORT_MUTEX_LOCK(pUartTaskData->runningMutexHandle);

    cellularPortLog("CELLULAR_PORT_UART_TEST_TASK: task started.\n");

    // Run until the control queue sends us a non-zero thing
    // or we spot an error
    while (control == 0) {
        // Wait for notification of UART data
        dataSize = cellularPortUartEventTryReceive(pUartTaskData->uartQueueHandle,
                                                   1000);
        // Note: can't assert on cellularPortUartGetReceiveSize()
        // being larger than or equal to dataSize since dataSize
        // might be an old queued value.
        while (dataSize > 0) {
            receiveSize = cellularPortUartGetReceiveSize(CELLULAR_PORT_TEST_UART);
            dataSize = cellularPortUartRead(CELLULAR_PORT_TEST_UART,
                                            pReceive,
                                            gUartBuffer + sizeof(gUartBuffer) - pReceive);
            // dataSize will be smaller than receiveSize
            // if our data buffer is smaller than the UART receive
            // buffer but something might also have been received
            // between the two calls, making it larger.  Just
            // can't easily check cellularPortUartGetReceiveSize()
            // for accuracy, so instead do a range check here
            CELLULAR_PORT_TEST_ASSERT(receiveSize >= 0);
            CELLULAR_PORT_TEST_ASSERT(receiveSize < CELLULAR_PORT_UART_RX_BUFFER_SIZE);
            // Compare the data with the expected data
            for (size_t x = 0; x < dataSize; x++) {
                if (gUartTestData[indexInBlock] == *pReceive) {
                    gUartBytesReceived++;
                    indexInBlock++;
                    // -1 below to omit gUartTestData string terminator
                    if (indexInBlock >= sizeof(gUartTestData) - 1) {
                        indexInBlock = 0;
                        blockNumber++;
                    }
                    pReceive++;
                    if (pReceive >= gUartBuffer + sizeof(gUartBuffer)) {
                        pReceive = gUartBuffer;
                    }
                } else {
                    control = -2;
                }
            }
        }

        // Check if there's anything for us on the control queue
        cellularPortQueueTryReceive(pUartTaskData->controlQueueHandle,
                                    10, (void *) &control);
    }

    if (control == -2) {
        cellularPortLog("CELLULAR_PORT_UART_TEST_TASK: error after %d character(s), %d block(s).\n",
                        gUartBytesReceived, blockNumber);
        cellularPortLog("CELLULAR_PORT_UART_TEST_TASK: expected %c (0x%02x), received %c (0x%02x).\n",
                        gUartTestData[indexInBlock], gUartTestData[indexInBlock],
                        *pReceive, *pReceive);
    } else {
        cellularPortLog("CELLULAR_PORT_UART_TEST_TASK: %d character(s), %d block(s) received.\n",
                        gUartBytesReceived, blockNumber);
    }

    CELLULAR_PORT_MUTEX_UNLOCK(pUartTaskData->runningMutexHandle);

    cellularPortLog("CELLULAR_PORT_UART_TEST_TASK: task ended.\n");

    CELLULAR_PORT_TEST_ASSERT(cellularPortTaskDelete(NULL) == 0);
}

// Run a UART test at the given baud rate and with/without flow control.
static void runUartTest(int32_t size, int32_t speed, bool flowControlOn)
{
    UartTestTaskData_t uartTestTaskData;
    CellularPortTaskHandle_t uartTaskHandle;
    int32_t control;
    int32_t bytesSent = 0;
    int32_t pinCts = -1;
    int32_t pinRts = -1;
    CellularPortGpioConfig_t gpioConfig = CELLULAR_PORT_GPIO_CONFIG_DEFAULT;

    gUartBytesReceived = 0;

    if (flowControlOn) {
        pinCts = CELLULAR_PORT_TEST_PIN_UART_CTS;
        pinRts = CELLULAR_PORT_TEST_PIN_UART_RTS;
    } else {
        // If we want to test with flow control off
        // but the flow control pins are actually
        // connected then they need to be set
        // to "get on with it"
#if (CELLULAR_PORT_TEST_PIN_UART_CTS >= 0)
            // Make it an output pin and low
            CELLULAR_PORT_TEST_ASSERT(cellularPortGpioSet(CELLULAR_PORT_TEST_PIN_UART_CTS, 0) == 0);
            gpioConfig.pin = CELLULAR_PORT_TEST_PIN_UART_CTS;
            gpioConfig.direction = CELLULAR_PORT_GPIO_DIRECTION_OUTPUT;
            CELLULAR_PORT_TEST_ASSERT(cellularPortGpioConfig(&gpioConfig) == 0);
            cellularPortTaskBlock(1);
#endif
#if (CELLULAR_PORT_TEST_PIN_UART_RTS >= 0)
            // Make it an output pin and low
            CELLULAR_PORT_TEST_ASSERT(cellularPortGpioSet(CELLULAR_PORT_TEST_PIN_UART_RTS, 0) == 0);
            gpioConfig.pin = CELLULAR_PORT_TEST_PIN_UART_RTS;
            gpioConfig.direction = CELLULAR_PORT_GPIO_DIRECTION_OUTPUT;
            CELLULAR_PORT_TEST_ASSERT(cellularPortGpioConfig(&gpioConfig) == 0);
            cellularPortTaskBlock(1);
#endif
    }

    cellularPortLog("CELLULAR_PORT_TEST: testing UART loop-back, %d byte(s) at %d bits/s with flow control %s.\n",
                    size, speed, ((pinCts >= 0) && (pinRts >= 0)) ? "on" : "off");

    CELLULAR_PORT_TEST_ASSERT(cellularPortUartInit(CELLULAR_PORT_TEST_PIN_UART_TXD,
                                                   CELLULAR_PORT_TEST_PIN_UART_RXD,
                                                   pinCts, pinRts,
                                                   speed,
                                                   CELLULAR_PORT_TEST_UART_RTS_THRESHOLD,
                                                   CELLULAR_PORT_TEST_UART,
                                                   &(uartTestTaskData.uartQueueHandle)) == 0);

    cellularPortLog("CELLULAR_PORT_TEST: creating OS items to test UART...\n");

    // Create a mutex so that we can tell if the UART receive task is running
    CELLULAR_PORT_TEST_ASSERT(cellularPortMutexCreate(&(uartTestTaskData.runningMutexHandle)) == 0);

    // Start a queue to control the UART receive task;
    // will send it -1 to exit
    CELLULAR_PORT_TEST_ASSERT(cellularPortQueueCreate(5, sizeof(int32_t),
                                                      &(uartTestTaskData.controlQueueHandle)) == 0);

    // Start the UART receive task
    CELLULAR_PORT_TEST_ASSERT(cellularPortTaskCreate(uartTestTask, "uartTestTask",
                                                     CELLULAR_PORT_TEST_OS_TASK_STACK_SIZE_BYTES,
                                                     (void *) &uartTestTaskData,
                                                     CELLULAR_PORT_TEST_OS_TASK_PRIORITY,
                                                     &uartTaskHandle) == 0);
    // Pause here to allow the task to start.
    cellularPortTaskBlock(100);

    // Send data over the UART N times, Rx task will check it
    while (bytesSent < size) {
        // -1 below to omit gUartTestData string terminator
        CELLULAR_PORT_TEST_ASSERT(cellularPortUartWrite(CELLULAR_PORT_TEST_UART,
                                                        gUartTestData,
                                                        sizeof(gUartTestData) - 1) == sizeof(gUartTestData) - 1);
        bytesSent += sizeof(gUartTestData) - 1;
        cellularPortLog("CELLULAR_PORT_TEST: %d byte(s) sent.\n", bytesSent);
    }

    // Wait long enough for everything to have been received
    cellularPortTaskBlock(1000);
    cellularPortLog("CELLULAR_PORT_TEST: at end of test %d byte(s) sent, %d byte(s) received.\n",
                    bytesSent, gUartBytesReceived);
    CELLULAR_PORT_TEST_ASSERT(gUartBytesReceived == bytesSent);

    cellularPortLog("CELLULAR_PORT_TEST: tidying up after UART test...\n");

    // Tell the UART Rx task to exit
    control = -1;
    CELLULAR_PORT_TEST_ASSERT(cellularPortQueueSend(uartTestTaskData.controlQueueHandle,
                                                    (void *) &control) == 0);
    // Wait for it to exit
    CELLULAR_PORT_MUTEX_LOCK(uartTestTaskData.runningMutexHandle);
    CELLULAR_PORT_MUTEX_UNLOCK(uartTestTaskData.runningMutexHandle);
    // Pause to allow it to be destroyed in the idle task
    cellularPortTaskBlock(100);

    // Tidy up the rest
    cellularPortQueueDelete(uartTestTaskData.controlQueueHandle);
    cellularPortMutexDelete(uartTestTaskData.runningMutexHandle);

    CELLULAR_PORT_TEST_ASSERT(cellularPortUartDeinit(CELLULAR_PORT_TEST_UART) == 0);
}

#endif // (CELLULAR_PORT_TEST_PIN_UART_TXD >= 0) && (CELLULAR_PORT_TEST_PIN_UART_RXD >= 0)

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: TESTS
 * -------------------------------------------------------------- */

/** Basic test: initialise and then deinitialise the porting layer.
 */
CELLULAR_PORT_TEST_FUNCTION(void cellularPortTestInitialisation(),
                            "portInitialisation",
                            "port")
{
    CELLULAR_PORT_TEST_ASSERT(cellularPortInit() == 0);
    cellularPortDeinit();
}

/** Test: all the OS stuff.
 */
CELLULAR_PORT_TEST_FUNCTION(void cellularPortTestOs(),
                            "portOs",
                            "port")
{
    int32_t errorCode;
    int64_t startTimeMs;
    int64_t timeNowMs;

    CELLULAR_PORT_TEST_ASSERT(cellularPortInit() == 0);

    startTimeMs = cellularPortGetTickTimeMs();
    cellularPortLog("CELLULAR_PORT_TEST: tick time now is %d.\n",
                    (int32_t) startTimeMs);

    cellularPortLog("CELLULAR_PORT_TEST: creating a mutex...\n");
    errorCode = cellularPortMutexCreate(&gMutexHandle);
    cellularPortLog("                    returned error code %d, handle 0x%08x.\n",
                    errorCode, gMutexHandle);
    CELLULAR_PORT_TEST_ASSERT(errorCode == 0);
    CELLULAR_PORT_TEST_ASSERT(gMutexHandle != NULL);

    cellularPortLog("CELLULAR_PORT_TEST: creating a queue...\n");
    errorCode = cellularPortQueueCreate(CELLULAR_PORT_TEST_QUEUE_LENGTH,
                                        CELLULAR_PORT_TEST_QUEUE_ITEM_SIZE,
                                        &gQueueHandle);
    cellularPortLog("                    returned error code %d, handle 0x%08x.\n",
                    errorCode, gQueueHandle);
    CELLULAR_PORT_TEST_ASSERT(errorCode == 0);
    CELLULAR_PORT_TEST_ASSERT(gQueueHandle != NULL);

    cellularPortLog("CELLULAR_PORT_TEST: creating a test task with stack %d byte(s) and priority %d, passing it the pointer 0x%08x containing the string \"%s\"...\n",
                    CELLULAR_PORT_TEST_OS_TASK_STACK_SIZE_BYTES,
                    CELLULAR_PORT_TEST_OS_TASK_PRIORITY,
                    &gpTaskParameter, gpTaskParameter);
    errorCode = cellularPortTaskCreate(osTestTask, "osTestTask",
                                       CELLULAR_PORT_TEST_OS_TASK_STACK_SIZE_BYTES,
                                       (void *) gpTaskParameter,
                                       CELLULAR_PORT_TEST_OS_TASK_PRIORITY,
                                       &gTaskHandle);
    cellularPortLog("                    returned error code %d, handle 0x%08x.\n",
                    errorCode, gTaskHandle);
    CELLULAR_PORT_TEST_ASSERT(errorCode == 0);
    CELLULAR_PORT_TEST_ASSERT(gTaskHandle != NULL);

    // Pause to let the task print its opening messages
    cellularPortTaskBlock(1000);

    cellularPortLog("CELLULAR_PORT_TEST: trying to lock the mutex, should fail...\n");
    CELLULAR_PORT_TEST_ASSERT(cellularPortMutexTryLock(gMutexHandle, 10) != 0);

    cellularPortLog("CELLULAR_PORT_TEST: sending stuff to task...\n");
    for (size_t x = 0; x < sizeof(gStuffToSend) / sizeof(gStuffToSend[0]); x++) {
        // Actually send the stuff in a function so as to check that the
        // stuff is copied rather than referenced.
        sendToQueue(gQueueHandle, gStuffToSend[x]);
    }
    cellularPortLog("CELLULAR_PORT_TEST: sending -1 to terminate test task and waiting for it to stop...\n");
    sendToQueue(gQueueHandle, -1);

    CELLULAR_PORT_MUTEX_LOCK(gMutexHandle);
    // Yield to let it get the message
    cellularPortTaskBlock(10);
    CELLULAR_PORT_MUTEX_UNLOCK(gMutexHandle);
    cellularPortLog("CELLULAR_PORT_TEST: task stopped.\n");

    // Pause to let the task print its final messages
    cellularPortTaskBlock(1000);

    cellularPortLog("CELLULAR_PORT_TEST: deleting mutex...\n");
    cellularPortMutexDelete(gMutexHandle);

    cellularPortLog("CELLULAR_PORT_TEST: deleting queue...\n");
    CELLULAR_PORT_TEST_ASSERT(cellularPortQueueDelete(gQueueHandle) == 0);

    // Some ports, e.g. the Nordic one, use the timer tick somewhat
    // differently when the UART is running so initialise that
    // here and re-measure time
    timeNowMs = cellularPortGetTickTimeMs();
    cellularPortLog("CELLULAR_PORT_TEST: tick time now is %d.\n",
                    (int32_t) timeNowMs);
    cellularPortLog("CELLULAR_PORT_TEST: initialising UART...\n");
    CELLULAR_PORT_TEST_ASSERT(cellularPortUartInit(CELLULAR_CFG_PIN_TXD,
                                                   CELLULAR_CFG_PIN_RXD,
                                                   CELLULAR_CFG_PIN_CTS,
                                                   CELLULAR_CFG_PIN_RTS,
                                                   CELLULAR_CFG_BAUD_RATE,
                                                   CELLULAR_CFG_RTS_THRESHOLD,
                                                   CELLULAR_CFG_UART,
                                                   &gQueueHandle) == 0);
    cellularPortLog("CELLULAR_PORT_TEST: waiting one second...\n");
    cellularPortTaskBlock(1000);
    timeNowMs = cellularPortGetTickTimeMs() - timeNowMs;
    cellularPortLog("CELLULAR_PORT_TEST: according to cellularPortGetTickTimeMs() %d ms have elapsed.\n",
                    (int32_t) timeNowMs);
    CELLULAR_PORT_TEST_ASSERT((timeNowMs > 950) && (timeNowMs < 1050));
    cellularPortLog("CELLULAR_PORT_TEST: deinitialising UART...\n");
    timeNowMs = cellularPortGetTickTimeMs();
    CELLULAR_PORT_TEST_ASSERT(cellularPortUartDeinit(CELLULAR_CFG_UART) == 0);
    cellularPortLog("CELLULAR_PORT_TEST: waiting one second...\n");
    cellularPortTaskBlock(1000);
    timeNowMs = cellularPortGetTickTimeMs() - timeNowMs;
    cellularPortLog("CELLULAR_PORT_TEST: according to cellularPortGetTickTimeMs() %d ms have elapsed.\n",
                    (int32_t) timeNowMs);
    CELLULAR_PORT_TEST_ASSERT((timeNowMs > 950) && (timeNowMs < 1050));

    timeNowMs = cellularPortGetTickTimeMs() - startTimeMs;
    cellularPortLog("CELLULAR_PORT_TEST: according to cellularPortGetTickTimeMs() the test took %d ms.\n",
                    (int32_t) timeNowMs);
    CELLULAR_PORT_TEST_ASSERT((timeNowMs > 0) &&
                              (timeNowMs < CELLULAR_PORT_TEST_OS_GUARD_DURATION_MS));

    cellularPortDeinit();
}

/** Test: strtok_r since we have our own implementation for some platforms.
 */
CELLULAR_PORT_TEST_FUNCTION(void cellularPortTest_strtok_r(),
                            "portStrtok_r",
                            "port")
{
    char *pSave;
    char buffer[8];

    CELLULAR_PORT_TEST_ASSERT(cellularPortInit() == 0);

    cellularPortLog("CELLULAR_PORT_TEST: testing strtok_r...\n");

    buffer[sizeof(buffer) - 1] = 'x';

    pCellularPort_strcpy(buffer, "abcabc");
    CELLULAR_PORT_TEST_ASSERT(cellularPort_strcmp(pCellularPort_strtok_r(buffer, "b", &pSave), "a") == 0);
    CELLULAR_PORT_TEST_ASSERT(buffer[sizeof(buffer) - 1] == 'x');
    CELLULAR_PORT_TEST_ASSERT(cellularPort_strcmp(pCellularPort_strtok_r(NULL, "b", &pSave), "ca") == 0);
    CELLULAR_PORT_TEST_ASSERT(buffer[sizeof(buffer) - 1] == 'x');
    CELLULAR_PORT_TEST_ASSERT(cellularPort_strcmp(pCellularPort_strtok_r(NULL, "b", &pSave), "c") == 0);
    CELLULAR_PORT_TEST_ASSERT(buffer[sizeof(buffer) - 1] == 'x');
    CELLULAR_PORT_TEST_ASSERT(pCellularPort_strtok_r(NULL, "b", &pSave) == NULL);
    CELLULAR_PORT_TEST_ASSERT(buffer[sizeof(buffer) - 1] == 'x');

    pCellularPort_strcpy(buffer, "abcade");
    CELLULAR_PORT_TEST_ASSERT(cellularPort_strcmp(pCellularPort_strtok_r(buffer, "a", &pSave), "bc") == 0);
    CELLULAR_PORT_TEST_ASSERT(buffer[sizeof(buffer) - 1] == 'x');
    pCellularPort_strcpy(buffer, "abcade");
    CELLULAR_PORT_TEST_ASSERT(cellularPort_strcmp(pCellularPort_strtok_r(buffer, "a", &pSave), "bc") == 0);
    CELLULAR_PORT_TEST_ASSERT(buffer[sizeof(buffer) - 1] == 'x');
    CELLULAR_PORT_TEST_ASSERT(cellularPort_strcmp(pCellularPort_strtok_r(NULL, "a", &pSave), "de") == 0);
    CELLULAR_PORT_TEST_ASSERT(buffer[sizeof(buffer) - 1] == 'x');
    CELLULAR_PORT_TEST_ASSERT(pCellularPort_strtok_r(NULL, "a", &pSave) == NULL);
    CELLULAR_PORT_TEST_ASSERT(buffer[sizeof(buffer) - 1] == 'x');

    pCellularPort_strcpy(buffer, "abcabc");
    CELLULAR_PORT_TEST_ASSERT(cellularPort_strcmp(pCellularPort_strtok_r(buffer, "d", &pSave), "abcabc") == 0);
    CELLULAR_PORT_TEST_ASSERT(buffer[sizeof(buffer) - 1] == 'x');
    CELLULAR_PORT_TEST_ASSERT(pCellularPort_strtok_r(NULL, "d", &pSave) == NULL);
    CELLULAR_PORT_TEST_ASSERT(buffer[sizeof(buffer) - 1] == 'x');

    cellularPortDeinit();
}

#if (CELLULAR_PORT_TEST_PIN_A >= 0) && (CELLULAR_PORT_TEST_PIN_B >= 0) && \
    (CELLULAR_PORT_TEST_PIN_C >= 0)
/** Test GPIOs.
 */
CELLULAR_PORT_TEST_FUNCTION(void cellularPortTestGpio(),
                            "portGpio",
                            "port")
{
    CellularPortGpioConfig_t gpioConfig = CELLULAR_PORT_GPIO_CONFIG_DEFAULT;

    CELLULAR_PORT_TEST_ASSERT(cellularPortInit() == 0);

    cellularPortLog("CELLULAR_PORT_TEST: testing GPIOs.\n");
    cellularPortLog("CELLULAR_PORT_TEST: pin A (%d, 0x%02x) must be connected to pin B (%d, 0x%02x) via a 1k resistor.\n",
                    CELLULAR_PORT_TEST_PIN_A, CELLULAR_PORT_TEST_PIN_A,
                    CELLULAR_PORT_TEST_PIN_B, CELLULAR_PORT_TEST_PIN_B);
    cellularPortLog("CELLULAR_PORT_TEST: pin C (%d, 0x%02x) must be connected to pin B (%d, 0x%02x).\n",
                    CELLULAR_PORT_TEST_PIN_C, CELLULAR_PORT_TEST_PIN_C,
                    CELLULAR_PORT_TEST_PIN_B, CELLULAR_PORT_TEST_PIN_B);

    // Make pins B and C inputs, no pull
    gpioConfig.pin = CELLULAR_PORT_TEST_PIN_B;
    gpioConfig.direction = CELLULAR_PORT_GPIO_DIRECTION_INPUT;
    gpioConfig.pullMode =  CELLULAR_PORT_GPIO_PULL_MODE_NONE;
    CELLULAR_PORT_TEST_ASSERT(cellularPortGpioConfig(&gpioConfig) == 0);
    gpioConfig.pin = CELLULAR_PORT_TEST_PIN_C;
    CELLULAR_PORT_TEST_ASSERT(cellularPortGpioConfig(&gpioConfig) == 0);

    // Set pin A high
    CELLULAR_PORT_TEST_ASSERT(cellularPortGpioSet(CELLULAR_PORT_TEST_PIN_A, 1) == 0);
    // Make it an output pin
    gpioConfig.pin = CELLULAR_PORT_TEST_PIN_A;
    gpioConfig.direction = CELLULAR_PORT_GPIO_DIRECTION_OUTPUT;
    CELLULAR_PORT_TEST_ASSERT(cellularPortGpioConfig(&gpioConfig) == 0);
    cellularPortTaskBlock(1);

    // Pins B and C should read 1
    CELLULAR_PORT_TEST_ASSERT(cellularPortGpioGet(CELLULAR_PORT_TEST_PIN_B) == 1);
    CELLULAR_PORT_TEST_ASSERT(cellularPortGpioGet(CELLULAR_PORT_TEST_PIN_C) == 1);

    // Set pin A low
    CELLULAR_PORT_TEST_ASSERT(cellularPortGpioSet(CELLULAR_PORT_TEST_PIN_A, 0) == 0);
    cellularPortTaskBlock(1);

    // Pins B and C should read 0
    CELLULAR_PORT_TEST_ASSERT(cellularPortGpioGet(CELLULAR_PORT_TEST_PIN_B) == 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPortGpioGet(CELLULAR_PORT_TEST_PIN_C) == 0);

    // Make pin B an output, low, open drain
    CELLULAR_PORT_TEST_ASSERT(cellularPortGpioSet(CELLULAR_PORT_TEST_PIN_B, 0) == 0);
    gpioConfig.pin = CELLULAR_PORT_TEST_PIN_B;
    gpioConfig.direction = CELLULAR_PORT_GPIO_DIRECTION_OUTPUT;
    gpioConfig.driveMode = CELLULAR_PORT_GPIO_DRIVE_MODE_OPEN_DRAIN;
    CELLULAR_PORT_TEST_ASSERT(cellularPortGpioConfig(&gpioConfig) == 0);
    cellularPortTaskBlock(1);

    // Pin C should still read 0
    CELLULAR_PORT_TEST_ASSERT(cellularPortGpioGet(CELLULAR_PORT_TEST_PIN_C) == 0);

    // Set pin A high
    CELLULAR_PORT_TEST_ASSERT(cellularPortGpioSet(CELLULAR_PORT_TEST_PIN_A, 1) == 0);
    cellularPortTaskBlock(1);

    // Pin C should still read 0
    CELLULAR_PORT_TEST_ASSERT(cellularPortGpioGet(CELLULAR_PORT_TEST_PIN_C) == 0);

    // Set pin B high
    CELLULAR_PORT_TEST_ASSERT(cellularPortGpioSet(CELLULAR_PORT_TEST_PIN_B, 1) == 0);
    cellularPortTaskBlock(1);

    // Pin C should now read 1
    CELLULAR_PORT_TEST_ASSERT(cellularPortGpioGet(CELLULAR_PORT_TEST_PIN_C) == 1);

    // Make pin A an input/output pin
    gpioConfig.pin = CELLULAR_PORT_TEST_PIN_A;
    gpioConfig.direction = CELLULAR_PORT_GPIO_DIRECTION_INPUT_OUTPUT;
    gpioConfig.driveMode = CELLULAR_PORT_GPIO_DRIVE_MODE_NORMAL;
    CELLULAR_PORT_TEST_ASSERT(cellularPortGpioConfig(&gpioConfig) == 0);

    // Pin A should read 1
    CELLULAR_PORT_TEST_ASSERT(cellularPortGpioGet(CELLULAR_PORT_TEST_PIN_A) == 1);

    // Set pin A low
    CELLULAR_PORT_TEST_ASSERT(cellularPortGpioSet(CELLULAR_PORT_TEST_PIN_A, 0) == 0);
    cellularPortTaskBlock(1);

    // Pins A and C should read 0
    CELLULAR_PORT_TEST_ASSERT(cellularPortGpioGet(CELLULAR_PORT_TEST_PIN_A) == 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPortGpioGet(CELLULAR_PORT_TEST_PIN_C) == 0);

    // Make pin B an input/output open-drain pin
    gpioConfig.pin = CELLULAR_PORT_TEST_PIN_B;
    gpioConfig.direction = CELLULAR_PORT_GPIO_DIRECTION_INPUT_OUTPUT;
    gpioConfig.driveMode = CELLULAR_PORT_GPIO_DRIVE_MODE_OPEN_DRAIN;
    CELLULAR_PORT_TEST_ASSERT(cellularPortGpioConfig(&gpioConfig) == 0);

    // All pins should read 0
    CELLULAR_PORT_TEST_ASSERT(cellularPortGpioGet(CELLULAR_PORT_TEST_PIN_A) == 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPortGpioGet(CELLULAR_PORT_TEST_PIN_B) == 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPortGpioGet(CELLULAR_PORT_TEST_PIN_C) == 0);

    // Note: it is impossible to check pull up/down
    // of input pins reliably as boards have level shifters
    // and protection resistors between the board pins and the
    // chip pins that drown-out the effect of the pull up/down
    // inside the chip.
    // Also can't easily test drive strength and in any case
    // it is not supported on all platforms.

    cellularPortDeinit();
}
#endif

#if (CELLULAR_PORT_TEST_PIN_UART_TXD >= 0) && (CELLULAR_PORT_TEST_PIN_UART_RXD >= 0)
/** Test UART.
 */
CELLULAR_PORT_TEST_FUNCTION(void cellularPortTestUart(),
                            "portUart",
                            "port")
{
    CELLULAR_PORT_TEST_ASSERT(cellularPortInit() == 0);

    // Run a UART test at 115,200
#if (CELLULAR_PORT_TEST_PIN_UART_CTS >= 0) && (CELLULAR_PORT_TEST_PIN_UART_RTS >= 0)
    // ...with flow control
    runUartTest(50000, 115200, true);
#endif
    // ...without flow control
    runUartTest(50000, 115200, false);

    cellularPortDeinit();
}
#endif

/** Clean-up to be run at the end of this round of tests, just
 * in case there were test failures which would have resulted
 * in the deinitialisation being skipped.
 */
CELLULAR_PORT_TEST_FUNCTION(void cellularPortTestCleanUp(),
                            "portCleanUp",
                            "port")
{
    CELLULAR_PORT_TEST_ASSERT(cellularPortUartDeinit(CELLULAR_CFG_UART) == 0);
    cellularPortDeinit();
}

// TODO: more ranges of OS stuff, stress testing, etc.

// End of file
