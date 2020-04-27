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
#include "cellular_port_gpio.h"

#include "stm32f4xx_hal.h"
#include "stm32f4xx_hal_gpio.h"

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS
 * -------------------------------------------------------------- */

// Configure a GPIO.
int32_t cellularPortGpioConfig(CellularPortGpioConfig_t *pConfig)
{
    CellularPortErrorCode_t errorCode = CELLULAR_PORT_INVALID_PARAMETER;
    bool badConfig = false;
    GPIO_InitTypeDef config;

    config.Pin = CELLULAR_PORT_STM32F4_GPIO_PIN(pConfig->pin);
    config.Mode = GPIO_MODE_INPUT;
    config.Pull = GPIO_NOPULL;
    config.Speed = GPIO_SPEED_FREQ_LOW;

    if (pConfig != NULL) {
        // Set the direction and mode
        switch (pConfig->direction) {
            case CELLULAR_PORT_GPIO_DIRECTION_NONE:
            case CELLULAR_PORT_GPIO_DIRECTION_INPUT:
                // Nothing to do
            break;
            case CELLULAR_PORT_GPIO_DIRECTION_INPUT_OUTPUT:
            case CELLULAR_PORT_GPIO_DIRECTION_OUTPUT:
                switch (pConfig->driveMode) {
                    case CELLULAR_PORT_GPIO_DRIVE_MODE_NORMAL:
                        config.Mode = GPIO_MODE_OUTPUT_PP;
                    break;
                    case CELLULAR_PORT_GPIO_DRIVE_MODE_OPEN_DRAIN:
                        config.Mode = GPIO_MODE_OUTPUT_OD;
                    break;
                    default:
                        badConfig = true;
                    break;
                }
            break;
            default:
                badConfig = true;
            break;
        }

        // Set pull up/down
        switch (pConfig->pullMode) {
            case CELLULAR_PORT_GPIO_PULL_MODE_NONE:
                // No need to do anything
            break;
            case CELLULAR_PORT_GPIO_PULL_MODE_PULL_UP:
                config.Pull = GPIO_PULLUP;
            break;
            case CELLULAR_PORT_GPIO_PULL_MODE_PULL_DOWN:
                config.Pull = GPIO_PULLDOWN;
            break;
            default:
                badConfig = true;
            break;
        }

        // Setting drive strength is not supported on this platform

        // Actually do the configuration
        if (!badConfig) {
            // The GPIO init function for STM32F4 takes a pointer
            // to the port register, the index for which is the upper
            // nibble of pin (they are in banks of 16), and then
            // the configuration structure which has the pin number
            // within that port.
            HAL_GPIO_Init(((GPIO_TypeDef *) GPIOA_BASE) +
                          CELLULAR_PORT_STM32F4_GPIO_PORT(pConfig->pin),
                          &config);
            errorCode = CELLULAR_PORT_SUCCESS;
        }
    }

    return (int32_t) errorCode;
}

// Set the state of a GPIO.
int32_t cellularPortGpioSet(int32_t pin, int32_t level)
{
    HAL_GPIO_WritePin(((GPIO_TypeDef *) GPIOA_BASE) +
                      CELLULAR_PORT_STM32F4_GPIO_PORT(pin),
                      CELLULAR_PORT_STM32F4_GPIO_PIN(pin),
                      level);

    return (int32_t) CELLULAR_PORT_SUCCESS;
}

// Get the state of a GPIO.
int32_t cellularPortGpioGet(int32_t pin)
{
    return HAL_GPIO_ReadPin(((GPIO_TypeDef *) GPIOA_BASE) +
                            CELLULAR_PORT_STM32F4_GPIO_PORT(pin),
                            CELLULAR_PORT_STM32F4_GPIO_PIN(pin));
}

// End of file
