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
#include "cellular_port_gpio.h"

#include "driver/gpio.h"
#include "driver/rtc_io.h"

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
    gpio_config_t config;

    if (pConfig != NULL) {
        // Set the things that won't change
        config.intr_type = GPIO_PIN_INTR_DISABLE;

        // Set the direction and drive mode
        switch (pConfig->direction) {
            case CELLULAR_PORT_GPIO_DIRECTION_NONE:
                config.mode = GPIO_MODE_DISABLE;
            break;
            case CELLULAR_PORT_GPIO_DIRECTION_INPUT:
                config.mode = GPIO_MODE_INPUT;
            break;
            case CELLULAR_PORT_GPIO_DIRECTION_OUTPUT:
                config.mode = GPIO_MODE_OUTPUT;
                if (pConfig->driveMode == CELLULAR_PORT_GPIO_DRIVE_MODE_OPEN_DRAIN) {
                    config.mode = GPIO_MODE_OUTPUT_OD;
                }
            break;
            case CELLULAR_PORT_GPIO_DIRECTION_INPUT_OUTPUT:
                config.mode = GPIO_MODE_INPUT_OUTPUT;
                if (pConfig->driveMode == CELLULAR_PORT_GPIO_DRIVE_MODE_OPEN_DRAIN) {
                    config.mode = GPIO_MODE_INPUT_OUTPUT_OD;
                }
            break;
            default:
                badConfig = true;
            break;
        }

        // Set the pull up/down:
        // Note that pulling both up and down is apparently
        // valid for ESP32
        config.pull_down_en = 0;
        config.pull_up_en = 0;
        switch (pConfig->pullMode) {
            case CELLULAR_PORT_GPIO_PULL_MODE_NONE:
            break;
            case CELLULAR_PORT_GPIO_PULL_MODE_PULL_UP:
                config.pull_up_en = 1;
            break;
            case CELLULAR_PORT_GPIO_PULL_MODE_PULL_DOWN:
                config.pull_down_en = 1;
            break;
            default:
                badConfig = true;
            break;
        }

        // Set the pin
        config.pin_bit_mask = 1ULL << pConfig->pin;

        // Actually do the configuration
        if (!badConfig) {
            errorCode = CELLULAR_PORT_PLATFORM_ERROR;
            if (gpio_config(&config) == ESP_OK) {
                // If it's an output pin, set the drive capability
                if (((pConfig->direction == CELLULAR_PORT_GPIO_DIRECTION_OUTPUT) ||
                     (pConfig->direction == CELLULAR_PORT_GPIO_DIRECTION_INPUT_OUTPUT)) &&
                    (gpio_set_drive_capability(pConfig->pin, pConfig->driveCapability) == ESP_OK)) {
                    errorCode = CELLULAR_PORT_SUCCESS;
                } else {
                    // It's not an output pin so we're done
                    errorCode = CELLULAR_PORT_SUCCESS;
                }
            }
        }
    }

    return (int32_t) errorCode;
}

// Set the state of a GPIO.
// Note there used to be code here which tried to
// handle the case of a GPIO being made to hold
// its state during sleep.  However, a side-effect
// of doing that was that setting a GPIO when it had
// not yet been made an output, so that when it was made
// an output it immediately had the right level,
// did not work, so that code was removed.
int32_t cellularPortGpioSet(int32_t pin, int32_t level)
{
    CellularPortErrorCode_t errorCode = CELLULAR_PORT_PLATFORM_ERROR;

    if (gpio_set_level(pin, level) == ESP_OK) {
        errorCode = CELLULAR_PORT_SUCCESS;
    }

    return (int32_t) errorCode;
}

// Get the state of a GPIO.
int32_t cellularPortGpioGet(int32_t pin)
{
    return gpio_get_level(pin);
}

// End of file
