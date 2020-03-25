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
 
#include "cellular_port_clib.h"
#include "cellular_port_debug.h"

#include "nrfx.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// The size of logging buffer.
#define CELLULAR_PORT_LOG_BUFFER_SIZE 128

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// The logging buffer.
char gLogBuffer[CELLULAR_PORT_LOG_BUFFER_SIZE];

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS
 * -------------------------------------------------------------- */

// printf()-style logging.
// Note: this cellularPort layer does not initialise logging,
// the application is expected to do that
void cellularPortLogF(const char *pFormat, ...)
{
    va_list args;

    va_start(args, pFormat);
    vsnprintf(gLogBuffer, sizeof(gLogBuffer), pFormat, args);

    NRF_LOG_RAW_INFO("%s", gLogBuffer);
    NRF_LOG_FLUSH();

    va_end(args);
}

// End of file
