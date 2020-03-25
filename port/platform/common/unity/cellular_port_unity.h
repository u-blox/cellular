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

#ifndef _CELLULAR_PORT_UNITY_H_
#define _CELLULAR_PORT_UNITY_H_

#include "unity.h"

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/** Macro to map a unit test assertion to Unity.
 */
#define CELLULAR_PORT_UNITY_TEST_ASSERT(condition) TEST_ASSERT(condition)

/** Make up a Unity unique function name.
 */
#define CELLULAR_PORT_UNITY_EXPAND2(x, y) x ## y
#define CELLULAR_PORT_UNITY_EXPAND(x, y) CELLULAR_PORT_UNITY_EXPAND2(x, y)
#define CELLULAR_PORT_UNITY_UID(x) CELLULAR_PORT_UNITY_EXPAND(x, __LINE__)

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/** A test function.
 */
typedef void (*pCellularPortUnityTestFunction_t)(void);

/** Full description of a test.
 */
typedef struct CellularPortUnityTestDescription_t {
    const char *pName;
    const char *pGroup;
    const pCellularPortUnityTestFunction_t pFunction;
    const char *pFile;
    int32_t line;
    struct CellularPortUnityTestDescription_t *pNext;
} CellularPortUnityTestDescription_t;

/* ----------------------------------------------------------------
 * FUNCTION: TEST CASE REGISTRATION
 * -------------------------------------------------------------- */

/** Like it says.
 *
 * @param pDescription the test case description.
 */
void cellularPortUnityTestRegister(CellularPortUnityTestDescription_t *pDescription);

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS: TEST DEFINITION MACRO
 * -------------------------------------------------------------- */

/** Macro to wrap the definition of a test function and
 * map it to Unity.
 */
#define CELLULAR_PORT_UNITY_TEST_FUNCTION(name, group)                                                       \
    static void CELLULAR_PORT_UNITY_UID(pTestFunction) (void);                                               \
    static void __attribute__((constructor)) CELLULAR_PORT_UNITY_UID(testRegistrationHelper) ()              \
    {                                                                                                        \
        static const pCellularPortUnityTestFunction_t pFunction = {&CELLULAR_PORT_UNITY_UID(pTestFunction)}; \
        static CellularPortUnityTestDescription_t CELLULAR_PORT_UNITY_UID(testDescription) = {               \
            .pName = name,                                                                                   \
            .pGroup = group,                                                                                 \
            .pFunction = pFunction,                                                                          \
            .pFile = __FILE__,                                                                               \
            .line = __LINE__,                                                                                \
            .pNext = NULL                                                                                    \
        };                                                                                                   \
        cellularPortUnityTestRegister(&CELLULAR_PORT_UNITY_UID(testDescription));                            \
    }                                                                                                        \
    static void CELLULAR_PORT_UNITY_UID(pTestFunction) (void)

/* ----------------------------------------------------------------
 * FUNCTIONS
 * -------------------------------------------------------------- */

/** Print out all the registered test cases.
 */
void cellularPortUnityPrintAll();

/** Run all the registered test cases.
 */
void cellularPortUnityRunAll();

#endif // _CELLULAR_PORT_UNITY_H_

// End of file
