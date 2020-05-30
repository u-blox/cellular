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

#include "FreeRTOS.h"
#include "task.h"

#include "nrf_drv_clock.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"


/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// Keep track of whether we've been initialised or not.
static bool gInitialised = false;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS
 * -------------------------------------------------------------- */

// Start the platform.
int32_t cellularPortPlatformStart(void (*pEntryPoint)(void *),
                                  void *pParameter,
                                  size_t stackSizeBytes,
                                  int32_t priority)
{
    CellularPortErrorCode_t errorCode = CELLULAR_PORT_INVALID_PARAMETER;
    TaskHandle_t taskHandle;

    if (pEntryPoint != NULL) {
        errorCode = CELLULAR_PORT_PLATFORM_ERROR;

        NRF_LOG_INIT(NULL);
        NRF_LOG_DEFAULT_BACKENDS_INIT();

#if configTICK_SOURCE == FREERTOS_USE_RTC
        // If the clock has not already been started, start it
        nrf_drv_clock_init();
#endif
        // Need to have the high frequency clock
        // running for the UART driver, otherwise
        // it can drop characters at 115,200 baud.
        // If you do NOT use the UART driver you don't
        // need this line: it is put here rather than
        // down in the UART driver as it should be the
        // application's responsibility to configure
        // clocks, not some random driver code that
        // has no context.
        nrfx_clock_hfclk_start();

        // Note that stack size is in words on the native FreeRTOS
        // that NRF52840 uses, hence the divide by four here.
        if (xTaskCreate(pEntryPoint, "EntryPoint",
                        stackSizeBytes / 4, pParameter,
                        priority, &taskHandle) == pdPASS) {

            // Activate deep sleep mode.
            SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;

            // Start the scheduler.
            vTaskStartScheduler();

            // Should never get here
        }
    }

    return errorCode;
}

// Initialise the porting layer.
int32_t cellularPortInit()
{
    CellularPortErrorCode_t errorCode = CELLULAR_PORT_SUCCESS;

    if (!gInitialised) {
        errorCode = cellularPortPrivateInit();
        gInitialised = (errorCode == 0);
    }

    return errorCode;
}

// Deinitialise the porting layer.
void cellularPortDeinit()
{
    if (gInitialised) {
        cellularPortPrivateDeinit();
        gInitialised = false;
    }
}

// Get the current tick converted to a time in milliseconds.
int64_t cellularPortGetTickTimeMs()
{
    int32_t tickTime = 0;

    if (gInitialised) {
        tickTime = cellularPortPrivateGetTickTimeMs();
    }

    return tickTime;
}

// End of file
