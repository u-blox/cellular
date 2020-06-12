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
#include "cellular_cfg_hw_platform_specific.h"
#include "cellular_port_clib.h"
#include "cellular_port.h"
#include "cellular_port_gpio.h"

#include "stm32f4xx_hal.h"

#include "stdio.h"
#include "assert.h"

#include "cellular_port_private.h" // Down here 'cos it needs GPIO_TypeDef

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

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *pFile, uint32_t line)
{
    _cellularPort_assert((char *) pFile, line, false);
}
#endif /* USE_FULL_ASSERT */

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS
 * -------------------------------------------------------------- */

/*
 * Start the cellular platform
 *
 * For mynewt, the platform has already been configured and this call
 * should occur in the main task. No entry point function is required.
 */

int32_t cellularPortPlatformStart(void (*pEntryPoint)(void *),
                                  void *pParameter,
                                  size_t stackSizeBytes,
                                  int32_t priority)
{
#if (CELLULAR_CFG_PIN_C030_ENABLE_3V3 >= 0) || (CELLULAR_CFG_PIN_RESET >= 0)
    CellularPortGpioConfig_t gpioConfig = CELLULAR_PORT_GPIO_CONFIG_DEFAULT;
#endif

#if CELLULAR_CFG_PIN_C030_ENABLE_3V3 >= 0
    // Enable power to 3V3 rail for the C030 board
    gpioConfig.pin = CELLULAR_CFG_PIN_C030_ENABLE_3V3;
    gpioConfig.driveMode = CELLULAR_PORT_GPIO_DRIVE_MODE_OPEN_DRAIN;
    gpioConfig.direction = CELLULAR_PORT_GPIO_DIRECTION_OUTPUT;
    if ((cellularPortGpioConfig(&gpioConfig) != 0) ||
        (cellularPortGpioSet(CELLULAR_CFG_PIN_C030_ENABLE_3V3, 1) != 0)) {
        return CELLULAR_PORT_PLATFORM_ERROR;
    }
#endif
#if CELLULAR_CFG_PIN_RESET >= 0
    // Set reset high (i.e. not reset) if it is connected
    gpioConfig.pin = CELLULAR_CFG_PIN_RESET;
    gpioConfig.driveMode = CELLULAR_PORT_GPIO_DRIVE_MODE_NORMAL;
    gpioConfig.direction = CELLULAR_PORT_GPIO_DIRECTION_OUTPUT;
    if ((cellularPortGpioConfig(&gpioConfig) != 0) ||
        (cellularPortGpioSet(CELLULAR_CFG_PIN_RESET, 1) != 0)) {
        return CELLULAR_PORT_PLATFORM_ERROR;
    }
#endif

    return CELLULAR_PORT_SUCCESS;
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
