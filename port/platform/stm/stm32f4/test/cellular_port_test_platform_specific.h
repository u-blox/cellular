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

#ifndef _CELLULAR_PORT_TEST_PLATFORM_SPECIFIC_H_
#define _CELLULAR_PORT_TEST_PLATFORM_SPECIFIC_H_

/* Only bring in #includes specifically related to the test framework */

#include "cellular_port_unity_addons.h"

/** Porting layer for test execution on the STM32 platform.
 * Since test execution is often macro-ised rather than
 * function-calling this header file forms part of the platform
 * test source code rather than pretending to be a generic API.
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/** Macro to wrap a test assertion and map it to our Unity port.
 */
#define CELLULAR_PORT_TEST_ASSERT(condition) CELLULAR_PORT_UNITY_TEST_ASSERT(condition)

/** Macro to wrap the definition of a test function and
 * map it to our Unity port.
 */
#define CELLULAR_PORT_TEST_FUNCTION(function, name, group) CELLULAR_PORT_UNITY_TEST_FUNCTION(name, group)

/** The stack size to use for the test task created during OS testing.
 */
#define CELLULAR_PORT_TEST_OS_TASK_STACK_SIZE_BYTES (1024 * 3)

/** The task priority to use for the task created during.
 * testing.  Normally this should be a higher than the task
 * running the tests, however in the case of the STM32F4
 * platform a task that is created is off and running before
 * the create function returns so the task handle variable
 * isn't populated and hence the test task check against it
 * will fail.  So here we make the OS test task a low priority
 * and the test code yields to let it in.
 */
#define CELLULAR_PORT_TEST_TASK_PRIORITY -2 // osPriorityLow

/** The stack size to use for the test task created during sockets testing.
 */
#define CELLULAR_PORT_TEST_SOCK_TASK_STACK_SIZE_BYTES (1024 * 5)

/** The priority to use for the test task created during sockets testing;
 * lower priority than the URC handler.
 */
#define CELLULAR_PORT_TEST_SOCK_TASK_PRIORITY (CELLULAR_CTRL_AT_TASK_URC_PRIORITY + 1)

/* ----------------------------------------------------------------
 * FUNCTIONS
 * -------------------------------------------------------------- */

#endif // _CELLULAR_PORT_TEST_PLATFORM_SPECIFIC_H_

// End of file
