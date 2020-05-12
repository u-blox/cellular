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

/* No #includes allowed here */

/** Porting layer for debug functions.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/** Define this to enable debug prints.  How they leave the building
 * depends upon the port.
 */
#if defined(CELLULAR_CFG_ENABLE_LOGGING) && CELLULAR_CFG_ENABLE_LOGGING
# define cellularPortLog(format, ...) cellularPortLogF(format, ##__VA_ARGS__)
#else
# define cellularPortLog(...)
#endif

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * FUNCTIONS
 * -------------------------------------------------------------- */

/** printf()-style logging.
 *
 * @param pFormat a printf() style format string.
 * @param ...     variable argument list.
 */
void cellularPortLogF(const char *pFormat, ...);

#ifdef __cplusplus
}
#endif

#endif // _CELLULAR_PORT_DEBUG_H_

// End of file
