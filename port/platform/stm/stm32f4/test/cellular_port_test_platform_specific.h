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
 * COMPILE-TIME MACROS: UNITY RELATED
 * -------------------------------------------------------------- */

/** Macro to wrap a test assertion and map it to our Unity port.
 */
#define CELLULAR_PORT_TEST_ASSERT(condition) CELLULAR_PORT_UNITY_TEST_ASSERT(condition)

/** Macro to wrap the definition of a test function and
 * map it to our Unity port.
 */
#define CELLULAR_PORT_TEST_FUNCTION(function, name, group) CELLULAR_PORT_UNITY_TEST_FUNCTION(name, group)

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS: OS RELATED
 * -------------------------------------------------------------- */

/** The stack size to use for the test task created during OS testing.
 */
#define CELLULAR_PORT_TEST_OS_TASK_STACK_SIZE_BYTES (1024 * 3)

/** The task priority to use for the task created during.
 * testing.
 */
#define CELLULAR_PORT_TEST_OS_TASK_PRIORITY (CELLULAR_PORT_OS_PRIORITY_MIN + 5)

/** The stack size to use for the test task created during sockets testing.
 */
#define CELLULAR_PORT_TEST_SOCK_TASK_STACK_SIZE_BYTES (1024 * 5)

/** The priority to use for the test task created during sockets testing;
 * lower priority than the URC handler.
 */
#define CELLULAR_PORT_TEST_SOCK_TASK_PRIORITY (CELLULAR_CTRL_AT_TASK_URC_PRIORITY - 1)

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS: HW RELATED
 * -------------------------------------------------------------- */

/** Pin A for GPIO testing: should be connected to pin B via a 1k
 * resistor.
 */
#ifndef CELLULAR_PORT_TEST_PIN_A
# define CELLULAR_PORT_TEST_PIN_A         0x05 // AKA PA_5 or D5 on a C030 board
#endif

/** Pin B for GPIO testing: should be connected to pin A via a 1k
 * resistor and also to pin C.
 */
#ifndef CELLULAR_PORT_TEST_PIN_B
# define CELLULAR_PORT_TEST_PIN_B         0x18 // AKA PB_8 or D6 on a C030 board
#endif

/** Pin C for GPIO testing: should be connected to pin B.
 */
#ifndef CELLULAR_PORT_TEST_PIN_C
# define CELLULAR_PORT_TEST_PIN_C         0x1f // AKA PB_15 or D7 on a C030 board
#endif

/** UART HW block for UART driver testing.
 * Note: make sure that the corresponding
 * CELLULAR_CFG_UARTx_AVAILABLE for this UART is
 * set to 1 in cellular_cfg_hw_platform_specific.h
 */
#ifndef CELLULAR_PORT_TEST_UART
# define CELLULAR_PORT_TEST_UART          3 // UART3
#endif

/** Handshake threshold for UART testing.
 */
#ifndef CELLULAR_PORT_TEST_UART_RTS_THRESHOLD
# define CELLULAR_PORT_TEST_UART_RTS_THRESHOLD 0 // Not used on this platform
#endif

/** Tx pin for UART testing: should be connected to the Rx UART pin.
 */
#ifndef CELLULAR_PORT_TEST_PIN_UART_TXD
# define CELLULAR_PORT_TEST_PIN_UART_TXD   0x38 // UART3 TX, PD_8 or D1 on a C030 board
#endif

/** Rx pin for UART testing: should be connected to the Tx UART pin.
 */
#ifndef CELLULAR_PORT_TEST_PIN_UART_RXD
# define CELLULAR_PORT_TEST_PIN_UART_RXD   0x39 // UART3 RX, PD_9 or D0 on a C030 board
#endif

/** CTS pin for UART testing: should be connected to the RTS UART pin.
 */
#ifndef CELLULAR_PORT_TEST_PIN_UART_CTS
# define CELLULAR_PORT_TEST_PIN_UART_CTS  0x3b // UART3 CTS, PD_11 or D2 on a C030 board
#endif

/** RTS pin for UART testing: should be connected to the CTS UART pin.
 */
#ifndef CELLULAR_PORT_TEST_PIN_UART_RTS
# define CELLULAR_PORT_TEST_PIN_UART_RTS  0x1e // UART3 RTS, PB_14 or D3 on a C030 board
#endif

/* ----------------------------------------------------------------
 * FUNCTIONS
 * -------------------------------------------------------------- */

#endif // _CELLULAR_PORT_TEST_PLATFORM_SPECIFIC_H_

// End of file
