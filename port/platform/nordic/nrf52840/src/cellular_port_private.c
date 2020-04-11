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
#include "cellular_port_private.h"

#include "nrfx.h"
#include "nrfx_timer.h"

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// The tick timer.
static nrfx_timer_t gTickTimer = NRFX_TIMER_INSTANCE(CELLULAR_PORT_TICK_TIMER_INSTANCE);

// Overflow counter that allows us to keep 64 bit time.
static int64_t gTickTimerOverflowCount;

// Flag to indicate whether the timer is running in
// "UART" mode or normal mode.  When it is running in
// UART mode it has to overflow quickly so that the
// callback can be used as an RX timeout.
static bool gTickTimerUartMode;

// A callback to be called when the UART overflows.
static void (*gpCb) (void *);

// The user parameter for the callback.
static void *gpCbParameter;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

// The tick handler.
static void timerTickHandler(nrf_timer_event_t eventType, void *pContext)
{
    (void) pContext;

    if (eventType == NRF_TIMER_EVENT_COMPARE0) {
        gTickTimerOverflowCount++;
        if (gpCb != NULL) {
            gpCb(gpCbParameter);
        }
        nrfx_timer_clear(&gTickTimer);
    }
}

// Start the tick timer.
static int32_t tickTimerStart(nrfx_timer_config_t *pTimerCfg,
                              int32_t limit)
{
    CellularPortErrorCode_t errorCode = CELLULAR_PORT_PLATFORM_ERROR;

    if (nrfx_timer_init(&gTickTimer,
                        pTimerCfg,
                        timerTickHandler) == NRFX_SUCCESS) {
        // Set the compare interrupt on CC zero comparing
        // with limit, and enable the interrupt
        nrfx_timer_compare(&gTickTimer, 0, limit, true);

        // Clear the timer
        nrfx_timer_clear(&gTickTimer);

        // Now enable the timer
        nrfx_timer_enable(&gTickTimer);

        errorCode = CELLULAR_PORT_SUCCESS;
    }

    return errorCode;
}

// Stop the tick timer.
static void tickTimerStop()
{
    // Now disable it and clear it
    nrfx_timer_disable(&gTickTimer);
    nrfx_timer_compare_int_disable(&gTickTimer, 0);
    nrfx_timer_uninit(&gTickTimer);
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS SPECIFIC TO THIS PORT
 * -------------------------------------------------------------- */

// Initalise the private stuff.
int32_t cellularPortPrivateInit()
{
    nrfx_timer_config_t timerCfg = NRFX_TIMER_DEFAULT_CONFIG;

    gTickTimerOverflowCount = 0;
    gTickTimerUartMode = false;
    gpCb = NULL;
    gpCbParameter = NULL;
    timerCfg.frequency = CELLULAR_PORT_TICK_TIMER_FREQUENCY_HZ;
    timerCfg.bit_width = CELLULAR_PORT_TICK_TIMER_BIT_WIDTH;

    return tickTimerStart(&timerCfg,
                          CELLULAR_PORT_TICK_TIMER_LIMIT_NORMAL_MODE);
}

// Deinitialise the private stuff.
void cellularPortPrivateDeinit()
{
    tickTimerStop();
}

// Register a callback to be called when the tick timer
// overflow interrupt occurs.
void cellularPortPrivateTickTimeSetInterruptCb(void (*pCb) (void *),
                                               void *pCbParameter)
{
    gpCb = pCb;
    gpCbParameter = pCbParameter;
}

// Switch the tick timer to UART mode.
void cellularPortPrivateTickTimeUartMode()
{
    if (!gTickTimerUartMode) {
        // Pause the timer
        nrfx_timer_pause(&gTickTimer);
        // Set the compare interrupt on CC zero comparing
        // with limit and enable the interrupt
        nrfx_timer_compare(&gTickTimer, 0,
                          CELLULAR_PORT_TICK_TIMER_LIMIT_UART_MODE,
                          true);
        // Re-calculate the overflow count for this bit-width
        gTickTimerOverflowCount <<= CELLULAR_PORT_TICK_TIMER_LIMIT_DIFF;
        gTickTimerUartMode = true;
        // Resume the timer
        nrfx_timer_resume(&gTickTimer);
    }
}

// Switch the tick timer back to normal mode.
void cellularPortPrivateTickTimeNormalMode()
{
    if (gTickTimerUartMode) {
        // Pause the timer
        nrfx_timer_pause(&gTickTimer);
        // Set the compare interrupt on CC zero comparing
        // with limit and enable the interrupt
        nrfx_timer_compare(&gTickTimer, 0,
                          CELLULAR_PORT_TICK_TIMER_LIMIT_NORMAL_MODE,
                          true);
        // Re-calculate the overflow count for this bit-width
        gTickTimerOverflowCount >>= CELLULAR_PORT_TICK_TIMER_LIMIT_DIFF;
        gTickTimerUartMode = false;
        // Resume the timer
        nrfx_timer_resume(&gTickTimer);
    }
}

// Get the current tick converted to a time in milliseconds.
int64_t cellularPortPrivateGetTickTimeMs()
{
    int64_t tickTimerValue = 0;

    // Read the timer on CC 1
    tickTimerValue = nrfx_timer_capture(&gTickTimer, 1);

    // Convert to milliseconds when running at 31.25 kHz, one tick
    // every 32 us, so shift left 5, then divide by 1000.
    tickTimerValue = (((uint64_t) tickTimerValue) << 5) / 1000;
    if (gTickTimerUartMode) {
        // The timer is 12 bits wide so each overflow represents
        // ((1 / 31250) * 4096) seconds, 131.072 milliseconds
        // or x * 131072 / 1000
        tickTimerValue += (((uint64_t) gTickTimerOverflowCount) << 18) / 1000;
    } else {
        // The timer is 24 bits wide so each overflow represents
        // ((1 / 31250) * (2 ^ 24)) seconds, about very 537 seconds.
        // Here just multiply 'cos ARM can do that in one clock cycle
        tickTimerValue += gTickTimerOverflowCount * 536871;
    }

    return tickTimerValue;
}

// End of file
