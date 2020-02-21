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
#include "cellular_cfg_test.h"

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

// Place to store the original RAT settings of the module.
static CellularCtrlRat_t gOriginalRats[CELLULAR_CTRL_MAX_NUM_SIMULTANEOUS_RATS];

// Place to store the original band mask settings of the module.
static uint64_t gOriginalMask;

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
     // Note: the answer looks peculiar but remember that element 0 of the array is at the lowest address in memory and element 3 at
     // the highest address so, for network byte order, the lowest two values (b and c in the first case below) are
     // stored in the lowest array index, etc.
     {"0:1:2:3:4:a:b:c", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0x000b000c, 0x0004000a, 0x00020003, 0x00000001}}, 0}, false, false},
     {"[0:1:2:3:4:a:b:c]:0", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0x000b000c, 0x0004000a, 0x00020003, 0x00000001}}, 0}, true, false},
     {"ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff}}, 0}, false, false},
     {"[ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff]:65535", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0xffffffff, 0xffffffff, 0xffffffff, 0xffffffff}}, 65535}, true, false},
     // IPV6 error cases
     {"[1ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff]:65535", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0x00000000, 0x00000000, 0x00000000, 0x00000000}}, 0}, true, true},
     {"[ffff:1ffff:ffff:ffff:ffff:ffff:ffff:ffff]:65535", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0x00000000, 0x00000000, 0x00000000, 0x00000000}}, 0}, true, true},
     {"[ffff:ffff:1ffff:ffff:ffff:ffff:ffff:ffff]:65535", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0x00000000, 0x00000000, 0x00000000, 0x00000000}}, 0}, true, true},
     {"[ffff:ffff:ffff:1ffff:ffff:ffff:ffff:ffff]:65535", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0x00000000, 0x00000000, 0x00000000, 0x00000000}}, 0}, true, true},
     {"[ffff:ffff:ffff:ffff:1ffff:ffff:ffff:ffff]:65535", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0x00000000, 0x00000000, 0x00000000, 0x00000000}}, 0}, true, true},
     {"[ffff:ffff:ffff:ffff:ffff:1ffff:ffff:ffff]:65535", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0x00000000, 0x00000000, 0x00000000, 0x00000000}}, 0}, true, true},
     {"[ffff:ffff:ffff:ffff:ffff:ffff:1ffff:ffff]:65535", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0x00000000, 0x00000000, 0x00000000, 0x00000000}}, 0}, true, true},
     {"[ffff:ffff:ffff:ffff:ffff:ffff:ffff:1ffff]:65535", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0x00000000, 0x00000000, 0x00000000, 0x00000000}}, 0}, true, true},
     {"[ffff:ffff:ffff:ffff:ffff:ffff:ffff:ffff]:65536", {{CELLULAR_SOCK_ADDRESS_TYPE_V6, .address.ipv6={0x00000000, 0x00000000, 0x00000000, 0x00000000}}, 0}, true, true}
                                                    };

// Data to exchange
static const char gSendData[] =  "_____0000:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0100:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0200:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0300:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0400:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0500:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0600:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0700:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0800:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____0900:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1000:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1100:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1200:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1300:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1400:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1500:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1600:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1700:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1800:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____1900:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789"
                                 "_____2000:0123456789012345678901234567890123456789"
                                 "01234567890123456789012345678901234567890123456789";

// Mutex wait on for data arrival.
static CellularPortMutexHandle_t gMutexHandleDataReceived = NULL;

// Mutex to show that the receive data task is running.
static CellularPortMutexHandle_t gMutexHandleDataReceivedTaskRunning = NULL;

// Task to receive data.
static CellularPortTaskHandle_t gTaskHandleDataReceived = NULL;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

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

// Callback function for the cellular networkConnect process.
static bool keepGoingCallback()
{
    bool keepGoing = true;

    if (cellularPortGetTickTimeMs() > gStopTimeMS) {
        keepGoing = false;
    }

    return keepGoing;
}

// Make sure that size is greater than 0 and no more than limit,
// useful since, when moduloing a very large number number,
// compilers sometimes screw up and produce a small *negative*
// number.  Who knew?  For example, GCC decided that
// 492318453 (0x1d582ef5) modulo 508 was -47 (0xffffffd1).
static int32_t fix(int32_t size, int32_t limit)
{
    if (size <= 0) {
        size = limit / 2; // better than 1
    } else if (size > limit) {
        size = limit;
    }

    return size;
}

// Connect to the network, saving existing settings first.
static void networkConnect(const char *pApn,
                           const char *pUsername,
                           const char *pPassword)
{
    for (size_t x = 0; x < sizeof (gOriginalRats) / sizeof (gOriginalRats[0]); x++) {
        gOriginalRats[x] = CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED;
    }

    cellularPortLog("CELLULAR_TEST_SOCK: saving existing settings...\n");
    // First, read out the existing RATs so that we can put them back
    for (size_t x = 0; x < sizeof (gOriginalRats) / sizeof (gOriginalRats[0]); x++) {
        gOriginalRats[x] = cellularCtrlGetRat(x);
    }
    // Then read out the existing band mask
    gOriginalMask = cellularCtrlGetBandMask(CELLULAR_CFG_TEST_RAT);

    cellularPortLog("CELLULAR_SOCK_TEST: setting sole RAT to %d...\n", CELLULAR_CFG_TEST_RAT);
    CELLULAR_PORT_TEST_ASSERT(cellularCtrlSetRat(CELLULAR_CFG_TEST_RAT) == 0);
    CELLULAR_PORT_TEST_ASSERT(cellularCtrlSetBandMask(CELLULAR_CFG_TEST_RAT,
                                                      CELLULAR_CFG_TEST_BANDMASK) == 0);
    CELLULAR_PORT_TEST_ASSERT(cellularCtrlReboot() == 0);

    cellularPortLog("CELLULAR_SOCK_TEST: connecting...\n");
    gStopTimeMS = cellularPortGetTickTimeMs() + (CELLULAR_CFG_TEST_CONNECT_TIMEOUT_SECONDS * 1000);
    CELLULAR_PORT_TEST_ASSERT(cellularCtrlConnect(keepGoingCallback,
                                                  pApn, pUsername, pPassword) == 0);
    cellularPortLog("CELLULAR_SOCK_TEST: RAT %d, cellularCtrlGetNetworkStatus() %d.\n",
                    CELLULAR_CFG_TEST_RAT, cellularCtrlGetNetworkStatus());
    CELLULAR_PORT_TEST_ASSERT(cellularCtrlGetNetworkStatus() == CELLULAR_CTRL_NETWORK_STATUS_REGISTERED);
}

// Disconnect from the network and restore teh saved settings.
static void networkDisconnect()
{
    bool screwy = false;

    cellularPortLog("CELLULAR_SOCK_TEST: disconnecting...\n");
    CELLULAR_PORT_TEST_ASSERT(cellularCtrlDisconnect() == 0);

    cellularPortLog("CELLULAR_SOCK_TEST: restoring saved settings...\n");
    // No asserts here, we need it to plough on and succeed
    if (cellularCtrlSetBandMask(CELLULAR_CFG_TEST_RAT, gOriginalMask) != 0) {
        cellularPortLog("CELLULAR_SOCK_TEST: !!! ATTENTION: the band mask for RAT %d on the module under test may have been left screwy, please check!!!\n", CELLULAR_CFG_TEST_RAT);
    }
    for (size_t x = 0; x < sizeof (gOriginalRats) / sizeof (gOriginalRats[0]); x++) {
        cellularCtrlSetRatRank(gOriginalRats[x], x);
    }
    cellularCtrlReboot();
    for (size_t x = 0; x < sizeof (gOriginalRats) / sizeof (gOriginalRats[0]); x++) {
        if (cellularCtrlGetRat(x) != gOriginalRats[x]) {
            screwy = true;
        }
    }
    if (screwy) {
        cellularPortLog("CELLULAR_SOCK_TEST: !!! ATTENTION: the RAT settings of the module under test may have been left screwy, please check!!!\n");
    }
}

// Check a buffer against the contents of gSendData
// and print out useful info if they differ
static bool checkAgainstSendData(char *pDataReceived, size_t sizeBytes)
{
    bool success = true;
    int32_t x;
    int32_t y;
    int32_t z;

    if (sizeBytes == sizeof(gSendData)) {
        for (x = 0; ((*(pDataReceived + x) == *(gSendData + x))) && (x < (int32_t) sizeof(gSendData)); x++) {
        }
        if (x != (int32_t) sizeof(gSendData)) {
            y = x - 5;
            if (y < 0) {
                y = 0;
            }
            z = 10;
            if (y + z > sizeof(gSendData)) {
                z = sizeof(gSendData) - y;
            }
            cellularPortLog("CELLULAR_TEST_SOCK: difference at character %d (sent \"%*.*s\", received \"%*.*s\").\n",
                             x + 1, z, z, gSendData + y, z, z, pDataReceived + y);
            success = false;
        }
    } else {
        cellularPortLog("CELLULAR_TEST_SOCK: %d byte(s) missing (%d byte(s) received when %d were expected)).\n",
                         sizeof(gSendData) - sizeBytes, sizeBytes, sizeof(gSendData));
        success = false;
    }

    return success;
}

// Do a UDP socket echo test to a given host of a given packet size.
static void doUdpEchoBasic(CellularSockDescriptor_t sockDescriptor,
                           CellularSockAddress_t *pRemoteAddress,
                           size_t sendSizeBytes)
{
    bool success = false;
    void *pDataReceived = pCellularPort_malloc(sendSizeBytes);
    CellularSockAddress_t senderAddress;
    int32_t sizeBytes;

    CELLULAR_PORT_TEST_ASSERT(pDataReceived != NULL);

    // Retry this a few times, don't want to fail due to a flaky link
    for (size_t x = 0; !success && (x < CELLULAR_CFG_TEST_UDP_RETRIES); x++) {
        cellularPortLog("CELLULAR_SOCK_TEST: echo testing UDP packet size %d byte(s), try %d.\n",
                        sendSizeBytes, x + 1);
        sizeBytes = cellularSockSendTo(sockDescriptor, pRemoteAddress,
                                       (void *) gSendData, sendSizeBytes);
        if (sizeBytes >= 0) {
           cellularPortLog("CELLULAR_SOCK_TEST: sent %d byte(s).\n", sizeBytes);
        } else {
           cellularPortLog("CELLULAR_SOCK_TEST: failed to send.\n");
        }
        if (sizeBytes == sendSizeBytes) {
            sizeBytes = cellularSockReceiveFrom(sockDescriptor, &senderAddress,
                                                pDataReceived, sendSizeBytes);
            if (sizeBytes >= 0) {
                cellularPortLog("CELLULAR_SOCK_TEST: received %d byte(s)", sizeBytes);
                cellularPortLog(" from ");
                printAddress(&senderAddress, true);
                cellularPortLog(".\n");
            } else {
                cellularPortLog("CELLULAR_SOCK_TEST: received nothing back.\n");
            }
            if (sizeBytes == sendSizeBytes) {
                CELLULAR_PORT_TEST_ASSERT(cellularPort_memcmp(gSendData, pDataReceived, sendSizeBytes) == 0);
                if (pRemoteAddress != NULL) {
                    addressAssert(pRemoteAddress, &senderAddress, true);
                }
                success = true;
            }
        }
    }

    CELLULAR_PORT_TEST_ASSERT(success);

    cellularPort_free(pDataReceived);
}

// Task to receive UPD packets asynchronously.
static void receiveDataTaskUdp(void *pParameters)
{
    int32_t lockResult;
    int32_t waitCountSeconds = 0;
    char *pDataReceived;
    size_t offset = 0;
    size_t sizeBytes;
    bool success = false;
    size_t packetCount = 0;
    CellularSockDescriptor_t sockDescriptor;

    cellularPortLog("1 Boo!.\n");
    cellularPortTaskBlock(100);

    CELLULAR_PORT_MUTEX_LOCK(gMutexHandleDataReceivedTaskRunning);

    cellularPortLog("2 Boo!.\n");
    cellularPortTaskBlock(100);

    sockDescriptor = *((CellularSockDescriptor_t *) pParameters);

    cellularPortLog("3 Boo!.\n");
    cellularPortTaskBlock(100);

    pDataReceived = (char *) pCellularPort_malloc(sizeof(gSendData));
    CELLULAR_PORT_TEST_ASSERT(pDataReceived != NULL)
    pCellularPort_memset(pDataReceived, 0, sizeof(gSendData));

    cellularPortLog("4 Boo!.\n");
    cellularPortTaskBlock(100);

    while ((waitCountSeconds < 20) && (offset < sizeof(gSendData))) {
        // Try to lock the received data mutex
        lockResult = cellularPortMutexTryLock(gMutexHandleDataReceived, 1000);
        if (lockResult == 0) {
            // We were able to unlock the mutex, which must mean there's data
            // Lock it again and process the data
            CELLULAR_PORT_TEST_ASSERT(cellularPortMutexUnlock(gMutexHandleDataReceived) == 0);
            // Do a receive
            sizeBytes = cellularSockReceiveFrom(sockDescriptor, NULL,
                                                pDataReceived + offset,
                                                sizeof(gSendData) - offset);
            CELLULAR_PORT_TEST_ASSERT(sizeBytes > 0);
            if (sizeBytes > 0) {
                offset += sizeBytes;
                packetCount++;
            }
            cellularPortTaskBlock(1000);
        }
        waitCountSeconds++;
    }

    // Check that we reassembled everything correctly
    success = checkAgainstSendData(pDataReceived, offset);
    cellularPort_free(pDataReceived);

    cellularPortLog("CELLULAR_SOCK_TEST: async received data task exiting after receiving %d packet(s) totalling %d byte(s) in %d second(s)ish.\n",
                    packetCount + 1, offset, waitCountSeconds);

    CELLULAR_PORT_TEST_ASSERT(success);

    CELLULAR_PORT_MUTEX_UNLOCK(gMutexHandleDataReceivedTaskRunning);

    // Delete ourself: only valid way out in Free RTOS
    cellularPortTaskDelete(NULL);
}

// Set the passed-in parameter pointer to be true.
static void setBool(void *pParam)
{
    *((bool *) pParam) = true;
    if (gMutexHandleDataReceived != NULL) {
        cellularPortMutexUnlock(gMutexHandleDataReceived);
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
 * TODO: test for string overruns.
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
        cellularPortLog("CELLULAR_SOCK_TEST: %d: cellularSockStringToAddress() returned %d.\n",
                        x, errorCode);
        if (gTestAddressList[x].shouldError) {
            CELLULAR_PORT_TEST_ASSERT(errorCode < 0);
        } else {
            CELLULAR_PORT_TEST_ASSERT(errorCode == 0);

            cellularPortLog("CELLULAR_SOCK_TEST: %d: address struct should contain ", x);
            printAddress(&gTestAddressList[x].address, gTestAddressList[x].hasPort);
            cellularPortLog(".\n");

            cellularPortLog("CELLULAR_SOCK_TEST: %d: address struct contains ", x);
            printAddress(&address, gTestAddressList[x].hasPort);
            cellularPortLog(".\n");

            addressAssert(&address, &gTestAddressList[x].address,
                          gTestAddressList[x].hasPort);

            if (gTestAddressList[x].hasPort) {
                // Now convert back to a string again
                pCellularPort_memset(buffer, 0xFF, sizeof(buffer));
                errorCode = cellularSockAddressToString(&address, buffer, sizeof(buffer));
                cellularPortLog("CELLULAR_SOCK_TEST: %d: cellularSockAddressToString() returned %d",
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
                cellularPortLog("CELLULAR_SOCK_TEST: %d: cellularSockIpAddressToString() returned %d",
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

/** Basic UDP echo test.
 * TODO: test error cases.
 */
CELLULAR_PORT_TEST_FUNCTION(void cellularSockTestUdp(),
                            "udpEchoBasic",
                            "sock")
{
    CellularPortQueueHandle_t queueHandle;
    CellularSockAddress_t remoteAddress;
    CellularSockAddress_t address;
    CellularSockDescriptor_t sockDescriptor;
    bool dataCallbackCalled;
    int32_t errorCode;
    size_t sizeBytes;

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

    CELLULAR_PORT_TEST_ASSERT(cellularCtrlPowerOn(NULL) == 0);

    // Connect to the cellular network
    networkConnect(CELLULAR_CFG_TEST_APN,
                   CELLULAR_CFG_TEST_USERNAME,
                   CELLULAR_CFG_TEST_PASSWORD);

    // Reset errno at the start
    cellularPort_errno_set(0);

    cellularPortLog("CELLULAR_TEST_SOCK: doing DNS look-up on \"%s\"...\n",
                    CELLULAR_CFG_TEST_ECHO_SERVER_DOMAIN_NAME);
    errorCode = cellularSockGetHostByName(CELLULAR_CFG_TEST_ECHO_SERVER_DOMAIN_NAME,
                                          &(remoteAddress.ipAddress));
    cellularPortLog("CELLULAR_TEST_SOCK: cellularSockGetHostByName() returned %d.\n",
                    errorCode);
    CELLULAR_PORT_TEST_ASSERT(errorCode == 0);
    remoteAddress.port = CELLULAR_CFG_TEST_ECHO_SERVER_UDP_PORT;

    cellularPortLog("CELLULAR_TEST_SOCK: creating UDP socket...\n");
    sockDescriptor = cellularSockCreate(CELLULAR_SOCK_TYPE_DGRAM,
                                        CELLULAR_SOCK_PROTOCOL_UDP);
    cellularPortLog("CELLULAR_TEST_SOCK: UDP socket descriptor 0x%x, errno %d.\n",
                    sockDescriptor, cellularPort_errno_get());
    CELLULAR_PORT_TEST_ASSERT(sockDescriptor >= 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() == 0);

    cellularPortLog("CELLULAR_TEST_SOCK: get local address...\n");
    CELLULAR_PORT_TEST_ASSERT(cellularSockGetLocalAddress(sockDescriptor,
                                                          &address) == 0);
    cellularPortLog("CELLULAR_TEST_SOCK: local address is: ");
    printAddress(&address, true);
    cellularPortLog(".\n");

    // Set up the callback
    dataCallbackCalled = false;
    CELLULAR_PORT_TEST_ASSERT(cellularSockRegisterCallbackData(sockDescriptor,
                                                               setBool,
                                                               &dataCallbackCalled) == 0);
    CELLULAR_PORT_TEST_ASSERT(!dataCallbackCalled);

    cellularPortLog("CELLULAR_TEST_SOCK: first test run without connect(), sending to address ");
    printAddress(&remoteAddress, true);
    cellularPortLog("...\n");
    // Test min size
    doUdpEchoBasic(sockDescriptor, &remoteAddress, 1);

    CELLULAR_PORT_TEST_ASSERT(dataCallbackCalled);
    dataCallbackCalled = false;
    // Remove the data callback
    CELLULAR_PORT_TEST_ASSERT(cellularSockRegisterCallbackData(sockDescriptor,
                                                               NULL, NULL) == 0);

    // Test max size
    doUdpEchoBasic(sockDescriptor, &remoteAddress, CELLULAR_SOCK_MAX_UDP_PACKET_SIZE);

    // Test some random sizes in-between
    for (size_t x = 0; x < 10; x++) {
        sizeBytes = (cellularPort_rand() % CELLULAR_SOCK_MAX_UDP_PACKET_SIZE) + 1;
        sizeBytes = fix(sizeBytes, CELLULAR_SOCK_MAX_UDP_PACKET_SIZE);
        doUdpEchoBasic(sockDescriptor, &remoteAddress, sizeBytes);
    }

    cellularPortLog("CELLULAR_TEST_SOCK: check that cellularSockGetRemoteAddress() fails...\n");
    CELLULAR_PORT_TEST_ASSERT(cellularSockGetRemoteAddress(sockDescriptor,
                                                           &address) < 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() > 0);
    cellularPort_errno_set(0);

    cellularPortLog("CELLULAR_TEST_SOCK: now connect socket to \"%s:%d\"...\n",
                    CELLULAR_CFG_TEST_ECHO_SERVER_DOMAIN_NAME,
                    CELLULAR_CFG_TEST_ECHO_SERVER_UDP_PORT);
    errorCode = cellularSockConnect(sockDescriptor,
                                    &remoteAddress);
    cellularPortLog("CELLULAR_TEST_SOCK: cellularSockConnect() returned %d, errno %d.\n",
                    errorCode, cellularPort_errno_get());
    CELLULAR_PORT_TEST_ASSERT(errorCode >= 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() == 0);

    cellularPortLog("CELLULAR_TEST_SOCK: check that cellularSockGetRemoteAddress() works...\n");
    CELLULAR_PORT_TEST_ASSERT(cellularSockGetRemoteAddress(sockDescriptor,
                                                           &address) == 0);
    addressAssert(&remoteAddress, &address, true);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() == 0);

    cellularPortLog("CELLULAR_TEST_SOCK: second run after connect()...\n");
    // Test min and max only this time
    doUdpEchoBasic(sockDescriptor, NULL, 1);
    doUdpEchoBasic(sockDescriptor, NULL, CELLULAR_SOCK_MAX_UDP_PACKET_SIZE);

    cellularPortLog("CELLULAR_TEST_SOCK: closing socket...\n");
    errorCode = cellularSockClose(sockDescriptor);
    cellularPortLog("CELLULAR_TEST_SOCK: cellularSockClose() returned %d, errno %d.\n",
                    errorCode, cellularPort_errno_get());
    CELLULAR_PORT_TEST_ASSERT(errorCode >= 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() == 0);

    cellularPortLog("CELLULAR_TEST_SOCK: cleaning up...\n");
    cellularSockCleanUp();

    // Disconnect from the cellular network and tidy up
    networkDisconnect();

    cellularCtrlPowerOff(NULL);

    cellularCtrlDeinit();
    CELLULAR_PORT_TEST_ASSERT(cellularPortUartDeinit(CELLULAR_CFG_UART) == 0);
    cellularPortDeinit();

    // Allow idle task to run so that any deleted
    // tasks are actually deleted, required by some
    // operating systems (e.g. freeRTOS)
    cellularPortTaskBlock(100);
}

/** UDP echo test that throws up multiple packets
 * before addressing the received packets.
 * TODO: test error cases.
 */
CELLULAR_PORT_TEST_FUNCTION(void cellularSockTestUdp(),
                            "udpEchoNonPingPong",
                            "sock")
{
    CellularPortQueueHandle_t queueHandle;
    CellularSockAddress_t remoteAddress;
    CellularSockDescriptor_t sockDescriptor;
    int32_t errorCode;
    bool dataCallbackCalled;
    bool allPacketsReceived;
    bool success;
    int32_t tries = 0;
    size_t sizeBytes = 0;
    size_t offset;
    int32_t x;
    char *pDataReceived;
    int64_t startTimeMs;

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

    CELLULAR_PORT_TEST_ASSERT(cellularCtrlPowerOn(NULL) == 0);

    // Connect to the cellular network
    networkConnect(CELLULAR_CFG_TEST_APN,
                   CELLULAR_CFG_TEST_USERNAME,
                   CELLULAR_CFG_TEST_PASSWORD);

    // Reset errno at the start
    cellularPort_errno_set(0);

    cellularPortLog("CELLULAR_TEST_SOCK: doing DNS look-up on \"%s\"...\n",
                    CELLULAR_CFG_TEST_ECHO_SERVER_DOMAIN_NAME);
    errorCode = cellularSockGetHostByName(CELLULAR_CFG_TEST_ECHO_SERVER_DOMAIN_NAME,
                                          &(remoteAddress.ipAddress));
    cellularPortLog("CELLULAR_TEST_SOCK: cellularSockGetHostByName() returned %d.\n",
                    errorCode);
    CELLULAR_PORT_TEST_ASSERT(errorCode == 0);
    remoteAddress.port = CELLULAR_CFG_TEST_ECHO_SERVER_UDP_PORT;

    cellularPortLog("CELLULAR_TEST_SOCK: creating UDP socket...\n");
    sockDescriptor = cellularSockCreate(CELLULAR_SOCK_TYPE_DGRAM,
                                        CELLULAR_SOCK_PROTOCOL_UDP);
    cellularPortLog("CELLULAR_TEST_SOCK: UDP socket descriptor 0x%x, errno %d.\n",
                    sockDescriptor, cellularPort_errno_get());
    CELLULAR_PORT_TEST_ASSERT(sockDescriptor >= 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() == 0);

    // Set up the callback
    dataCallbackCalled = false;
    CELLULAR_PORT_TEST_ASSERT(cellularSockRegisterCallbackData(sockDescriptor,
                                                               setBool,
                                                               &dataCallbackCalled) == 0);
    CELLULAR_PORT_TEST_ASSERT(!dataCallbackCalled);

    cellularPortLog("CELLULAR_TEST_SOCK: sending to address ");
    printAddress(&remoteAddress, true);
    cellularPortLog("...\n");

    do {
        cellularPortLog("CELLULAR_TEST_SOCK: UDP packet size test, test try %d, flushing socket...\n", tries + 1);
        pDataReceived = (char *) pCellularPort_malloc(CELLULAR_SOCK_MAX_UDP_PACKET_SIZE);
        CELLULAR_PORT_TEST_ASSERT(pDataReceived != NULL);
        /// TODO set a short socket timeout just for this bit
        while (cellularSockReceiveFrom(sockDescriptor, NULL, pDataReceived,
                                       CELLULAR_SOCK_MAX_UDP_PACKET_SIZE) > 0) {
            // Throw it away
        }
        cellularPort_free(pDataReceived);

        // Throw random sized UDP packets up...
        offset = 0;
        x = 0;
        while (offset < sizeof(gSendData)) {
            sizeBytes = (cellularPort_rand() % (CELLULAR_SOCK_MAX_UDP_PACKET_SIZE / 2)) + 1;
            sizeBytes = fix(sizeBytes, CELLULAR_SOCK_MAX_UDP_PACKET_SIZE / 2);
            if (offset + sizeBytes > sizeof(gSendData)) {
                sizeBytes = sizeof(gSendData) - offset;
            }
            success = false;
            for (size_t y = 0; !success && (y < CELLULAR_CFG_TEST_UDP_RETRIES); y++) {
                cellularPortLog("CELLULAR_TEST_SOCK: sending UDP packet number %d, size %d byte(s), send try %d.\n",
                                x + 1, sizeBytes, y + 1);
                if (cellularSockSendTo(sockDescriptor, &remoteAddress,
                                       gSendData + offset, sizeBytes) == sizeBytes) {
                    success = true;
                    offset += sizeBytes;
                }
            }
            x++;
            CELLULAR_PORT_TEST_ASSERT(success);
        }
        cellularPortLog("CELLULAR_TEST_SOCK: a total of %d UDP packet(s) sent, now receiving...\n", x + 1);

        // ...and capture them all again afterwards
        pDataReceived = (char *) pCellularPort_malloc(sizeof(gSendData));
        CELLULAR_PORT_TEST_ASSERT(pDataReceived != NULL);
        pCellularPort_memset(pDataReceived, 0, sizeof(gSendData));
        startTimeMs = cellularPortGetTickTimeMs();
        offset = 0;
        for (x = 0; (offset < sizeof(gSendData)) &&
                    (cellularPortGetTickTimeMs() - startTimeMs < 10000); x++) {
            sizeBytes = cellularSockReceiveFrom(sockDescriptor, NULL,
                                                pDataReceived + offset,
                                                sizeof(gSendData) - offset);
            if (sizeBytes > 0) {
                cellularPortLog("CELLULAR_TEST_SOCK: received UDP packet number %d, size %d byte(s).\n",
                                x + 1, sizeBytes);
                offset += sizeBytes;
            }
        }
        sizeBytes = offset;
        cellularPortLog("CELLULAR_TEST_SOCK: either received everything back or timed out waiting.\n");

        // Check that we reassembled everything correctly
        allPacketsReceived = checkAgainstSendData(pDataReceived, sizeBytes);
        cellularPort_free(pDataReceived);
        tries++;
    } while (!allPacketsReceived && (tries < CELLULAR_CFG_TEST_UDP_RETRIES));

    CELLULAR_PORT_TEST_ASSERT(allPacketsReceived);
    CELLULAR_PORT_TEST_ASSERT(dataCallbackCalled);

    cellularPortLog("CELLULAR_TEST_SOCK: closing socket...\n");
    errorCode = cellularSockClose(sockDescriptor);
    cellularPortLog("CELLULAR_TEST_SOCK: cellularSockClose() returned %d, errno %d.\n",
                    errorCode, cellularPort_errno_get());
    CELLULAR_PORT_TEST_ASSERT(errorCode >= 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() == 0);

    cellularPortLog("CELLULAR_TEST_SOCK: cleaning up...\n");
    cellularSockCleanUp();

    // Disconnect from the cellular network and tidy up
    networkDisconnect();

    cellularCtrlPowerOff(NULL);

    cellularCtrlDeinit();
    CELLULAR_PORT_TEST_ASSERT(cellularPortUartDeinit(CELLULAR_CFG_UART) == 0);
    cellularPortDeinit();

    // Allow idle task to run so that any deleted
    // tasks are actually deleted, required by some
    // operating systems (e.g. freeRTOS)
    cellularPortTaskBlock(100);
}

/** UDP echo test that does asynchronous receive.
 */
CELLULAR_PORT_TEST_FUNCTION(void cellularSockTestUdp(),
                            "udpEchoAsync",
                            "sock")
{
    CellularPortQueueHandle_t queueHandle;
    CellularSockAddress_t remoteAddress;
    CellularSockDescriptor_t sockDescriptor;
    int32_t errorCode;
    bool dataCallbackCalled;
    bool success;
    size_t sizeBytes = 0;
    size_t offset = 0;
    int32_t x = 0;
    void *pParam = &sockDescriptor;

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

    CELLULAR_PORT_TEST_ASSERT(cellularCtrlPowerOn(NULL) == 0);

    // Connect to the cellular network
    networkConnect(CELLULAR_CFG_TEST_APN,
                   CELLULAR_CFG_TEST_USERNAME,
                   CELLULAR_CFG_TEST_PASSWORD);

    // Reset errno at the start
    cellularPort_errno_set(0);

    cellularPortLog("CELLULAR_TEST_SOCK: doing DNS look-up on \"%s\"...\n",
                    CELLULAR_CFG_TEST_ECHO_SERVER_DOMAIN_NAME);
    errorCode = cellularSockGetHostByName(CELLULAR_CFG_TEST_ECHO_SERVER_DOMAIN_NAME,
                                          &(remoteAddress.ipAddress));
    cellularPortLog("CELLULAR_TEST_SOCK: cellularSockGetHostByName() returned %d.\n",
                    errorCode);
    CELLULAR_PORT_TEST_ASSERT(errorCode == 0);
    remoteAddress.port = CELLULAR_CFG_TEST_ECHO_SERVER_UDP_PORT;

    cellularPortLog("CELLULAR_TEST_SOCK: creating UDP socket...\n");
    sockDescriptor = cellularSockCreate(CELLULAR_SOCK_TYPE_DGRAM,
                                        CELLULAR_SOCK_PROTOCOL_UDP);
    cellularPortLog("CELLULAR_TEST_SOCK: UDP socket descriptor 0x%x, errno %d.\n",
                    sockDescriptor, cellularPort_errno_get());
    CELLULAR_PORT_TEST_ASSERT(sockDescriptor >= 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() == 0);

    // Create a mutex that we can wait on for received data.
    CELLULAR_PORT_TEST_ASSERT(cellularPortMutexCreate(&gMutexHandleDataReceived) == 0);

    // Create a mutex that we can use to indicate that the
    // received data task is running
    CELLULAR_PORT_TEST_ASSERT(cellularPortMutexCreate(&gMutexHandleDataReceivedTaskRunning) == 0);

    // Create a task that will receive the data
    CELLULAR_PORT_TEST_ASSERT(cellularPortTaskCreate(receiveDataTaskUdp,
                                                     "testTaskRxData",
                                                     5128, (void **) &pParam, 20,
                                                     &gTaskHandleDataReceived) == 0);

    // Set up the callback that will signal data reception
    CELLULAR_PORT_TEST_ASSERT(cellularSockRegisterCallbackData(sockDescriptor,
                                                               setBool,
                                                               &dataCallbackCalled) == 0);

    cellularPortLog("CELLULAR_TEST_SOCK: sending to address ");
    printAddress(&remoteAddress, true);
    cellularPortLog("...\n");

    // Throw random sized UDP packets up...
    offset = 0;
    x = 0;
    while (offset < sizeof(gSendData)) {
        sizeBytes = (cellularPort_rand() % (CELLULAR_SOCK_MAX_UDP_PACKET_SIZE / 2)) + 1;
        sizeBytes = fix(sizeBytes, CELLULAR_SOCK_MAX_UDP_PACKET_SIZE / 2);
        if (offset + sizeBytes > sizeof(gSendData)) {
            sizeBytes = sizeof(gSendData) - offset;
        }
        success = false;
        for (size_t y = 0; !success && (y < CELLULAR_CFG_TEST_UDP_RETRIES); y++) {
            cellularPortLog("CELLULAR_TEST_SOCK: sending UDP packet number %d, size %d byte(s), send try %d.\n",
                            x + 1, sizeBytes, y + 1);
            if (cellularSockSendTo(sockDescriptor, &remoteAddress,
                                   gSendData + offset, sizeBytes) == sizeBytes) {
                success = true;
                offset += sizeBytes;
            }
        }
        x++;
        CELLULAR_PORT_TEST_ASSERT(success);
    }
    cellularPortLog("CELLULAR_TEST_SOCK: a total of %d UDP packet(s) sent, %d byte(s).\n",
                    x + 1, offset);

    // Wait for the receive task to finish receiving them
    CELLULAR_PORT_MUTEX_LOCK(gMutexHandleDataReceivedTaskRunning);
    CELLULAR_PORT_MUTEX_UNLOCK(gMutexHandleDataReceivedTaskRunning);

    CELLULAR_PORT_TEST_ASSERT(cellularPortMutexDelete(gMutexHandleDataReceived) == 0);
    gMutexHandleDataReceived = NULL;
    CELLULAR_PORT_TEST_ASSERT(cellularPortMutexDelete(gMutexHandleDataReceivedTaskRunning) == 0);
    gMutexHandleDataReceivedTaskRunning = NULL;

    cellularPortLog("CELLULAR_TEST_SOCK: closing socket...\n");
    errorCode = cellularSockClose(sockDescriptor);
    cellularPortLog("CELLULAR_TEST_SOCK: cellularSockClose() returned %d, errno %d.\n",
                    errorCode, cellularPort_errno_get());
    CELLULAR_PORT_TEST_ASSERT(errorCode >= 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() == 0);

    cellularPortLog("CELLULAR_TEST_SOCK: cleaning up...\n");
    cellularSockCleanUp();

    // Disconnect from the cellular network and tidy up
    networkDisconnect();

    cellularCtrlPowerOff(NULL);

    cellularCtrlDeinit();
    CELLULAR_PORT_TEST_ASSERT(cellularPortUartDeinit(CELLULAR_CFG_UART) == 0);
    cellularPortDeinit();

    // Allow idle task to run so that any deleted
    // tasks are actually deleted, required by some
    // operating systems (e.g. freeRTOS)
    cellularPortTaskBlock(100);
}


// End of file
