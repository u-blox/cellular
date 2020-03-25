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

#ifndef _CELLULAR_CFG_HW_H_
#define _CELLULAR_CFG_HW_H_

/* No #includes allowed here */

/* This header file contains hardware configuration information for
 * a Nordic NRF52840 board.
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS FOR NRF52840: MISC
 * -------------------------------------------------------------- */

#ifndef CELLULAR_CFG_UART
/** The UART HW block to use inside the NRF52840 chip.
 */
# define CELLULAR_CFG_UART                           0
#endif

#ifndef CELLULAR_CFG_RTS_THRESHOLD
/** The buffer threshold at which RTS is de-asserted, indicating the
 * cellular module should stop sending data to us.  Must be defined
 * if CELLULAR_CFG_PIN_RTS is not -1.
 */
# define CELLULAR_CFG_RTS_THRESHOLD                  100
#endif

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS FOR NRF52840: PINS
 * -------------------------------------------------------------- */

#ifndef CELLULAR_CFG_PIN_ENABLE_POWER
/** The NRF52840 GPIO output that enables power to the cellular module.
 * -1 is used where there is no such connection.
 */
# define CELLULAR_CFG_PIN_ENABLE_POWER     2
#endif

#ifndef CELLULAR_CFG_PIN_CP_ON
/** The NRF52840 GPIO output that that is connected to the CP_ON pin of
 * the cellular module.
 */
# define CELLULAR_CFG_PIN_CP_ON            25
#endif

#ifndef CELLULAR_CFG_PIN_VINT
/** The NRF52840GPIO input that is connected to the VInt pin of the
 * cellular module.
 * -1 is used where there is no such connection.
 */
# define CELLULAR_CFG_PIN_VINT             36
#endif

#ifndef CELLULAR_CFG_PIN_TXD
/** The NRF52840 GPIO output pin that sends UART data to the cellular
 * module.
 */
# define CELLULAR_CFG_PIN_TXD              4
#endif

#ifndef CELLULAR_CFG_PIN_RXD
/** The NRF52840 GPIO input pin that receives UART data from the cellular
 * module.
 */
# define CELLULAR_CFG_PIN_RXD              15
#endif

#ifndef CELLULAR_CFG_PIN_CTS
/** The NRF52840 GPIO input pin that the cellular modem will use to
 * indicate that data can be sent to it.
 * -1 is used where there is no such connection.
 */
# define CELLULAR_CFG_PIN_CTS              -1
#endif

#ifndef CELLULAR_CFG_PIN_RTS
/** The NRF52840 GPIO output pin that tells the cellular modem that it
 * can send more data to the NRF52840 UART.
 * -1 is used where there is no such connection.
 * If this is *not* -1 then be sure to set up
 * CELLULAR_CFG_RTS_THRESHOLD also.
 */
# define CELLULAR_CFG_PIN_RTS              -1
#endif

#endif // _CELLULAR_CFG_HW_H_

// End of file
