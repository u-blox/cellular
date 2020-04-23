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
#include "cellular_port_private.h"

#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_tim.h"

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// Macro melder called by all those below.
#define CELLULAR_PORT_TIM_MELD(x, y, z) x ## y ## z

// Make TIMx_BASE
#define CELLULAR_PORT_TIM_BASE(x) ((TIM_TypeDef *) CELLULAR_PORT_TIM_MELD(TIM, x, _BASE))

// Make TIMx_IRQHandler()
#define CELLULAR_PORT_TIM_IRQ_HANDLER(x) CELLULAR_PORT_TIM_MELD(TIM, x, _IRQHandler())

// Make TIMx_IRQn
#define CELLULAR_PORT_TIM_IRQ_N(x) CELLULAR_PORT_TIM_MELD(TIM, x, _IRQn)

// Make __TIMx_CLK_ENABLE()
#define CELLULAR_PORT_TIM_CLK_ENABLE(x) CELLULAR_PORT_TIM_MELD(__TIM, x, _CLK_ENABLE())

// Make __TIMx_CLK_DISABLE()
#define CELLULAR_PORT_TIM_CLK_DISABLE(x) CELLULAR_PORT_TIM_MELD(__TIM, x, _CLK_DISABLE())

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// The timer handle structure
static TIM_HandleTypeDef gTimerHandle;

// Overflow counter that allows us to keep 64 bit time.
static int64_t gTickTimerOverflowCount;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

// IRQ handler for the tick timer.
void CELLULAR_PORT_TIM_IRQ_HANDLER(CELLULAR_PORT_TICK_TIMER_INSTANCE)
{
    // Call the HAL's generic timer IRQ handler
    HAL_TIM_IRQHandler(&gTimerHandle);
}

// This will be called from the HAL's generic timer IRQ handler
// if the cause of the interrupt was that the timer period had
// elapsed.
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *pTimerHandle)
{
    gTickTimerOverflowCount++;
}

// Start the tick timer.
static int32_t tickTimerStart()
{
    CellularPortErrorCode_t errorCode = CELLULAR_PORT_PLATFORM_ERROR;

    // Enable the clock
    CELLULAR_PORT_TIM_CLK_ENABLE(CELLULAR_PORT_TICK_TIMER_INSTANCE);

    // Fill in the handle structure
    gTimerHandle.Instance = CELLULAR_PORT_TIM_BASE(CELLULAR_PORT_TICK_TIMER_INSTANCE);
    gTimerHandle.Init.Prescaler = CELLULAR_PORT_TICK_TIMER_PRESCALER;
    gTimerHandle.Init.CounterMode = TIM_COUNTERMODE_UP;
    gTimerHandle.Init.Period = CELLULAR_PORT_TICK_TIMER_PERIOD;
    gTimerHandle.Init.ClockDivision = CELLULAR_PORT_TICK_TIMER_DIVIDER;
    gTimerHandle.Init.RepetitionCounter = 0;

    // Initialise and start the timer
    if ((HAL_TIM_Base_Init(&gTimerHandle) == HAL_OK) &&
        (HAL_TIM_Base_Start(&gTimerHandle) == HAL_OK)) {
        // Enable interruptsf
        HAL_NVIC_SetPriority(CELLULAR_PORT_TIM_IRQ_N(CELLULAR_PORT_TICK_TIMER_INSTANCE),
                             0, 0);
        HAL_NVIC_EnableIRQ(CELLULAR_PORT_TIM_IRQ_N(CELLULAR_PORT_TICK_TIMER_INSTANCE));
        errorCode = CELLULAR_PORT_SUCCESS;
    }

    return errorCode;
}

// Stop the tick timer.
static void tickTimerStop()
{
    // Disable interrupts
    HAL_NVIC_DisableIRQ(CELLULAR_PORT_TIM_IRQ_N(CELLULAR_PORT_TICK_TIMER_INSTANCE));

    // Disable the clock
    CELLULAR_PORT_TIM_CLK_DISABLE(CELLULAR_PORT_TICK_TIMER_INSTANCE);
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS SPECIFIC TO THIS PORT
 * -------------------------------------------------------------- */

// Initalise the private stuff.
int32_t cellularPortPrivateInit()
{
    gTickTimerOverflowCount = 0;

    return tickTimerStart();
}

// Deinitialise the private stuff.
void cellularPortPrivateDeinit()
{
    tickTimerStop();
}

// Get the current tick converted to a time in milliseconds.
int64_t cellularPortPrivateGetTickTimeMs()
{
    int64_t tickTimerValue = 0;

    // Read the timer
    tickTimerValue = __HAL_TIM_GET_COUNTER(&gTimerHandle);

    // Add the overflow count.
    tickTimerValue += gTickTimerOverflowCount *
                      CELLULAR_PORT_TICK_TIMER_PERIOD;

    return tickTimerValue;
}

// End of file
