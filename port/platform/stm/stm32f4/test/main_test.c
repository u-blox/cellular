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
#include "cellular_port_test_platform_specific.h"
#include "cellular_port_clib.h"

#include "stdio.h"
#include "stdbool.h"
#include "assert.h"

#include "cmsis_os.h"
#include "stm32f4xx_hal.h"

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// How much stack the task running all the tests needs in bytes.
#define CELLULAR_PORT_TEST_RUNNER_TASK_STACK_SIZE_BYTES (1024 * 4)

// The priority of the task running the tests.
#define CELLULAR_PORT_TEST_RUNNER_TASK_PRIORITY osPriorityLow

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

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

// Unity setUp() function.
void setUp(void)
{
    // Nothing to do
}

// Unity tearDown() function.
void tearDown(void)
{
    // Nothing to do
}

void testFail(void)
{
    
}


// The task within which testing runs.
void testTask(void *pParam)
{
    (void) pParam;

    printf("\n\nCELLULAR_TEST: Test task started.\n");

    UNITY_BEGIN();

    printf("CELLULAR_TEST: Tests available:\n\n");
    cellularPortUnityPrintAll("CELLULAR_TEST: ");
    printf("CELLULAR_TEST: Running all tests.\n");
    cellularPortUnityRunAll("CELLULAR_TEST: ");

    UNITY_END();

    printf("\n\nCELLULAR_TEST: Test task ended.\n");

    while(1){}
}

// Entry point
int main(void)
{
    osThreadId threadId = NULL;

    // Reset all peripherals, initialize the Flash interface and the Systick
    HAL_Init();

    // Configure the system clock
    systemClockConfig();

    // TODO: if I put a printf() here then all is fine.
    // If I don't then any attempt to print later
    // results in a hard fault.  Need to find out why.
    printf("\n\nCELLULAR_TEST: starting RTOS...\n");

    // Create the test task and have it running
    // at a low priority
    osThreadDef(TestTask, (os_pthread) testTask,
                CELLULAR_PORT_TEST_RUNNER_TASK_PRIORITY, 0,
                CELLULAR_PORT_TEST_RUNNER_TASK_STACK_SIZE_BYTES);
    threadId = osThreadCreate(osThread(TestTask), NULL);
    assert(threadId != NULL);

    // Start the scheduler.
    osKernelStart();

    // Should never get here
    assert(false);

    return 0;
}
