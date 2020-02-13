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

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: CREATE/OPEN/CLOSE
 * -------------------------------------------------------------- */

// Create a socket.
int32_t cellularSockCreate(CellularSockType_t type,
                           CellularSockProtocol_t protocol)
{
    CellularSockErrorCode_t descriptorOrErrorCode = CELLULAR_SOCK_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) descriptorOrErrorCode;
}

// Make an outgoing connection on the given socket.
int32_t cellularSockConnect(CellularSockDescriptor_t descriptor,
                            const CellularSockAddress_t *pRemoteAddress)
{
    CellularSockErrorCode_t errorCode = CELLULAR_SOCK_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Close a socket.
int32_t cellularSockClose(CellularSockDescriptor_t descriptor)
{
    CellularSockErrorCode_t errorCode = CELLULAR_SOCK_NOT_IMPLEMENTED;

    // TODO

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
    int32_t errorCode = -1;

    // TODO

    return errorCode;
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

// End of file
