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

#ifndef _CELLULAR_PORT_PRIVATE_H_
#define _CELLULAR_PORT_PRIVATE_H_

/** Stuff private to the STM32F4 porting layer.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// Get the port number of a pin, which is the upper nibble.
#define CELLULAR_PORT_STM32F4_GPIO_PORT(x) ((uint16_t ) (((uint32_t) x) >> 4))

// Get the pin number of a pin, which is the lower nibble.
#define CELLULAR_PORT_STM32F4_GPIO_PIN(x) ((uint16_t ) (x & 0x0f))

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * FUNCTIONS
 * -------------------------------------------------------------- */

/** Initialise the private stuff.
 *
 * @return zero on success else negative error code.
 */
int32_t cellularPortPrivateInit();

/** Deinitialise the private stuff.
 */
void cellularPortPrivateDeinit();

/** Get the current OS tick converted to a time in milliseconds.
 */
int64_t cellularPortPrivateGetTickTimeMs();

/** Return the address of the port register for a given GPIO pin.
 *
 * @param pin the pin number.
 * @return    the GPIO port address.
 * */
GPIO_TypeDef *pCellularPortPrivateGpioGetReg(int32_t pin);

/** Enable the clock to the register of the given GPIO pin.
 *
 * @param pin the pin number.
 */
void cellularPortPrivateGpioEnableClock(int32_t pin);

#ifdef __cplusplus
}
#endif

#endif // _CELLULAR_PORT_PRIVATE_H_

// End of file
