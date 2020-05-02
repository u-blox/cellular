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

#include "cellular_port_private.h"  // Down here 'cos it needs GPIO_TypeDef

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// Counter to keep track of RTOS ticks: NOT static
// so that the stm32f4xx_it.c can update it.
int32_t gTickTimerRtosCount;

// Get the GPIOx address for a given GPIO port.
static GPIO_TypeDef * const gpGpioReg[] = {GPIOA,
                                           GPIOB,
                                           GPIOC,
                                           GPIOD,
                                           GPIOE,
                                           GPIOF,
                                           GPIOG,
                                           GPIOH,
                                           GPIOI,
                                           GPIOJ,
                                           GPIOK};

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS SPECIFIC TO THIS PORT
 * -------------------------------------------------------------- */

// Initalise the private stuff.
int32_t cellularPortPrivateInit()
{
    gTickTimerRtosCount = 0;
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
    return gTickTimerRtosCount;
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

// End of file
