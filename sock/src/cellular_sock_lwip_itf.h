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

#ifndef _CELLULAR_SOCK_LWIP_ITF_H_
#define _CELLULAR_SOCK_LWIP_ITF_H_

/* Only LWIP #includes allowed in here. */

#include "lwip/sockets.h" // Needed for socklen_t, sockaddr,
                          // iovec, msghdr, fd_set and timeval

/* This header file defines the LWIP interface to the cellular
 * sockets API.  These functions are thread-safe.
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * FUNCTIONS
 * -------------------------------------------------------------- */

/** Create a socket.  This function conforms to the behaviour
 * defined by LWIP, please see their documentation for a description
 * of how it works.
 */
int cellular_lwip_socket(int domain, int type, int protocol);

/** Make an outgoing connection on the given socket.  This function
 * conforms to the behaviour defined by LWIP, please see their
 * documentation for a description of how it works.
 */
int cellular_lwip_connect(int s, const struct sockaddr *name,
                          socklen_t namelen);

/** Close a socket.  This function conforms to the behaviour
 * defined by LWIP, please see their documentation for a description
 * of how it works.
 */
int cellular_lwip_close(int s);

/** Configure the given socket's file parameters.  This function
 * conforms to the behaviour defined by LWIP except that only
 * setting/getting O_NONBLOCK is supported; please see the LWIP
 * documentation for a description of how it works.
 */
int cellular_lwip_fcntl(int s, int cmd, int val);

/** Configure the given socket's device parameters.  This
 * function conforms to the behaviour defined by LWIP except
 * that only the value FIONBAR is supported; please
 * see the LWIP documentation for a description of how it works.
 */
int cellular_lwip_ioctl(int s, long cmd, void *argp);

/** Set the options for the given socket.  This function
 * conforms to the behaviour defined by LWIP, please see
 * their documentation for a description of how it works.
 */
int cellular_lwip_setsockopt (int s, int level, int optname,
                              const void *optval,
                              socklen_t optlen);

/** Get the options for the given socket.  This function
 * conforms to the behaviour defined by LWIP, please see
 * their documentation for a description of how it works.
 */
int cellular_lwip_getsockopt (int s, int level, int optname,
                              void *optval, socklen_t *optlen);

/** Send a datagram to the given host.  This function
 * conforms to the behaviour defined by LWIP, please see
 * their documentation for a description of how it works.
 */
int cellular_lwip_sendto(int s, const void *dataptr,
                         size_t size, int flags,
                         const struct sockaddr *to,
                         socklen_t tolen);

/** Receive a datagram from the given host.  This function
 * conforms to the behaviour defined by LWIP, please see
 * their documentation for a description of how it works.
 */
int cellular_lwip_recvfrom(int s, void *mem, size_t len,
                           int flags,
                           struct sockaddr *from,
                           socklen_t *fromlen);

/** Send data.  This function conforms to the behaviour
 * defined by LWIP, please see their documentation for a
 * description of how it works.
 */
int cellular_lwip_write(int s, const void *dataptr,
                        size_t size);

/** Send data, with flags.  This function conforms to the
 * behaviour defined by LWIP except that the flag parameters
 * is ignored; please see the LWIP documentation for a
 * description of how it works.
 */
int cellular_lwip_send(int s, const void *dataptr,
                       size_t size, int flags);

/** Send data, multiple buffers at a time.  This function
 * conforms to the behaviour defined by LWIP, please see
 * their documentation for a description of how it works.
 */
int cellular_lwip_writev(int s, const struct iovec *iov,
                         int iovcnt);

/** Send data, with flags, multiple buffers at a time and
 * including additional control information.  This
 * function conforms to the behaviour defined by LWIP
 * except that the fields over and abvoe the iov* fields
 * in message and the flags parameter are ignored; please
 * see the LWIP documentation for a description of how it
 * works.
 */
int cellular_lwip_sendmsg(int s,
                          const struct msghdr *message,
                          int flags);

/** Receive data.  This function conforms to the behaviour
 * defined by LWIP, please see their documentation for a
 * description of how it works.
 */
int cellular_lwip_read(int s, void *mem, size_t len);

/** Receive data, with flags, multiple buffers at a time
 * and including additional control information.  This
 * function conforms to the behaviour defined by LWIP
 * except that the flags parameters is ignored; please
 * see the LWIP documentation for a description of
 * how it works.
 */
int cellular_lwip_recv(int s, void *mem, size_t len,
                       int flags);

/** Prepare a TCP socket for being closed.  This function
 * conforms to the behaviour defined by LWIP, please see
 * their documentation for a description of how it works.
 */
int cellular_lwip_shutdown(int s, int how);

/** Bind a socket to a local address.  This function
 * conforms to the behaviour defined by LWIP, please see
 * their documentation for a description of how it works.
 */
int cellular_lwip_bind(int s, const struct sockaddr *name,
                       socklen_t namelen);

/** Set the given socket into listening mode.  This function
 * conforms to the behaviour defined by LWIP, please see
 * their documentation for a description of how it works.
 */
int cellular_lwip_listen(int s, int backlog);

/** Accept an incoming TCP connection on the given socket.
 * This function conforms to the behaviour defined by LWIP,
 * please see their documentation for a description of how
 * it works.
 */
int cellular_lwip_accept(int s, struct sockaddr *addr,
                         socklen_t *addrlen);

/** Select: wait for one of a set of sockets to become
 * unblocked. This function conforms to the behaviour defined
 * by LWIP, please see their documentation for a description
 * of how it works.
 */
int cellular_lwip_select(int maxfdp1, fd_set *readset,
                         fd_set *writeset,
                         fd_set *exceptset,
                         struct timeval *timeout);

/** Get the address of the remote host connected to a given
 * socket. This function conforms to the behaviour defined
 * by LWIP, please see their documentation for a description
 * of how it works.
 */
int cellular_lwip_getpeername(int s, struct sockaddr *name,
                              socklen_t *namelen);

/** Get the local address of the given socket. This function
 * conforms to the behaviour defined by LWIP, please see
 * their documentation for a description of how it works.
 */
int cellular_lwip_getsockname(int s, struct sockaddr *name,
                              socklen_t *namelen);


#endif // _CELLULAR_SOCK_LWIP_ITF_H_

// End of file
