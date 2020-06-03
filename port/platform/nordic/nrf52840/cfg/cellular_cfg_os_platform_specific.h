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

#ifndef _CELLULAR_CFG_OS_PLATFORM_SPECIFIC_H_
#define _CELLULAR_CFG_OS_PLATFORM_SPECIFIC_H_

/* No #includes allowed here */

/* This header file contains OS configuration information for
 * an NRF52840 board.
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS FOR NRF52840: OS GENERIC
 * -------------------------------------------------------------- */

#ifndef CELLULAR_PORT_OS_PRIORITY_MIN
/** The minimum task priority.
 * In FreeRTOS, as used on this platform, low numbers indicate
 * lower priority.
 */
# define CELLULAR_PORT_OS_PRIORITY_MIN 0
#endif

#ifndef CELLULAR_PORT_OS_PRIORITY_MAX
/** The maximum task priority, should be less than or
 * equal to configMAX_PRIORITIES defined in FreeRTOSConfig.h,
 * which is set to 15.
 */
# define CELLULAR_PORT_OS_PRIORITY_MAX 15
#endif

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS FOR NRF52840: AT CLIENT RELATED
 * -------------------------------------------------------------- */

#ifndef CELLULAR_CTRL_AT_TASK_URC_STACK_SIZE_BYTES
/** The stack size for the AT task that handles URCs.
 * Note: this size worst case for unoptimised compilation
 * (so that a debugger can be used sensibly) under the worst compiler.
 */
# define CELLULAR_CTRL_AT_TASK_URC_STACK_SIZE_BYTES (1024 * 5)
#endif

#ifndef CELLULAR_CTRL_AT_TASK_URC_PRIORITY
/** The task priority for the URC handler.
 */
# define CELLULAR_CTRL_AT_TASK_URC_PRIORITY (CELLULAR_PORT_OS_PRIORITY_MAX - 5)
#endif

#ifndef CELLULAR_CTRL_TASK_CALLBACK_STACK_SIZE_BYTES
/** The stack size of the task in the context of which the callbacks
 * of AT command URCs will be run.  5 kbytes should be plenty of room.
 */
# define CELLULAR_CTRL_TASK_CALLBACK_STACK_SIZE_BYTES (1024 * 5)
#endif

#ifndef CELLULAR_CTRL_TASK_CALLBACK_PRIORITY
/** The task priority for any callback made via
 * cellular_ctrl_at_callback().
 */
# define CELLULAR_CTRL_TASK_CALLBACK_PRIORITY (CELLULAR_PORT_OS_PRIORITY_MIN + 2)
#endif

#if (CELLULAR_CTRL_TASK_CALLBACK_PRIORITY >= CELLULAR_CTRL_AT_TASK_URC_PRIORITY)
# error CELLULAR_CTRL_TASK_CALLBACK_PRIORITY must be less than CELLULAR_CTRL_AT_TASK_URC_PRIORITY
#endif

#endif // _CELLULAR_CFG_OS_PLATFORM_SPECIFIC_H_

// End of file
