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
#include "cellular_sock.h"

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

// Type for testing address string conversion.
typedef struct {
    const char *pAddressString;
    CellularSockAddress_t address;
    bool hasPort;
    bool shouldError;
} CellularSockTestAddress_t;

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// Used for keepGoingCallback() timeout.
static int64_t gStopTimeMS;

// Array of inputs for address string testing.
// TODO: random crap attack.
static CellularSockTestAddress_t gTestAddressList[] = {
     // IPV4
     {"0.0.0.0", {{CELLULAR_SOCK_ADDRESS_TYPE_V4, {0x00000000}}, 0}, false, false},
     {"0.0.0.0:0", {{CELLULAR_SOCK_ADDRESS_TYPE_V4, {0x00000000}}, 0}, true, false},
     {"0.1.2.3", {{CELLULAR_SOCK_ADDRESS_TYPE_V4, {0x00010203}}, 0}, false, false},
     {"0.1.2.3:0", {{CELLULAR_SOCK_ADDRESS_TYPE_V4, {0x00010203}}, 0}, true, false},
     {"255.255.255.255", {{CELLULAR_SOCK_ADDRESS_TYPE_V4, {0xffffffff}}, 0}, false, false},
     {"255.255.255.255:65535", {{CELLULAR_SOCK_ADDRESS_TYPE_V4, {0xffffffff}}, 65535}, true, false},
     // IPV4 error cases
     {"256.255.255.255:65535", {{CELLULAR_SOCK_ADDRESS_TYPE_V4, {0x00000000}}, 0}, true, true},
     {"255.256.255.255:65535", {{CELLULAR_SOCK_ADDRESS_TYPE_V4, {0x00000000}}, 0}, true, true},
     {"255.255.256.255:65535", {{CELLULAR_SOCK_ADDRESS_TYPE_V4, {0x00000000}}, 0}, true, true},
     {"255.255.255.256:65535", {{CELLULAR_SOCK_ADDRESS_TYPE_V4, {0x00000000}}, 0}, true, true},
     {"255.255.255.255:65536", {{CELLULAR_SOCK_ADDRESS_TYPE_V4, {0x00000000}}, 0}, true, true},
     // IPV6
     {"0:0:0:0:0:0:0:0", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0x00000000, 0x00000000, 0x00000000, 0x00000000}}, 0}, false, false},
     {"[0:0:0:0:0:0:0:0]:0", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0x00000000, 0x00000000, 0x00000000, 0x00000000}}, 0}, true, false},
     {"0:1:2:3:4:a:b:c", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0x00000001, 0x00020003, 0x0004000a, 0x000b000c}}, 0}, false, false},
     {"[0:1:2:3:4:a:b:c]:0", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0x00000001, 0x00020003, 0x0004000a, 0x000b000c}}, 0}, true, false},
     {"ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff}}, 0}, false, false},
     {"[ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff]:65535", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff}}, 65535}, true, false},
     // IPV6 error cases
     {"[1ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff]:65535", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0x00000000, 0x00000000, 0x00000000, 0x00000000}}, 0}, true, false},
     {"[ffff:1ffff:ffff:ffff:ffff:ffff:ffff:ffff]:65535", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0x00000000, 0x00000000, 0x00000000, 0x00000000}}, 0}, true, false},
     {"[ffff:ffff:1ffff:ffff:ffff:ffff:ffff:ffff]:65535", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0x00000000, 0x00000000, 0x00000000, 0x00000000}}, 0}, true, false},
     {"[ffff:ffff:ffff:1ffff:ffff:ffff:ffff:ffff]:65535", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0x00000000, 0x00000000, 0x00000000, 0x00000000}}, 0}, true, false},
     {"[ffff:ffff:ffff:ffff:1ffff:ffff:ffff:ffff]:65535", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0x00000000, 0x00000000, 0x00000000, 0x00000000}}, 0}, true, false},
     {"[ffff:ffff:ffff:ffff:ffff:1ffff:ffff:ffff]:65535", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0x00000000, 0x00000000, 0x00000000, 0x00000000}}, 0}, true, false},
     {"[ffff:ffff:ffff:ffff:ffff:ffff:1ffff:ffff]:65535", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0x00000000, 0x00000000, 0x00000000, 0x00000000}}, 0}, true, false},
     {"[ffff:ffff:ffff:ffff:ffff:ffff:ffff:1ffff]:65535", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0x00000000, 0x00000000, 0x00000000, 0x00000000}}, 0}, true, false},
     {"[ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff]:65536", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0x00000000, 0x00000000, 0x00000000, 0x00000000}}, 0}, true, true},
     {"ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff:65535", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0x00000000, 0x00000000, 0x00000000, 0x00000000}}, 0}, false, true},
     {"ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff:65535", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0x00000000, 0x00000000, 0x00000000, 0x00000000}}, 0}, true, true}
                                                    };

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

// Callback function for the cellular connect process.
static bool keepGoingCallback()
{
    bool keepGoing = true;

    if (cellularPortGetTimeMs() > gStopTimeMS) {
        keepGoing = false;
    }

    return keepGoing;
}

// Print out an address structure.
static void printAddress(CellularSockAddress_t *pAddress,
                         bool hasPort)
{
    switch (pAddress->ipAddress.type) {
        case CELLULAR_SOCK_ADDRESS_TYPE_V4:
            cellularPortLog("IPV4");
        break;
        case CELLULAR_SOCK_ADDRESS_TYPE_V6:
            cellularPortLog("IPV6");
        break;
        case CELLULAR_SOCK_ADDRESS_TYPE_V4_V6:
            cellularPortLog("IPV4V6");
        break;
        default:
            cellularPortLog("unknown type (%d)", pAddress->ipAddress.type);
        break;
    }

    cellularPortLog(" ");

    if (pAddress->ipAddress.type == CELLULAR_SOCK_ADDRESS_TYPE_V4) {
        for (int32_t x = 3; x >= 0; x--) {
            cellularPortLog("%u", (pAddress->ipAddress.address.ipv4 >> (x * 8)) & 0xFF);
            if (x > 0) {
                cellularPortLog(".");
            }
        }
        if (hasPort) {
            cellularPortLog(":%u", pAddress->port);
        }
    } else if (pAddress->ipAddress.type == CELLULAR_SOCK_ADDRESS_TYPE_V6) {
        if (hasPort) {
            cellularPortLog("[");
        }
        for (int32_t x = 3; x >= 0; x--) {
            cellularPortLog("%x:%x", pAddress->ipAddress.address.ipv6[x] >> 16,
                                     pAddress->ipAddress.address.ipv6[x] & 0xFFFF);
            if (x > 0) {
                cellularPortLog(":");
            }
        }
        if (hasPort) {
            cellularPortLog("]:%u", pAddress->port);
        }
    }
}

// Test that two address structures are the same
static void addressAssert(const CellularSockAddress_t *pAddress1,
                          const CellularSockAddress_t *pAddress2,
                          bool hasPort)
{
    CELLULAR_PORT_TEST_ASSERT(pAddress1->ipAddress.type == pAddress2->ipAddress.type);

    switch (pAddress1->ipAddress.type) {
        case CELLULAR_SOCK_ADDRESS_TYPE_V4:
            CELLULAR_PORT_TEST_ASSERT(pAddress1->ipAddress.address.ipv4 ==
                                      pAddress2->ipAddress.address.ipv4);
        break;
        case CELLULAR_SOCK_ADDRESS_TYPE_V6:
            CELLULAR_PORT_TEST_ASSERT(cellularPort_memcmp(pAddress1->ipAddress.address.ipv6,
                                                          pAddress2->ipAddress.address.ipv6,
                                                          sizeof(pAddress2->ipAddress.address.ipv6)) == 0);
        break;
        case CELLULAR_SOCK_ADDRESS_TYPE_V4_V6:
        // Deliberate fall-through
        default:
            CELLULAR_PORT_TEST_ASSERT(false);
        break;
    }

    if (hasPort) {
        CELLULAR_PORT_TEST_ASSERT(pAddress1->port == pAddress2->port);
    }
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: TESTS
 * -------------------------------------------------------------- */

/** Basic test: initialise and then deinitialise everything.
 */
CELLULAR_PORT_TEST_FUNCTION(void cellularSockTestInitialisation(),
                            "initialisation",
                            "sock")
{
    CellularPortQueueHandle_t queueHandle;

    CELLULAR_PORT_TEST_ASSERT(cellularPortInit() == 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPortUartInit(CELLULAR_CFG_WHRE_PIN_TXD,
                                                   CELLULAR_CFG_WHRE_PIN_RXD,
                                                   CELLULAR_CFG_WHRE_PIN_CTS,
                                                   CELLULAR_CFG_WHRE_PIN_RTS,
                                                   CELLULAR_CFG_BAUD_RATE,
                                                   CELLULAR_CFG_RTS_THRESHOLD,
                                                   CELLULAR_CFG_UART,
                                                   &queueHandle) == 0);
    CELLULAR_PORT_TEST_ASSERT(cellularCtrlInit(CELLULAR_CFG_WHRE_PIN_ENABLE_POWER,
                                               CELLULAR_CFG_WHRE_PIN_CP_ON,
                                               CELLULAR_CFG_WHRE_PIN_VINT,
                                               false,
                                               CELLULAR_CFG_UART,
                                               queueHandle) == 0);
    cellularCtrlDeinit();
    CELLULAR_PORT_TEST_ASSERT(cellularPortUartDeinit(CELLULAR_CFG_UART) == 0);
    cellularPortDeinit();

    // Allow idle task to run so that any deleted
    // tasks are actually deleted, required by some
    // operating systems (e.g. freeRTOS)
    cellularPortTaskBlock(100);
}

/** Test conversion of address strings into structs and
 * back again.
 */
CELLULAR_PORT_TEST_FUNCTION(void cellularSockTestAddressStrings(),
                            "addressStrings",
                            "sock")
{
    char buffer[64];
    int32_t errorCode;
    CellularSockAddress_t address;

    // No need initialise anything for this test
    for (size_t x = 0; x < sizeof(gTestAddressList) / sizeof(gTestAddressList[0]); x++) {
        cellularPortLog("CELLULAR_TEST: %d: original address string \"%s\" (%d byte(s)).\n",
                        x,
                        gTestAddressList[x].pAddressString,
                        cellularPort_strlen(gTestAddressList[x].pAddressString));
        // Convert string to struct
        pCellularPort_memset(&address, 0xFF, sizeof(address));
        errorCode = cellularSockStringToAddress(gTestAddressList[x].pAddressString, &address);
        cellularPortLog("CELLULAR_TEST: %d: cellularSockStringToAddress() returned %d.\n",
                        x, errorCode);
        if (gTestAddressList[x].shouldError) {
            CELLULAR_PORT_TEST_ASSERT(errorCode < 0);
        } else {
            CELLULAR_PORT_TEST_ASSERT(errorCode == 0);

            cellularPortLog("CELLULAR_TEST: %d: address struct should contain ", x);
            printAddress(&gTestAddressList[x].address, gTestAddressList[x].hasPort);
            cellularPortLog(".\n");

            cellularPortLog("CELLULAR_TEST: %d: address struct contains ", x);
            printAddress(&address, gTestAddressList[x].hasPort);
            cellularPortLog(".\n");

            addressAssert(&address, &gTestAddressList[x].address,
                          gTestAddressList[x].hasPort);

            if (gTestAddressList[x].hasPort) {
                // Now convert back to a string again
                pCellularPort_memset(buffer, 0xFF, sizeof(buffer));
                errorCode = cellularSockAddressToString(&address, buffer, sizeof(buffer));
                cellularPortLog("CELLULAR_TEST: %d: cellularSockAddressToString() returned %d",
                                x, errorCode);
                if (errorCode >= 0) {
                    cellularPortLog(", string is \"%s\" (%d byte(s))", buffer,
                                    cellularPort_strlen(buffer));
                }
                cellularPortLog(".\n");
                CELLULAR_PORT_TEST_ASSERT(errorCode == cellularPort_strlen(buffer));
                CELLULAR_PORT_TEST_ASSERT(cellularPort_strcmp(gTestAddressList[x].pAddressString, buffer) == 0);
            } else {
                // For ones without a port number we can converting the non-port
                // part of the address back into a string also
                pCellularPort_memset(buffer, 0xFF, sizeof(buffer));
                errorCode = cellularSockIpAddressToString(&(address.ipAddress), buffer,
                                                                        sizeof(buffer));
                cellularPortLog("CELLULAR_TEST: %d: cellularSockIpAddressToString() returned %d",
                                x, errorCode);
                if (errorCode >= 0) {
                    cellularPortLog(", address string is \"%s\" (%d byte(s))", buffer,
                                    cellularPort_strlen(buffer));
                }
                cellularPortLog(".\n");
                CELLULAR_PORT_TEST_ASSERT(errorCode == cellularPort_strlen(buffer));
                CELLULAR_PORT_TEST_ASSERT(cellularPort_strcmp(gTestAddressList[x].pAddressString, buffer) == 0);
            }
        }
    }
}

// End of file
