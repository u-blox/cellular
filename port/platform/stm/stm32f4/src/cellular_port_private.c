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

#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_tim.h"

#include "cellular_port_private.h"  // Down here 'cos it needs GPIO_TypeDef

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

// Counter to keep track of RTOS ticks
static int32_t gTickTimerRtosCount;

// Overflow counter that allows us to keep 64 bit time.
static int64_t gTickTimerOverflowCount;

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
 * PUBLIC FUNCTIONS SPECIFIC TO THIS PORT: HAL FUNCTIONS
 * -------------------------------------------------------------- */

// IRQ handler for the tick timer.
void CELLULAR_PORT_TIM_IRQ_HANDLER(CELLULAR_PORT_TICK_TIMER_INSTANCE)
{
    // Call the HAL's generic timer IRQ handler
    HAL_TIM_IRQHandler(&gTimerHandle);
}

// Called when the timer period interrupt occurs.
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *pTimerHandle)
{
    (void) pTimerHandle;

    // Call into the HAL to increment
    // a global variable "uwTick" used as the RTOS
    // time-base.
    HAL_IncTick();

    // Increment the local count and check for
    // overflow of the longer term overflow counter
    // that allows us to keep 64 bit time
    gTickTimerRtosCount++;
    if (gTickTimerRtosCount > CELLULAR_PORT_TICK_TIMER_OVERFLOW_PERIOD_MS) {
        gTickTimerOverflowCount++;
        gTickTimerRtosCount = 0;
    }
}

// Start the tick timer.
// This function is called automagically by HAL_Init()
// at start of day.
HAL_StatusTypeDef HAL_InitTick(uint32_t tickPriority)
{
    HAL_StatusTypeDef errorCode = HAL_ERROR;

    // Set interrupt priority
    HAL_NVIC_SetPriority(CELLULAR_PORT_TIM_IRQ_N(CELLULAR_PORT_TICK_TIMER_INSTANCE),
                         tickPriority, 0);

    // Set the global uwTickPrio based on this so that when
    // HAL_RCC_ClockConfig() is called later it uses the
    // correct value
    uwTickPrio = tickPriority;

    // Enable interrupt
    HAL_NVIC_EnableIRQ(CELLULAR_PORT_TIM_IRQ_N(CELLULAR_PORT_TICK_TIMER_INSTANCE));

    // Enable the clock
    CELLULAR_PORT_TIM_CLK_ENABLE(CELLULAR_PORT_TICK_TIMER_INSTANCE);

    // Fill in the handle structure
    gTimerHandle.Instance = CELLULAR_PORT_TIM_BASE(CELLULAR_PORT_TICK_TIMER_INSTANCE);
    gTimerHandle.Init.Prescaler = CELLULAR_PORT_TICK_TIMER_PRESCALER;
    gTimerHandle.Init.CounterMode = TIM_COUNTERMODE_UP;
    gTimerHandle.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    gTimerHandle.Init.Period = CELLULAR_PORT_TICK_TIMER_PERIOD_US - 1;
    gTimerHandle.Init.ClockDivision = CELLULAR_PORT_TICK_TIMER_DIVIDER;
    gTimerHandle.Init.RepetitionCounter = 0;

    // Initialise and start the timer
    errorCode = HAL_TIM_Base_Init(&gTimerHandle);
    if (errorCode == HAL_OK) {
        gTickTimerRtosCount = 0;
        gTickTimerOverflowCount = 0;
        errorCode = HAL_TIM_Base_Start_IT(&gTimerHandle);
    }

    return errorCode;
}

// Suspend Tick increment.
void HAL_SuspendTick(void)
{
    // Disable TIM update interrupt
    __HAL_TIM_DISABLE_IT(&gTimerHandle, TIM_IT_UPDATE);
}

// Resume Tick increment.
void HAL_ResumeTick(void)
{
    // Enable TIM Update interrupt
    __HAL_TIM_ENABLE_IT(&gTimerHandle, TIM_IT_UPDATE);
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS SPECIFIC TO THIS PORT: INIT FUNCTIONS
 * -------------------------------------------------------------- */

// Initalise the private stuff.
int32_t cellularPortPrivateInit()
{
    // Nothing to do, all done when
    // HAL_InitTick() is called from HAL_Init()
    return CELLULAR_PORT_SUCCESS;
}

// Deinitialise the private stuff.
void cellularPortPrivateDeinit()
{
    // Disable interrupts
    HAL_NVIC_DisableIRQ(CELLULAR_PORT_TIM_IRQ_N(CELLULAR_PORT_TICK_TIMER_INSTANCE));

    // Disable the clock
    CELLULAR_PORT_TIM_CLK_DISABLE(CELLULAR_PORT_TICK_TIMER_INSTANCE);
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS SPECIFIC TO THIS PORT: GET TIME TICK
 * -------------------------------------------------------------- */

// Get the current tick converted to a time in milliseconds.
int64_t cellularPortPrivateGetTickTimeMs()
{
    int64_t tickTimerValue;

    // Read the timer
    tickTimerValue = __HAL_TIM_GET_COUNTER(&gTimerHandle);

    // Add the overflow count.
    tickTimerValue += gTickTimerOverflowCount *
                      CELLULAR_PORT_TICK_TIMER_OVERFLOW_PERIOD_MS;

    return tickTimerValue;
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
