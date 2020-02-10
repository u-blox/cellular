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

#ifndef _CELLULAR_PORT_GPIO_H_
#define _CELLULAR_PORT_GPIO_H_

/** Porting layer for GPIO access functions.  These functions
 * are thread-safe.
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/** The possible GPIO directions.
 */
typedef enum {
    CELLULAR_PORT_GPIO_DIRECTION_NONE,
    CELLULAR_PORT_GPIO_DIRECTION_INPUT,
    CELLULAR_PORT_GPIO_DIRECTION_OUTPUT,
    CELLULAR_PORT_GPIO_DIRECTION_INPUT_OUTPUT,
    MAX_NUM_CELLULAR_PORT_GPIO_DIRECTIONS
} CellularPortGpioDirection;

/** The possible GPIO pull modes.
 */
typedef enum {
    CELLULAR_PORT_GPIO_PULL_MODE_NONE,
    CELLULAR_PORT_GPIO_PULL_MODE_PULL_UP,
    CELLULAR_PORT_GPIO_PULL_MODE_PULL_DOWN,
    MAX_NUM_CELLULAR_PORT_GPIO_PULL_MODES
} CellularPortGpioPullMode;

/** The possible GPIO drive modes.
 */
typedef enum {
    CELLULAR_PORT_GPIO_DRIVE_MODE_NORMAL,
    CELLULAR_PORT_GPIO_DRIVE_MODE_OPEN_DRAIN,
    MAX_NUM_CELLULAR_PORT_GPIO_DRIVE_MODES
} CellularPortGpioDriveMode;

/** GPIO configuration structure.
 * If you update this, don't forget to update
 * CELLULAR_PORT_GPIO_CONFIG_DEFAULT also.
 */
typedef struct {
    int32_t pin;
    CellularPortGpioDirection direction;
    CellularPortGpioPullMode pullMode;
    CellularPortGpioDriveMode driveMode;
} CellularPortGpioConfig;

/** Default values for the above.
 */
#define CELLULAR_PORT_GPIO_CONFIG_DEFAULT = {-1, \
                                             CELLULAR_PORT_GPIO_DIRECTION_NONE,    \
                                             CELLULAR_PORT_GPIO_PULL_MODE_NONE,    \
                                             CELLULAR_PORT_GPIO_DRIVE_MODE_NORMAL}

/* ----------------------------------------------------------------
 * FUNCTIONS
 * -------------------------------------------------------------- */

/** Configure a GPIO.
 *
 * @param pConfig a pointer to the configuration to set, cannot be NULL.
 * @return        zero on success else negative error code.
 */
int32_t cellularPortGpioConfig(CellularPortGpioConfig *pConfig);

/** Set the state of a GPIO.
 *
 * @param pin   the pin to set.
 * @param level the level to set, 0 for low or non-zero for high.
 * @return      zero on success else negative error code.
 */
int32_t cellularPortGpioSet(int32_t pin, int32_t level);

/** Get the state of a GPIO.
 *
 * @param pin   the pin to get the state of.
 * @return      on success the level (0 or 1) else negative error code.
 */
int32_t cellularPortGpioGet(int32_t pin);

#endif // _CELLULAR_PORT_GPIO_H_

// End of file
