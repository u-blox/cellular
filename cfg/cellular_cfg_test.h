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

#ifndef _CELLULAR_CFG_TEST_H_
#define _CELLULAR_CFG_TEST_H_

/* No #includes allowed here */

/* This header file contains configuration information to be used
 * when testing cellular.
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

#ifndef CELLULAR_CFG_TEST_RAT
/** The RAT to use during testing.
 */
# define CELLULAR_CFG_TEST_RAT        CELLULAR_CTRL_RAT_CATM1
#endif

#ifndef CELLULAR_CFG_TEST_BANDMASK
/** The bandmask to use during testing. 0x080092 is bands
 * 2, 5, 8 and 20.
 */
# define CELLULAR_CFG_TEST_BANDMASK   0x080092
#endif

#ifndef CELLULAR_CFG_TEST_APN
/** The APN to use when testing connectivity, NULL for unspecified,
 * "" for empty.
 */
# define CELLULAR_CFG_TEST_APN        "internet"
#endif

#ifndef CELLULAR_CFG_TEST_USERNAME
/** The username to use when testing connectivity, NULL for unspecified,
 * "" for empty.
 */
# define CELLULAR_CFG_TEST_USERNAME   NULL
#endif

#ifndef CELLULAR_CFG_TEST_PASSWORD
/** The password to use when testing connectivity, NULL for unspecified,
 * "" for empty.
 */
# define CELLULAR_CFG_TEST_PASSWORD   NULL
#endif

#ifndef CELLULAR_CFG_TEST_CONNECT_TIMEOUT_SECONDS
// The time in seconds allowed for a connection to complete.
#define CELLULAR_CFG_TEST_CONNECT_TIMEOUT_SECONDS 240
#endif

#ifndef CELLULAR_CFG_TEST_ECHO_UDP_SERVER_DOMAIN_NAME
/** Echo server to use for UDP sockets testing as a domain name.
 */
# define CELLULAR_CFG_TEST_ECHO_UDP_SERVER_DOMAIN_NAME  "echo.u-blox.com"
#endif

#ifndef CELLULAR_CFG_TEST_ECHO_UDP_SERVER_IP_ADDRESS
/** Echo server to use for UDP sockets testing as an IP address.
 */
# define CELLULAR_CFG_TEST_ECHO_UDP_SERVER_IP_ADDRESS  "195.34.89.241"
#endif

#ifndef CELLULAR_CFG_TEST_ECHO_UDP_SERVER_PORT
/** Port number on the echo server to use for UDP testing.
 */
# define CELLULAR_CFG_TEST_ECHO_UDP_SERVER_PORT  7
#endif

#ifndef CELLULAR_CFG_TEST_ECHO_TCP_SERVER_DOMAIN_NAME
/** Echo server to use for TCP sockets testing as a domain name.
 * (note: the u-blox one adds a prefix to the echoed TCP packets
 * which is undesirable; mbed don't seem to mind us using theirs).
 */
# define CELLULAR_CFG_TEST_ECHO_TCP_SERVER_DOMAIN_NAME  "echo.mbedcloudtesting.com"
#endif

#ifndef CELLULAR_CFG_TEST_ECHO_TCP_SERVER_IP_ADDRESS
/** Echo server to use for TCP sockets testing as an IP address.
 * (note: the u-blox one adds a prefix to the echoed TCP packets
 * which is undesirable; mbed don't seem to mind us using theirs).
 */
# define CELLULAR_CFG_TEST_ECHO_TCP_SERVER_IP_ADDRESS  "52.215.34.155"
#endif

#ifndef CELLULAR_CFG_TEST_ECHO_TCP_SERVER_PORT
/** Port number on the echo server to use for TCP testing.
 */
# define CELLULAR_CFG_TEST_ECHO_TCP_SERVER_PORT  7
#endif

#ifndef CELLULAR_CFG_TEST_LOCAL_PORT
/** Local port number, used when testing binding.
 */
# define CELLULAR_CFG_TEST_LOCAL_PORT 65543
#endif

#ifndef CELLULAR_CFG_TEST_UDP_RETRIES
/** The number of retries to allow when sendingrunning
 * data over UDP.
 */
# define CELLULAR_CFG_TEST_UDP_RETRIES 5
#endif

#endif // _CELLULAR_CFG_TEST_H_

// End of file
