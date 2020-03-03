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

/** This file maps the cellular sock interface to LWIP. It relies
 * on the LWIP include files being available.
 */

/* #includes of cellular_* and LWIP headers (e.g. lwip/sockets.h,
 * lwip/inet.h) are allowed here but no C lib, platform stuff
 * or OS stuff.  Anything required from the platform/C library/OS
 * must be brought in through cellular_port* to maintain portability.
 */

#include "cellular_port_clib.h"
#include "cellular_sock_errno.h"
#include "cellular_sock.h"
#include "cellular_sock_lwip_itf.h"

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS: MISC
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS: COPIED FROM LWIP (sockets.c)
 * -------------------------------------------------------------- */

#if LWIP_IPV4

#define IP4ADDR_PORT_TO_SOCKADDR(sin, ipaddr, port) do { \
      (sin)->sin_len = sizeof(struct sockaddr_in); \
      (sin)->sin_family = AF_INET; \
      (sin)->sin_port = lwip_htons((port)); \
      inet_addr_from_ip4addr(&(sin)->sin_addr, ipaddr); \
      pCellularPort_memset((sin)->sin_zero, 0, SIN_ZERO_LEN); }while(0)

#define SOCKADDR4_TO_IP4ADDR_PORT(sin, ipaddr, port) do { \
    inet_addr_to_ip4addr(ip_2_ip4(ipaddr), &((sin)->sin_addr)); \
    (port) = lwip_ntohs((sin)->sin_port); }while(0)

#endif /* LWIP_IPV4 */

#if LWIP_IPV6

#define IP6ADDR_PORT_TO_SOCKADDR(sin6, ipaddr, port) do { \
      (sin6)->sin6_len = sizeof(struct sockaddr_in6); \
      (sin6)->sin6_family = AF_INET6; \
      (sin6)->sin6_port = lwip_htons((port)); \
      (sin6)->sin6_flowinfo = 0; \
      inet6_addr_from_ip6addr(&(sin6)->sin6_addr, ipaddr); \
      (sin6)->sin6_scope_id = 0; }while(0)
#define SOCKADDR6_TO_IP6ADDR_PORT(sin6, ipaddr, port) do { \
    inet6_addr_to_ip6addr(ip_2_ip6(ipaddr), &((sin6)->sin6_addr)); \
    (port) = lwip_ntohs((sin6)->sin6_port); }while(0)

#endif /* LWIP_IPV6 */

#if LWIP_IPV4 && LWIP_IPV6
static void sockaddr_to_ipaddr_port(const struct sockaddr* sockaddr, ip_addr_t* ipaddr, u16_t* port);

# define IS_SOCK_ADDR_LEN_VALID(namelen)  (((namelen) == sizeof(struct sockaddr_in)) || \
                                           ((namelen) == sizeof(struct sockaddr_in6)))
# define SOCKADDR_TO_IPADDR_PORT(_sockaddr, ipaddr, port) sockaddr_to_ipaddr_port(_sockaddr, ipaddr, &(port))

#elif LWIP_IPV6 /* LWIP_IPV4 && LWIP_IPV6 */

# define IS_SOCK_ADDR_LEN_VALID(namelen)  ((namelen) == sizeof(struct sockaddr_in6))
# define SOCKADDR_TO_IPADDR_PORT(sockaddr, ipaddr, port) \
         SOCKADDR6_TO_IP6ADDR_PORT((const struct sockaddr_in6*)(const void*)(sockaddr), ipaddr, port)

#else /*-> LWIP_IPV4: LWIP_IPV4 && LWIP_IPV6 */

# define IS_SOCK_ADDR_LEN_VALID(namelen)  ((namelen) == sizeof(struct sockaddr_in))
# define SOCKADDR_TO_IPADDR_PORT(sockaddr, ipaddr, port) \
         SOCKADDR4_TO_IP4ADDR_PORT((const struct sockaddr_in*)(const void*)(sockaddr), ipaddr, port)

#endif /* LWIP_IPV6 */

#define IS_SOCK_ADDR_ALIGNED(name)      ((((mem_ptr_t)(name)) % 4) == 0)

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

#if LWIP_IPV4 && LWIP_IPV6
// This from LWIP sockets.c.
static void
sockaddr_to_ipaddr_port(const struct sockaddr* sockaddr, ip_addr_t* ipaddr, u16_t* port)
{
  if ((sockaddr->sa_family) == AF_INET6) {
    SOCKADDR6_TO_IP6ADDR_PORT((const struct sockaddr_in6*)(const void*)(sockaddr), ipaddr, *port);
    ipaddr->type = IPADDR_TYPE_V6;
  } else {
    SOCKADDR4_TO_IP4ADDR_PORT((const struct sockaddr_in*)(const void*)(sockaddr), ipaddr, *port);
    ipaddr->type = IPADDR_TYPE_V4;
  }
}
#endif /* LWIP_IPV4 && LWIP_IPV6 */

// Convert sockaddr into CellularSockAddress_t.
static int sockaddrToCellularSockAddress(const struct sockaddr *pName,
                                         socklen_t namelen,
                                         CellularSockAddress_t *pAddress)
{
    int errorCode = -1;
    ip_addr_t addressLwip;
    u16_t portLwip;

    if (pName != NULL) {
        if ((pName->sa_family == AF_INET) ||
            (pName->sa_family == AF_INET6)) {
            // This from lwip_connect()
            if (IS_SOCK_ADDR_LEN_VALID(namelen) &&
                IS_SOCK_ADDR_ALIGNED(pName)) {

                // This from lwip_connect()
                SOCKADDR_TO_IPADDR_PORT(pName, &addressLwip,
                                        portLwip);

                // This from lwip_connect()
#if LWIP_IPV4 && LWIP_IPV6
                // Dual-stack: Unmap IPv4 mapped IPv6 addresses
                if (IP_IS_V6_VAL(addressLwip) &&
                    ip6_addr_isipv4mappedipv6(ip_2_ip6(&addressLwip))) {
                    unmap_ipv4_mapped_ipv6(ip_2_ip4(&addressLwip),
                                           ip_2_ip6(&addressLwip));
                    IP_SET_TYPE_VAL(addressLwip, IPADDR_TYPE_V4);
                }
#endif

                // Now that we have an address in addressLwip,
                // copy it into pAddress
                if (IP_IS_V4_VAL(addressLwip)) {
                    pAddress->ipAddress.type = CELLULAR_SOCK_ADDRESS_TYPE_V4;
                    pCellularPort_memcpy(&(pAddress->ipAddress.address.ipv4),
                                         ip_2_ip4(&addressLwip),
                                         sizeof(pAddress->ipAddress.address.ipv4));
                    pAddress->port = portLwip;
                    errorCode = 0;
                } else if (IP_IS_V6_VAL(addressLwip)) {
                    pAddress->ipAddress.type = CELLULAR_SOCK_ADDRESS_TYPE_V6;
                    pCellularPort_memcpy(pAddress->ipAddress.address.ipv6,
                                         ip_2_ip6(&addressLwip),
                                         sizeof(pAddress->ipAddress.address.ipv6));
                    pAddress->port = portLwip;
                    errorCode = 0;
                } else {
                    cellularPort_errno_set(CELLULAR_SOCK_EPROTONOSUPPORT);
                }
            } else {
                cellularPort_errno_set(CELLULAR_SOCK_EINVAL);
            }
        } else {
            cellularPort_errno_set(CELLULAR_SOCK_EAFNOSUPPORT);
        }
    } else {
        cellularPort_errno_set(CELLULAR_SOCK_EINVAL);
    }

    return errorCode;
}

// Convert CellularSockAddress_t into sockaddr.
static int cellularSockAddressToSockaddr(const CellularSockAddress_t *pAddress,
                                         const struct sockaddr *pName,
                                         socklen_t *pNamelen)
{
    int errorCode = -1;
    ip_addr_t addressLwip;

    if ((pName != NULL) && (pNamelen != NULL) && (pAddress != NULL)) {
        addressLwip.type = pAddress->ipAddress.type;
        if (pAddress->ipAddress.type == CELLULAR_SOCK_ADDRESS_TYPE_V4) {
            if (*pNamelen >= sizeof(struct sockaddr_in)) {
                pCellularPort_memcpy(&(addressLwip.u_addr.ip4.addr),
                                     &(pAddress->ipAddress.address.ipv4),
                                     sizeof(addressLwip.u_addr.ip4.addr));
                IP4ADDR_PORT_TO_SOCKADDR((struct sockaddr_in *) (void *) pName,
                                         &(addressLwip.u_addr.ip4),
                                         pAddress->port);
                *pNamelen = sizeof(struct sockaddr_in);
                errorCode = 0;
            } else {
                cellularPort_errno_set(CELLULAR_SOCK_ENOBUFS);
            }
        } else if (pAddress->ipAddress.type == CELLULAR_SOCK_ADDRESS_TYPE_V6) {
            if (*pNamelen >= sizeof(struct sockaddr_in6)) {
                pCellularPort_memcpy(&(addressLwip.u_addr.ip6.addr),
                                     pAddress->ipAddress.address.ipv6,
                                     sizeof(addressLwip.u_addr.ip6.addr));
                IP6ADDR_PORT_TO_SOCKADDR((struct sockaddr_in6 *) (void *) pName,
                                         &(addressLwip.u_addr.ip6),
                                         pAddress->port);
                *pNamelen = sizeof(struct sockaddr_in6);
                errorCode = 0;
            } else {
                cellularPort_errno_set(CELLULAR_SOCK_ENOBUFS);
            }
        } else {
            cellularPort_errno_set(CELLULAR_SOCK_EPROTONOSUPPORT);
        }
    }

    return errorCode;
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS
 * -------------------------------------------------------------- */

// Create a socket.
int cellular_lwip_socket(int domain, int type, int protocol)
{
    int errorCode = -1;

    if ((domain == AF_INET) || (domain == AF_INET6)) {
        errorCode = cellularSockCreate(type, protocol);
    } else {
        cellularPort_errno_set(CELLULAR_SOCK_EAFNOSUPPORT);
    }

    return errorCode;
}

// Make an outgoing connection on the given socket.
int cellular_lwip_connect(int s, const struct sockaddr *name,
                          socklen_t namelen)
{
    int errorCode;
    CellularSockAddress_t remoteAddress;

    errorCode = sockaddrToCellularSockAddress(name, namelen,
                                              &remoteAddress);
    if (errorCode == 0) {
        errorCode = cellularSockConnect((CellularSockDescriptor_t) s,
                                        &remoteAddress);
    }

    return errorCode;
}

// Close a socket.
int cellular_lwip_close(int s)
{
    return cellularSockClose((CellularSockDescriptor_t) s);
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: CONFIGURE
 * -------------------------------------------------------------- */

// Configure the given socket's file parameters.
int cellular_lwip_fcntl(int s, int cmd, int val)
{
    return cellularSockFcntl((CellularSockDescriptor_t) s,
                             cmd, val);
}

// Configure the given socket's device parameters.
int cellular_lwip_ioctl(int s, long cmd, void *argp)
{
    return cellularSockIoctl((CellularSockDescriptor_t) s,
                             cmd, argp);
}

// Set the options for the given socket.
int cellular_lwip_setsockopt(int s, int level, int optname,
                             const void *optval,
                             socklen_t optlen)
{
    return cellularSockSetOption((CellularSockDescriptor_t) s,
                                 level, optname,
                                 optval, optlen);
}

// Get the options for the given socket.
int cellular_lwip_getsockopt(int s, int level, int optname,
                             void *optval, socklen_t *optlen)
{
    return cellularSockGetOption((CellularSockDescriptor_t) s,
                                 level, optname,
                                 optval, optlen);
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: UDP ONLY
 * -------------------------------------------------------------- */

// Send a datagram to the given host.
int cellular_lwip_sendto(int s, const void *dataptr,
                         size_t size, int flags,
                         const struct sockaddr *to,
                         socklen_t tolen)
{
    int errorCode;
    CellularSockAddress_t remoteAddress;

    // Flags are not supported, ignore them
    (void) flags;

    errorCode = sockaddrToCellularSockAddress(to, tolen,
                                              &remoteAddress);
    if (errorCode == 0) {
        errorCode = cellularSockSendTo((CellularSockDescriptor_t) s,
                                        &remoteAddress,
                                        dataptr, size);
    }

    return errorCode;
}

// Receive a datagram from the given host.
int cellular_lwip_recvfrom(int s, void *mem, size_t len,
                           int flags,
                           struct sockaddr *from,
                           socklen_t *fromlen)
{
    int errorCode;
    CellularSockAddress_t remoteAddress;

    // Flags are not supported, ignore them
    (void) flags;

    errorCode = cellularSockReceiveFrom((CellularSockDescriptor_t) s,
                                        &remoteAddress,
                                        mem, len);
    if (errorCode == 0) {
        errorCode = cellularSockAddressToSockaddr(&remoteAddress,
                                                  from, fromlen);
    }

    return errorCode;
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: STREAM (TCP)
 * -------------------------------------------------------------- */

// Send data.
int cellular_lwip_write(int s, const void *dataptr,
                        size_t size)
{
    return cellularSockWrite((CellularSockDescriptor_t) s,
                             dataptr, size);
}

// Send data, with flags (which we ignore).
int cellular_lwip_send(int s, const void *dataptr,
                       size_t size, int flags)
{
    // Flags are not supported, ignore them
    (void) flags;

    return cellularSockWrite((CellularSockDescriptor_t) s,
                             dataptr, size);
}

// Send data, multiple buffers at a time.
int cellular_lwip_writev(int s, const struct iovec *iov,
                         int iovcnt)
{
    int errorCodeOrSize = 0;
    int thisSize = 0;

    for (size_t x = 0; (x < iovcnt) &&
                       (thisSize >= 0); x++) {
        thisSize = cellularSockWrite((CellularSockDescriptor_t) s,
                                     (iov + x)->iov_base,
                                     (iov + x)->iov_len);
        if (thisSize >= 0) {
            errorCodeOrSize += thisSize;
        }
    }

    if (thisSize < 0) {
        errorCodeOrSize = thisSize;
    }

    return errorCodeOrSize;
}

// Send data, multiple buffers at a time and including
// additional control information (which we discard).
int cellular_lwip_sendmsg(int s,
                          const struct msghdr *message,
                          int flags)
{
    return cellular_lwip_writev(s, message->msg_iov,
                                message->msg_iovlen);
}

// Receive data.
int cellular_lwip_read(int s, void *mem, size_t len)
{
    return cellularSockRead((CellularSockDescriptor_t) s,
                             mem, len);
}

// Receive data, with flags (which we ignore).
int cellular_lwip_recv(int s, void *mem, size_t len,
                       int flags)
{
    // Flags are not supported, ignore them
    (void) flags;

    return cellularSockRead((CellularSockDescriptor_t) s,
                             mem, len);
}


// Prepare a TCP socket for being closed.
int cellular_lwip_shutdown(int s, int how)
{
    return cellularSockShutdown((CellularSockDescriptor_t) s,
                                how);
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: TCP INCOMING (TCP SERVER) ONLY
 * -------------------------------------------------------------- */

// Bind a socket to a local address.
int cellular_lwip_bind(int s, const struct sockaddr *name,
                       socklen_t namelen)
{
    int errorCode;
    CellularSockAddress_t localAddress;

    errorCode = sockaddrToCellularSockAddress(name, namelen,
                                              &localAddress);
    if (errorCode == 0) {
        errorCode = cellularSockBind((CellularSockDescriptor_t) s,
                                     &localAddress);
    }

    return errorCode;
}

// Set the given socket into listening mode.
int cellular_lwip_listen(int s, int backlog)
{
    return cellularSockListen((CellularSockDescriptor_t) s,
                              backlog);
}

// Accept an incoming TCP connection on the given socket.
int cellular_lwip_accept(int s, struct sockaddr *addr,
                         socklen_t *addrlen)
{
    int errorCode;
    CellularSockAddress_t remoteAddress;

    errorCode = cellularSockAccept((CellularSockDescriptor_t) s,
                                   &remoteAddress);
    if (errorCode == 0) {
        errorCode = cellularSockAddressToSockaddr(&remoteAddress,
                                                  addr, addrlen);
    }

    return errorCode;
}

// Select: wait for one of a set of sockets to become unblocked.
int cellular_lwip_select(int maxfdp1, fd_set *readset,
                         fd_set *writeset,
                         fd_set *exceptset,
                         struct timeval *timeout)
{
    int32_t timeMs = (timeout->tv_sec * 1000) + (timeout->tv_usec / 1000);

    return cellularSockSelect(maxfdp1,
                              (CellularSockDescriptorSet_t *) readset,
                              (CellularSockDescriptorSet_t *) writeset,
                              (CellularSockDescriptorSet_t *) exceptset,
                              timeMs);
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: FINDING ADDRESSES
 * -------------------------------------------------------------- */

// Get the address of the remote host connected to a given socket.
int cellular_lwip_getpeername(int s, struct sockaddr *name,
                              socklen_t *namelen)
{
    int errorCode;
    CellularSockAddress_t remoteAddress;

    errorCode = cellularSockGetRemoteAddress((CellularSockDescriptor_t) s,
                                              &remoteAddress);
    if (errorCode == 0) {
        errorCode = cellularSockAddressToSockaddr(&remoteAddress,
                                                  name, namelen);
    }

    return errorCode;
}

// Get the local address of the given socket.
int cellular_lwip_getsockname(int s, struct sockaddr *name,
                              socklen_t *namelen)
{
    int errorCode;
    CellularSockAddress_t localAddress;

    errorCode = cellularSockGetLocalAddress((CellularSockDescriptor_t) s,
                                            &localAddress);
    if (errorCode == 0) {
        errorCode = cellularSockAddressToSockaddr(&localAddress,
                                                  name, namelen);
    }

    return errorCode;
}

// End of file
