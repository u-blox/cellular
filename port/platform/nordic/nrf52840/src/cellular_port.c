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
#include "cellular_port.h"

#include "nrfx.h"
#include "nrfx_timer.h"

/* ----------------------------------------------------------------
 * EXTERNED THINGS
 * -------------------------------------------------------------- */

// Pull in any private functions/variables shared between the
// porting .c files here.

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

/** Keep track of whether we've been initialised or not.
 */
static bool gInitialised = false;

// The tick timer.
static nrfx_timer_t gTickTimer = NRFX_TIMER_INSTANCE(CELLULAR_PORT_TICK_TIMER_INSTANCE);

// Overflow counter that allows us to keep 64 bit time.
static int32_t gTickTimerOverflowCount = 0;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

// The tick handler.
static void timerTickHandler(nrf_timer_event_t eventType, void *pContext)
{
    (void) pContext;

    if (eventType == NRF_TIMER_EVENT_COMPARE0) {
        gTickTimerOverflowCount++;
    }
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS:
 * -------------------------------------------------------------- */

// Initialise the porting layer.
int32_t cellularPortInit()
{
    CellularPortErrorCode_t errorCode = CELLULAR_PORT_PLATFORM_ERROR;
    nrfx_timer_config_t timerCfg = NRFX_TIMER_DEFAULT_CONFIG;

    if (!gInitialised) {
        // IMPORTANT: if you change either of these then you also
        // need to change the calculation in cellularPortGetTickTimeMs()
        timerCfg.frequency = NRF_TIMER_FREQ_31250Hz;
        timerCfg.bit_width = NRF_TIMER_BIT_WIDTH_24;

        // There is no 64-bit timer on the NRF52840,
        // hence start a timer with interrupt
        // here so that we can make our own
        if (nrfx_timer_init(&gTickTimer,
                            &timerCfg,
                            timerTickHandler) == NRFX_SUCCESS) {
            // Set the compare interrupt on CC zero comparing
            // with 0, and enable the interrupt
            nrfx_timer_compare(&gTickTimer, 0, 0, true);

            // Now enable the timer
            nrfx_timer_enable(&gTickTimer);

            errorCode = CELLULAR_PORT_SUCCESS;
            gInitialised = true;
        }
    } else {
        errorCode = CELLULAR_PORT_SUCCESS;
    }

    return errorCode;
}

// Deinitialise the porting layer.
void cellularPortDeinit()
{
    if (gInitialised) {
        nrfx_timer_disable(&gTickTimer);
        nrfx_timer_compare_int_disable(&gTickTimer, 0);
        nrfx_timer_uninit(&gTickTimer);
        gInitialised = false;
    }
}

// Get the current tick converted to a time in milliseconds.
int64_t cellularPortGetTickTimeMs()
{
    int64_t tickTimerValue = 0;

    if (gInitialised) {
        // Read the timer on CC 1
        tickTimerValue = nrfx_timer_capture(&gTickTimer, 1);

        // Convert to milliseconds when running at 31.25 kHz (one tick
        // every 32 us)
        tickTimerValue = (((uint64_t) tickTimerValue) << 5) / 1000;

        // The timer is running at 31.25 kHz and is 24 bits wide so
        // each overflow represents ((1 / 31250) * (2 ^24)) seconds,
        // so about very 537 seconds.
        tickTimerValue += gTickTimerOverflowCount * 536871;
    }

    return tickTimerValue;
}

// End of file
