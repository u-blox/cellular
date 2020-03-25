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
 * learing from the implementation of the esp-idf unity component.
 * It may be included in a build for a platform which includes no
 * unit test framework of its own.
 */

#include "cellular_port_clib.h"
#include "cellular_port.h"
#include "unity.h"
#include "cellular_port_unity.h"

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

// Start and end of linked list of test descriptions.
static CellularPortUnityTestDescription_t *gpTestHead = NULL;
static CellularPortUnityTestDescription_t *gpTestTail = NULL;

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS
 * -------------------------------------------------------------- */

// Add a test to the list.
void cellularPortUnityTestRegister(CellularPortUnityTestDescription_t *pDescription)
{
    CellularPortUnityTestDescription_t *pTmp;

    if (!gpTestHead) {
        gpTestHead = pDescription;
        gpTestTail = pDescription;
    } else {
        pTmp = gpTestHead;
        gpTestHead = pDescription;
        gpTestHead->pNext = pTmp;
    }
}

// Print out the test names and groups
void cellularPortUnityPrintAll()
{
    const CellularPortUnityTestDescription_t *pTest = gpTestHead;
    size_t count = 0;

    while (pTest != NULL) {
        UnityPrint(pTest->pName);
        UnityPrint(" [");
        UnityPrint(pTest->pGroup);
        UnityPrint("]");
        UNITY_PRINT_EOL();
        pTest = pTest->pNext;
        count++;
    }
    UnityPrintNumber((UNITY_INT) (count));
    UnityPrint(" test(s).");
    UNITY_PRINT_EOL();
}

// Run all of the tests.
void cellularPortUnityRunAll()
{
    const CellularPortUnityTestDescription_t *pTest = gpTestHead;

    while (pTest != NULL) {
        UnityPrint("Running ");
        UnityPrint(pTest->pName);
        UnityPrint("...");
        UNITY_PRINT_EOL();
        UNITY_OUTPUT_FLUSH();

        Unity.TestFile = pTest->pFile;
        Unity.CurrentDetail1 = pTest->pGroup;
        UnityDefaultTestRun(pTest->pFunction, pTest->pName, pTest->line);

        pTest = pTest->pNext;
    }
}

// End of file
