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
/** The bandmask to use during testing, favourite being
 * band 5 as it's not used by a network on-air.
 */
# define CELLULAR_CFG_TEST_BANDMASK   0x10
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

#endif // _CELLULAR_CFG_TEST_H_

// End of file
