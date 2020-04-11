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

#ifndef _CELLULAR_SOCK_H_
#define _CELLULAR_SOCK_H_

/* No #includes allowed here */

/* This header file defines the cellular sockets API.  These
 * functions are thread-safe.
 */

/* IMPORTANT: this API is still in the process of definition, anything
 * may change, beware!  We aim to have it settled by the middle of 2020,
 * COVID 19 permitting.
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS: SOCKET OPTIONS FOR SOCKET LEVEL (-1)
 * -------------------------------------------------------------- */

/** The level for socket options. The value matches LWIP.
 */
#define CELLULAR_SOCK_OPT_LEVEL_SOCK   0x0fff

/** Socket option: turn on debugging info recording.
 * The value matches LWIP.
 */
#define CELLULAR_SOCK_OPT_SO_DEBUG     0x0001

/** Socket option: socket has had listen().
 * The value matches LWIP.
 */
#define CELLULAR_SOCK_OPT_ACCEPTCONN   0x0002

/** Socket option: allow local address re-use.
 * The value matches LWIP.
 */
#define CELLULAR_SOCK_OPT_REUSEADDR    0x0004

/** Socket option: keep connections alive.
 * The value matches LWIP.
 */
#define CELLULAR_SOCK_OPT_KEEPALIVE    0x0008

/** Socket option: just use interface addresses.
 * The value matches LWIP.
 */
#define CELLULAR_SOCK_OPT_DONTROUTE    0x0010

/** Socket option: permit sending of broadcast messages.
 * The value matches LWIP.
 */
#define CELLULAR_SOCK_OPT_BROADCAST    0x0020

/** Socket option: linger on close if data present.
 * The value matches LWIP.
 */
#define CELLULAR_SOCK_OPT_LINGER       0x0080

/** Socket option: leave received OOB data in line.
 * The value matches LWIP.
 */
#define CELLULAR_SOCK_OPT_OOBINLINE    0x0100

/** Socket option: allow local address and port
 * re-use.  The value matches LWIP.
 */
#define CELLULAR_SOCK_OPT_REUSEPORT    0x0200

/** Socket option: send buffer size.
 * The value matches LWIP.
 */
#define CELLULAR_SOCK_OPT_SNDBUF       0x1001

/** Socket option: receive buffer size.
 * The value matches LWIP.
 */
#define CELLULAR_SOCK_OPT_RCVBUF       0x1002

/** Socket option: send low-water mark.
 * The value matches LWIP.
 */
#define CELLULAR_SOCK_OPT_SNDLOWAT     0x1003

/** Socket option: receive low-water mark.
 * The value matches LWIP.
 */
#define CELLULAR_SOCK_OPT_RCVLOWAT     0x1004

/** Socket option: send timeout.
 * The value matches LWIP.
 */
#define CELLULAR_SOCK_OPT_SNDTIMEO     0x1005

/** Socket option: receive timeout.
 * The value matches LWIP.
 */
#define CELLULAR_SOCK_OPT_RCVTIMEO     0x1006

/** Socket option: get and then clear error status.
 * The value matches LWIP.
 */
#define CELLULAR_SOCK_OPT_ERROR        0x1007

/** Socket option: get socekt type.
 * The value matches LWIP.
 */
#define CELLULAR_SOCK_OPT_TYPE         0x1008

/** Socket option: connect timeout.
 * The value matches LWIP.
 */
#define CELLULAR_SOCK_OPT_CONTIMEO     0x1009

/** Socket option: don't create UDP checksum.
 * The value matches LWIP.
 */
#define CELLULAR_SOCK_OPT_NO_CHECK     0x100a

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS: SOCKET OPTIONS FOR IP LEVEL (0)
 * -------------------------------------------------------------- */

/** The level for IP options. The value matches LWIP.
 */
#define CELLULAR_SOCK_OPT_LEVEL_IP     0

/** IP socket option: type of service.
 * The value matches LWIP.
 */
#define CELLULAR_SOCK_OPT_IP_TOS       0x0001

/** IP socket option: time to live.
 * The value matches LWIP.
 */
#define CELLULAR_SOCK_OPT_IP_TTL       0x0002

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS: SOCKET OPTIONS FOR TCP LEVEL (6)
 * -------------------------------------------------------------- */

/** The level for TCP options. The value matches LWIP.
 */
#define CELLULAR_SOCK_OPT_LEVEL_TCP     6

/** TCP socket option: turn off Nagle's algorithm.
 * The value matches LWIP.
 */
#define CELLULAR_SOCK_OPT_TCP_NODELAY  0x0001

/** TCP socket option: turn off Nagle's algorithm.
 * The value matches LWIP.
 */
#define CELLULAR_SOCK_OPT_TCP_KEEPIDLE 0x0002

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS: FCTL COMMANDS
 * -------------------------------------------------------------- */

/** Set file status command. The value matches LWIP.
 */
#define CELLULAR_SOCK_FCNTL_SET_STATUS 4

/** Get file status command. The value matches LWIP.
 */
#define CELLULAR_SOCK_FCNTL_GET_STATUS 3

/** The non-block bit in the file status integer. The value matches LWIP.
 */
#define CELLULAR_SOCK_FCNTL_STATUS_NONBLOCK 0x00000001

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS: IOCTL COMMANDS
 * -------------------------------------------------------------- */

/** The non-block command. The value matches LWIP.
 */
#define CELLULAR_SOCK_IOCTL_SET_NONBLOCK 0x8004667E

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS: MISC
 * -------------------------------------------------------------- */

/** The size that should be allowed for an address string, 
 * which could be IPV6 and could include a port number.
 */
#define CELLULAR_SOCK_ADDRESS_STRING_MAX_LENGTH_BYTES 64

/** The maximum amount of data that we can cram in
 * over the cellular interface in one go.  For UDP this
 * is the maximum packet size, though of course the public
 * internet is generally limited to 500 bytes as fragmentation
 * (bad for UDP) will occur above that.  For TCP this is
 * the maximum that one can write at any one time and
 * bears no explicit relation to packet size since
 * TCP is a stream.
 */
#define CELLULAR_SOCK_MAX_SEGMENT_LENGTH_BYTES 1024

/** The maximum number of simultaneously open sockets
 * for the cellular module.
 */
#define CELLULAR_SOCK_MODULE_MAX_NUM_SOCKETS 7

/** The number of statically allocated sockets.  When
 * more than this number of sockets are required to
 * be open simultaneously they will be malloc()ed and
 * it is up to the user to call cellularSockCleanUp()
 * to release the memory occupied by closed malloc()ed
 * sockets when done.
 */
#define CELLULAR_SOCK_NUM_STATIC_SOCKETS CELLULAR_SOCK_MODULE_MAX_NUM_SOCKETS

/** The maximum number of sockets that can be in existence at once.
 * Note: this software doesn't care how big this number is,
 * subject to heap memory availability, but the cellular module
 * does
 */
#define CELLULAR_SOCK_MAX CELLULAR_SOCK_MODULE_MAX_NUM_SOCKETS

/** The maximum number of sockets that can be cellularSockSelect()ed
 * from.  Note that increasing this may increase stack usage
 * as applications normally declare their descriptor sets as
 * automatic variables.  A size of 256 will be 256 / 8 = 32 bytes.
 */
#define CELLULAR_SOCK_DESCRIPTOR_SETSIZE CELLULAR_SOCK_MAX

/** The default socket timeout in milliseconds.
 */
#define CELLULAR_SOCK_RECEIVE_TIMEOUT_DEFAULT_MS 10000

/** Zero a file descriptor set.
 */
#define CELLULAR_SOCK_FD_ZERO(pSet) pCellularPort_memset(*(pSet), 0,     \
                                                         sizeof(*(pSet)))

/** Set the bit corresponding to a given file descriptor in a set.
 */
#define CELLULAR_SOCK_FD_SET(d, pSet) if (((d) >= 0) &&                               \
                                          ((d) < CELLULAR_SOCK_DESCRIPTOR_SETSIZE)) { \
                                          (*(pSet))[(d) / 8] |= 1 << ((d) & 7);       \
                                      }

/** Clear the bit corresponding to a given file descriptor in a set.
 */
#define CELLULAR_SOCK_FD_CLR(d, pSet) if (((d) >= 0) &&                                \
                                          ((d) < CELLULAR_SOCK_DESCRIPTOR_SETSIZE)) {  \
                                          (*(pSet))[(d) / 8] &= ~(1 << ((d) & 7));     \
                                      }

/** Determine if the bit corresponding to a given file descriptor is set.
 */
#define CELLULAR_SOCK_FD_ISSET(d, pSet) if (((d) >= 0) &&                               \
                                            ((d) < CELLULAR_SOCK_DESCRIPTOR_SETSIZE)) { \
                                            (*(pSet))[(d) / 8] & (1 << ((d) & 7));      \
                                        }

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/** Socket descriptor.
 */
typedef int32_t CellularSockDescriptor_t;

/** A socket descriptor set, for use with cellularSockSelect().
 */
typedef uint8_t CellularSockDescriptorSet_t[(CELLULAR_SOCK_DESCRIPTOR_SETSIZE + 7) / 8];

/** Supported socket types: the numbers match those of LWIP.
 */
typedef enum {
    CELLULAR_SOCK_TYPE_NONE   = 0,
    CELLULAR_SOCK_TYPE_STREAM = 1, //<! TCP.
    CELLULAR_SOCK_TYPE_DGRAM  = 2  //<! UDP.
} CellularSockType_t;

/** Supported protocols: the numbers match those of LWIP.
 */
typedef enum {
    CELLULAR_SOCK_PROTOCOL_TCP     = 6,
    CELLULAR_SOCK_PROTOCOL_UDP     = 17
} CellularSockProtocol_t;

/** IP address type: the numbers match those of LWIP.
 */
typedef enum {
    CELLULAR_SOCK_ADDRESS_TYPE_V4    = 0,
    CELLULAR_SOCK_ADDRESS_TYPE_V6    = 6,
    CELLULAR_SOCK_ADDRESS_TYPE_V4_V6 = 46
} CellularSockIpAddressType_t;

/** IP address (doesn't include port number).
 */
typedef struct {
    CellularSockIpAddressType_t type; //<! do NOT use
                                      // CELLULAR_SOCK_ADDRESS_TYPE_V4_V6
                                      // here!
    union {
        uint32_t ipv4;
        uint32_t ipv6[4];
    } address;
} CellularSockIpAddress_t;

/** Address (includes port number).
 */
typedef struct {
    CellularSockIpAddress_t ipAddress;
    uint16_t port;
} CellularSockAddress_t;

/** Socket shut-down types: the numbers match those of LWIP.
 */
typedef enum {
    CELLULAR_SOCK_SHUTDOWN_READ = 0,
    CELLULAR_SOCK_SHUTDOWN_WRITE = 1,
    CELLULAR_SOCK_SHUTDOWN_READ_WRITE = 2
} CellularSockShutdown_t;

/** Struct to define the CELLULAR_SOCK_OPT_LINGER socket option.
 * This struct matches that of LWIP.
 */
typedef struct {
       int32_t l_onoff;   //<! option on/off.
       int32_t l_linger;  //<! linger time in seconds.
} CellularSockLinger_t;

/** Error codes.
 */
typedef enum {
    CELLULAR_SOCK_SUCCESS = 0,
    CELLULAR_SOCK_UNKNOWN_ERROR = -1,
    CELLULAR_SOCK_BSD_ERROR = -1, //<! BSD always returns -1
    CELLULAR_SOCK_NOT_INITIALISED = -2,
    CELLULAR_SOCK_NOT_IMPLEMENTED = -3,
    CELLULAR_SOCK_NOT_RESPONDING = -4,
    CELLULAR_SOCK_INVALID_PARAMETER = -5,
    CELLULAR_SOCK_NO_MEMORY = -6,
    CELLULAR_SOCK_WOULD_BLOCK = -7, //<! Value matches LWIP.
    CELLULAR_SOCK_PLATFORM_ERROR = -8,
    CELLULAR_SOCK_INVALID_ADDRESS = -9,
    CELLULAR_SOCK_FORCE_32_BIT = 0x7FFFFFFF // Force this enum to be 32 bit
                                            // as it can be used as a size
                                            // also
} CellularSockErrorCode_t;

/* ----------------------------------------------------------------
 * FUNCTIONS: CREATE/OPEN/CLOSE/CLEAN-UP
 * -------------------------------------------------------------- */

/** Create a socket.
 *
 * @param type     the type of socket to create.
 * @param protocol the protocol that will run over the given socket.
 * @return         the descriptor of the socket else negative error
 *                 code.
 */
int32_t cellularSockCreate(CellularSockType_t type,
                           CellularSockProtocol_t protocol);

/** Make an outgoing connection on the given socket.
 *
 * @param descriptor     the descriptor of the socket.
 * @param pRemoteAddress the address of the remote host to connect
 *                       to.
 * @return               zero on success else negative error code.
 */
int32_t cellularSockConnect(CellularSockDescriptor_t descriptor,
                            const CellularSockAddress_t *pRemoteAddress);

/** Close a socket.  Note that a TCP socket should be shutdown
 * with a call to cellularSockShutdown() before it is closed.
 *
 * @param descriptor the descriptor of the socket to be closed.
 * @return           zero on success else negative error code.
 */
int32_t cellularSockClose(CellularSockDescriptor_t descriptor);

/** In order to maintain thread-safe operation, when a socket is
 * closed, either locally or by the remote host, it is only marked
 * as closed and the memory is retained, since some other thread
 * may be refering to it.  If you ever have more than
 * CELLULAR_SOCK_NUM_STATIC_SOCKETS open at the same time then
 * you must call this clean-up function when you are sure that
 * there is no socket activity, either locally or from the remote
 * host, in order to free memory malloc()ed for those additional
 * sockets.  A socket that is closed locally but waiting for the
 * far end to close WILL be clean-up by this function and so no
 * callback registered by cellularSockRegisterCallbackClosed()
 * will be triggered when the remote server finally closes the
 * connection.
 */
void cellularSockCleanUp();

/** Sockets has no initialisation function, it initialises
 * itself as required on any call.  If the application requires
 * sockets to be shut down in an organised way, with all sockets
 * closed locally, it can be done by calling this function.
 * It is different from cellularSockCleanUp() in that
 * all sockets, whatever their state, are closed locally.
 */
void cellularSockDeinit();

/* ----------------------------------------------------------------
 * FUNCTIONS: CONFIGURE
 * -------------------------------------------------------------- */

/** Configure the given socket's file parameters.
 *
 * @param descriptor the descriptor of the socket.
 * @param command    the command to be sent.  Only setting/getting
 *                   the O_NONBLOCK bit is supported.
 * @param value      an argument relevant to command, e.g.
 *                   the file descriptor flags.
 * @return           on success a value that is dependent
 *                   upon command (e.g. the file descriptor flags)
 *                   else -1 on error.
 */
int32_t cellularSockFcntl(CellularSockDescriptor_t descriptor,
                          int32_t command,
                          int32_t value);

/** Configure the given socket's device parameters.
 *
 * @param descriptor the descriptor of the socket.
 * @param command    the command to be sent.  Only setting
 *                   FIONBIO (non-blocking) is supported.
 * @param pValue     a pointer argument relevant to command,
 *                   e.g. a pointer to the Boolean value of
 *                   FIONBIO (1 for non-blocking, 0 for blocking).
 * @return           on success a value that is dependent
 *                   upon command (in the case of FIONBIO 0)
 *                   else -1 on error.
 */
int32_t cellularSockIoctl(CellularSockDescriptor_t descriptor,
                          int32_t command,
                          void *pValue);

/** Set the options for the given socket.  This function obeys
 * the BSD socket conventions and hence, for instance, to set
 * the socket receive timeout one would pass in a level
 * of CELLULAR_SOCK_OPT_LEVEL_SOCK, and option value of 
 * CELLULAR_SOCK_OPT_RCVTIMEO and then the option value would be
 * a pointer to a structure of type CellularPort_timeval.
 *
 * @param descriptor        the descriptor of the socket.
 * @param level             the option level
 *                          (see CELLULAR_SOCK_OPT_LEVEL*).
 * @param option            the option (see CELLULAR_SOCK_OPT*).
 * @param pOptionValue      a pointer to the option value to set.
 * @param optionValueLength the length of the data at pOptionValue.
 * @return                  zero on success else negative error code.
 */
int32_t cellularSockSetOption(CellularSockDescriptor_t descriptor,
                              int32_t level,
                              uint32_t option,
                              const void *pOptionValue,
                              size_t optionValueLength);

/** Get the options for the given socket.
 *
 * @param descriptor         the descriptor of the socket.
 * @param level              the option level
 *                           (see CELLULAR_SOCK_OPT_LEVEL*).
 * @param option             the option (see CELLULAR_SOCK_OPT*).
 * @param pOptionValue       a pointer to a place to put the option value.
 *                           May be NULL in which case pOptionValueLength
 *                           still returns the length that would have
 *                           been written.
 * @param pOptionValueLength when called, the length of the space pointed
 *                           to by pOptionValue, on return the length
 *                           of data in bytes that would be written to
 *                           pOptionValue if it were not NULL.
 * @return                   zero on success else negative error code.
 */
int32_t cellularSockGetOption(CellularSockDescriptor_t descriptor,
                              int32_t level,
                              uint32_t option,
                              void *pOptionValue,
                              size_t *pOptionValueLength);

/* ----------------------------------------------------------------
 * FUNCTIONS: UDP ONLY
 * -------------------------------------------------------------- */

/** Send a datagram to the given host.
 *
 * @param descriptor     the descriptor of the socket.
 * @param pRemoteAddress the address of the remote host to send to;
 *                       may be NULL in which case the address
 *                       from the cellularSockConnect() call
 *                       is used (and if no cellularSockConnect()
 *                       has been called on he socket this will
 *                       fail).
 * @param pData          the data to send, may be NULL, in which
 *                       case this function does nothing.
 * @param dataSizeBytes  the number of bytes of data to send;
 *                       must be zero if pData is NULL.
 * @return               on success the number of bytes sent else
 *                       negative error code.
 */
int32_t cellularSockSendTo(CellularSockDescriptor_t descriptor,
                           const CellularSockAddress_t *pRemoteAddress,
                           const void *pData, size_t dataSizeBytes);

/** Receive a datagram from the given host.
 *
 * @param descriptor     the descriptor of the socket.
 * @param pRemoteAddress a place to put the address of the remote
 *                       host from which the datagram was received;
 *                       may be NULL.
 * @param pData          a buffer in which to store the arriving
 *                       datagram.
 * @param dataSizeBytes  the number of bytes of storage available
 *                       at pData.
 * @return               on success the number of bytes received
 *                       else negative error code.
 */
int32_t cellularSockReceiveFrom(CellularSockDescriptor_t descriptor,
                                CellularSockAddress_t *pRemoteAddress,
                                void *pData, size_t dataSizeBytes);

/* ----------------------------------------------------------------
 * FUNCTIONS: STREAM (TCP)
 * -------------------------------------------------------------- */

/** Send data.
 *
 * @param descriptor     the descriptor of the socket.
 * @param pData          the data to send.
 * @param dataSizeBytes  the number of bytes of data to send.
 * @return               on success the number of bytes sent else
 *                       negative error code.
 */
int32_t cellularSockWrite(CellularSockDescriptor_t descriptor,
                          const void *pData, size_t dataSizeBytes);

/** Receive data.
 *
 * @param descriptor     the descriptor of the socket.
 * @param pData          a buffer in which to store the arriving
 *                       data.
 * @param dataSizeBytes  the number of bytes of storage available
 *                       at pData.
 * @return               on success the number of bytes received
 *                       else negative error code.
 */
int32_t cellularSockRead(CellularSockDescriptor_t descriptor,
                         void *pData, size_t dataSizeBytes);

/** Prepare a TCP socket for being closed.
 * This is provided for BSD socket compatibility however
 * it is not required for the u-blox AT sockets interface; all it
 * does here is prevent further reads/writes to the socket.
 *
 * @param descriptor the descriptor of the socket to be prepared.
 * @param how        what type of shutdown to perform.
 * @return           zero on success else negative error code.
 */
int32_t cellularSockShutdown(CellularSockDescriptor_t descriptor,
                             CellularSockShutdown_t how);

/* ----------------------------------------------------------------
 * FUNCTIONS: ASYNC
 * -------------------------------------------------------------- */

/** Register a callback which will be called when incoming
 * data has arrived on a socket.
 * IMPORTANT: don't spend long in your callback, i.e. don't
 * call back into this API, don't call things that will cause any
 * sort of processing load or might get stuck.  It must return
 * quickly as data reception and communication with the cellular
 * module in general is blocked while the callback is executing.
 * A short printf() or sending an OS signal is fine.
 * ALSO IMPORTANT: if you use the callback to send an OS
 * signal to a task that then calls one of this module's data
 * reception functions, make sure that task is running at a
 * priority lower than CELLULAR_CTRL_CALLBACK_PRIORITY otherwise
 * it will lock-out this module.
 *
 * @param descriptor     the descriptor of the socket.
 * @param pCallback      the function to call when data arrives,
 *                       use NULL to cancel a previously
 *                       registered callback.
 * @param pCallbackParam parameter to be passed to the
 *                       pCallback function when it is called;
 *                       may be NULL.
 * @return               zero on success else negative error code.
 */
int32_t cellularSockRegisterCallbackData(CellularSockDescriptor_t descriptor,
                                         void (*pCallback) (void *),
                                         void *pCallbackParam);

/** Register a callback which will be called when a socket is 
 * closed by the remote host.
 * IMPORTANT: don't spend long in your callback, i.e. don't
 * call back into this API, don't call things that will cause any
 * sort of processing load or might get stuck.  It must return
 * quickly as data reception and communication with the cellular
 * module in general is blocked while the callback is executing.
 * A short printf() or sending an OS signal is fine.
 *
 * @param descriptor     the descriptor of the socket.
 * @param pCallback      the function to call, use NULL
 *                       to cancel a previously registered
 *                       callback.
 * @param pCallbackParam parameter to be passed to the
 *                       pCallback function when it is
 *                       called; may be NULL.
 * @return               zero on success else negative error
 *                       code.
 */
int32_t cellularSockRegisterCallbackClosed(CellularSockDescriptor_t descriptor,
                                           void (*pCallback) (void *),
                                           void *pCallbackParam);

/* ----------------------------------------------------------------
 * FUNCTIONS: TCP INCOMING (TCP SERVER) ONLY
 * -------------------------------------------------------------- */

/** Prepare a socket for receiving incoming TCP connections
 * by binding it to an address.
 *
 * @param descriptor     the descriptor of the socket to be prepared.
 * @param pLocalAddress  the local IP address to bind to.
 * @return               zero on success else negative error code.
 */
int32_t cellularSockBind(CellularSockDescriptor_t descriptor,
                         const CellularSockAddress_t *pLocalAddress);

/** Set the given socket into listening mode for an incoming
 * TCP connection. The socket must have been bound to an address
 * first.
 *
 * @param descriptor the descriptor of the socket to listen on.
 * @param backlog    the number of pending connections that can
 *                   be queued.
 * @return           zero on success else negative error code.
 */
int32_t cellularSockListen(CellularSockDescriptor_t descriptor,
                           size_t backlog);

/** Accept an incoming TCP connection on the given socket.
 *
 * @param descriptor      the descriptor of the socket with the queued
 *                        incoming connection.
 * @param pRemoteAddress  a pointer to a place to put the address
 *                        of the thing from which the connection has
 *                        been accepted.
 * @return                the descriptor of the new socket connection
 *                        that must be used for TCP communication
 *                        with the thing from now on else negative
 *                        error code.
 */
int32_t cellularSockAccept(CellularSockDescriptor_t descriptor,
                           CellularSockAddress_t *pRemoteAddress);

/** Select: wait for one of a set of sockets to become unblocked.
 *
 * @param maxDescriptor         the highest numbered descriptor in the
 *                              sets that follow to select on + 1.
 * @param pReadDescriptorSet    the set of descriptors to check for
 *                              unblocking for a read operation. May
 *                              be NULL.
 * @param pWriteDescriptorSet   the set of descriptors to check for
 *                              unblocking for a write operation. May
 *                              be NULL.
 * @param pExceptDescriptorSet  the set of descriptors to check for
 *                              exceptional conditions. May be NULL.
 * @param timeMs                the timeout for the select operation
 *                              in milliseconds.
 * @return                      a positive value if an unblocking
 *                              occurred, zero on timeout, negative
 *                              on any other error.  Use
 *                              CELLULAR_SOCK_FD_ISSET() to determine
 *                              which descriptor(s) were unblocked.
 */
int32_t cellularSockSelect(int32_t maxDescriptor,
                           CellularSockDescriptorSet_t *pReadDescriptorSet,
                           CellularSockDescriptorSet_t *pWriteDescriptoreSet,
                           CellularSockDescriptorSet_t *pExceptDescriptorSet,
                           int32_t timeMs);

/* ----------------------------------------------------------------
 * FUNCTIONS: FINDING ADDRESSES
 * -------------------------------------------------------------- */

/** Get the address of the remote host connected to a given socket.
 *
 * @param descriptor     the descriptor of the socket.
 * @param pRemoteAddress a pointer to a place to put the address
 *                       of the remote end of the socket.
 * @return               zero on success else negative error code.
 */
int32_t cellularSockGetRemoteAddress(CellularSockDescriptor_t descriptor,
                                     CellularSockAddress_t *pRemoteAddress);

/** Get the local address of the given socket.
 *
 * @param descriptor    the descriptor of the socket.
 * @param pLocalAddress a pointer to a place to put the address
 *                      of the local end of the socket.
 * @return              zero on success else negative error code.
 */
int32_t cellularSockGetLocalAddress(CellularSockDescriptor_t descriptor,
                                    CellularSockAddress_t *pLocalAddress);

/** Get the IP address of the given host name.  If the host name
 * is already an IP address then the IP address is returned 
 * straight away without any external action, hence this also
 * implements "get host by address".
 *
 * @param pHostName      a string representing the host to search
 *                       for, e.g. "google.com" or "192.168.1.0".
 * @param pHostIpAddress a pointer to a place to put the IP address
 *                       of the host.  Set this to NULL to determine
 *                       if a host is there without bothering to return
 *                       the address.
 * @return               zero on success else negative error code.
 */
int32_t cellularSockGetHostByName(const char *pHostName,
                                  CellularSockIpAddress_t *pHostIpAddress);


/* ----------------------------------------------------------------
 * FUNCTIONS: ADDRESS CONVERSION
 * -------------------------------------------------------------- */

/** Convert an IP address string into a struct.
 *
 * @param pAddressString the string to convert.  Both IPV4 and
 *                       IPV6 addresses are supported and a port
 *                       number may be included.  Note that the
 *                       IPV6 optimisation of removing a single
 *                       consecutive set of zero hextets in the
 *                       string by using "::" is NOT supported.
 *                       The string does not have to be NULL
 *                       terminated, it may contain random crap
 *                       after the address; provided the first
 *                       N bytes form a valid address then the
 *                       conversion will succeed.
 * @param pAddress       a pointer to a place to put the address.
 * @return               zero on success else negative error code.
 */
int32_t cellularSockStringToAddress(const char *pAddressString,
                                    CellularSockAddress_t *pAddress);

/** Convert an IP address struct (i.e. without a port number)
 * into a string.
 *
 * @param pIpAddress a pointer to the IP address to convert.
 * @param pBuffer    a buffer in which to place the string.
 *                   Allow 64 bytes for a full IPV6 address
 *                   and NULL terminator.
 * @param sizeBytes  the amount of memory pointed to by
 *                   pBuffer.
 * @return           on success the length of the string, not
 *                   including the NULL terminator (i.e. what
 *                   strlen() would return) else negative
 *                   error code.
 */
int32_t cellularSockIpAddressToString(const CellularSockIpAddress_t *pIpAddress,
                                      char *pBuffer,
                                      size_t sizeBytes);

/** Convert an address struct (i.e. with a port number) into a
 * string.
 *
 * @param pAddress   a pointer to the address to convert.
 * @param pBuffer    a buffer in which to place the string.
 *                   Allow 64 bytes for a full IPV6 address
 *                   with port number and NULL terminator.
 * @param sizeBytes  the amount of memory pointed to by
 *                   pBuffer.
 * @return           on success the length of the string, not
 *                   including the NULL terminator (i.e. what
 *                   strlen() would return) else negative
 *                   error code.
 */
int32_t cellularSockAddressToString(const CellularSockAddress_t *pAddress,
                                    char *pBuffer,
                                    size_t sizeBytes);

#endif // _CELLULAR_SOCK_H_

// End of file
