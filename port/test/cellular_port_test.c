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

#include "cellular_port_clib.h"
#include "cellular_cfg_hw.h"
#include "cellular_cfg_sw.h"
#include "cellular_port.h"
#include "cellular_port_debug.h"
#include "cellular_port_os.h"
#include "cellular_port_uart.h"
#include "cellular_port_test.h"
#include "cellular_ctrl.h"

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// The queue length to create.
#define CELLULAR_PORT_TEST_QUEUE_LENGTH 20

// The size of each item on the queue.
#define CELLULAR_PORT_TEST_QUEUE_ITEM_SIZE sizeof(int32_t)

// The task priority to use; platform dependent, hope this is good.
#define CELLULAR_PORT_TEST_TASK_PRIORITY 12

// The task stack size; platform dependent, hope this is good.
#define CELLULAR_PORT_TEST_TASK_STACK_SIZE 2048

// The guard time for the OS test.
#define CELLULAR_PORT_TEST_OS_GUARD_DURATION_MS 2000

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
    CellularPortTaskHandle_t taskHandle;
    int32_t queueItem = 0;
    int32_t index = 0;

    cellularPortLog("CELLULAR_PORT_TEST_TASK: task started, received parameter pointer 0x%08x containing string \"%s\".\n",
                    pParameters, (const char *) pParameters);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_strcmp(pParameters,
                                                  gpTaskParameter) == 0);

    CELLULAR_PORT_TEST_ASSERT(cellularPortTaskIsThis(gTaskHandle));

    cellularPortLog("CELLULAR_PORT_TEST_TASK: task trying to lock the mutex.\n");
    CELLULAR_PORT_TEST_ASSERT(gMutexHandle != NULL);
    CELLULAR_PORT_TEST_ASSERT(cellularPortMutexTryLock(gMutexHandle, 10) == 0)
    cellularPortLog("CELLULAR_PORT_TEST_TASK: unlocking it again.\n");
    CELLULAR_PORT_TEST_ASSERT(cellularPortMutexUnlock(gMutexHandle) == 0);

    cellularPortLog("CELLULAR_PORT_TEST_TASK: locking it again (non-try version).\n");
    CELLULAR_PORT_MUTEX_LOCK(gMutexHandle);

    // Check that we have it
    taskHandle = cellularPortMutexGetLocker(gMutexHandle);
    cellularPortLog("CELLULAR_PORT_TEST_TASK: task 0x%08x has the mutex.\n",
                    taskHandle);
    CELLULAR_PORT_TEST_ASSERT(taskHandle == gTaskHandle);

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
                            "initialisation",
                            "port")
{
    CELLULAR_PORT_TEST_ASSERT(cellularPortInit() == 0);
    cellularPortDeinit();
}

/** Test: all the OS stuff.
 */
CELLULAR_PORT_TEST_FUNCTION(void cellularPortTestEverything(),
                            "os",
                            "port")
{
    int32_t errorCode;
    CellularPortTaskHandle_t taskHandle;
    int32_t timeNowMs;

    CELLULAR_PORT_TEST_ASSERT(cellularPortInit() == 0);

    timeNowMs = cellularPortGetTimeMs();
    cellularPortLog("CELLULAR_PORT_TEST: time now is %d.\n",
                    timeNowMs);

    cellularPortLog("CELLULAR_PORT_TEST: creating a mutex...\n");
    errorCode = cellularPortMutexCreate(&gMutexHandle);
    cellularPortLog("                    returned error code %d, handle 0x%08x.\n",
                    errorCode, gMutexHandle);
    CELLULAR_PORT_TEST_ASSERT(errorCode == 0);
    CELLULAR_PORT_TEST_ASSERT(gMutexHandle != NULL);

    // Check that the mutex it not currently locked
    taskHandle = cellularPortMutexGetLocker(gMutexHandle);
    CELLULAR_PORT_TEST_ASSERT(taskHandle == NULL);

    cellularPortLog("CELLULAR_PORT_TEST: creating a queue...\n");
    errorCode = cellularPortQueueCreate(CELLULAR_PORT_TEST_QUEUE_LENGTH,
                                        CELLULAR_PORT_TEST_QUEUE_ITEM_SIZE,
                                        &gQueueHandle);
    cellularPortLog("                    returned error code %d, handle 0x%08x.\n",
                    errorCode, gQueueHandle);
    CELLULAR_PORT_TEST_ASSERT(errorCode == 0);
    CELLULAR_PORT_TEST_ASSERT(gQueueHandle != NULL);

    cellularPortLog("CELLULAR_PORT_TEST: creating a test task with stack %d byte(s) and priority %d, passing it the pointer 0x%08x containing the string \"%s\"...\n",
                    CELLULAR_PORT_TEST_TASK_STACK_SIZE,
                    CELLULAR_PORT_TEST_TASK_PRIORITY,
                    &gpTaskParameter, gpTaskParameter);
    errorCode = cellularPortTaskCreate(testTask, "test_task",
                                       CELLULAR_PORT_TEST_TASK_STACK_SIZE,
                                       (void **) &gpTaskParameter,
                                       CELLULAR_PORT_TEST_TASK_PRIORITY,
                                       &gTaskHandle);
    cellularPortLog("                    returned error code %d, handle 0x%08x.\n",
                    errorCode, gTaskHandle);
    CELLULAR_PORT_TEST_ASSERT(errorCode == 0);
    CELLULAR_PORT_TEST_ASSERT(gTaskHandle != NULL);

    // Pause to let the task print its opening messages
    cellularPortTaskBlock(1000);

    // Check that the mutex is now locked by our test task
    taskHandle = cellularPortMutexGetLocker(gMutexHandle);
    cellularPortLog("CELLULAR_PORT_TEST: task handle 0x%08x has the mutex.\n",
                    taskHandle);
    CELLULAR_PORT_TEST_ASSERT(taskHandle == gTaskHandle);

    cellularPortLog("CELLULAR_PORT_TEST: trying to lock the mutex, should fail...\n");
    CELLULAR_PORT_TEST_ASSERT(cellularPortMutexTryLock(gMutexHandle, 10) != 0)

    cellularPortLog("CELLULAR_PORT_TEST: sending stuff to task...\n");
    for (size_t x = 0; x < sizeof(gStuffToSend) / sizeof(gStuffToSend[0]); x++) {
        // Actually send the stuff in a function so as to check that the
        // stuff is copied rather than referenced.
        sendToQueue(gQueueHandle, gStuffToSend[x]);
    }
    cellularPortLog("CELLULAR_PORT_TEST: sending -1 to terminate test task and waiting for it to stop...\n");
    sendToQueue(gQueueHandle, -1);

    CELLULAR_PORT_MUTEX_LOCK(gMutexHandle);
    CELLULAR_PORT_MUTEX_UNLOCK(gMutexHandle);
    cellularPortLog("CELLULAR_PORT_TEST: task stopped.\n");

    cellularPortLog("CELLULAR_PORT_TEST: deleting mutex...\n");
    cellularPortMutexDelete(gMutexHandle);

    cellularPortLog("CELLULAR_PORT_TEST: deleting queue...\n");
    CELLULAR_PORT_TEST_ASSERT(cellularPortQueueDelete(gQueueHandle) == 0);

    timeNowMs = cellularPortGetTimeMs() - timeNowMs;
    cellularPortLog("CELLULAR_PORT_TEST: according to cellularPortGetTimeMs() the test took %d ms.\n",
                    timeNowMs);
    CELLULAR_PORT_TEST_ASSERT((timeNowMs > 0) &&
                              (timeNowMs < CELLULAR_PORT_TEST_OS_GUARD_DURATION_MS));

    cellularPortDeinit();
}

// TODO: more ranges of OS stuff, stress testing, etc.
// TODO: tests for clib

// End of file
