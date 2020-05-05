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

#include "stm32f437xx.h"
#include "stm32f4xx_hal.h"
#include "cmsis_os.h"

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

// System Clock Configuration
static void systemClockConfig(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

    // Configure the main internal regulator output voltage
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

    // Initialize the CPU, AHB and APB bus clocks
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 12;
    RCC_OscInitStruct.PLL.PLLN = 336;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 7;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        assert(false);
    }

    // Initialize the CPU, AHB and APB bus clocks
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_SYSCLK | RCC_CLOCKTYPE_PCLK1 |
                                  RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV4;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) {
        assert(false);
    }
}

#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *pFile, uint32_t line)
{
    _cellularPort_assert((char *) pFile, line, false);
}
#endif /* USE_FULL_ASSERT */

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
    osThreadId threadId;
#if (CELLULAR_CFG_PIN_C030_ENABLE_3V3 >= 0) || (CELLULAR_CFG_PIN_RESET >= 0)
    CellularPortGpioConfig_t gpioConfig = CELLULAR_PORT_GPIO_CONFIG_DEFAULT;
#endif

    if (pEntryPoint != NULL) {
        errorCode = CELLULAR_PORT_PLATFORM_ERROR;

        // Reset all peripherals, initialize the Flash interface and the Systick
        HAL_Init();

        // Configure the system clock
        systemClockConfig();

#if CELLULAR_CFG_PIN_C030_ENABLE_3V3 >= 0
        // Enable power to 3V3 rail for the C030 board
        gpioConfig.pin = CELLULAR_CFG_PIN_C030_ENABLE_3V3;
        gpioConfig.driveMode = CELLULAR_PORT_GPIO_DRIVE_MODE_OPEN_DRAIN;
        gpioConfig.direction = CELLULAR_PORT_GPIO_DIRECTION_OUTPUT;
        if ((cellularPortGpioConfig(&gpioConfig) == 0) &&
            (cellularPortGpioSet(CELLULAR_CFG_PIN_C030_ENABLE_3V3, 1) == 0)) {
#endif
#if CELLULAR_CFG_PIN_RESET >= 0
            // Set reset high (i.e. not reset) if it is connected
            gpioConfig.pin = CELLULAR_CFG_PIN_RESET;
            gpioConfig.driveMode = CELLULAR_PORT_GPIO_DRIVE_MODE_NORMAL;
            gpioConfig.direction = CELLULAR_PORT_GPIO_DIRECTION_OUTPUT;
            if ((cellularPortGpioConfig(&gpioConfig) == 0) &&
                (cellularPortGpioSet(CELLULAR_CFG_PIN_RESET, 1) == 0)) {
#endif
                // TODO: if I put a printf() here then all is fine.
                // If I don't then any attempt to print later
                // results in a hard fault.  Need to find out why.
                printf("\n\nCELLULAR_TEST: starting RTOS...\n");

                // Create the task
                osThreadDef(EntryPoint, (os_pthread) pEntryPoint,
                            priority, 0, stackSizeBytes);
                threadId = osThreadCreate(osThread(EntryPoint), pParameter);

                if (threadId != NULL) {
                    // Start the scheduler.
                    osKernelStart();
                    // Should never get here
                }
#if CELLULAR_CFG_PIN_RESET >= 0
            }
#endif
#if CELLULAR_CFG_PIN_C030_ENABLE_3V3 >= 0
        }
#endif
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
