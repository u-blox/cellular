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

#include "nrfx.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

#if NRF_LOG_ENABLED
// The logging buffer.
char gLogBuffer[NRF_LOG_BUFSIZE];
#endif

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
#if NRF_LOG_ENABLED
    vsnprintf(gLogBuffer, sizeof(gLogBuffer), pFormat, args);
    NRF_LOG_RAW_INFO("%s", gLogBuffer);
    NRF_LOG_FLUSH();
#else
    vprintf(pFormat, args);
#endif

    va_end(args);
}

// End of file
