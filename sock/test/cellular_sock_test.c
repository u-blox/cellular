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

// The guard length to include before and after a packet buffer
#define CELLULAR_SOCK_TEST_GUARD_LENGTH_SIZE_BYTES 256

// The fill character that should be in that guard area
#define CELLULAR_SOCK_TEST_FILL_CHARACTER 0xAA

// The queue length, used for asynchronous tests.
#define CELLULAR_SOCK_TEST_RECEIVE_QUEUE_LENGTH 10

// A sensible maximum size for UDP packets sent over
// the public internet.
#define CELLULAR_SOCK_TEST_MAX_UDP_PACKET_SIZE 500

// The maximum TCP read/write size.
#define CELLULAR_SOCK_TEST_MAX_TCP_READ_WRITE_SIZE CELLULAR_SOCK_MAX_SEGMENT_LENGTH_BYTES

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

// A string of all possible characters, including strings`
// that might appear as terminators in the AT interfacce,
// and including an 'x' on the end which is intended to be
// overwritten with a NULL in order to test that NULLs are
// carried through also
static const char gAllChars[] = "The quick brown fox jumps over the lazy dog 0123456789 "
                                "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e"
                                "\x0f\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c"
                                "\x1d\x1e!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~\x7f"
                                "\r\nOK\r\n \r\nERROR\r\nx";

// Queue on which to send notifications of data arrival.
static CellularPortQueueHandle_t gQueueHandleDataReceived = NULL;

// Mutex to show that the receive data task is running.
static CellularPortMutexHandle_t gMutexHandleDataReceivedTaskRunning = NULL;

// Task to receive data.
static CellularPortTaskHandle_t gTaskHandleDataReceived = NULL;

// Success flag for asynch receive data task.
static int32_t gAsyncReturnCode;

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

    cellularPortLog("CELLULAR_SOCK_TEST: saving existing settings...\n");
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
        // Run through checking that the characters are the same
        for (x = 0; ((*(pDataReceived + x + CELLULAR_SOCK_TEST_GUARD_LENGTH_SIZE_BYTES) == *(gSendData + x))) &&
                    (x < (int32_t) sizeof(gSendData)); x++) {
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
            cellularPortLog("CELLULAR_SOCK_TEST: difference at character %d (sent \"%*.*s\", received \"%*.*s\").\n",
                             x + 1, z, z, gSendData + y,
                             z, z, pDataReceived + y + CELLULAR_SOCK_TEST_GUARD_LENGTH_SIZE_BYTES);
            success = false;
        } else {
            // If they were all the same, check for overrun and underrun
            for (x = 0; x < CELLULAR_SOCK_TEST_GUARD_LENGTH_SIZE_BYTES; x++) {
                if (*(pDataReceived + x) != CELLULAR_SOCK_TEST_FILL_CHARACTER) {
                    cellularPortLog("CELLULAR_SOCK_TEST: guard area %d byte(s) before start of buffer has been overwritten (expected 0x%02x, got 0x%02x %d '%c').\n",
                                     CELLULAR_SOCK_TEST_GUARD_LENGTH_SIZE_BYTES - x, CELLULAR_SOCK_TEST_FILL_CHARACTER,
                                     *(pDataReceived + x), *(pDataReceived + x), *(pDataReceived + x));
                    break;
                    success = false;
                }
                if (*(pDataReceived + CELLULAR_SOCK_TEST_GUARD_LENGTH_SIZE_BYTES + sizeof(gSendData) + x) != CELLULAR_SOCK_TEST_FILL_CHARACTER) {
                    cellularPortLog("CELLULAR_SOCK_TEST: guard area %d byte(s) after end of buffer has been overwritten (expected 0x%02x, got 0x%02x %d '%c').\n",
                                     x, CELLULAR_SOCK_TEST_FILL_CHARACTER,
                                     *(pDataReceived + sizeof(gSendData) + x),
                                     *(pDataReceived + sizeof(gSendData) + x),
                                     *(pDataReceived + sizeof(gSendData) + x));
                    break;
                    success = false;
                }
            }
        }
    } else {
        cellularPortLog("CELLULAR_SOCK_TEST: %d byte(s) missing (%d byte(s) received when %d were expected)).\n",
                         sizeof(gSendData) - sizeBytes, sizeBytes, sizeof(gSendData));
        success = false;
    }

    return success;
}

// Do a UDP socket echo test to a given host of a given packet size.
static void doUdpEchoBasic(CellularSockDescriptor_t sockDescriptor,
                           CellularSockAddress_t *pRemoteAddress,
                           const char *pSendData,
                           size_t sendSizeBytes)
{
    bool success = false;
    char *pDataReceived = (char *) pCellularPort_malloc(sendSizeBytes + (CELLULAR_SOCK_TEST_GUARD_LENGTH_SIZE_BYTES * 2));
    CellularSockAddress_t senderAddress;
    int32_t sizeBytes;

    CELLULAR_PORT_TEST_ASSERT(pDataReceived != NULL);

    // Retry this a few times, don't want to fail due to a flaky link
    for (size_t x = 0; !success && (x < CELLULAR_CFG_TEST_UDP_RETRIES); x++) {
        cellularPortLog("CELLULAR_SOCK_TEST: echo testing UDP packet size %d byte(s), try %d.\n",
                        sendSizeBytes, x + 1);
        sizeBytes = cellularSockSendTo(sockDescriptor, pRemoteAddress,
                                       (void *) pSendData, sendSizeBytes);
        if (sizeBytes >= 0) {
           cellularPortLog("CELLULAR_SOCK_TEST: sent %d byte(s).\n", sizeBytes);
        } else {
           cellularPortLog("CELLULAR_SOCK_TEST: failed to send.\n");
        }
        if (sizeBytes == sendSizeBytes) {
            pCellularPort_memset(pDataReceived, CELLULAR_SOCK_TEST_FILL_CHARACTER, sendSizeBytes + (CELLULAR_SOCK_TEST_GUARD_LENGTH_SIZE_BYTES * 2));
            sizeBytes = cellularSockReceiveFrom(sockDescriptor, &senderAddress,
                                                pDataReceived + CELLULAR_SOCK_TEST_GUARD_LENGTH_SIZE_BYTES, sendSizeBytes);
            if (sizeBytes >= 0) {
                cellularPortLog("CELLULAR_SOCK_TEST: received %d byte(s)", sizeBytes);
                cellularPortLog(" from ");
                printAddress(&senderAddress, true);
                cellularPortLog(".\n");
            } else {
                cellularPortLog("CELLULAR_SOCK_TEST: received nothing back.\n");
            }
            if (sizeBytes == sendSizeBytes) {
                CELLULAR_PORT_TEST_ASSERT(cellularPort_memcmp(pSendData, pDataReceived + CELLULAR_SOCK_TEST_GUARD_LENGTH_SIZE_BYTES, sendSizeBytes) == 0);
                for (size_t x = 0; x < CELLULAR_SOCK_TEST_GUARD_LENGTH_SIZE_BYTES; x++) {
                    CELLULAR_PORT_TEST_ASSERT(*(pDataReceived + x) == CELLULAR_SOCK_TEST_FILL_CHARACTER);
                    CELLULAR_PORT_TEST_ASSERT(*(pDataReceived + CELLULAR_SOCK_TEST_GUARD_LENGTH_SIZE_BYTES + sendSizeBytes + x) == CELLULAR_SOCK_TEST_FILL_CHARACTER);
                }
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
// Note: I don't call CELLULAR_PORT_TEST_ASSERT() in here
// as I found that the momenth it went off ESP32 also
// flagged a stack overrun.  Instead set gAsyncReturnCode
// and check that when the task has completed.
static void receiveDataTaskUdp(void *pParameters)
{
    int32_t errorCode;
    int32_t anInt = 0;
    char *pDataReceived;
    size_t offset = 0;
    int32_t sizeBytes;
    size_t packetCount = 0;
    CellularSockDescriptor_t sockDescriptor;

    CELLULAR_PORT_MUTEX_LOCK(gMutexHandleDataReceivedTaskRunning);

    sockDescriptor = *((CellularSockDescriptor_t *) pParameters);
    gAsyncReturnCode = 0;

    pDataReceived = (char *) pCellularPort_malloc(sizeof(gSendData) + (CELLULAR_SOCK_TEST_GUARD_LENGTH_SIZE_BYTES * 2));
    if (pDataReceived != NULL) {
        pCellularPort_memset(pDataReceived, CELLULAR_SOCK_TEST_FILL_CHARACTER, sizeof(gSendData) + (CELLULAR_SOCK_TEST_GUARD_LENGTH_SIZE_BYTES * 2));
        // Wait for an event indicating that some data has been received.
        // The return value is meaningless except that if it is negative
        // that is a signal to exit
        while ((anInt >= 0) && (offset < sizeof(gSendData)) && (gAsyncReturnCode == 0)) {
            errorCode = cellularPortQueueReceive(gQueueHandleDataReceived, &anInt);
            if (errorCode >= 0) {
                if (anInt >= 0) {
                    // Do a receive
                    sizeBytes = cellularSockReceiveFrom(sockDescriptor, NULL,
                                                        pDataReceived + offset + CELLULAR_SOCK_TEST_GUARD_LENGTH_SIZE_BYTES,
                                                        sizeof(gSendData) - offset);
                    if (sizeBytes >= 0) {
                        cellularPortLog("CELLULAR_SOCK_TEST: received %d byte(s).\n", sizeBytes);
                        offset += sizeBytes;
                        packetCount++;
                    } else {
                        gAsyncReturnCode = sizeBytes;
                    }
                    cellularPortTaskBlock(1000);
                }
            } else {
                gAsyncReturnCode = errorCode;
                cellularPortLog("CELLULAR_SOCK_TEST: cellularPortQueueReceive() returned %d.\n",
                                errorCode);
            }
        }
        sizeBytes = offset;

        // Check that we reassembled everything correctly
        if (!checkAgainstSendData(pDataReceived, sizeBytes)) {
            gAsyncReturnCode = -1;
        }
        cellularPort_free(pDataReceived);

        cellularPortLog("CELLULAR_SOCK_TEST: async received data task exiting after receiving %d packet(s) totalling %d byte(s).\n",
                        packetCount, sizeBytes);

    } else {
        gAsyncReturnCode = -1;
        cellularPortLog("CELLULAR_SOCK_TEST: unable to allocate %d byte(s) of data for test.\n",
                        sizeof(gSendData) + (CELLULAR_SOCK_TEST_GUARD_LENGTH_SIZE_BYTES * 2));
    }

    CELLULAR_PORT_MUTEX_UNLOCK(gMutexHandleDataReceivedTaskRunning);

    // Delete ourself: only valid way out in Free RTOS
    cellularPortTaskDelete(NULL);
}

// Send an entire TCP data buffer until done
static int32_t sendTcp(CellularSockDescriptor_t sockDescriptor,
                       const char *pData, size_t sizeBytes)
{
    int32_t x;
    size_t sentSizeBytes = 0;
    int32_t startTime;

    startTime = cellularPortGetTickTimeMs();
    while ((sentSizeBytes < sizeBytes) &&
           ((cellularPortGetTickTimeMs() - startTime) < 10000)) {
        x = cellularSockWrite(sockDescriptor, (void *) pData,
                              sizeBytes - sentSizeBytes);
        if (x > 0) {
            sentSizeBytes += x;
            cellularPortLog("CELLULAR_SOCK_TEST: sent %d byte(s), %d byte(s) left to send.\n",
                            sentSizeBytes, sizeBytes - sentSizeBytes);
        }
    }

    return sentSizeBytes;
}

// Set the passed-in parameter pointer to be true.
static void setBool(void *pParam)
{
    int32_t anInt = 0;

    *((bool *) pParam) = true;
    if (gQueueHandleDataReceived != NULL) {
        CELLULAR_PORT_TEST_ASSERT(cellularPortQueueSend(gQueueHandleDataReceived, &anInt) == 0);
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

    cellularPortLog("CELLULAR_SOCK_TEST: doing DNS look-up on \"%s\"...\n",
                    CELLULAR_CFG_TEST_ECHO_UDP_SERVER_DOMAIN_NAME);
    errorCode = cellularSockGetHostByName(CELLULAR_CFG_TEST_ECHO_UDP_SERVER_DOMAIN_NAME,
                                          &(remoteAddress.ipAddress));
    cellularPortLog("CELLULAR_SOCK_TEST: cellularSockGetHostByName() returned %d.\n",
                    errorCode);
    CELLULAR_PORT_TEST_ASSERT(errorCode == 0);
    remoteAddress.port = CELLULAR_CFG_TEST_ECHO_SERVER_UDP_PORT;

    cellularPortLog("CELLULAR_SOCK_TEST: creating UDP socket...\n");
    sockDescriptor = cellularSockCreate(CELLULAR_SOCK_TYPE_DGRAM,
                                        CELLULAR_SOCK_PROTOCOL_UDP);
    cellularPortLog("CELLULAR_SOCK_TEST: UDP socket descriptor 0x%x, errno %d.\n",
                    sockDescriptor, cellularPort_errno_get());
    CELLULAR_PORT_TEST_ASSERT(sockDescriptor >= 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() == 0);

    cellularPortLog("CELLULAR_SOCK_TEST: get local address...\n");
    CELLULAR_PORT_TEST_ASSERT(cellularSockGetLocalAddress(sockDescriptor,
                                                          &address) == 0);
    cellularPortLog("CELLULAR_SOCK_TEST: local address is: ");
    printAddress(&address, true);
    cellularPortLog(".\n");

    // Set up the callback
    dataCallbackCalled = false;
    CELLULAR_PORT_TEST_ASSERT(cellularSockRegisterCallbackData(sockDescriptor,
                                                               setBool,
                                                               &dataCallbackCalled) == 0);
    CELLULAR_PORT_TEST_ASSERT(!dataCallbackCalled);

    cellularPortLog("CELLULAR_SOCK_TEST: first test run without connect(), sending to address ");
    printAddress(&remoteAddress, true);
    cellularPortLog("...\n");
    // Test min size
    doUdpEchoBasic(sockDescriptor, &remoteAddress, gSendData, 1);

    CELLULAR_PORT_TEST_ASSERT(dataCallbackCalled);
    dataCallbackCalled = false;
    // Remove the data callback
    CELLULAR_PORT_TEST_ASSERT(cellularSockRegisterCallbackData(sockDescriptor,
                                                               NULL, NULL) == 0);

    // Test max size
    doUdpEchoBasic(sockDescriptor, &remoteAddress, gSendData, CELLULAR_SOCK_TEST_MAX_UDP_PACKET_SIZE);

    // Test some random sizes in-between
    for (size_t x = 0; x < 10; x++) {
        sizeBytes = (cellularPort_rand() % CELLULAR_SOCK_TEST_MAX_UDP_PACKET_SIZE) + 1;
        sizeBytes = fix(sizeBytes, CELLULAR_SOCK_TEST_MAX_UDP_PACKET_SIZE);
        doUdpEchoBasic(sockDescriptor, &remoteAddress, gSendData, sizeBytes);
    }

    cellularPortLog("CELLULAR_SOCK_TEST: check that cellularSockGetRemoteAddress() fails...\n");
    CELLULAR_PORT_TEST_ASSERT(cellularSockGetRemoteAddress(sockDescriptor,
                                                           &address) < 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() > 0);
    cellularPort_errno_set(0);

    cellularPortLog("CELLULAR_SOCK_TEST: now connect socket to \"%s:%d\"...\n",
                    CELLULAR_CFG_TEST_ECHO_UDP_SERVER_DOMAIN_NAME,
                    CELLULAR_CFG_TEST_ECHO_SERVER_UDP_PORT);
    errorCode = cellularSockConnect(sockDescriptor,
                                    &remoteAddress);
    cellularPortLog("CELLULAR_SOCK_TEST: cellularSockConnect() returned %d, errno %d.\n",
                    errorCode, cellularPort_errno_get());
    CELLULAR_PORT_TEST_ASSERT(errorCode >= 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() == 0);

    cellularPortLog("CELLULAR_SOCK_TEST: check that cellularSockGetRemoteAddress() works...\n");
    CELLULAR_PORT_TEST_ASSERT(cellularSockGetRemoteAddress(sockDescriptor,
                                                           &address) == 0);
    addressAssert(&remoteAddress, &address, true);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() == 0);

    cellularPortLog("CELLULAR_SOCK_TEST: second run after connect()...\n");
    // Test min and max
    doUdpEchoBasic(sockDescriptor, NULL, gSendData, 1);
    doUdpEchoBasic(sockDescriptor, NULL, gSendData, CELLULAR_SOCK_TEST_MAX_UDP_PACKET_SIZE);

    cellularPortLog("CELLULAR_SOCK_TEST: testing that we can send and receive all possible characters...\n");
    doUdpEchoBasic(sockDescriptor, NULL, gAllChars, sizeof(gAllChars));

    cellularPortLog("CELLULAR_SOCK_TEST: closing socket...\n");
    errorCode = cellularSockClose(sockDescriptor);
    cellularPortLog("CELLULAR_SOCK_TEST: cellularSockClose() returned %d, errno %d.\n",
                    errorCode, cellularPort_errno_get());
    CELLULAR_PORT_TEST_ASSERT(errorCode >= 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() == 0);

    cellularPortLog("CELLULAR_SOCK_TEST: cleaning up...\n");
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

    cellularPortLog("CELLULAR_SOCK_TEST: doing DNS look-up on \"%s\"...\n",
                    CELLULAR_CFG_TEST_ECHO_UDP_SERVER_DOMAIN_NAME);
    errorCode = cellularSockGetHostByName(CELLULAR_CFG_TEST_ECHO_UDP_SERVER_DOMAIN_NAME,
                                          &(remoteAddress.ipAddress));
    cellularPortLog("CELLULAR_SOCK_TEST: cellularSockGetHostByName() returned %d.\n",
                    errorCode);
    CELLULAR_PORT_TEST_ASSERT(errorCode == 0);
    remoteAddress.port = CELLULAR_CFG_TEST_ECHO_SERVER_UDP_PORT;

    cellularPortLog("CELLULAR_SOCK_TEST: creating UDP socket...\n");
    sockDescriptor = cellularSockCreate(CELLULAR_SOCK_TYPE_DGRAM,
                                        CELLULAR_SOCK_PROTOCOL_UDP);
    cellularPortLog("CELLULAR_SOCK_TEST: UDP socket descriptor 0x%x, errno %d.\n",
                    sockDescriptor, cellularPort_errno_get());
    CELLULAR_PORT_TEST_ASSERT(sockDescriptor >= 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() == 0);

    // Set up the callback
    dataCallbackCalled = false;
    CELLULAR_PORT_TEST_ASSERT(cellularSockRegisterCallbackData(sockDescriptor,
                                                               setBool,
                                                               &dataCallbackCalled) == 0);
    CELLULAR_PORT_TEST_ASSERT(!dataCallbackCalled);

    cellularPortLog("CELLULAR_SOCK_TEST: sending to address ");
    printAddress(&remoteAddress, true);
    cellularPortLog("...\n");

    do {
        // Throw random sized UDP packets up...
        offset = 0;
        x = 0;
        while (offset < sizeof(gSendData)) {
            sizeBytes = (cellularPort_rand() % CELLULAR_SOCK_TEST_MAX_UDP_PACKET_SIZE) + 1;
            sizeBytes = fix(sizeBytes, CELLULAR_SOCK_TEST_MAX_UDP_PACKET_SIZE);
            if (offset + sizeBytes > sizeof(gSendData)) {
                sizeBytes = sizeof(gSendData) - offset;
            }
            success = false;
            for (size_t y = 0; !success && (y < CELLULAR_CFG_TEST_UDP_RETRIES); y++) {
                cellularPortLog("CELLULAR_SOCK_TEST: sending UDP packet number %d, size %d byte(s), send try %d.\n",
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
        cellularPortLog("CELLULAR_SOCK_TEST: a total of %d UDP packet(s) sent, now receiving...\n", x + 1);

        // ...and capture them all again afterwards
        pDataReceived = (char *) pCellularPort_malloc(sizeof(gSendData) + (CELLULAR_SOCK_TEST_GUARD_LENGTH_SIZE_BYTES * 2));
        CELLULAR_PORT_TEST_ASSERT(pDataReceived != NULL);
        pCellularPort_memset(pDataReceived, CELLULAR_SOCK_TEST_FILL_CHARACTER, sizeof(gSendData) + (CELLULAR_SOCK_TEST_GUARD_LENGTH_SIZE_BYTES * 2));
        startTimeMs = cellularPortGetTickTimeMs();
        offset = 0;
        for (x = 0; (offset < sizeof(gSendData)) &&
                    (cellularPortGetTickTimeMs() - startTimeMs < 10000); x++) {
            sizeBytes = cellularSockReceiveFrom(sockDescriptor, NULL,
                                                pDataReceived + offset + CELLULAR_SOCK_TEST_GUARD_LENGTH_SIZE_BYTES,
                                                sizeof(gSendData) - offset);
            if (sizeBytes > 0) {
                cellularPortLog("CELLULAR_SOCK_TEST: received UDP packet number %d, size %d byte(s).\n",
                                x + 1, sizeBytes);
                offset += sizeBytes;
            }
        }
        sizeBytes = offset;
        cellularPortLog("CELLULAR_SOCK_TEST: either received everything back or timed out waiting.\n");

        // Check that we reassembled everything correctly
        allPacketsReceived = checkAgainstSendData(pDataReceived, sizeBytes);
        cellularPort_free(pDataReceived);
        tries++;
    } while (!allPacketsReceived && (tries < CELLULAR_CFG_TEST_UDP_RETRIES));

    CELLULAR_PORT_TEST_ASSERT(allPacketsReceived);
    CELLULAR_PORT_TEST_ASSERT(dataCallbackCalled);

    cellularPortLog("CELLULAR_SOCK_TEST: closing socket...\n");
    errorCode = cellularSockClose(sockDescriptor);
    cellularPortLog("CELLULAR_SOCK_TEST: cellularSockClose() returned %d, errno %d.\n",
                    errorCode, cellularPort_errno_get());
    CELLULAR_PORT_TEST_ASSERT(errorCode >= 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() == 0);

    cellularPortLog("CELLULAR_SOCK_TEST: cleaning up...\n");
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
    int32_t lockResult = -1;
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

    cellularPortLog("CELLULAR_SOCK_TEST: doing DNS look-up on \"%s\"...\n",
                    CELLULAR_CFG_TEST_ECHO_UDP_SERVER_DOMAIN_NAME);
    errorCode = cellularSockGetHostByName(CELLULAR_CFG_TEST_ECHO_UDP_SERVER_DOMAIN_NAME,
                                          &(remoteAddress.ipAddress));
    cellularPortLog("CELLULAR_SOCK_TEST: cellularSockGetHostByName() returned %d.\n",
                    errorCode);
    CELLULAR_PORT_TEST_ASSERT(errorCode == 0);
    remoteAddress.port = CELLULAR_CFG_TEST_ECHO_SERVER_UDP_PORT;

    cellularPortLog("CELLULAR_SOCK_TEST: creating UDP socket...\n");
    sockDescriptor = cellularSockCreate(CELLULAR_SOCK_TYPE_DGRAM,
                                        CELLULAR_SOCK_PROTOCOL_UDP);
    cellularPortLog("CELLULAR_SOCK_TEST: UDP socket descriptor 0x%x, errno %d.\n",
                    sockDescriptor, cellularPort_errno_get());
    CELLULAR_PORT_TEST_ASSERT(sockDescriptor >= 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() == 0);

    // A queue on which we will send notifications of data arrival.
    CELLULAR_PORT_TEST_ASSERT(cellularPortQueueCreate(CELLULAR_SOCK_TEST_RECEIVE_QUEUE_LENGTH,
                                                      sizeof(int32_t),
                                                      &gQueueHandleDataReceived) == 0);

    // Create a mutex that we can use to indicate that the
    // received data task is running
    CELLULAR_PORT_TEST_ASSERT(cellularPortMutexCreate(&gMutexHandleDataReceivedTaskRunning) == 0);

    // Create a task that will receive the data
    CELLULAR_PORT_TEST_ASSERT(cellularPortTaskCreate(receiveDataTaskUdp,
                                                     "testTaskRxData",
                                                     5128, (void **) &pParam,
                                                     // lower priority than the callback made
                                                     // from the URC
                                                     CELLULAR_CTRL_CALLBACK_PRIORITY + 1,
                                                     &gTaskHandleDataReceived) == 0);

    // Set up the callback that will signal data reception
    CELLULAR_PORT_TEST_ASSERT(cellularSockRegisterCallbackData(sockDescriptor,
                                                               setBool,
                                                               &dataCallbackCalled) == 0);

    cellularPortLog("CELLULAR_SOCK_TEST: sending to address ");
    printAddress(&remoteAddress, true);
    cellularPortLog("...\n");

    // Throw random sized UDP packets up...
    offset = 0;
    x = 0;
    while (offset < sizeof(gSendData)) {
        sizeBytes = (cellularPort_rand() % CELLULAR_SOCK_TEST_MAX_UDP_PACKET_SIZE) + 1;
        sizeBytes = fix(sizeBytes, CELLULAR_SOCK_TEST_MAX_UDP_PACKET_SIZE);
        if (offset + sizeBytes > sizeof(gSendData)) {
            sizeBytes = sizeof(gSendData) - offset;
        }
        success = false;
        for (size_t y = 0; !success && (y < CELLULAR_CFG_TEST_UDP_RETRIES); y++) {
            cellularPortLog("CELLULAR_SOCK_TEST: sending UDP packet number %d, size %d byte(s), send try %d.\n",
                            x + 1, sizeBytes, y + 1);
            if (cellularSockSendTo(sockDescriptor, &remoteAddress,
                                   gSendData + offset, sizeBytes) == sizeBytes) {
                success = true;
                offset += sizeBytes;
                x++;
            }
        }
        CELLULAR_PORT_TEST_ASSERT(success);
    }
    cellularPortLog("CELLULAR_SOCK_TEST: a total of %d UDP packet(s) sent, %d byte(s).\n",
                    x, offset);

    // Wait for the receive task to finish receiving the packets back again
    x = 0;
    while ((x < 20) && (lockResult != 0)) {
        lockResult = cellularPortMutexTryLock(gMutexHandleDataReceivedTaskRunning, 1000);
        x++;
    }

    // If we never managed to lock the mutex, the task must still be running
    // so send it an exit messge to force it to complete
    if (lockResult != 0) {
        x = -1;
        CELLULAR_PORT_TEST_ASSERT(cellularPortQueueSend(gQueueHandleDataReceived, &x) == 0);
        // Wait for it to obey
        CELLULAR_PORT_MUTEX_LOCK(gMutexHandleDataReceivedTaskRunning);
        CELLULAR_PORT_MUTEX_UNLOCK(gMutexHandleDataReceivedTaskRunning);
    }

    CELLULAR_PORT_TEST_ASSERT(gAsyncReturnCode == 0);

    CELLULAR_PORT_TEST_ASSERT(cellularPortQueueDelete(gQueueHandleDataReceived) == 0);
    gQueueHandleDataReceived = NULL;
    CELLULAR_PORT_TEST_ASSERT(cellularPortMutexDelete(gMutexHandleDataReceivedTaskRunning) == 0);
    gMutexHandleDataReceivedTaskRunning = NULL;

    cellularPortLog("CELLULAR_SOCK_TEST: closing socket...\n");
    errorCode = cellularSockClose(sockDescriptor);
    cellularPortLog("CELLULAR_SOCK_TEST: cellularSockClose() returned %d, errno %d.\n",
                    errorCode, cellularPort_errno_get());
    CELLULAR_PORT_TEST_ASSERT(errorCode >= 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() == 0);

    cellularPortLog("CELLULAR_SOCK_TEST: cleaning up...\n");
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

/** Basic TCP echo test.
 * TODO: test error cases.
 */
CELLULAR_PORT_TEST_FUNCTION(void cellularSockTestUdp(),
                            "tcpEchoBasic",
                            "sock")
{
    CellularPortQueueHandle_t queueHandle;
    CellularSockAddress_t remoteAddress;
    CellularSockAddress_t address;
    CellularSockDescriptor_t sockDescriptor;
    bool dataCallbackCalled;
    int32_t errorCode;
    size_t sizeBytes;
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

    cellularPortLog("CELLULAR_SOCK_TEST: doing DNS look-up on \"%s\"...\n",
                    CELLULAR_CFG_TEST_ECHO_TCP_SERVER_DOMAIN_NAME);
    errorCode = cellularSockGetHostByName(CELLULAR_CFG_TEST_ECHO_TCP_SERVER_DOMAIN_NAME,
                                          &(remoteAddress.ipAddress));
    cellularPortLog("CELLULAR_SOCK_TEST: cellularSockGetHostByName() returned %d.\n",
                    errorCode);
    CELLULAR_PORT_TEST_ASSERT(errorCode == 0);
    remoteAddress.port = CELLULAR_CFG_TEST_ECHO_SERVER_TCP_PORT;

    cellularPortLog("CELLULAR_SOCK_TEST: creating TCP socket...\n");
    sockDescriptor = cellularSockCreate(CELLULAR_SOCK_TYPE_STREAM,
                                        CELLULAR_SOCK_PROTOCOL_TCP);
    cellularPortLog("CELLULAR_SOCK_TEST: TCP socket descriptor 0x%x, errno %d.\n",
                    sockDescriptor, cellularPort_errno_get());
    CELLULAR_PORT_TEST_ASSERT(sockDescriptor >= 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() == 0);

    cellularPortLog("CELLULAR_SOCK_TEST: get local address...\n");
    CELLULAR_PORT_TEST_ASSERT(cellularSockGetLocalAddress(sockDescriptor,
                                                          &address) == 0);
    cellularPortLog("CELLULAR_SOCK_TEST: local address is: ");
    printAddress(&address, true);
    cellularPortLog(".\n");

    cellularPortLog("CELLULAR_SOCK_TEST: check that cellularSockGetRemoteAddress() fails...\n");
    CELLULAR_PORT_TEST_ASSERT(cellularSockGetRemoteAddress(sockDescriptor,
                                                           &address) < 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() > 0);
    cellularPort_errno_set(0);

    // Set up the callback
    dataCallbackCalled = false;
    CELLULAR_PORT_TEST_ASSERT(cellularSockRegisterCallbackData(sockDescriptor,
                                                               setBool,
                                                               &dataCallbackCalled) == 0);
    CELLULAR_PORT_TEST_ASSERT(!dataCallbackCalled);

    cellularPortLog("CELLULAR_SOCK_TEST: connect socket to \"%s:%d\"...\n",
                    CELLULAR_CFG_TEST_ECHO_TCP_SERVER_DOMAIN_NAME,
                    CELLULAR_CFG_TEST_ECHO_SERVER_UDP_PORT);
    errorCode = cellularSockConnect(sockDescriptor,
                                    &remoteAddress);
    cellularPortLog("CELLULAR_SOCK_TEST: cellularSockConnect() returned %d, errno %d.\n",
                    errorCode, cellularPort_errno_get());
    CELLULAR_PORT_TEST_ASSERT(errorCode >= 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() == 0);

    cellularPortLog("CELLULAR_SOCK_TEST: check that cellularSockGetRemoteAddress() works...\n");
    CELLULAR_PORT_TEST_ASSERT(cellularSockGetRemoteAddress(sockDescriptor,
                                                           &address) == 0);
    addressAssert(&remoteAddress, &address, true);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() == 0);

    cellularPortLog("CELLULAR_SOCK_TEST: sending/receiving data...\n");

    // Throw random sized TCP segments up...
    offset = 0;
    x = 0;
    while (offset < sizeof(gSendData)) {
        sizeBytes = (cellularPort_rand() % CELLULAR_SOCK_TEST_MAX_TCP_READ_WRITE_SIZE) + 1;
        sizeBytes = fix(sizeBytes, CELLULAR_SOCK_TEST_MAX_TCP_READ_WRITE_SIZE);
        if (offset + sizeBytes > sizeof(gSendData)) {
            sizeBytes = sizeof(gSendData) - offset;
        }
        if (sendTcp(sockDescriptor,
                    gSendData + offset, sizeBytes) == sizeBytes) {
            offset += sizeBytes;
        }
        x++;
    }
    sizeBytes = offset;
    cellularPortLog("CELLULAR_SOCK_TEST: %d byte(s) sent via TCP in %d segments, now receiving...\n",
                    sizeBytes);

    // ...and capture them all again afterwards
    pDataReceived = (char *) pCellularPort_malloc(sizeof(gSendData) + (CELLULAR_SOCK_TEST_GUARD_LENGTH_SIZE_BYTES * 2));
    CELLULAR_PORT_TEST_ASSERT(pDataReceived != NULL);
    pCellularPort_memset(pDataReceived, CELLULAR_SOCK_TEST_FILL_CHARACTER, sizeof(gSendData) + (CELLULAR_SOCK_TEST_GUARD_LENGTH_SIZE_BYTES * 2));
    startTimeMs = cellularPortGetTickTimeMs();
    offset = 0;
    for (x = 0; (offset < sizeof(gSendData)) &&
                (cellularPortGetTickTimeMs() - startTimeMs < 10000); x++) {
        sizeBytes = cellularSockRead(sockDescriptor,
                                     pDataReceived + offset + CELLULAR_SOCK_TEST_GUARD_LENGTH_SIZE_BYTES,
                                     sizeof(gSendData) - offset);
        if (sizeBytes > 0) {
            cellularPortLog("CELLULAR_SOCK_TEST: received TCP segment %d, size %d byte(s).\n",
                            x + 1, sizeBytes);
            offset += sizeBytes;
        }
    }
    sizeBytes = offset;
    cellularPortLog("CELLULAR_SOCK_TEST: either all %d byte(s) received back or timed out waiting.\n",
                    sizeBytes);

    // Check that we reassembled everything correctly
    CELLULAR_PORT_TEST_ASSERT(checkAgainstSendData(pDataReceived, sizeBytes));
    cellularPort_free(pDataReceived);

    cellularPortLog("CELLULAR_SOCK_TEST: shutting down socket for read...\n");
    errorCode = cellularSockShutdown(sockDescriptor,
                                     CELLULAR_SOCK_SHUTDOWN_READ);
    cellularPortLog("CELLULAR_SOCK_TEST: cellularSockShutdown() returned %d, errno %d.\n",
                    errorCode, cellularPort_errno_get());
    CELLULAR_PORT_TEST_ASSERT(errorCode >= 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() == 0);
    CELLULAR_PORT_TEST_ASSERT(cellularSockRead(sockDescriptor,
                                               pDataReceived + CELLULAR_SOCK_TEST_GUARD_LENGTH_SIZE_BYTES,
                                               sizeof(gSendData)) < 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() > 0);
    cellularPort_errno_set(0);

    cellularPortLog("CELLULAR_SOCK_TEST: shutting down socket for write...\n");
    errorCode = cellularSockShutdown(sockDescriptor,
                                     CELLULAR_SOCK_SHUTDOWN_WRITE);
    cellularPortLog("CELLULAR_SOCK_TEST: cellularSockShutdown() returned %d, errno %d.\n",
                    errorCode, cellularPort_errno_get());
    CELLULAR_PORT_TEST_ASSERT(errorCode >= 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() == 0);
    CELLULAR_PORT_TEST_ASSERT(cellularSockWrite(sockDescriptor, gSendData,
                                                sizeof(gSendData)) < 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() > 0);
    cellularPort_errno_set(0);

    // TODO: test this in a different TCP test
    cellularPortLog("CELLULAR_SOCK_TEST: shutting down socket for read/write...\n");
    errorCode = cellularSockShutdown(sockDescriptor,
                                     CELLULAR_SOCK_SHUTDOWN_READ_WRITE);
    cellularPortLog("CELLULAR_SOCK_TEST: cellularSockShutdown() returned %d, errno %d.\n",
                    errorCode, cellularPort_errno_get());
    CELLULAR_PORT_TEST_ASSERT(errorCode >= 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() == 0);
    CELLULAR_PORT_TEST_ASSERT(cellularSockRead(sockDescriptor,
                                               pDataReceived + CELLULAR_SOCK_TEST_GUARD_LENGTH_SIZE_BYTES,
                                               sizeof(gSendData)) < 0);
    CELLULAR_PORT_TEST_ASSERT(cellularSockWrite(sockDescriptor, gSendData,
                                                sizeof(gSendData)) < 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() > 0);
    cellularPort_errno_set(0);

    cellularPortLog("CELLULAR_SOCK_TEST: closing socket...\n");
    errorCode = cellularSockClose(sockDescriptor);
    cellularPortLog("CELLULAR_SOCK_TEST: cellularSockClose() returned %d, errno %d.\n",
                    errorCode, cellularPort_errno_get());
    CELLULAR_PORT_TEST_ASSERT(errorCode >= 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_errno_get() == 0);

    cellularPortLog("CELLULAR_SOCK_TEST: cleaning up...\n");
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
