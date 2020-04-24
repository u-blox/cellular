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

#ifdef CELLULAR_CFG_OVERRIDE
# include "cellular_cfg_override.h" // For a customer's configuration override
#endif
#include "cellular_port_clib.h"
#include "cellular_port_debug.h"

#include "stdio.h"
#include "stdarg.h"

#include "stm32f437xx.h" // For ITM_SendChar()

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS
 * -------------------------------------------------------------- */

// This function will replace the weakly-linked _write() function in
// syscalls.c and will send output to the RTT trace port.
int _write(int file, char *pStr, int len)
{
    for (size_t x = 0 ; x < len ; x++) {
        ITM_SendChar(*pStr);
        pStr++;
    }

    return len;
}

// printf()-style logging.
void cellularPortLogF(const char *pFormat, ...)
{
    va_list args;

    va_start(args, pFormat);
    vprintf(pFormat, args);
    va_end(args);
}

// End of file
