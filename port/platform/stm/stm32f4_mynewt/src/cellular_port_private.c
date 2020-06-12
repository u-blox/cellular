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

#include "stm32f4xx_hal.h"
#include "stm32f4xx_ll_bus.h"

#include "cellular_port_private.h"  // Down here 'cos it needs GPIO_TypeDef
#include "os/os.h"

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// Get the GPIOx address for a given GPIO port.
static GPIO_TypeDef * const gpGpioReg[] = {GPIOA,
                                           GPIOB,
                                           GPIOC,
                                           GPIOD,
                                           GPIOE,
                                           0,
                                           0,
                                           GPIOH,
                                           0,
                                           0,
                                           0};

// Get the LL driver peripheral number for a given GPIO port.
static const int32_t gLlApbGrpPeriphGpioPort[] = {LL_AHB1_GRP1_PERIPH_GPIOA,
                                                  LL_AHB1_GRP1_PERIPH_GPIOB,
                                                  LL_AHB1_GRP1_PERIPH_GPIOC,
                                                  LL_AHB1_GRP1_PERIPH_GPIOD,
                                                  LL_AHB1_GRP1_PERIPH_GPIOE,
                                                  0,
                                                  0,
                                                  LL_AHB1_GRP1_PERIPH_GPIOH,
                                                  0,
                                                  0,
                                                  0};

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS SPECIFIC TO THIS PORT
 * -------------------------------------------------------------- */

// Initalise the private stuff.
int32_t cellularPortPrivateInit()
{
    return CELLULAR_PORT_SUCCESS;
}

// Deinitialise the private stuff.
void cellularPortPrivateDeinit()
{
    // Nothing to do
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS SPECIFIC TO THIS PORT: GET TIME TICK
 * -------------------------------------------------------------- */

// Get the current tick converted to a time in milliseconds.
int64_t cellularPortPrivateGetTickTimeMs()
{
    int64_t time_ms;

    /* XXX: os time is a 32-bit counter so there will be wrapping issues */
    time_ms = (int64_t)os_time_get();
    time_ms = (time_ms * 1000) / OS_TICKS_PER_SEC;
    return time_ms;
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS SPECIFIC TO THIS PORT: MISC
 * -------------------------------------------------------------- */

// Return the base address for a given GPIO pin.
GPIO_TypeDef * const pCellularPortPrivateGpioGetReg(int32_t pin)
{
    int32_t port = CELLULAR_PORT_STM32F4_GPIO_PORT(pin);

    cellularPort_assert(port >= 0);
    cellularPort_assert(port < sizeof(gpGpioReg) / sizeof(gpGpioReg[0]));

    return gpGpioReg[port];
}

// Enable the clock to the register of the given GPIO pin.
void cellularPortPrivateGpioEnableClock(int32_t pin)
{
    int32_t port = CELLULAR_PORT_STM32F4_GPIO_PORT(pin);

    cellularPort_assert(port >= 0);
    cellularPort_assert(port < sizeof(gLlApbGrpPeriphGpioPort) / sizeof(gLlApbGrpPeriphGpioPort[0]));
    LL_AHB1_GRP1_EnableClock(gLlApbGrpPeriphGpioPort[port]);
}

// End of file
