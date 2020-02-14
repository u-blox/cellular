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

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

// A socket.
 typedef struct {
     CellularSockType_t type;
     CellularSockProtocol_t protocol;
     int32_t modemHandle;
 } CellularSockSocket_t;

// A socket container.
typedef struct CellularSockContainer_t {
    CellularSockDescriptor_t descriptor;
    CellularSockSocket_t socket;
    struct CellularSockContainer_t *pNext;
} CellularSockContainer_t;

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// Keep track of whether we're initialised or not.
static bool gInitialised = false;

// Mutex to protect the container list.
static CellularPortMutexHandle_t gMutex;

// Root of the socket container list.
static CellularSockContainer_t *gpContainerListHead = NULL;

// The next descriptor to use.
static CellularSockDescriptor_t gNextDescriptor = 0;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: MISC
 * -------------------------------------------------------------- */

// Initialise.
static bool init()
{
    if (!gInitialised) {
        gInitialised = (cellularPortMutexCreate(&gMutex) == 0);
    }

    return gInitialised;
}

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: CONTAINER STUFF
 * -------------------------------------------------------------- */

// Find the socket container for the given descriptor.
// This does NOT lock the mutex, you need to do that.
static CellularSockContainer_t *pContainerFind(CellularSockDescriptor_t descriptor)
{
    CellularSockContainer_t *pContainer = NULL;
    CellularSockContainer_t *ppContainerThis = gpContainerListHead;

    for (size_t x = 0; (ppContainerThis != NULL) &&
                       (pContainer == NULL) &&
                       (x < CELLULAR_SOCK_MAX); x++) {
        if (ppContainerThis->descriptor == descriptor) {
            pContainer = ppContainerThis;
        }
        ppContainerThis = ppContainerThis->pNext;
    }

    return pContainer;
}

// Create a container with the given descriptor.
// This does NOT lock the mutex, you need to do that.
static CellularSockContainer_t *pContainerCreate(CellularSockDescriptor_t descriptor)
{
    CellularSockContainer_t *pContainer = NULL;
    CellularSockContainer_t **ppContainerThis = &gpContainerListHead;

    // Go to the end of the list
    while (*ppContainerThis != NULL) {
        ppContainerThis = &((*ppContainerThis)->pNext);
    }

    // Allocate memory for the new container and add it to the list
    pContainer = (CellularSockContainer_t *) pCellularPort_malloc(sizeof (*pContainer));
    if (pContainer != NULL) {
        pContainer->descriptor = descriptor;
        pCellularPort_memset(&(pContainer->socket), 0, sizeof(pContainer->socket));
        pContainer->pNext = NULL;
        *ppContainerThis = pContainer;
    }

    return pContainer;
}

// Free the given container.
// This does NOT lock the mutex, you need to do that.
static bool containerFree(CellularSockDescriptor_t descriptor)
{
    CellularSockContainer_t **ppContainer = NULL;
    CellularSockContainer_t *pContainerPrevious = NULL;
    CellularSockContainer_t **ppContainerThis = &gpContainerListHead;
    bool success = false;

    for (size_t x = 0; (*ppContainerThis != NULL) &&
                       (*ppContainer == NULL) &&
                       (x < CELLULAR_SOCK_MAX); x++) {
        if ((*ppContainerThis)->descriptor == descriptor) {
            ppContainer = ppContainerThis;
        } else {
            pContainerPrevious = *ppContainerThis;
            ppContainerThis = &((*ppContainerThis)->pNext);
        }
    }

    // If we found it...
    if ((ppContainer != NULL) && (*ppContainer != NULL)) {
        // If there was a previous container, move the
        // contents of pNext there
        if (pContainerPrevious != NULL) {
            pContainerPrevious->pNext = (*ppContainerThis)->pNext;
        }
        // Free the memory and NULL the pointer
        cellularPort_free(*ppContainer);
        *ppContainer = NULL;
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
            pAddress->ipAddress.address.ipv6[0] = (a << 16) | (b & 0xFFFF);
            pAddress->ipAddress.address.ipv6[1] = (c << 16) | (d & 0xFFFF);
            pAddress->ipAddress.address.ipv6[2] = (e << 16) | (f & 0xFFFF);
            pAddress->ipAddress.address.ipv6[3] = (g << 16) | (h & 0xFFFF);

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
            stringLengthOrError += cellularPort_snprintf(pBuffer,
                                                         sizeBytes,
                                                         ":%u",
                                                         pAddress->port);
            if (stringLengthOrError >= sizeBytes) {
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

                CELLULAR_PORT_MUTEX_LOCK(gMutex);

                // Find the next free descriptor
                for (count = 0; (descriptorOrErrorCode < 0) &&
                                (count < CELLULAR_SOCK_MAX); count++) {
                    // Try the descriptor value, making sure 
                    // each time that it can't be found.
                    if (pContainerFind(descriptor) == NULL) {
                        gNextDescriptor = descriptor;
                        CELLULAR_SOCK_INC_DESCRIPTOR(gNextDescriptor);
                        // Found a free descriptor, now try to
                        // create the container
                        pContainer = pContainerCreate(descriptor);
                        if (pContainer != NULL) {
                            // Container allocated, fill in the socket values
                            pContainer->socket.type = type;
                            pContainer->socket.protocol = protocol;
                            descriptorOrErrorCode = descriptor;
                        } else {
                            cellularPortLog("CELLULAR_SOCK: unable to allocate memory for socket.\n");
                        }
                    }
                    CELLULAR_SOCK_INC_DESCRIPTOR(descriptor);
                }

                CELLULAR_PORT_MUTEX_UNLOCK(gMutex);

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
                        // All is good
                        descriptorOrErrorCode = CELLULAR_SOCK_SUCCESS;
                        cellularPortLog("CELLULAR_SOCK: socket created, descriptor %d, modem handle %d.\n",
                                        descriptorOrErrorCode,
                                        pContainer->socket.modemHandle);
                    } else {
                        // If the modem could not create the socket,
                        // free the container once more
                        CELLULAR_PORT_MUTEX_LOCK(gMutex);
                        containerFree(descriptorOrErrorCode);
                        CELLULAR_PORT_MUTEX_UNLOCK(gMutex);
                        descriptorOrErrorCode = CELLULAR_SOCK_BSD_ERROR;
                        // Use a distinctly different errno for this
                        errno = CELLULAR_SOCK_EIO;
                        cellularPortLog("CELLULAR_SOCK: modem could not create socket.\n");
                    }
                } else {
                    // No buffers available
                    errno = CELLULAR_SOCK_ENOBUFS;
                }
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
    char buffer[64]; // Big enough for an IPV6 address with port number

    if (init()) {
        // Check that the remote IP address is sensible
        if ((pRemoteAddress != NULL) &&
            (addressToString(pRemoteAddress, false, buffer, sizeof(buffer)) > 0)) {
            // Find the container
            CELLULAR_PORT_MUTEX_LOCK(gMutex);
            pContainer = pContainerFind(descriptor);
            CELLULAR_PORT_MUTEX_UNLOCK(gMutex);

            // If we have found the container, talk to cellular to
            // make the connection
            if (pContainer != NULL) {
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
                    errorCode = CELLULAR_SOCK_SUCCESS;
                    cellularPortLog("CELLULAR_SOCK: socket with descriptor %d, modem handle %d, is connected to address %.*s.\n",
                                    descriptor,
                                    pContainer->socket.modemHandle,
                                    addressToString(pRemoteAddress, true,
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
                // Indicate that we weren't passed a valid socket descriptor
                errno = CELLULAR_SOCK_EBADF;
            }
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
        // Find the container
        CELLULAR_PORT_MUTEX_LOCK(gMutex);
        pContainer = pContainerFind(descriptor);
        CELLULAR_PORT_MUTEX_UNLOCK(gMutex);

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
                // Now free the container
                CELLULAR_PORT_MUTEX_LOCK(gMutex);
                if (!containerFree(descriptor)) {
                    cellularPortLog("CELLULAR_SOCK: warning, socket is closed but couldn't free the memory.\n");
                }
                CELLULAR_PORT_MUTEX_UNLOCK(gMutex);
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
    CellularSockErrorCode_t errorCode = CELLULAR_SOCK_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Receive a datagram from the given host.
int32_t cellularSockReceiveFrom(CellularSockDescriptor_t descriptor,
                                CellularSockAddress_t *pRemoteAddress,
                                void *pData, size_t dataSizeBytes)
{
    CellularSockErrorCode_t errorCode = CELLULAR_SOCK_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: STREAM (TCP)
 * -------------------------------------------------------------- */

// Send data.
int32_t cellularSockWrite(CellularSockDescriptor_t descriptor,
                          const CellularSockAddress_t *pRemoteAddress,
                          const void *pData, size_t dataSizeBytes)
{
    CellularSockErrorCode_t errorCode = CELLULAR_SOCK_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Receive data.
int32_t cellularSockRead(CellularSockDescriptor_t descriptor,
                         void *pData, size_t dataSizeBytes)
{
    CellularSockErrorCode_t errorCode = CELLULAR_SOCK_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
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
    CellularSockErrorCode_t errorCode = CELLULAR_SOCK_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Get the local address of the given socket.
int32_t cellularSockGetLocalAddress(CellularSockDescriptor_t descriptor,
                                    CellularSockAddress_t *pLocalAddress)
{
    CellularSockErrorCode_t errorCode = CELLULAR_SOCK_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Get the IP address of the given host name.
int32_t cellularSockGetHostByName(const char *pHostName,
                                  CellularSockIpAddress_t *pHostIpAddress)
{
    CellularSockErrorCode_t errorCode = CELLULAR_SOCK_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

/* ----------------------------------------------------------------
 * PUBIC FUNCTIONS: ADDRESS CONVERSION
 * -------------------------------------------------------------- */

// Convert an IP address string into a struct.
int32_t cellularSockStringToAddress(const char *pAddressString,
                                    CellularSockAddress_t *pAddress)
{
    CellularSockErrorCode_t errorCode = CELLULAR_SOCK_INVALID_PARAMETER;

    // No need to call init() here, this is not a sockets function
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

    // No need to call init() here, this is not a sockets function
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

    // No need to call init() here, this is not a sockets function
    if ((pAddress != NULL) && (pBuffer != NULL)) {
        stringLengthOrErrorCode = addressToString(pAddress,
                                                  true,
                                                  pBuffer,
                                                  sizeBytes);
    }

    return (int32_t) stringLengthOrErrorCode;
}

// End of file
