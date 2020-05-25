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

/* This file implements a mechanism to run a series of Unity tests,
 * learning from the implementation of the esp-idf unity component,
 * which in turn learned it from the catch framework.
 * It may be included in a build for a platform which includes no
 * unit test framework of its own.
 */

#ifdef CELLULAR_CFG_OVERRIDE
# include "cellular_cfg_override.h" // For a customer's configuration override
#endif
#include "cellular_port_clib.h"
#include "cellular_port.h"
#include "cellular_port_test_platform_specific.h"
#include "cellular_port_unity_addons.h"

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

// Linked list anchor.
static CellularPortUnityTestDescription_t *gpTestList = NULL;

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

// Run a test.
void runTest(const CellularPortUnityTestDescription_t *pTest,
             const char *pPrefix)
{
    UNITY_PRINT_EOL();
    UnityPrint(pPrefix);
    UnityPrint("Running ");
    UnityPrint(pTest->pName);
    UnityPrint("...");
    UNITY_PRINT_EOL();
    UNITY_OUTPUT_FLUSH();

    Unity.TestFile = pTest->pFile;
    Unity.CurrentDetail1 = pTest->pGroup;
    UnityDefaultTestRun(pTest->pFunction, pTest->pName, pTest->line);
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS
 * -------------------------------------------------------------- */

// Add a test to the list.
void cellularPortUnityTestRegister(CellularPortUnityTestDescription_t *pDescription)
{
    CellularPortUnityTestDescription_t **ppTest = &gpTestList;

    while (*ppTest != NULL) {
        ppTest = &((*ppTest)->pNext);
    }
    *ppTest = pDescription;
}

// Print out the test names and groups
void cellularPortUnityPrintAll(const char *pPrefix)
{
    const CellularPortUnityTestDescription_t *pTest = gpTestList;
    size_t count = 0;
    char buffer[16];

    while (pTest != NULL) {
        UnityPrint(pPrefix);
        cellularPort_snprintf(buffer, sizeof(buffer), "%3.d: ", count + 1);
        UnityPrint(buffer);
        UnityPrint(pTest->pName);
        UnityPrint(" [");
        UnityPrint(pTest->pGroup);
        UnityPrint("]");
        UNITY_PRINT_EOL();
        pTest = pTest->pNext;
        count++;
    }
    UNITY_PRINT_EOL();
}

// Run a named test.
void cellularPortUnityRunNamed(const char *pName,
                               const char *pPrefix)
{
    const CellularPortUnityTestDescription_t *pTest = gpTestList;

    while (pTest != NULL) {
        if ((pName == NULL) ||
            (cellularPort_strcmp(pTest->pName, pName) == 0)) {
            runTest(pTest, pPrefix);
        }
        pTest = pTest->pNext;
    }
}

// Run all of the tests whose names
// begin with the given filter string.
void cellularPortUnityRunFiltered(const char *pFilter,
                                  const char *pPrefix)
{
    const CellularPortUnityTestDescription_t *pTest = gpTestList;

    while (pTest != NULL) {
        if ((pFilter == NULL) ||
            (pCellularPort_strstr(pTest->pName, pFilter) == pTest->pName)) {
            runTest(pTest, pPrefix);
        }
        pTest = pTest->pNext;
    }
}

// Run all of the tests in a group.
void cellularPortUnityRunGroup(const char *pGroup,
                               const char *pPrefix)
{
    const CellularPortUnityTestDescription_t *pTest = gpTestList;

    while (pTest != NULL) {
        if ((pGroup == NULL) ||
            (cellularPort_strcmp(pTest->pGroup, pGroup) == 0)) {
            runTest(pTest, pPrefix);
        }
        pTest = pTest->pNext;
    }
}

// Run all of the tests.
void cellularPortUnityRunAll(const char *pPrefix)
{
    const CellularPortUnityTestDescription_t *pTest = gpTestList;

    while (pTest != NULL) {
        runTest(pTest, pPrefix);
        pTest = pTest->pNext;
    }
}

// End of file
