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
#include "cellular_port_gpio.h"
#include "cellular_port_uart.h"
#include "cellular_ctrl_at.h"
#include "cellular_ctrl.h" // For cellularCtrlGetIpAddressStr()
#include "cellular_sock_errno.h"
#include "cellular_sock.h"

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// Increment a socket descriptor.
#define CELLULAR_SOCK_INC_DESCRIPTOR(d) (d)++;                          \
                                        if ((d) >= CELLULAR_SOCK_MAX) { \
                                            d = 0;                      \
                                        }

// Swap endianness.
#define CELLULAR_SOCK_ENDIAN_SWAP_32(x) ((((uint32_t) (x) & 0xff000000) >> 24) | \
                                         (((uint32_t) (x) & 0x00ff0000) >> 8)  | \
                                         (((uint32_t) (x) & 0x0000ff00) << 8)  | \
                                         (((uint32_t) (x) & 0x000000ff) << 24))

// Convert an int32_t on this processor to network byte order.
#define CELLULAR_SOCK_HTONL(x) (isBigEndian(x) ? (x) : \
                                CELLULAR_SOCK_ENDIAN_SWAP_32(x))

// Convert network byte order int32_t to on this processor.
#define CELLULAR_SOCK_NTOHL(x) (isBigEndian(x) ? (x) : \
                                CELLULAR_SOCK_ENDIAN_SWAP_32(x))

// If a TCP socket fails to send the requested number of bytes
// this many times then return an error
#define CELLULAR_SOCK_TCP_RETRY_LIMIT 10

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

// Socket state
typedef enum {
    CELLULAR_SOCK_STATE_CREATED,
    CELLULAR_SOCK_STATE_CONNECTED,
    CELLULAR_SOCK_STATE_SHUTDOWN,
    CELLULAR_SOCK_STATE_CLOSED
} CellularSockState_t;

// A socket.
 typedef struct {
     CellularSockType_t type;
     CellularSockProtocol_t protocol;
     int32_t modemHandle;
     CellularSockState_t state;
     CellularSockAddress_t remoteAddress;
     int32_t timeoutMs;
     bool nonBlocking;
     size_t pendingBytes;
     void (*pPendingDataCallback) (void *);
     void *pPendingDataCallbackParam;
     void (*pConnectionClosedCallback) (void *);
     void *pConnectionClosedCallbackParam;
 } CellularSockSocket_t;

// A socket container.
typedef struct CellularSockContainer_t {
    struct CellularSockContainer_t *pPrevious;
    CellularSockDescriptor_t descriptor;
    bool isStatic;
    CellularSockSocket_t socket;
    struct CellularSockContainer_t *pNext;
} CellularSockContainer_t;

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// Keep track of whether we're initialised or not.
static bool gInitialised = false;

// Mutex to protect the container list.
static CellularPortMutexHandle_t gMutexContainer = NULL;

// Mutex to protect just the callbacks in the container list.
static CellularPortMutexHandle_t gMutexCallbacks = NULL;

// Root of the socket container list.
static CellularSockContainer_t *gpContainerListHead = NULL;

// The next descriptor to use.
static CellularSockDescriptor_t gNextDescriptor = 0;

// Containers for statically allocated sockets.
static CellularSockContainer_t gStaticContainers[CELLULAR_SOCK_NUM_STATIC_SOCKETS];

/* ----------------------------------------------------------------
 * STATIC FUNCTION PROTOTYPES (ONLY WHERE REQUIRED)
 * -------------------------------------------------------------- */

// Find the socket container for the given modem handle.
// This does NOT lock the mutex, you need to do that.
static CellularSockContainer_t *pContainerFindByModemHandle(int32_t modemHandle);

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: URCs
 * -------------------------------------------------------------- */

// Socket Read/Read-From URC.
static void UUSORD_UUSORF_urc(void *pUnused)
{
    int32_t modemHandle;
    int32_t dataSizeBytes;
    CellularSockContainer_t *pContainer = NULL;

    (void) pUnused;

    // +UUSORx: <socket>,<length>
    modemHandle = cellular_ctrl_at_read_int();
    dataSizeBytes = cellular_ctrl_at_read_int();

    if (modemHandle >= 0) {

        // Don't lock the container mutex here as this
        // needs to be called while a send or receive is
        // in progress and that already has the mutex

        // Find the container
        pContainer = pContainerFindByModemHandle(modemHandle);
        if (pContainer != NULL) {
            // Set pending bytes
            pContainer->socket.pendingBytes = dataSizeBytes;
            if (pContainer->socket.pPendingDataCallback != NULL) {
                CELLULAR_PORT_MUTEX_LOCK(gMutexCallbacks);
                cellular_ctrl_at_urc_callback(pContainer->socket.pPendingDataCallback,
                                              pContainer->socket.pPendingDataCallbackParam);
                CELLULAR_PORT_MUTEX_UNLOCK(gMutexCallbacks);
            }
        }
    }
}

// Callback for Socket Close URC.
static void UUSOCL_urc(void *pUnused)
{
    int32_t modemHandle;
    CellularSockContainer_t *pContainer = NULL;

    (void) pUnused;

    // +UUSOCL: <socket>
    modemHandle = cellular_ctrl_at_read_int();
    if (modemHandle >= 0) {

        // Don't lock the container mutex here as this
        // needs to be called while a send or receive is
        // in progress and that already has the mutex
        pContainer = pContainerFindByModemHandle(modemHandle);
        if (pContainer != NULL) {
            // Mark the container as closed
            pContainer->socket.state = CELLULAR_SOCK_STATE_CLOSED;
            if (pContainer->socket.pConnectionClosedCallback != NULL) {
                CELLULAR_PORT_MUTEX_LOCK(gMutexCallbacks);
                cellular_ctrl_at_urc_callback(pContainer->socket.pConnectionClosedCallback,
                                              pContainer->socket.pConnectionClosedCallbackParam);
                CELLULAR_PORT_MUTEX_UNLOCK(gMutexCallbacks);
            }
        }

    }
}

// Callback for Connection Lost URC.
static void UUPSDD_urc(void *pUnused)
{
    // int32_t profileId;

    (void) pUnused;

    // +UUPSDD: <profile ID>
    // TODO: sort out checking of profile ID as it is used on R5 (not R4)
    // profileId = cellular_ctrl_at_read_int();
}

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: MISC
 * -------------------------------------------------------------- */

// Initialise.
static bool init()
{
    CellularSockContainer_t **ppContainer = &gpContainerListHead;
    CellularSockContainer_t *pTmp = NULL;
    CellularSockContainer_t **ppPreviousNext = NULL;

    // The mutexes are set up once only
    if (gMutexContainer == NULL) {
        cellularPortMutexCreate(&gMutexContainer);
    }
    if (gMutexCallbacks == NULL) {
        cellularPortMutexCreate(&gMutexCallbacks);
    }

    if (!gInitialised) {
        cellular_ctrl_at_set_urc_handler("+UUSORD:", UUSORD_UUSORF_urc, NULL);
        cellular_ctrl_at_set_urc_handler("+UUSORF:", UUSORD_UUSORF_urc, NULL);
        cellular_ctrl_at_set_urc_handler("+UUSOCL:", UUSOCL_urc, NULL);
        cellular_ctrl_at_set_urc_handler("+UUPSDD:", UUPSDD_urc, NULL);

        //  Link the static containers into the start of the container list
        for (size_t x = 0; x < sizeof(gStaticContainers) / sizeof(gStaticContainers[0]); x++) {
            *ppContainer = &gStaticContainers[x];
            (*ppContainer)->isStatic = true;
            (*ppContainer)->socket.state= CELLULAR_SOCK_STATE_CLOSED;
            (*ppContainer)->pNext = NULL;
            if (ppPreviousNext != NULL) {
                *ppPreviousNext = *ppContainer;
            }
            ppPreviousNext = &((*ppContainer)->pNext);
            (*ppContainer)->pPrevious = pTmp;
            pTmp = *ppContainer;
        }

        gInitialised = true;
    }

    return gInitialised;
}

// Deinitialise.
static void deinitButNotMutex()
{
    if (gInitialised) {
        // IMPORTANT: can't delete the mutexes here as we can't
        // know if anyone has hold of them.  They just have to remain.
        cellular_ctrl_at_remove_urc_handler("+UUSORD:");
        cellular_ctrl_at_remove_urc_handler("+UUSORF:");
        cellular_ctrl_at_remove_urc_handler("+UUSOCL:");
        cellular_ctrl_at_remove_urc_handler("+UUPSDD:");
        gInitialised = false;
    }
}

// Determine endianness.  Note that there is no reliable
// cross-platform mechanism for doing this at compile time since
// the pre-processor has no concept of an integer.  You _could_
// rely on the compiler's built in __ENDIAN__ macros but these
// aren't necessarily consistent across all platforms.
static bool inline isBigEndian()
{
  const uint16_t endianness = 0x0100;
  return (bool) (*(const uint8_t *) &endianness);
}

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: CONTAINER STUFF
 * -------------------------------------------------------------- */

// Find the socket container for the given descriptor.
// Will not find sockets in state CLOSED.
// This does NOT lock the mutex, you need to do that.
static CellularSockContainer_t *pContainerFindByDescriptor(CellularSockDescriptor_t descriptor)
{
    CellularSockContainer_t *pContainer = NULL;
    CellularSockContainer_t *pContainerThis = gpContainerListHead;

    for (size_t x = 0; (pContainerThis != NULL) &&
                       (pContainer == NULL) &&
                       (x < CELLULAR_SOCK_MAX); x++) {
        if ((pContainerThis->descriptor == descriptor) &&
            (pContainerThis->socket.state != CELLULAR_SOCK_STATE_CLOSED)) {
            pContainer = pContainerThis;
        }
        pContainerThis = pContainerThis->pNext;
    }

    return pContainer;
}

// Find the socket container for the given modem handle.
// Will not find sockets in state CLOSED.
// This does NOT lock the mutex, you need to do that.
static CellularSockContainer_t *pContainerFindByModemHandle(int32_t modemHandle)
{
    CellularSockContainer_t *pContainer = NULL;
    CellularSockContainer_t *pContainerThis = gpContainerListHead;

    for (size_t x = 0; (pContainerThis != NULL) &&
                       (pContainer == NULL) &&
                       (x < CELLULAR_SOCK_MAX); x++) {
        if ((pContainerThis->socket.modemHandle == modemHandle) &&
            (pContainerThis->socket.state != CELLULAR_SOCK_STATE_CLOSED)) {
            pContainer = pContainerThis;
        }
        pContainerThis = pContainerThis->pNext;
    }

    return pContainer;
}

// Create a socket in a container with the given descriptor.
// This does NOT lock the mutex, you need to do that.
static CellularSockContainer_t *pSockContainerCreate(CellularSockDescriptor_t descriptor,
                                                     CellularSockType_t type,
                                                     CellularSockProtocol_t protocol)
{
    CellularSockContainer_t *pContainer = NULL;
    CellularSockContainer_t *pContainerPrevious = NULL;
    CellularSockContainer_t **ppContainerThis = &gpContainerListHead;

    // Traverse the list, stopping if there is a container
    // that holds a closed socket, which we could re-use
    while ((*ppContainerThis != NULL) && (pContainer == NULL)) {
        if ((*ppContainerThis)->socket.state == CELLULAR_SOCK_STATE_CLOSED) {
            pContainer = *ppContainerThis;
        }
        pContainerPrevious = *ppContainerThis;
        ppContainerThis = &((*ppContainerThis)->pNext);
    }

    if (pContainer == NULL) {
        // Reached the end of the list and found no re-usable
        // containers, so allocate memory for the new container
        // and add it to the list
        pContainer = (CellularSockContainer_t *) pCellularPort_malloc(sizeof (*pContainer));
        if (pContainer != NULL) {
            pContainer->pPrevious = pContainerPrevious;
            pContainer->pNext = NULL;
            *ppContainerThis = pContainer;
        }
    }

    // Set up the new container and socket
    if (pContainer != NULL) {
        pContainer->descriptor = descriptor;
        pCellularPort_memset(&(pContainer->socket),
                             0,
                             sizeof(pContainer->socket));
        pContainer->socket.type = type;
        pContainer->socket.protocol = protocol;
        pContainer->socket.modemHandle = -1;
        pContainer->socket.state = CELLULAR_SOCK_STATE_CREATED;
        pContainer->socket.timeoutMs = CELLULAR_SOCK_TIMEOUT_DEFAULT_MS;
        pContainer->socket.nonBlocking = false;
        pContainer->socket.pPendingDataCallback = NULL;
        pContainer->socket.pPendingDataCallbackParam = NULL;
        pContainer->socket.pConnectionClosedCallback = NULL;
        pContainer->socket.pConnectionClosedCallbackParam = NULL;
    }

    return pContainer;
}

// Free the container corresponding to the descriptor.
// Has no effect on static containers.
// This does NOT lock the mutex, you need to do that.
static bool containerFree(CellularSockDescriptor_t descriptor)
{
    CellularSockContainer_t **ppContainer = NULL;
    CellularSockContainer_t **ppContainerThis = &gpContainerListHead;
    bool success = false;

    for (size_t x = 0; (*ppContainerThis != NULL) &&
                       (ppContainer == NULL) &&
                       (x < CELLULAR_SOCK_MAX); x++) {
        if ((*ppContainerThis)->descriptor == descriptor) {
            ppContainer = ppContainerThis;
        } else {
            ppContainerThis = &((*ppContainerThis)->pNext);
        }
    }

    if ((ppContainer != NULL) && (*ppContainer != NULL)) {
        if (!(*ppContainer)->isStatic) {
            // If we found it, and it wasn't static, free it
            // If there is a previous container, move its pNext
            if ((*ppContainer)->pPrevious != NULL) {
                (*ppContainer)->pPrevious->pNext = (*ppContainer)->pNext;
            }
            // If there is a next container, move its pPrevious
            if ((*ppContainer)->pNext != NULL) {
                (*ppContainer)->pNext->pPrevious = (*ppContainer)->pPrevious;
            }

            // Free the memory and NULL the pointer
            cellularPort_free(*ppContainer);
            *ppContainer = NULL;
        } else {
            // Nothing to do for a static container, 
        }

        success = true;
    }

    return success;
}

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: ADDRESS CONVERSION
 * -------------------------------------------------------------- */

// Determine whether the given IP address string is IPV4.
static bool addressStringIsIpv4(const char *pAddressString)
{
    // If it's got a dot in it, must be IPV4
    return (pCellularPort_strchr(pAddressString, '.') != NULL);
}

// Convert an IPV4 address string "xxx.yyy.www.zzz:65535" into
// a struct.
static bool ipv4StringToAddress(const char *pAddressString,
                                CellularSockAddress_t *pAddress)
{
    bool success = true;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t port;
    const char *pColon;

    pAddress->ipAddress.type = CELLULAR_SOCK_ADDRESS_TYPE_V4;
    pAddress->ipAddress.address.ipv4 = 0;
    pAddress->port = 0;

    // Get the IP address part
    if (cellularPort_sscanf(pAddressString, "%u.%u.%u.%u",
                            &a, &b, &c, &d) == 4) {
        // Range check
        if ((a <= UCHAR_MAX) && (b <= UCHAR_MAX) &&
            (c <= UCHAR_MAX) && (d <= UCHAR_MAX)) {

            // Calculate the IP address part, network byte order
            pAddress->ipAddress.address.ipv4 = (a << 24) |
                                               (b << 16) |
                                               (c << 8)  |
                                               (d << 0);

            // Check for a port number on the end
            pColon = pCellularPort_strchr(pAddressString, ':');
            if (pColon != NULL) {
                // Fill in the port number
                port = cellularPort_strtol(pColon + 1, NULL, 10);
                if (port <= USHRT_MAX) {
                    pAddress->port = port;
                } else {
                    success = false;
                }
            }
        } else {
            success = false;
        }
    } else {
        success = false;
    }

    return success;
}

// Convert an IPV6 address string "2001:0db8:85a3:0000:0000:8a2e:0370:7334" 
// or "[2001:0db8:85a3:0000:0000:8a2e:0370:7334]:65535" into a struct.
static bool ipv6StringToAddress(const char *pAddressString,
                                CellularSockAddress_t *pAddress)
{
    bool success = true;
    size_t stringLength = cellularPort_strlen(pAddressString);
    bool hasPort = false;
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    uint32_t port;
    const char *pStr;

    pAddress->ipAddress.type = CELLULAR_SOCK_ADDRESS_TYPE_V6;
    pCellularPort_memset(pAddress->ipAddress.address.ipv6, 0,
                         sizeof(pAddress->ipAddress.address.ipv6));
    pAddress->port = 0;

    // See if there's a '[' on the start
    if ((stringLength > 0) && (*pAddressString == '[')) {
        hasPort = true;
        pAddressString++;
    }

    // Get the IP address part
    if (cellularPort_sscanf(pAddressString, "%x:%x:%x:%x:%x:%x:%x:%x",
                            &a, &b, &c, &d, &e, &f, &g, &h) == 8) {
        // Range check
        if ((a <= USHRT_MAX) && (b <= USHRT_MAX) &&
            (c <= USHRT_MAX) && (d <= USHRT_MAX) &&
            (e <= USHRT_MAX) && (f <= USHRT_MAX) &&
            (g <= USHRT_MAX) && (h <= USHRT_MAX)) {

            // Slot the uint16_t's into the array in network-byte order
            pAddress->ipAddress.address.ipv6[3] = (a << 16) | (b & 0xFFFF);
            pAddress->ipAddress.address.ipv6[2] = (c << 16) | (d & 0xFFFF);
            pAddress->ipAddress.address.ipv6[1] = (e << 16) | (f & 0xFFFF);
            pAddress->ipAddress.address.ipv6[0] = (g << 16) | (h & 0xFFFF);

            // Get the port number if there was one
            if (hasPort) {
                pStr = pCellularPort_strchr(pAddressString, ']');
                if (pStr != NULL) {
                    pStr = pCellularPort_strchr(pStr, ':');
                    if (pStr != NULL) {
                        // Fill in the port number
                        port = cellularPort_strtol(pStr + 1, NULL, 10);
                        if (port <= USHRT_MAX) {
                            pAddress->port = port;
                        } else {
                            success = false;
                        }
                    }
                } else {
                    success = false;
                }
            }
        } else {
            success = false;
        }
    } else {
        success = false;
    }

    return success;
}

// Convert an IP address struct (i.e. without a port number) into a
// string, returning the length of the string.
static int32_t ipAddressToString(const CellularSockIpAddress_t *pIpAddress,
                                 char *pBuffer,
                                 size_t sizeBytes)
{
    CellularSockErrorCode_t stringLengthOrError = CELLULAR_SOCK_INVALID_PARAMETER;
    int32_t thisLength;

    // Convert the address in network byte order (MSB first);
    switch (pIpAddress->type) {
        case CELLULAR_SOCK_ADDRESS_TYPE_V4:
            stringLengthOrError = cellularPort_snprintf(pBuffer,
                                                        sizeBytes,
                                                        "%u.%u.%u.%u",
                                                        (pIpAddress->address.ipv4 >> 24) & 0xFF,
                                                        (pIpAddress->address.ipv4 >> 16) & 0xFF,
                                                        (pIpAddress->address.ipv4 >> 8)  & 0xFF,
                                                        (pIpAddress->address.ipv4 >> 0)  & 0xFF);
        break;
        case CELLULAR_SOCK_ADDRESS_TYPE_V6:
            stringLengthOrError = 0;
            for (int32_t x = 3; (x >= 0) && (stringLengthOrError >= 0); x--) {
                thisLength = cellularPort_snprintf(pBuffer,
                                                   sizeBytes,
                                                   "%x:%x",
                                                   (pIpAddress->address.ipv6[x] >> 16) & 0xFFFF,
                                                   (pIpAddress->address.ipv6[x] >> 0)  & 0xFFFF);
                if (x > 0) {
                    if ((thisLength >= 0) && (thisLength < sizeBytes)) {
                        *(pBuffer + thisLength) = ':';
                        thisLength++;
                    } else {
                        thisLength = CELLULAR_SOCK_NO_MEMORY;
                    }
                }
                if ((thisLength >= 0) && (thisLength < sizeBytes)) {
                    sizeBytes -= thisLength;
                    pBuffer += thisLength;
                    stringLengthOrError += thisLength;
                } else {
                    stringLengthOrError = CELLULAR_SOCK_NO_MEMORY;
                }
            }
        break;
        default:
        break;
    }

    return (int32_t) stringLengthOrError;
}

// Convert an address struct, which includes a port number,
// into a string, returning the length of the string.
static int32_t addressToString(const CellularSockAddress_t *pAddress,
                               bool includePortNumber,
                               char *pBuffer,
                               size_t sizeBytes)
{
    CellularSockErrorCode_t stringLengthOrError = 0;
    int32_t thisLength;

    if (includePortNumber) {
        // If this is an IPV6 address, then start with a square bracket
        // to delineate the IP address part
        if (pAddress->ipAddress.type == CELLULAR_SOCK_ADDRESS_TYPE_V6) {
            if (sizeBytes > 1) {
                *pBuffer = '[';
                stringLengthOrError++;
                sizeBytes--;
                pBuffer++;
            } else {
                stringLengthOrError = CELLULAR_SOCK_NO_MEMORY;
            }
        }
        // Do the IP address part
        if (stringLengthOrError >= 0) {
            thisLength = ipAddressToString(&(pAddress->ipAddress), pBuffer, sizeBytes);
            if (thisLength >= 0) {
                sizeBytes -= thisLength;
                pBuffer += thisLength;
                stringLengthOrError += thisLength;
                // If this is an IPV6 address then close the square brackets
                if (pAddress->ipAddress.type == CELLULAR_SOCK_ADDRESS_TYPE_V6) {
                    if (sizeBytes > 1) {
                        *pBuffer = ']';
                        stringLengthOrError++;
                        sizeBytes--;
                        pBuffer++;
                    } else {
                        stringLengthOrError = CELLULAR_SOCK_NO_MEMORY;
                    }
                }
            } else {
                stringLengthOrError = CELLULAR_SOCK_NO_MEMORY;
            }
        }
        // Add the port number
        if (stringLengthOrError >= 0) {
            thisLength = cellularPort_snprintf(pBuffer,
                                               sizeBytes,
                                               ":%u",
                                               pAddress->port);
            if (thisLength < sizeBytes) {
                stringLengthOrError += thisLength;
            } else {
                stringLengthOrError = CELLULAR_SOCK_NO_MEMORY;
            }
        }
    } else {
        // No port number required, just do the ipAddress part
        stringLengthOrError = ipAddressToString(&(pAddress->ipAddress), pBuffer, sizeBytes);
    }

    return (int32_t) stringLengthOrError;
}

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: SENDING AND RECEIVING
 * -------------------------------------------------------------- */

// Send data, UDP style.
int32_t sendTo(CellularSockContainer_t *pContainer,
               const CellularSockAddress_t *pRemoteAddress,
               const void *pData, size_t dataSizeBytes)
{
    CellularSockErrorCode_t errorCodeOrSize = CELLULAR_SOCK_BSD_ERROR;
    int32_t errno = CELLULAR_SOCK_ENONE;
    char buffer[CELLULAR_SOCK_ADDRESS_STRING_MAX_LENGTH_BYTES];
    int32_t sentSize = 0;

    // Get the address as a string
    if (addressToString(pRemoteAddress,
                        false,
                        buffer, 
                        sizeof(buffer)) > 0) {
        if (dataSizeBytes > 0) {
            if (dataSizeBytes <= CELLULAR_SOCK_MAX_UDP_PACKET_SIZE) {
                cellular_ctrl_at_lock();
                cellular_ctrl_at_cmd_start("AT+USOST=");
                // Handle
                cellular_ctrl_at_write_int(pContainer->socket.modemHandle);
                // IP address
                cellular_ctrl_at_write_string(buffer, true);
                // Port number
                cellular_ctrl_at_write_int(pRemoteAddress->port);
                // Number of bytes to follow
                cellular_ctrl_at_write_int(dataSizeBytes);
                cellular_ctrl_at_cmd_stop();
                // Wait for the prompt
                cellular_ctrl_at_wait_char('@');
                // Wait for it...
                cellularPortTaskBlock(50);
                // Go!
                cellular_ctrl_at_write_bytes((uint8_t *) pData,
                                             dataSizeBytes);
                // Grab the response
                cellular_ctrl_at_resp_start("+USOST:", false);
                // Skip the socket ID
                cellular_ctrl_at_skip_param(1);
                // Bytes sent
                sentSize = cellular_ctrl_at_read_int();
                cellular_ctrl_at_resp_stop();
                if (cellular_ctrl_at_unlock_return_error() == 0) {
                    // All is good, probably
                    errorCodeOrSize = sentSize;
                } else {
                    // No route to host
                    errno = CELLULAR_SOCK_EHOSTUNREACH;
                }
            } else {
                // Indicate that the message was too long
                errno = CELLULAR_SOCK_EMSGSIZE;
            }
        } else {
            // Nothing to do
            errorCodeOrSize = CELLULAR_SOCK_SUCCESS;
        }
    } else {
        // Seems appropriate
        errno = CELLULAR_SOCK_EDESTADDRREQ;
    }

    if (errno != CELLULAR_SOCK_ENONE) {
        // Write the errno
        cellularPort_errno_set(errno);
    }

    return (int32_t) errorCodeOrSize;
}

// Send data, TCP style.
int32_t send(CellularSockContainer_t *pContainer,
             const void *pData, size_t dataSizeBytes)
{
    CellularSockErrorCode_t errorCodeOrSize = CELLULAR_SOCK_BSD_ERROR;
    int32_t errno = CELLULAR_SOCK_ENONE;
    int32_t sentSize = 0;
    int32_t leftToSendSize = dataSizeBytes;
    int32_t thisSendSize = CELLULAR_SOCK_MAX_SEGMENT_LENGTH_BYTES;
    size_t loopCounter = 0;
    bool success = true;

    while ((leftToSendSize > 0) && success) {
        loopCounter++;
        if (leftToSendSize < thisSendSize) {
            thisSendSize = leftToSendSize;
        }
        cellular_ctrl_at_lock();
        cellular_ctrl_at_cmd_start("AT+USOWR=");
        // Handle
        cellular_ctrl_at_write_int(pContainer->socket.modemHandle);
        // Number of bytes to follow
        cellular_ctrl_at_write_int(thisSendSize);
        cellular_ctrl_at_cmd_stop();
        // Wait for the prompt
        cellular_ctrl_at_wait_char('@');
        // Wait for it...
        cellularPortTaskBlock(50);
        // Go!
        cellular_ctrl_at_write_bytes((uint8_t *) pData,
                                     dataSizeBytes);
        // Grab the response
        cellular_ctrl_at_resp_start("+USOWR:", false);
        // Skip the socket ID
        cellular_ctrl_at_skip_param(1);
        // Bytes sent
        sentSize = cellular_ctrl_at_read_int();
        cellular_ctrl_at_resp_stop();
        if (cellular_ctrl_at_unlock_return_error() == 0) {
            pData += sentSize;
            leftToSendSize -= sentSize;
            // Technically, it should be OK to
            // send fewer bytes than asked for,
            // however if this happens a lot we'll
            // get stuck, which isn't desirable,
            // so use the loop counter to avoid that
            if ((sentSize < thisSendSize) &&
                (loopCounter >= CELLULAR_SOCK_TCP_RETRY_LIMIT)) {
               success = false;
            }
        } else {
           success = false;
        }
    }

    if (success && (cellular_ctrl_at_get_last_error() == 0)) {
        // All is good
        errorCodeOrSize = dataSizeBytes - leftToSendSize;
    }

    if (errno != CELLULAR_SOCK_ENONE) {
        // Write the errno
        cellularPort_errno_set(errno);
    }

    return (int32_t) errorCodeOrSize;
}

// Receive data, UDP style.
// Notes: pRemoteAddress may be NULL, it is valid
// to receive a zero length UDP packet, one whole
// UDP packet is received by each USORF command,
// gMutexContainer must be locked on entry.
int32_t receiveFrom(CellularSockContainer_t *pContainer,
                    CellularSockAddress_t *pRemoteAddress,
                    void *pData, size_t dataSizeBytes)
{
    CellularSockErrorCode_t errorCodeOrSize = CELLULAR_SOCK_BSD_ERROR;
    int32_t errno = CELLULAR_SOCK_ENONE;
    int32_t startTimeMs = cellularPortGetTickTimeMs();
    char buffer[CELLULAR_SOCK_ADDRESS_STRING_MAX_LENGTH_BYTES];
    int32_t port = -1;
    int32_t wantedReceiveSize;
    int32_t actualReceiveSize;
    int32_t receivedSize = -1;
    uint8_t quoteMark;
    bool success = true;

    // Note: the real maximum length of UDP packet we can receive
    // comes from fitting all of the following into one buffer:
    //
    // +USORF: xx,"max.len.ip.address.ipv4.or.ipv6",yyyyy,wwww,"the_data"\r\n
    //
    // where xx is the handle, max.len.ip.address.ipv4.or.ipv6 is NSAPI_IP_SIZE,
    // yyyyy is the port number (max 65536), wwww is the length of the data and
    // the_data is binary data. I make that 29 + 48 + len(the_data),
    // so the overhead is 77 bytes.

    // Run around the loop until a packet of data turns up or we time out
    while (success && (dataSizeBytes > 0) && (receivedSize < 0)) {
        wantedReceiveSize = CELLULAR_SOCK_MAX_UDP_PACKET_SIZE;
        if (wantedReceiveSize > dataSizeBytes) {
            wantedReceiveSize = dataSizeBytes;
        }
        if (pContainer->socket.pendingBytes > 0) {
            cellular_ctrl_at_lock();
            cellular_ctrl_at_cmd_start("AT+USORF=");
            // Handle
            cellular_ctrl_at_write_int(pContainer->socket.modemHandle);
            // Number of bytes to read
            cellular_ctrl_at_write_int(wantedReceiveSize);
            cellular_ctrl_at_cmd_stop();
            cellular_ctrl_at_resp_start("+USORF:", false);
            // Skip the socket ID
            cellular_ctrl_at_skip_param(1);
            // Read the IP address
            cellular_ctrl_at_read_string(buffer, sizeof(buffer), false);
            // Read the port
            port = cellular_ctrl_at_read_int();
            // Read the amount of data
            actualReceiveSize = cellular_ctrl_at_read_int();
            if (actualReceiveSize > dataSizeBytes) {
                actualReceiveSize = dataSizeBytes;
            }
            // Don't stop for anything!
            cellular_ctrl_at_set_delimiter(0);
            cellular_ctrl_at_set_stop_tag(NULL);
            // Get the leading quote mark out of the way
            cellular_ctrl_at_read_bytes(&quoteMark, 1);
            // Now read the actual data
            cellular_ctrl_at_read_bytes((uint8_t *) pData,
                                        actualReceiveSize);
            cellular_ctrl_at_resp_stop();
            cellular_ctrl_at_set_default_delimiter();
            if (cellular_ctrl_at_unlock_return_error() == 0) {
                // Must use what +USORF returns here as it may be less or more than we asked for
                if (actualReceiveSize > pContainer->socket.pendingBytes) {
                    pContainer->socket.pendingBytes = 0;
                } else {
                    pContainer->socket.pendingBytes -= actualReceiveSize;
                }
                if (actualReceiveSize >= 0) {
                    receivedSize = actualReceiveSize;
                    dataSizeBytes -= actualReceiveSize;
                } else {
                    // cellular_ctrl_at_read_bytes() should not fail
                    success = false;
                }
            } else {
                success = false;
            }
        } else if (cellularPortGetTickTimeMs() - startTimeMs < pContainer->socket.timeoutMs) {
            // Yield to the AT parser task that is listening for URCs
            // that indicated incoming data
            cellularPortTaskBlock(10);
        } else {
            // Timeout with nothing received
            success = false;
            // If we've been set non-blocking,
            // indicate that we would have blocked here
            if (pContainer->socket.nonBlocking) {
                errno = CELLULAR_SOCK_EWOULDBLOCK;
            }
        }
    }

    if (success && (receivedSize >= 0) && (pRemoteAddress != NULL) && (port >= 0)) {
        success = (cellularSockStringToAddress(buffer, pRemoteAddress) == 0);
        pRemoteAddress->port = port;
    }

    // Set the return code
    if (success) {
        errorCodeOrSize = receivedSize;
    }

    if (errno != CELLULAR_SOCK_ENONE) {
        // Write the errno
        cellularPort_errno_set(errno);
    }

    return (int32_t) errorCodeOrSize;
}

// Receive data, TCP style.
// Note: gMutexContainer must be locked on entry.
int32_t receive(CellularSockContainer_t *pContainer,
                void *pData, size_t dataSizeBytes)
{
    CellularSockErrorCode_t errorCodeOrSize = CELLULAR_SOCK_BSD_ERROR;
    int32_t errno = CELLULAR_SOCK_ENONE;
    int32_t startTimeMs = cellularPortGetTickTimeMs();
    int32_t wantedReceiveSize;
    int32_t actualReceiveSize;
    int32_t receivedSize = 0;
    bool success = true;
    uint8_t quoteMark;

    // Run around the loop until we run out of room in the buffer
    // or we time out
    while (success && (dataSizeBytes > 0)) {
        wantedReceiveSize = CELLULAR_SOCK_MAX_SEGMENT_LENGTH_BYTES;
        if (wantedReceiveSize > dataSizeBytes) {
            wantedReceiveSize = dataSizeBytes;
        }
        if (pContainer->socket.pendingBytes > 0) {
            cellular_ctrl_at_lock();
            cellular_ctrl_at_cmd_start("AT+USORD=");
            // Handle
            cellular_ctrl_at_write_int(pContainer->socket.modemHandle);
            // Number of bytes to read
            cellular_ctrl_at_write_int(wantedReceiveSize);
            cellular_ctrl_at_cmd_stop();
            cellular_ctrl_at_resp_start("+USORD:", false);
            // Skip the socket ID
            cellular_ctrl_at_skip_param(1);
            // Read the amount of data
            actualReceiveSize = cellular_ctrl_at_read_int();
            if (actualReceiveSize > dataSizeBytes) {
                actualReceiveSize = dataSizeBytes;
            }
            // Don't stop for anything!
            cellular_ctrl_at_set_delimiter(0);
            cellular_ctrl_at_set_stop_tag(NULL);
            // Get the leading quote mark out of the way
            cellular_ctrl_at_read_bytes(&quoteMark, 1);
            // Now read the actual data
            cellular_ctrl_at_read_bytes((uint8_t *) (pData + receivedSize),
                                        actualReceiveSize);
            cellular_ctrl_at_resp_stop();
            cellular_ctrl_at_set_default_delimiter();
            if (cellular_ctrl_at_unlock_return_error() == 0) {
                // Must use what +USORF returns here as it may be less or more than we asked for
                if (actualReceiveSize > pContainer->socket.pendingBytes) {
                    pContainer->socket.pendingBytes = 0;
                } else {
                    pContainer->socket.pendingBytes -= actualReceiveSize;
                }
                if (actualReceiveSize > 0) {
                    receivedSize += actualReceiveSize;
                    dataSizeBytes -= actualReceiveSize;
                } else {
                    // cellular_ctrl_at_read_bytes() should not fail
                    success = false;
                }
            } else {
                success = false;
            }
        } else if (cellularPortGetTickTimeMs() - startTimeMs < pContainer->socket.timeoutMs) {
            // Yield to the AT parser task that is listening for URCs
            // that indicated incoming data
            cellularPortTaskBlock(10);
        } else {
            if (receivedSize == 0) {
                // Timeout with nothing received
                success = false;
                // If we've been set non-blocking,
                // indicate that we would have blocked here
                if (pContainer->socket.nonBlocking) {
                    errno = CELLULAR_SOCK_EWOULDBLOCK;
                }
            }
            // Timed out, leave with what we have
            break;
        }
    }

    // Set the return code
    if (success) {
        errorCodeOrSize = receivedSize;
    }

    if (errno != CELLULAR_SOCK_ENONE) {
        // Write the errno
        cellularPort_errno_set(errno);
    }

    return (int32_t) errorCodeOrSize;
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: CREATE/OPEN/CLOSE
 * -------------------------------------------------------------- */

// Create a socket.
int32_t cellularSockCreate(CellularSockType_t type,
                           CellularSockProtocol_t protocol)
{
    CellularSockErrorCode_t descriptorOrErrorCode = CELLULAR_SOCK_BSD_ERROR;
    int32_t errno = CELLULAR_SOCK_ENONE;
    CellularSockDescriptor_t descriptor = gNextDescriptor;
    CellularSockContainer_t *pContainer = NULL;
    int32_t count;

    if (init()) {
        if ((type == CELLULAR_SOCK_TYPE_STREAM) ||
            (type == CELLULAR_SOCK_TYPE_DGRAM)) {
            if ((protocol == CELLULAR_SOCK_PROTOCOL_TCP) ||
                (protocol == CELLULAR_SOCK_PROTOCOL_UDP)) {

                CELLULAR_PORT_MUTEX_LOCK(gMutexContainer);

                // Find the next free descriptor
                for (count = 0; (descriptorOrErrorCode < 0) &&
                                (count < CELLULAR_SOCK_MAX); count++) {
                    // Try the descriptor value, making sure 
                    // each time that it can't be found.
                    if (pContainerFindByDescriptor(descriptor) == NULL) {
                        gNextDescriptor = descriptor;
                        CELLULAR_SOCK_INC_DESCRIPTOR(gNextDescriptor);
                        // Found a free descriptor, now try to
                        // create the socket in a container
                        pContainer = pSockContainerCreate(descriptor,
                                                          type, protocol);
                        if (pContainer != NULL) {
                            descriptorOrErrorCode = descriptor;
                        } else {
                            cellularPortLog("CELLULAR_SOCK: unable to allocate memory for socket.\n");
                        }
                    }
                    CELLULAR_SOCK_INC_DESCRIPTOR(descriptor);
                }

                if (count >= CELLULAR_SOCK_MAX) {
                    cellularPortLog("CELLULAR_SOCK: unable to create socket, no free descriptors.\n");
                }

                // If we have a container, talk to cellular to
                // create the socket there
                if (pContainer != NULL) {
                    cellular_ctrl_at_lock();
                    cellular_ctrl_at_cmd_start("AT+USOCR=");
                    // Protocol will be 6 or 17
                    cellular_ctrl_at_write_int(protocol);
                    cellular_ctrl_at_cmd_stop();
                    cellular_ctrl_at_resp_start("+USOCR:", false);
                    pContainer->socket.modemHandle = cellular_ctrl_at_read_int();
                    cellular_ctrl_at_resp_stop();
                    if (cellular_ctrl_at_unlock_return_error() == 0) {
                        // All is good, no need to set descriptorOrErrorCode
                        // as it was already set above
                        cellularPortLog("CELLULAR_SOCK: socket created, descriptor %d, modem handle %d.\n",
                                        descriptorOrErrorCode,
                                        pContainer->socket.modemHandle);
                    } else {
                        // If the modem could not create the socket,
                        // free the container once more
                        containerFree(descriptorOrErrorCode);
                        descriptorOrErrorCode = CELLULAR_SOCK_BSD_ERROR;
                        // Use a distinctly different errno for this
                        errno = CELLULAR_SOCK_EIO;
                        cellularPortLog("CELLULAR_SOCK: modem could not create socket.\n");
                    }
                } else {
                    // No buffers available
                    errno = CELLULAR_SOCK_ENOBUFS;
                }

                CELLULAR_PORT_MUTEX_UNLOCK(gMutexContainer);

            } else {
                // Not a protocol we support
                errno = CELLULAR_SOCK_EPROTONOSUPPORT;
            }
        } else {
            // Not a protocol type we support
            errno = CELLULAR_SOCK_EPFNOSUPPORT;
        }
    } else {
        // The only reason initialisation might fail
        errno = CELLULAR_SOCK_ENOMEM;
    }

    if (errno != CELLULAR_SOCK_ENONE) {
        // Write the errno
        cellularPort_errno_set(errno);
    }

    return (int32_t) descriptorOrErrorCode;
}

// Make an outgoing connection on the given socket.
int32_t cellularSockConnect(CellularSockDescriptor_t descriptor,
                            const CellularSockAddress_t *pRemoteAddress)
{
    CellularSockErrorCode_t errorCode = CELLULAR_SOCK_BSD_ERROR;
    int32_t errno = CELLULAR_SOCK_ENONE;
    CellularSockContainer_t *pContainer = NULL;
    char buffer[CELLULAR_SOCK_ADDRESS_STRING_MAX_LENGTH_BYTES];

    if (init()) {
        // Check that the remote IP address is sensible
        if ((pRemoteAddress != NULL) &&
            (addressToString(pRemoteAddress, false, buffer, sizeof(buffer)) > 0)) {

            CELLULAR_PORT_MUTEX_LOCK(gMutexContainer);

            // Find the container
            pContainer = pContainerFindByDescriptor(descriptor);

            // If we have found the container, talk to cellular to
            // make the connection
            if (pContainer != NULL) {
                if (pContainer->socket.state == CELLULAR_SOCK_STATE_CREATED) {
                    cellularPortLog("CELLULAR_CTRL_SOCK: connecting socket to \"%s\"...\n",
                                    buffer);
                    cellular_ctrl_at_lock();
                    cellular_ctrl_at_cmd_start("AT+USOCO=");
                    // Handle
                    cellular_ctrl_at_write_int(pContainer->socket.modemHandle);
                    // IP address
                    cellular_ctrl_at_write_string(buffer, true);
                    // Port number
                    if (pRemoteAddress->port > 0) {
                        cellular_ctrl_at_write_int(pRemoteAddress->port);
                    }
                    cellular_ctrl_at_cmd_stop_read_resp();
                    if (cellular_ctrl_at_unlock_return_error() == 0) {
                        // All is good
                        pCellularPort_memcpy(&pContainer->socket.remoteAddress,
                                             pRemoteAddress,
                                             sizeof (pContainer->socket.remoteAddress));
                        pContainer->socket.state = CELLULAR_SOCK_STATE_CONNECTED;
                        errorCode = CELLULAR_SOCK_SUCCESS;
                        cellularPortLog("CELLULAR_SOCK: socket with descriptor %d, modem handle %d, is connected to address %.*s.\n",
                                        descriptor,
                                        pContainer->socket.modemHandle,
                                        addressToString(&pContainer->socket.remoteAddress,
                                                        true,
                                                        buffer, sizeof(buffer)),
                                        buffer);
                    } else {
                        // Host is not reachable
                        errno = CELLULAR_SOCK_EHOSTUNREACH;
                        cellularPortLog("CELLULAR_SOCK: remote address %.*s is not reachable.\n",
                                        addressToString(pRemoteAddress, true,
                                                        buffer, sizeof(buffer)),
                                        buffer);
                    }
                } else {
                    // TODO: is "operation not permitted" the right error?
                    errno = CELLULAR_SOCK_EPERM;
                }
            } else {
                // Indicate that we weren't passed a valid socket descriptor
                errno = CELLULAR_SOCK_EBADF;
            }

            CELLULAR_PORT_MUTEX_UNLOCK(gMutexContainer);

        } else {
            // Seems appropriate
            errno = CELLULAR_SOCK_EDESTADDRREQ;
        }
    } else {
        // The only reason initialisation might fail
        errno = CELLULAR_SOCK_ENOMEM;
    }

    if (errno != CELLULAR_SOCK_ENONE) {
        // Write the errno
        cellularPort_errno_set(errno);
    }

    return (int32_t) errorCode;
}

// Close a socket.
int32_t cellularSockClose(CellularSockDescriptor_t descriptor)
{
    CellularSockErrorCode_t errorCode = CELLULAR_SOCK_BSD_ERROR;
    int32_t errno = CELLULAR_SOCK_ENONE;
    CellularSockContainer_t *pContainer = NULL;

    if (init()) {

        CELLULAR_PORT_MUTEX_LOCK(gMutexContainer);

        // Find the container
        pContainer = pContainerFindByDescriptor(descriptor);

        // If we have found the container, talk to cellular to
        // close the socket there
        if (pContainer != NULL) {
            cellular_ctrl_at_lock();
            cellular_ctrl_at_cmd_start("AT+USOCL=");
            cellular_ctrl_at_write_int(pContainer->socket.modemHandle);
            cellular_ctrl_at_cmd_stop_read_resp();
            if (cellular_ctrl_at_unlock_return_error() == 0) {
                cellularPortLog("CELLULAR_SOCK: socket with descriptor %d, modem handle %d, has been closed.\n",
                                descriptor,
                                pContainer->socket.modemHandle);
                errorCode = CELLULAR_SOCK_SUCCESS;
                // Now mark the socket as closed.
                // Socket is only freed by a call to
                // cellularSockCleanUp() in order to ensure
                // thread-safeness
                pContainer->socket.state = CELLULAR_SOCK_STATE_CLOSED;
            } else {
                // Use a distinctly different errno for this
                errno = CELLULAR_SOCK_EIO;
                cellularPortLog("CELLULAR_SOCK: modem could not close socket with descriptor %d, handle %d.\n",
                                 descriptor,
                                 pContainer->socket.modemHandle);
            }
        } else {
            // Indicate that we weren't passed a valid socket descriptor
            errno = CELLULAR_SOCK_EBADF;
        }

        CELLULAR_PORT_MUTEX_UNLOCK(gMutexContainer);

    } else {
        // The only reason initialisation might fail
        errno = CELLULAR_SOCK_ENOMEM;
    }

    if (errno != CELLULAR_SOCK_ENONE) {
        // Write the errno
        cellularPort_errno_set(errno);
    }

    return (int32_t) errorCode;
}

// Clean-up memory occupied by closed sockets.
void cellularSockCleanUp()
{
    CellularSockContainer_t *pContainer = gpContainerListHead;
    CellularSockContainer_t *pTmp;
    size_t numNonClosedSockets = 0;

    if (gInitialised) {

        CELLULAR_PORT_MUTEX_LOCK(gMutexContainer);

        // Move through the list removing closed sockets
        while (pContainer != NULL) {
            if (pContainer->socket.state == CELLULAR_SOCK_STATE_CLOSED) {
                if (!(pContainer->isStatic)) {
                    // If this socket is not static, uncouple it
                    // If there is a previous container, move its pNext
                    if (pContainer->pPrevious != NULL) {
                        pContainer->pPrevious->pNext = pContainer->pNext;
                    } else {
                        // If there is no previous container, must be
                        // at the start of the list so move the head
                        // pointer on instead
                        gpContainerListHead = pContainer->pNext;
                    }
                    // If there is a next container, move its pPrevious
                    if (pContainer->pNext != NULL) {
                        pContainer->pNext->pPrevious = pContainer->pPrevious;
                    }

                    // Remember the next pointer
                    pTmp = pContainer->pNext;
                    // Free the memory
                    cellularPort_free(pContainer);
                    // Move to the next entry
                    pContainer = pTmp;
                } else {
                    // Just move on
                   pContainer = pContainer->pNext;
                }
            } else {
                // Move on but count the number of non-closed sockets
                numNonClosedSockets++;
                pContainer = pContainer->pNext;
            }
        }

        // If everything has been closed, we can deinit()
        if (numNonClosedSockets == 0) {
            deinitButNotMutex();
        }

        CELLULAR_PORT_MUTEX_UNLOCK(gMutexContainer);
    }
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: CONFIGURE
 * -------------------------------------------------------------- */

// Configure the given socket's file parameters.
int32_t cellularSockFctl(CellularSockDescriptor_t descriptor,
                         int32_t command,
                         int32_t value)
{
    // Since the return value depends upon the command, 
    // the only reliable error value for FCTL is -1.
    CellularSockErrorCode_t errorCode = CELLULAR_SOCK_BSD_ERROR;

    // TODO

    return (int32_t) errorCode;
}

// Set the options for the given socket.
int32_t cellularSockSetOption(CellularSockDescriptor_t descriptor,
                              int32_t level,
                              uint32_t option,
                              const void *pOptionValue,
                              size_t optionValueLength)
{
    CellularSockErrorCode_t errorCode = CELLULAR_SOCK_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Get the options for the given socket.
int32_t cellularSockGetOption(CellularSockDescriptor_t descriptor,
                              int32_t level,
                              uint32_t option,
                              void *pOptionValue,
                              size_t optionValueLength)
{
    CellularSockErrorCode_t errorCode = CELLULAR_SOCK_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: UDP ONLY
 * -------------------------------------------------------------- */

// Send a datagram to the given host.
int32_t cellularSockSendTo(CellularSockDescriptor_t descriptor,
                           const CellularSockAddress_t *pRemoteAddress,
                           const void *pData, size_t dataSizeBytes)
{
    CellularSockErrorCode_t errorCodeOrSize = CELLULAR_SOCK_BSD_ERROR;
    int32_t errno = CELLULAR_SOCK_ENONE;
    CellularSockContainer_t *pContainer = NULL;

    if (init()) {

        CELLULAR_PORT_MUTEX_LOCK(gMutexContainer);

        // Find the container
        pContainer = pContainerFindByDescriptor(descriptor);

        // If we have found the container, talk to cellular to
        // do the sending
        if (pContainer != NULL) {
            // Check parameters
            if (pRemoteAddress == NULL) {
                // If there is no remote address and the socket was
                // connected we must use the stored address
                if (pContainer->socket.state == CELLULAR_SOCK_STATE_CONNECTED) {
                    pRemoteAddress = &(pContainer->socket.remoteAddress);
                } else {
                    if (pContainer->socket.state == CELLULAR_SOCK_STATE_SHUTDOWN) {
                        // Socket is shut down
                        errno = CELLULAR_SOCK_ESHUTDOWN;
                    } else {
                        // Destination address required?
                        errno = CELLULAR_SOCK_EDESTADDRREQ;
                    }
                }
            }
            if (pRemoteAddress != NULL) {
                if ((pData == NULL) && (dataSizeBytes > 0)) {
                    // Invalid argument
                    errno = CELLULAR_SOCK_EINVAL;
                } else {
                    if ((pData != NULL) && (dataSizeBytes > 0)) {
                        // It's OK to send UDP packets on a TCP socket
                        if ((pContainer->socket.protocol == CELLULAR_SOCK_PROTOCOL_UDP) ||
                            (pContainer->socket.protocol == CELLULAR_SOCK_PROTOCOL_TCP)) {
                            errorCodeOrSize = sendTo(pContainer, pRemoteAddress,
                                                     pData, dataSizeBytes);

                            // the AT parser in, do not rely on it here.
                        } else {
                            // Should never get here, throw 'em a googley so that the
                            // error is distinct
                            errno = CELLULAR_SOCK_EPROTOTYPE;
                        }
                    } else {
                        // Nothing to do
                        errorCodeOrSize = CELLULAR_SOCK_SUCCESS;
                    }
                }
            }
        } else {
            // Indicate that we weren't passed a valid socket descriptor
            errno = CELLULAR_SOCK_EBADF;
        }

        CELLULAR_PORT_MUTEX_UNLOCK(gMutexContainer);

    } else {
        // The only reason initialisation might fail
        errno = CELLULAR_SOCK_ENOMEM;
    }

    if (errno != CELLULAR_SOCK_ENONE) {
        // Write the errno
        cellularPort_errno_set(errno);
    }

    return (int32_t) errorCodeOrSize;
}

// Receive a datagram from the given host.
int32_t cellularSockReceiveFrom(CellularSockDescriptor_t descriptor,
                                CellularSockAddress_t *pRemoteAddress,
                                void *pData, size_t dataSizeBytes)
{
    CellularSockErrorCode_t errorCodeOrSize = CELLULAR_SOCK_BSD_ERROR;
    int32_t errno = CELLULAR_SOCK_ENONE;
    CellularSockContainer_t *pContainer = NULL;

    if (init()) {

        CELLULAR_PORT_MUTEX_LOCK(gMutexContainer);

        // Find the container
        pContainer = pContainerFindByDescriptor(descriptor);

        // If we have found the container, talk to cellular to
        // do the receiving
        if (pContainer != NULL) {
            if ((pData == NULL) && (dataSizeBytes > 0)) {
                // Invalid argument
                errno = CELLULAR_SOCK_EINVAL;
            } else {
                if ((pData != NULL) && (dataSizeBytes > 0)) {
                    // It's OK to receive UDP packets on a TCP socket
                    if ((pContainer->socket.protocol == CELLULAR_SOCK_PROTOCOL_UDP) ||
                        (pContainer->socket.protocol == CELLULAR_SOCK_PROTOCOL_TCP)) {
                        errorCodeOrSize = receiveFrom(pContainer, pRemoteAddress,
                                                      pData, dataSizeBytes);
                    } else {
                        // Should never get here, throw 'em a googley so that the
                        // error is distinct
                        errno = CELLULAR_SOCK_EPROTOTYPE;
                    }
                } else {
                    // Not an error, just nothing to do
                    errorCodeOrSize = CELLULAR_SOCK_SUCCESS;
                }
            }
        } else {
            // Indicate that we weren't passed a valid socket descriptor
            errno = CELLULAR_SOCK_EBADF;
        }

        CELLULAR_PORT_MUTEX_UNLOCK(gMutexContainer);

    } else {
        // The only reason initialisation might fail
        errno = CELLULAR_SOCK_ENOMEM;
    }

    if (errno != CELLULAR_SOCK_ENONE) {
        // Write the errno
        cellularPort_errno_set(errno);
    }

    return (int32_t) errorCodeOrSize;
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: STREAM (TCP)
 * -------------------------------------------------------------- */

// Send data.
int32_t cellularSockWrite(CellularSockDescriptor_t descriptor,
                          const void *pData, size_t dataSizeBytes)
{
    CellularSockErrorCode_t errorCodeOrSize = CELLULAR_SOCK_BSD_ERROR;
    int32_t errno = CELLULAR_SOCK_ENONE;
    CellularSockContainer_t *pContainer = NULL;

    if (init()) {
        // Check parameters
        if (pData != NULL) {

            CELLULAR_PORT_MUTEX_LOCK(gMutexContainer);

            // Find the container
            pContainer = pContainerFindByDescriptor(descriptor);

            // If we have found the container, talk to cellular to
            // do the sending
            if (pContainer != NULL) {
                if (pContainer->socket.protocol == CELLULAR_SOCK_PROTOCOL_TCP) {
                    if (pContainer->socket.state == CELLULAR_SOCK_STATE_CONNECTED) {
                        if ((pData == NULL) && (dataSizeBytes > 0)) {
                            // Invalid argument
                            errno = CELLULAR_SOCK_EINVAL;
                        } else {
                            if ((pData != NULL) && (dataSizeBytes > 0)) {
                                errorCodeOrSize = send(pContainer, pData, dataSizeBytes);
                            } else {
                                // Nothing to do
                                errorCodeOrSize = CELLULAR_SOCK_SUCCESS;
                            }
                        }
                    } else {
                        if (pContainer->socket.state == CELLULAR_SOCK_STATE_SHUTDOWN) {
                            // Socket is shut down
                            errno = CELLULAR_SOCK_ESHUTDOWN;
                        } else {
                            // No route to host?
                            errno = CELLULAR_SOCK_EHOSTUNREACH;
                        }
                    }
                } else {
                    // Should never get here, throw 'em a googley so that the
                    // error is distinct
                    errno = CELLULAR_SOCK_EPROTOTYPE;
                }
            } else {
                // Indicate that we weren't passed a valid socket descriptor
                errno = CELLULAR_SOCK_EBADF;
            }

            CELLULAR_PORT_MUTEX_UNLOCK(gMutexContainer);

        } else {
            // Invalid argument
            errno = CELLULAR_SOCK_EINVAL;
        }
    } else {
        // The only reason initialisation might fail
        errno = CELLULAR_SOCK_ENOMEM;
    }

    if (errno != CELLULAR_SOCK_ENONE) {
        // Write the errno
        cellularPort_errno_set(errno);
    }

    return (int32_t) errorCodeOrSize;
}

// Receive data.
int32_t cellularSockRead(CellularSockDescriptor_t descriptor,
                         void *pData, size_t dataSizeBytes)
{
    CellularSockErrorCode_t errorCodeOrSize = CELLULAR_SOCK_BSD_ERROR;
    int32_t errno = CELLULAR_SOCK_ENONE;
    CellularSockContainer_t *pContainer = NULL;

    if (init()) {

        CELLULAR_PORT_MUTEX_LOCK(gMutexContainer);

        // Find the container
        pContainer = pContainerFindByDescriptor(descriptor);

        // If we have found the container, talk to cellular to
        // do the receiving
        if (pContainer != NULL) {
            if (pContainer->socket.protocol == CELLULAR_SOCK_PROTOCOL_TCP) {
                if (pContainer->socket.state == CELLULAR_SOCK_STATE_CONNECTED) {
                    if ((pData == NULL) && (dataSizeBytes > 0)) {
                        // Invalid argument
                        errno = CELLULAR_SOCK_EINVAL;
                    } else {
                        if ((pData != NULL) && (dataSizeBytes != 0)) {
                            if (pContainer->socket.protocol == CELLULAR_SOCK_PROTOCOL_TCP) {
                                errorCodeOrSize = receive(pContainer, pData, dataSizeBytes);
                            }
                        } else {
                            // Not an error, just nothing to do
                            errorCodeOrSize = CELLULAR_SOCK_SUCCESS;
                        }
                    }
                } else {
                    // No route to host?
                    errno = CELLULAR_SOCK_EHOSTUNREACH;
                }
            } else {
                // Should never get here, throw 'em a googley so that the
                // error is distinct
                errno = CELLULAR_SOCK_EPROTOTYPE;
            }
        } else {
            // Indicate that we weren't passed a valid socket descriptor
            errno = CELLULAR_SOCK_EBADF;
        }

        CELLULAR_PORT_MUTEX_UNLOCK(gMutexContainer);

    } else {
        // The only reason initialisation might fail
        errno = CELLULAR_SOCK_ENOMEM;
    }

    if (errno != CELLULAR_SOCK_ENONE) {
        // Write the errno
        cellularPort_errno_set(errno);
    }

    return (int32_t) errorCodeOrSize;
}

// Prepare a TCP socket for being closed.
int32_t cellularSockShutdown(CellularSockDescriptor_t descriptor,
                             CellularSockShutdown_t how)
{
    CellularSockErrorCode_t errorCode = CELLULAR_SOCK_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: ASYNC
 * -------------------------------------------------------------- */

// Register a callback for incoming data.
int32_t cellularSockRegisterCallbackData(CellularSockDescriptor_t descriptor,
                                         void (*pCallback) (void *),
                                         void *pCallbackParam)
{
    CellularSockErrorCode_t errorCode = CELLULAR_SOCK_BSD_ERROR;
    int32_t errno = CELLULAR_SOCK_ENONE;
    CellularSockContainer_t *pContainer = NULL;

    if (init()) {

        CELLULAR_PORT_MUTEX_LOCK(gMutexContainer);

        // Find the container
        pContainer = pContainerFindByDescriptor(descriptor);

        // If we have found the container, set up the callback
        if (pContainer != NULL) {

            CELLULAR_PORT_MUTEX_LOCK(gMutexCallbacks);

            pContainer->socket.pPendingDataCallback = pCallback;
            pContainer->socket.pPendingDataCallbackParam = pCallbackParam;

            CELLULAR_PORT_MUTEX_UNLOCK(gMutexCallbacks);

            errorCode = CELLULAR_SOCK_SUCCESS;
        } else {
            // Indicate that we weren't passed a valid socket descriptor
            errno = CELLULAR_SOCK_EBADF;
        }

        CELLULAR_PORT_MUTEX_UNLOCK(gMutexContainer);

    } else {
        // The only reason initialisation might fail
        errno = CELLULAR_SOCK_ENOMEM;
    }

    if (errno != CELLULAR_SOCK_ENONE) {
        // Write the errno
        cellularPort_errno_set(errno);
    }

    return (int32_t) errorCode;
}

// Register a callback for socket closed.
int32_t cellularSockRegisterCallbackClosed(CellularSockDescriptor_t descriptor,
                                           void (*pCallback) (void *),
                                           void *pCallbackParam)
{
    CellularSockErrorCode_t errorCode = CELLULAR_SOCK_BSD_ERROR;
    int32_t errno = CELLULAR_SOCK_ENONE;
    CellularSockContainer_t *pContainer = NULL;

    if (init()) {

        CELLULAR_PORT_MUTEX_LOCK(gMutexContainer);

        // Find the container
        pContainer = pContainerFindByDescriptor(descriptor);

        // If we have found the container, set up the callbacks
        if (pContainer != NULL) {

            CELLULAR_PORT_MUTEX_LOCK(gMutexCallbacks);

            pContainer->socket.pConnectionClosedCallback = pCallback;
            pContainer->socket.pConnectionClosedCallbackParam = pCallbackParam;

            CELLULAR_PORT_MUTEX_UNLOCK(gMutexCallbacks);

            errorCode = CELLULAR_SOCK_SUCCESS;
        } else {
            // Indicate that we weren't passed a valid socket descriptor
            errno = CELLULAR_SOCK_EBADF;
        }

        CELLULAR_PORT_MUTEX_UNLOCK(gMutexContainer);

    } else {
        // The only reason initialisation might fail
        errno = CELLULAR_SOCK_ENOMEM;
    }

    if (errno != CELLULAR_SOCK_ENONE) {
        // Write the errno
        cellularPort_errno_set(errno);
    }

    return (int32_t) errorCode;
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: TCP INCOMING (TCP SERVER) ONLY
 * -------------------------------------------------------------- */

// Bind a socket to a local address.
int32_t cellularSockBind(CellularSockDescriptor_t descriptor,
                         const CellularSockAddress_t *pLocalAddress)
{
    CellularSockErrorCode_t errorCode = CELLULAR_SOCK_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Set the given socket into listening mode.
int32_t cellularSockListen(CellularSockDescriptor_t descriptor,
                           size_t backlog)
{
    CellularSockErrorCode_t errorCode = CELLULAR_SOCK_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Accept an incoming TCP connection on the given socket.
int32_t cellularSockAccept(CellularSockDescriptor_t descriptor,
                           CellularSockAddress_t *pRemoteAddress)
{
    CellularSockErrorCode_t errorCode = CELLULAR_SOCK_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Select: wait for one of a set of sockets to become unblocked.
int32_t cellularSockSelect(int32_t maxDescriptor,
                           CellularSockDescriptorSet_t *pReadDescriptorSet,
                           CellularSockDescriptorSet_t *pWriteDescriptoreSet,
                           CellularSockDescriptorSet_t *pExceptDescriptorSet,
                           int32_t timeMs)
{
    CellularSockErrorCode_t errorCode = CELLULAR_SOCK_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: FINDING ADDRESSES
 * -------------------------------------------------------------- */

// Get the address of the remote host connected to a given socket.
int32_t cellularSockGetRemoteAddress(CellularSockDescriptor_t descriptor,
                                     CellularSockAddress_t *pRemoteAddress)
{
    CellularSockErrorCode_t errorCode = CELLULAR_SOCK_BSD_ERROR;
    int32_t errno = CELLULAR_SOCK_ENONE;
    CellularSockContainer_t *pContainer = NULL;

    if (init()) {

        // Check parameters
        if (pRemoteAddress != NULL) {

            CELLULAR_PORT_MUTEX_LOCK(gMutexContainer);

            // Find the container
            pContainer = pContainerFindByDescriptor(descriptor);

            if (pContainer != NULL) {
                if (pContainer->socket.state == CELLULAR_SOCK_STATE_CONNECTED) {
                    pCellularPort_memcpy(pRemoteAddress,
                                         &(pContainer->socket.remoteAddress),
                                         sizeof(*pRemoteAddress));
                    errorCode = CELLULAR_SOCK_SUCCESS;
                } else {
                    // No route to host?
                    errno = CELLULAR_SOCK_EHOSTUNREACH;
                }
            } else {
                // Indicate that we weren't passed a valid socket descriptor
                errno = CELLULAR_SOCK_EBADF;
            }

            CELLULAR_PORT_MUTEX_UNLOCK(gMutexContainer);

        } else {
            // Invalid argument
            errno = CELLULAR_SOCK_EINVAL;
        }
    } else {
        // The only reason initialisation might fail
        errno = CELLULAR_SOCK_ENOMEM;
    }

    if (errno != CELLULAR_SOCK_ENONE) {
        // Write the errno
        cellularPort_errno_set(errno);
    }

    return (int32_t) errorCode;
}

// Get the local address of the given socket.
int32_t cellularSockGetLocalAddress(CellularSockDescriptor_t descriptor,
                                    CellularSockAddress_t *pLocalAddress)
{
    CellularSockErrorCode_t errorCode = CELLULAR_SOCK_BSD_ERROR;
    int32_t errno = CELLULAR_SOCK_ENONE;
    CellularSockContainer_t *pContainer = NULL;
    char buffer[CELLULAR_CTRL_IP_ADDRESS_SIZE];

    if (init()) {

        // Check parameters
        if (pLocalAddress != NULL) {

            CELLULAR_PORT_MUTEX_LOCK(gMutexContainer);

            // Check that the descriptor is at least valid
            pContainer = pContainerFindByDescriptor(descriptor);
            if (pContainer != NULL) {
                // IP address is that of cellular, for all sockets
                if (cellularCtrlGetIpAddressStr(buffer) > 0) {
                    if (cellularSockStringToAddress(buffer,
                                                    pLocalAddress) == 0) {
                        errorCode = CELLULAR_SOCK_SUCCESS;
                    }
                    // TODO: where to get the port number from?
                } else {
                    // Network is down
                    errno = CELLULAR_SOCK_ENETDOWN;
                }
            } else {
                // Indicate that we weren't passed a valid socket descriptor
                errno = CELLULAR_SOCK_EBADF;
            }

            CELLULAR_PORT_MUTEX_UNLOCK(gMutexContainer);

        } else {
            // Nothing to do
            errorCode = CELLULAR_SOCK_SUCCESS;
        }
    } else {
        // The only reason initialisation might fail
        errno = CELLULAR_SOCK_ENOMEM;
    }

    if (errno != CELLULAR_SOCK_ENONE) {
        // Write the errno
        cellularPort_errno_set(errno);
    }

    return (int32_t) errorCode;
}

// Get the IP address of the given host name.
int32_t cellularSockGetHostByName(const char *pHostName,
                                  CellularSockIpAddress_t *pHostIpAddress)
{
    CellularSockErrorCode_t errorCode = CELLULAR_SOCK_BSD_ERROR;
    char buffer[CELLULAR_SOCK_ADDRESS_STRING_MAX_LENGTH_BYTES];
    CellularSockAddress_t address;
    int32_t bytesRead;
    int32_t atError;

    // No need to call init() here, this does not use the mutexes
    if (pHostName != NULL) {
        cellularPortLog("CELLULAR_SOCK: looking up IP address of \"%s\".\n",
                        pHostName);
        cellular_ctrl_at_lock();
        // Allow plenty of time
        cellular_ctrl_at_set_at_timeout(60000, false);
        cellular_ctrl_at_cmd_start("AT+UDNSRN=");
        cellular_ctrl_at_write_int(0);
        cellular_ctrl_at_write_string(pHostName, true);
        cellular_ctrl_at_cmd_stop();
        cellular_ctrl_at_resp_start("+UDNSRN:", false);
        bytesRead = cellular_ctrl_at_read_string(buffer,
                                                 sizeof(buffer),
                                                 false);
        cellular_ctrl_at_resp_stop();
        cellular_ctrl_at_restore_at_timeout();
        atError = cellular_ctrl_at_unlock_return_error();
        if ((bytesRead >= 0) && (atError == 0)) {
            // All is good
            cellularPortLog("CELLULAR_SOCK: found it at \"%.*s\".\n",
                            bytesRead, buffer);
            if (pHostIpAddress != NULL) {
                // Convert to struct
                if (cellularSockStringToAddress(buffer,
                                                &address) == 0) {
                    pCellularPort_memcpy(pHostIpAddress,
                                         &(address.ipAddress),
                                         sizeof(*pHostIpAddress));
                    errorCode = CELLULAR_SOCK_SUCCESS;
                }
            } else {
                errorCode = CELLULAR_SOCK_SUCCESS;
            }
        } else {
            cellularPortLog("CELLULAR_SOCK: host not found.\n");
        }
    } else {
        // Nothing to do
        errorCode = CELLULAR_SOCK_SUCCESS;
    }

    return (int32_t) errorCode;
}

/* ----------------------------------------------------------------
 * PUBIC FUNCTIONS: ADDRESS CONVERSION
 * -------------------------------------------------------------- */

// Convert an address string into a struct.
int32_t cellularSockStringToAddress(const char *pAddressString,
                                    CellularSockAddress_t *pAddress)
{
    CellularSockErrorCode_t errorCode = CELLULAR_SOCK_INVALID_PARAMETER;

    // No need to call init() here, this does not use the mutexes
    if ((pAddressString != NULL) && (pAddress != NULL)) {
        errorCode = CELLULAR_SOCK_INVALID_ADDRESS;
        if (addressStringIsIpv4(pAddressString)) {
            if (ipv4StringToAddress(pAddressString, pAddress)) {
                errorCode = CELLULAR_SOCK_SUCCESS;
            }
        } else {
            if (ipv6StringToAddress(pAddressString, pAddress)) {
                errorCode = CELLULAR_SOCK_SUCCESS;
            }
        }
    }

    return (int32_t) errorCode;
}

// Convert an IP address struct into a string.
int32_t cellularSockIpAddressToString(const CellularSockIpAddress_t *pIpAddress,
                                      char *pBuffer,
                                      size_t sizeBytes)
{
    CellularSockErrorCode_t stringLengthOrErrorCode = CELLULAR_SOCK_INVALID_PARAMETER;

    // No need to call init() here, this does not use the mutexes
    if ((pIpAddress != NULL) && (pBuffer != NULL)) {
        stringLengthOrErrorCode = ipAddressToString(pIpAddress,
                                                    pBuffer,
                                                    sizeBytes);
    }

    return (int32_t) stringLengthOrErrorCode;
}

// Convert an address struct into a string.
int32_t cellularSockAddressToString(const CellularSockAddress_t *pAddress,
                                    char *pBuffer,
                                    size_t sizeBytes)
{
    CellularSockErrorCode_t stringLengthOrErrorCode = CELLULAR_SOCK_INVALID_PARAMETER;

    // No need to call init() here, this does not use the mutexes
    if ((pAddress != NULL) && (pBuffer != NULL)) {
        stringLengthOrErrorCode = addressToString(pAddress,
                                                  true,
                                                  pBuffer,
                                                  sizeBytes);
    }

    return (int32_t) stringLengthOrErrorCode;
}

// End of file
