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

#ifndef _CELLULAR_SOCK_ERRNO_H_
#define _CELLULAR_SOCK_ERRNO_H_

/* No #includes allowed here */

/* This header file defines the ERRNO values used by the cellular
 * sockets API.  The values match those of LWIP.
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS: ERRNO
 * -------------------------------------------------------------- */

#define  CELLULAR_SOCK_ENONE            0      //<! No error.
#define  CELLULAR_SOCK_EPERM            1      //<! Operation not permitted.
#define  CELLULAR_SOCK_ENOENT           2      //<! No such file or directory.
#define  CELLULAR_SOCK_ESRCH            3      //<! No such process.
#define  CELLULAR_SOCK_EINTR            4      //<! Interrupted system call.
#define  CELLULAR_SOCK_EIO              5      //<! I/O error.
#define  CELLULAR_SOCK_ENXIO            6      //<! No such device or address.
#define  CELLULAR_SOCK_E2BIG            7      //<! Arg list too long.
#define  CELLULAR_SOCK_ENOEXEC          8      //<! Exec format error.
#define  CELLULAR_SOCK_EBADF            9      //<! Bad file number.
#define  CELLULAR_SOCK_ECHILD          10      //<! No child processes.
#define  CELLULAR_SOCK_EAGAIN          11      //<! Try again.
#define  CELLULAR_SOCK_ENOMEM          12      //<! Out of memory.
#define  CELLULAR_SOCK_EACCES          13      //<! Permission denied.
#define  CELLULAR_SOCK_EFAULT          14      //<! Bad address.
#define  CELLULAR_SOCK_ENOTBLK         15      //<! Block device required.
#define  CELLULAR_SOCK_EBUSY           16      //<! Device or resource busy.
#define  CELLULAR_SOCK_EEXIST          17      //<! File exists.
#define  CELLULAR_SOCK_EXDEV           18      //<! Cross-device link.
#define  CELLULAR_SOCK_ENODEV          19      //<! No such device.
#define  CELLULAR_SOCK_ENOTDIR         20      //<! Not a directory.
#define  CELLULAR_SOCK_EISDIR          21      //<! Is a directory.
#define  CELLULAR_SOCK_EINVAL          22      //<! Invalid argument.
#define  CELLULAR_SOCK_ENFILE          23      //<! File table overflow.
#define  CELLULAR_SOCK_EMFILE          24      //<! Too many open files.
#define  CELLULAR_SOCK_ENOTTY          25      //<! Not a typewriter.
#define  CELLULAR_SOCK_ETXTBSY         26      //<! Text file busy.
#define  CELLULAR_SOCK_EFBIG           27      //<! File too large.
#define  CELLULAR_SOCK_ENOSPC          28      //<! No space left on device.
#define  CELLULAR_SOCK_ESPIPE          29      //<! Illegal seek.
#define  CELLULAR_SOCK_EROFS           30      //<! Read-only file system.
#define  CELLULAR_SOCK_EMLINK          31      //<! Too many links.
#define  CELLULAR_SOCK_EPIPE           32      //<! Broken pipe.
#define  CELLULAR_SOCK_EDOM            33      //<! Math argument out of domain of func.
#define  CELLULAR_SOCK_ERANGE          34      //<! Math result not representable.
#define  CELLULAR_SOCK_EDEADLK         35      //<! Resource deadlock would occur.
#define  CELLULAR_SOCK_ENAMETOOLONG    36      //<! File name too long.
#define  CELLULAR_SOCK_ENOLCK          37      //<! No record locks available.
#define  CELLULAR_SOCK_ENOSYS          38      //<! Function not implemented.
#define  CELLULAR_SOCK_ENOTEMPTY       39      //<! Directory not empty.
#define  CELLULAR_SOCK_ELOOP           40      //<! Too many symbolic links encountered.
#define  CELLULAR_SOCK_EWOULDBLOCK     EAGAIN  //<! Operation would block.
#define  CELLULAR_SOCK_ENOMSG          42      //<! No message of desired type.
#define  CELLULAR_SOCK_EIDRM           43      //<! Identifier removed.
#define  CELLULAR_SOCK_ECHRNG          44      //<! Channel number out of range.
#define  CELLULAR_SOCK_EL2NSYNC        45      //<! Level 2 not synchronized.
#define  CELLULAR_SOCK_EL3HLT          46      //<! Level 3 halted.
#define  CELLULAR_SOCK_EL3RST          47      //<! Level 3 reset.
#define  CELLULAR_SOCK_ELNRNG          48      //<! Link number out of range.
#define  CELLULAR_SOCK_EUNATCH         49      //<! Protocol driver not attached.
#define  CELLULAR_SOCK_ENOCSI          50      //<! No CSI structure available.
#define  CELLULAR_SOCK_EL2HLT          51      //<! Level 2 halted.
#define  CELLULAR_SOCK_EBADE           52      //<! Invalid exchange.
#define  CELLULAR_SOCK_EBADR           53      //<! Invalid request descriptor.
#define  CELLULAR_SOCK_EXFULL          54      //<! Exchange full.
#define  CELLULAR_SOCK_ENOANO          55      //<! No anode.
#define  CELLULAR_SOCK_EBADRQC         56      //<! Invalid request code.
#define  CELLULAR_SOCK_EBADSLT         57      //<! Invalid slot.

#define  CELLULAR_SOCK_EDEADLOCK       EDEADLK

#define  CELLULAR_SOCK_EBFONT          59      //<! Bad font file format.
#define  CELLULAR_SOCK_ENOSTR          60      //<! Device not a stream.
#define  CELLULAR_SOCK_ENODATA         61      //<! No data available.
#define  CELLULAR_SOCK_ETIME           62      //<! Timer expired.
#define  CELLULAR_SOCK_ENOSR           63      //<! Out of streams resources.
#define  CELLULAR_SOCK_ENONET          64      //<! Machine is not on the network.
#define  CELLULAR_SOCK_ENOPKG          65      //<! Package not installed.
#define  CELLULAR_SOCK_EREMOTE         66      //<! Object is remote.
#define  CELLULAR_SOCK_ENOLINK         67      //<! Link has been severed.
#define  CELLULAR_SOCK_EADV            68      //<! Advertise error.
#define  CELLULAR_SOCK_ESRMNT          69      //<! Srmount error.
#define  CELLULAR_SOCK_ECOMM           70      //<! Communication error on send.
#define  CELLULAR_SOCK_EPROTO          71      //<! Protocol error.
#define  CELLULAR_SOCK_EMULTIHOP       72      //<! Multihop attempted.
#define  CELLULAR_SOCK_EDOTDOT         73      //<! RFS specific error.
#define  CELLULAR_SOCK_EBADMSG         74      //<! Not a data message.
#define  CELLULAR_SOCK_EOVERFLOW       75      //<! Value too large for defined data type.
#define  CELLULAR_SOCK_ENOTUNIQ        76      //<! Name not unique on network.
#define  CELLULAR_SOCK_EBADFD          77      //<! File descriptor in bad state.
#define  CELLULAR_SOCK_EREMCHG         78      //<! Remote address changed.
#define  CELLULAR_SOCK_ELIBACC         79      //<! Can not access a needed shared library.
#define  CELLULAR_SOCK_ELIBBAD         80      //<! Accessing a corrupted shared library.
#define  CELLULAR_SOCK_ELIBSCN         81      //<! .lib section in a.out corrupted.
#define  CELLULAR_SOCK_ELIBMAX         82      //<! Attempting to link in too many shared libraries.
#define  CELLULAR_SOCK_ELIBEXEC        83      //<! Cannot exec a shared library directly.
#define  CELLULAR_SOCK_EILSEQ          84      //<! Illegal byte sequence.
#define  CELLULAR_SOCK_ERESTART        85      //<! Interrupted system call should be restarted.
#define  CELLULAR_SOCK_ESTRPIPE        86      //<! Streams pipe error.
#define  CELLULAR_SOCK_EUSERS          87      //<! Too many users.
#define  CELLULAR_SOCK_ENOTSOCK        88      //<! Socket operation on non-socket.
#define  CELLULAR_SOCK_EDESTADDRREQ    89      //<! Destination address required.
#define  CELLULAR_SOCK_EMSGSIZE        90      //<! Message too long.
#define  CELLULAR_SOCK_EPROTOTYPE      91      //<! Protocol wrong type for socket.
#define  CELLULAR_SOCK_ENOPROTOOPT     92      //<! Protocol not available.
#define  CELLULAR_SOCK_EPROTONOSUPPORT 93      //<! Protocol not supported.
#define  CELLULAR_SOCK_ESOCKTNOSUPPORT 94      //<! Socket type not supported.
#define  CELLULAR_SOCK_EOPNOTSUPP      95      //<! Operation not supported on transport endpoint.
#define  CELLULAR_SOCK_EPFNOSUPPORT    96      //<! Protocol family not supported.
#define  CELLULAR_SOCK_EAFNOSUPPORT    97      //<! Address family not supported by protocol.
#define  CELLULAR_SOCK_EADDRINUSE      98      //<! Address already in use.
#define  CELLULAR_SOCK_EADDRNOTAVAIL   99      //<! Cannot assign requested address.
#define  CELLULAR_SOCK_ENETDOWN       100      //<! Network is down.
#define  CELLULAR_SOCK_ENETUNREACH    101      //<! Network is unreachable.
#define  CELLULAR_SOCK_ENETRESET      102      //<! Network dropped connection because of reset.
#define  CELLULAR_SOCK_ECONNABORTED   103      //<! Software caused connection abort.
#define  CELLULAR_SOCK_ECONNRESET     104      //<! Connection reset by peer.
#define  CELLULAR_SOCK_ENOBUFS        105      //<! No buffer space available.
#define  CELLULAR_SOCK_EISCONN        106      //<! Transport endpoint is already connected.
#define  CELLULAR_SOCK_ENOTCONN       107      //<! Transport endpoint is not connected.
#define  CELLULAR_SOCK_ESHUTDOWN      108      //<! Cannot send after transport endpoint shutdown.
#define  CELLULAR_SOCK_ETOOMANYREFS   109      //<! Too many references: cannot splice.
#define  CELLULAR_SOCK_ETIMEDOUT      110      //<! Connection timed out.
#define  CELLULAR_SOCK_ECONNREFUSED   111      //<! Connection refused.
#define  CELLULAR_SOCK_EHOSTDOWN      112      //<! Host is down.
#define  CELLULAR_SOCK_EHOSTUNREACH   113      //<! No route to host.
#define  CELLULAR_SOCK_EALREADY       114      //<! Operation already in progress.
#define  CELLULAR_SOCK_EINPROGRESS    115      //<! Operation now in progress.
#define  CELLULAR_SOCK_ESTALE         116      //<! Stale NFS file handle.
#define  CELLULAR_SOCK_EUCLEAN        117      //<! Structure needs cleaning.
#define  CELLULAR_SOCK_ENOTNAM        118      //<! Not a XENIX named type file.
#define  CELLULAR_SOCK_ENAVAIL        119      //<! No XENIX semaphores available.
#define  CELLULAR_SOCK_EISNAM         120      //<! Is a named type file.
#define  CELLULAR_SOCK_EREMOTEIO      121      //<! Remote I/O error.
#define  CELLULAR_SOCK_EDQUOT         122      //<! Quota exceeded.

#define  CELLULAR_SOCK_ENOMEDIUM      123      //<! No medium found.
#define  CELLULAR_SOCK_EMEDIUMTYPE    124      //<! Wrong medium type.

#endif // _CELLULAR_SOCK_ERRNO_H_

// End of file
