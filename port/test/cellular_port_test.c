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

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// Mutex handle.
static CellularPortMutexHandle_t gMutexHandle = NULL;

// Queue handle.
static CellularPortQueueHandle_t gQueueHandle = NULL;

// Task handle.
static CellularPortTaskHandle_t gTaskHandle = NULL;

// Task parameter.
static const char *gpTaskParameter = "Boo!";

// Stuff to send to task, must all be positive numbers.
static int32_t gStuffToSend[] = {0, 100, 25, 3};

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

// The test task.
static void testTask(void *pParameters)
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
                            "os",
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
                    CELLULAR_PORT_TEST_TASK_PRIORITY,
                    &gpTaskParameter, gpTaskParameter);
    errorCode = cellularPortTaskCreate(testTask, "test_task",
                                       CELLULAR_PORT_TEST_OS_TASK_STACK_SIZE_BYTES,
                                       (void *) gpTaskParameter,
                                       CELLULAR_PORT_TEST_TASK_PRIORITY,
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
                            "strtok_r",
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

/** Clean-up to be run at the end of this round of tests, just
 * in case there were test failures which would have resulted
 * in the deinitialisation being skipped.
 */
CELLULAR_PORT_TEST_FUNCTION(void cellularPortTestCleanUp(),
                            "portCleanUp",
                            "port")
{
    cellularCtrlDeinit();
    CELLULAR_PORT_TEST_ASSERT(cellularPortUartDeinit(CELLULAR_CFG_UART) == 0);
    cellularPortDeinit();
}

// TODO: more ranges of OS stuff, stress testing, etc.

// End of file
