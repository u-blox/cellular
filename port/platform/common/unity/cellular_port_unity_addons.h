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

#ifndef _CELLULAR_PORT_UNITY_ADDONS_H_
#define _CELLULAR_PORT_UNITY_ADDONS_H_

/** This file defines some additional bits and bobs to make Unity
 * work for cellular.
 */

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
    /* Test function prototype */                                                                            \
    static void CELLULAR_PORT_UNITY_UID(testFunction) (void);                                                \
    /* Use constructor attribute so that this is run during C initialisation before anything else runs */    \
    static void __attribute__((constructor)) CELLULAR_PORT_UNITY_UID(testRegistrationHelper) ()              \
    {                                                                                                        \
        /* Static pointer to the test function at the end of this macro */                                   \
        static const pCellularPortUnityTestFunction_t pFunction = &CELLULAR_PORT_UNITY_UID(testFunction);    \
        /* Static description of the tests to pass to the register function */                               \
        static CellularPortUnityTestDescription_t CELLULAR_PORT_UNITY_UID(testDescription) = {               \
            .pName = name,                                                                                   \
            .pGroup = group,                                                                                 \
            .pFunction = pFunction,                                                                          \
            .pFile = __FILE__,                                                                               \
            .line = __LINE__,                                                                                \
            .pNext = NULL                                                                                    \
        };                                                                                                   \
        /* Call the register function with the description so it can keep a list of the tests */             \
        cellularPortUnityTestRegister(&CELLULAR_PORT_UNITY_UID(testDescription));                            \
    }                                                                                                        \
    /* Actual start of test function */                                                                      \
    static void CELLULAR_PORT_UNITY_UID(testFunction) (void)

/* ----------------------------------------------------------------
 * FUNCTIONS
 * -------------------------------------------------------------- */

/** Print out all the registered test cases.
 *
 * @param pPrefix prefix string to print at start of line.
 */
void cellularPortUnityPrintAll(const char *pPrefix);

/** Run all the registered test cases.
 *
 * @param pPrefix prefix string to print at start of line.
 */
void cellularPortUnityRunAll(const char *pPrefix);

#endif // _CELLULAR_PORT_UNITY_ADDONS_H_

// End of file
