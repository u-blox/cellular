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

#ifndef _CELLULAR_PORT_CLIB_PLATFORM_SPECIFIC_H_
#define _CELLULAR_PORT_CLIB_PLATFORM_SPECIFIC_H_

/** Implementations of C library functions not available on this
 * platform.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * FUNCTIONS
 * -------------------------------------------------------------- */

/** strtok().
 *
 * @param pStr        the string to search.
 * @param pDelimiters the set of delimiters to look for.
 * @param ppSave      place for this function to store context.
 * @return            the next occurrence of pDelimiter in pStr.
 */
char *strtok_r(char *pStr, const char *pDelimiters, char **ppSave);

#ifdef __cplusplus
}
#endif

#endif // _CELLULAR_PORT_CLIB_PLATFORM_SPECIFIC_H_

// End of file
