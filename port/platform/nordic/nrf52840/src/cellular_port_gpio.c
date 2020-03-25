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

#include "cellular_port_clib.h"
#include "cellular_port.h"
#include "cellular_port_gpio.h"

#include "nrf.h"
#include "nrf_gpio.h"

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
    nrf_gpio_pin_dir_t direction = CELLULAR_PORT_GPIO_DIRECTION_NONE;
    nrf_gpio_pin_input_t input = NRF_GPIO_PIN_INPUT_DISCONNECT;
    nrf_gpio_pin_pull_t pullMode = NRF_GPIO_PIN_NOPULL;
    nrf_gpio_pin_drive_t driveMode = GPIO_PIN_CNF_DRIVE_S0S1;

    if (pConfig != NULL) {
        // Set the direction and drive mode
        switch (pConfig->direction) {
            case CELLULAR_PORT_GPIO_DIRECTION_NONE:
                // Do nothing here, disconnect is later
            break;
            case CELLULAR_PORT_GPIO_DIRECTION_INPUT:
                direction = NRF_GPIO_PIN_DIR_INPUT;
            break;
            case CELLULAR_PORT_GPIO_DIRECTION_OUTPUT:
                direction = NRF_GPIO_PIN_DIR_OUTPUT;
            break;
            case CELLULAR_PORT_GPIO_DIRECTION_INPUT_OUTPUT:
                direction = NRF_GPIO_PIN_DIR_OUTPUT;
                input = NRF_GPIO_PIN_INPUT_CONNECT;
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
                pullMode = NRF_GPIO_PIN_PULLUP;
            break;
            case CELLULAR_PORT_GPIO_PULL_MODE_PULL_DOWN:
                pullMode = NRF_GPIO_PIN_PULLDOWN;
            break;
            default:
                badConfig = true;
            break;
        }

        // Set the drive strength
        switch (pConfig->driveCapability) {
            case CELLULAR_PORT_GPIO_DRIVE_CAPABILITY_WEAKEST:
            case CELLULAR_PORT_GPIO_DRIVE_CAPABILITY_WEAK:
                // No need to do anything
            break;
            case CELLULAR_PORT_GPIO_DRIVE_CAPABILITY_STRONG:
            case CELLULAR_PORT_GPIO_DRIVE_CAPABILITY_STRONGEST:
                driveMode = NRF_GPIO_PIN_H0H1;
            break;
            default:
                badConfig = true;
            break;
        }

        // Set the drive mode
        switch (pConfig->driveMode) {
            case CELLULAR_PORT_GPIO_DRIVE_MODE_NORMAL:
                // No need to do anything
            break;
            case CELLULAR_PORT_GPIO_DRIVE_MODE_OPEN_DRAIN:
                switch (driveMode) {
                    case NRF_GPIO_PIN_S0S1:
                        driveMode = NRF_GPIO_PIN_S0D1;
                    break;
                    case NRF_GPIO_PIN_H0H1:
                        driveMode = NRF_GPIO_PIN_H0D1;
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

        // Actually do the configuration
        if (!badConfig) {
            if (pConfig->direction == CELLULAR_PORT_GPIO_DIRECTION_NONE) {
                nrf_gpio_input_disconnect(pConfig->pin);
            } else {
                nrf_gpio_cfg(pConfig->pin, direction, input,
                             pullMode, driveMode,
                             NRF_GPIO_PIN_NOSENSE);
            }
            errorCode = CELLULAR_PORT_SUCCESS;
        }
    }

    return (int32_t) errorCode;
}

// Set the state of a GPIO.
int32_t cellularPortGpioSet(int32_t pin, int32_t level)
{
    if (level != 0) {
        nrf_gpio_pin_set(pin);
    } else {
        nrf_gpio_pin_clear(pin);
    }

    return (int32_t) CELLULAR_PORT_SUCCESS;
}

// Get the state of a GPIO.
int32_t cellularPortGpioGet(int32_t pin)
{
    return nrf_gpio_pin_read(pin);
}

// End of file
