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

#ifndef _CELLULAR_PORT_DEBUG_H_
#define _CELLULAR_PORT_DEBUG_H_

/** Porting layer for debug functions.
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/** Define this to enable printing out of the serial port.
 */
#if defined(CONFIG_CELLULAR_ENABLE_LOGGING) && CONFIG_CELLULAR_ENABLE_LOGGING
# define cellularPortLog(format, ...) printf(format, ##__VA_ARGS__)
#else
# define cellularPortLog(...)
#endif

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * FUNCTIONS
 * -------------------------------------------------------------- */

#endif // _CELLULAR_PORT_DEBUG_H_

// End of file
