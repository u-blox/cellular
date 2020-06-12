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
#include "cellular_port_clib.h"
#include "cellular_port.h"
#include "cellular_port_debug.h"
#include "cellular_port_test_platform_specific.h"

#include "cmsis_os.h"

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// How much stack the task running all the tests needs in bytes.
#define CELLULAR_PORT_TEST_RUNNER_TASK_STACK_SIZE_BYTES (1024 * 4)

// The priority of the task running the tests: should be low.
#define CELLULAR_PORT_TEST_RUNNER_TASK_PRIORITY osPriorityLow

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

// The task within which testing runs.
static void testTask(void *pParam)
{
    (void) pParam;

    cellularPortInit();
    cellularPortLog("\n\nCELLULAR_TEST: Test task started.\n");

    UNITY_BEGIN();

    cellularPortLog("CELLULAR_TEST: Tests available:\n\n");
    cellularPortUnityPrintAll("CELLULAR_TEST: ");
    cellularPortLog("CELLULAR_TEST: Running all tests.\n");
    cellularPortUnityRunAll("CELLULAR_TEST: ");

    UNITY_END();

    cellularPortLog("\n\nCELLULAR_TEST: Test task ended.\n");
    cellularPortDeinit();

    while(1){}
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS
 * -------------------------------------------------------------- */

// Unity setUp() function.
void setUp(void)
{
    // Nothing to do
}

// Unity tearDown() function.
void tearDown(void)
{
    // Nothing to do
}

void testFail(void)
{
    
}

// Entry point
int main(void)
{
    // Execute the test task
    cellularPortPlatformStart(testTask, NULL,
                              CELLULAR_PORT_TEST_RUNNER_TASK_STACK_SIZE_BYTES,
                              CELLULAR_PORT_TEST_RUNNER_TASK_PRIORITY);

    // Should never get here
    cellularPort_assert(false);

    return 0;
}
