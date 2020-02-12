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

#ifndef _CELLULAR_PORT_H_
#define _CELLULAR_PORT_H_

/* No #includes allowed here */

/** Common stuff for porting layer.
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/** Error codes.
 */
typedef enum {
    CELLULAR_PORT_SUCCESS = 0,
    CELLULAR_PORT_UNKNOWN_ERROR = -1,
    CELLULAR_PORT_NOT_INITIALISED = -2,
    CELLULAR_PORT_NOT_IMPLEMENTED = -3,
    CELLULAR_PORT_INVALID_PARAMETER = -4,
    CELLULAR_PORT_OUT_OF_MEMORY = -5,
    CELLULAR_PORT_TIMEOUT = -6,
    CELLULAR_PORT_PLATFORM_ERROR = -7
} CellularPortErrorCode_t;

/* ----------------------------------------------------------------
 * FUNCTIONS
 * -------------------------------------------------------------- */

/** Initialise the porting layer.
 *
 * @return zero on success else negative error code.
 */
int32_t cellularPortInit();

/** Deinitialise the porting layer.
 */
void cellularPortDeinit();

#endif // _CELLULAR_PORT_H_

// End of file
