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

#ifndef _CELLULAR_CFG_HW_PLATFORM_SPECIFIC_H_
#define _CELLULAR_CFG_HW_PLATFORM_SPECIFIC_H_

/* No #includes allowed here */

/* This header file contains hardware configuration information for
 * a u-blox C030 board.  Note that "UART" is used throughout this code,
 * rather than switching between "UART" and "USART" and having to remember
 * which number UART/USART is which.
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS FOR STM32F4: UART/USART
 * -------------------------------------------------------------- */

#ifndef CELLULAR_CFG_UART
/** The UART/USART to use, a number between 1 and 8,
 * though only the USARTs (1, 2, 3 and 6) are capable of HW flow
 * control.  If you change this number you will also need to
 * make sure that the corresponding CELLULAR_CFG_UARTx_AVAILABLE
 * #define below is not set to zero.
 * For a C030-R412M board this HAS to be 1 as that's the UART
 * HW block that is wired INSIDE the STM32F437VG chip to
 * the correct alternate functions for the Tx/Rx/RTS/CTS pins
 * below (see table 12 of the STM32F437VG data sheet).
 * For a C030-U201 board this HAS to be 2 for the same reasons.
 */
# define CELLULAR_CFG_UART                           1
#endif

#ifndef CELLULAR_CFG_RTS_THRESHOLD
/** The buffer threshold at which RTS is de-asserted, indicating the
 * cellular module should stop sending data to us.  Must be defined
 * if CELLULAR_CFG_PIN_RTS is not -1.
 */
# define CELLULAR_CFG_RTS_THRESHOLD                  100
#endif

/** The STM32F4 chip has 8 UARTs.  The UARTs that may be used
 * by the UART driver should be set to 1 here.
 * Note that in the STM32F4 chip the UART to pin relationship
 * is pretty much fixed, you can't just chose any UART and
 * expect it to connect to your chosen pins.  You have
 * to look in the STM32F4 data sheet for your particular
 * flavour of STM32F4 (e.g. table 12 in the STM32F437 datasheet)
 * to determine what connects to what.
 */
#ifndef CELLULAR_CFG_UART1_AVAILABLE
/** Whether USART1 is available to the UART driver or not
 * This USART must be available for the C030-R412M board
 * since that's the way the pins are connected.
 */
# define CELLULAR_CFG_UART1_AVAILABLE  1
#endif

#ifndef CELLULAR_CFG_UART2_AVAILABLE
/** Whether USART2 is available to the UART driver or not
 * This USART must be available for the C030-U201 board
 * since that's the way the pins are connected.
 */
# define CELLULAR_CFG_UART2_AVAILABLE  0
#endif

#ifndef CELLULAR_CFG_UART3_AVAILABLE
/** Whether USART3 is available to the UART driver or not.
 * This is set to 1 because the unit tests for the UART
 * driver on this platform use UART3
 * (see cellular_port_test_platform_specific.h), which comes
 * out of the D0/D1/D2/D3 pins of the Arduino connector
 * on a C030 board.  If you are not going to run
 * the unit tests you can set this to 0.
 */
# define CELLULAR_CFG_UART3_AVAILABLE  0
#endif

#ifndef CELLULAR_CFG_UART4_AVAILABLE
/** Whether UART4 is available to the UART driver or not.
 */
# define CELLULAR_CFG_UART4_AVAILABLE  0
#endif

#ifndef CELLULAR_CFG_UART5_AVAILABLE
/** Whether UART5 is available to the UART driver or not.
 */
# define CELLULAR_CFG_UART5_AVAILABLE  0
#endif

#ifndef CELLULAR_CFG_UART6_AVAILABLE
/** Whether USART6 is available to the UART driver or not.
 */
# define CELLULAR_CFG_UART6_AVAILABLE  0
#endif

#ifndef CELLULAR_CFG_UART7_AVAILABLE
/** Whether UART7 is available to the UART driver or not.
 */
# define CELLULAR_CFG_UART7_AVAILABLE  0
#endif

#ifndef CELLULAR_CFG_UART8_AVAILABLE
/** Whether UART8 is available to the UART driver or not.
 */
# define CELLULAR_CFG_UART8_AVAILABLE  0
#endif

/** For the UART driver to operate it needs a DMA
 * channel (0 to 7) on a DMA stream (0 to 7) on a
 * DMA engine (1 or 2) for each UART that it is
 * requested to use through the cellular port UART API.
 *
 * The choice of DMA engine/stream/channel for
 * a given peripheral is fixed in the STM32F4 chip,
 * see table 42 of their RM0090 document.  It is the
 * fixed mapping of engine/stream/channel to
 * UART/USART RX which is represented below.
 */

/** The DMA engine for USART1 Rx: fixed in the
 * STM32F4 chip.
 */
#define CELLULAR_CFG_UART1_DMA_ENGINE              2
#ifndef CELLULAR_CFG_UART1_DMA_STREAM
/** The DMA stream for USART1 Rx: can also be set to 5
 * (with the same DMA engine/channel).
 */
# define CELLULAR_CFG_UART1_DMA_STREAM             2
#endif

/** The DMA channel for USART1 Rx: fixed in the
 * STM32F4 chip.
 */
#define CELLULAR_CFG_UART1_DMA_CHANNEL             4

/** The DMA engine for USART2 Rx: fixed in the
 * STM32F4 chip.
 */
#define CELLULAR_CFG_UART2_DMA_ENGINE              1

/** The DMA stream for USART2 Rx: fixed in the
 * STM32F4 chip.
 */
#define CELLULAR_CFG_UART2_DMA_STREAM              5
/** The DMA channel for USART2 Rx: fixed in the
 * STM32F4 chip.
 */
#define CELLULAR_CFG_UART2_DMA_CHANNEL             4

/** The DMA engine for USART3 Rx: fixed in the
 * STM32F4 chip.
 */
#define CELLULAR_CFG_UART3_DMA_ENGINE              1

/** The DMA stream for USART3 Rx: fixed in the
 * STM32F4 chip.
 */
#define CELLULAR_CFG_UART3_DMA_STREAM              1

/** The DMA channel for USART3 Rx: fixed in the
 * STM32F4 chip.
 */
#define CELLULAR_CFG_UART3_DMA_CHANNEL             4

/** The DMA engine for UART4 Rx: fixed in the
 * STM32F4 chip.
 */
#define CELLULAR_CFG_UART4_DMA_ENGINE              1

/** The DMA stream for UART4 Rx: fixed in the
 * STM32F4 chip.
 */
#define CELLULAR_CFG_UART4_DMA_STREAM              2

/** The DMA channel for UART4 Rx: fixed in the
 * STM32F4 chip.
 */
#define CELLULAR_CFG_UART4_DMA_CHANNEL             4

/** The DMA engine for UART5 Rx: fixed in the
 * STM32F4 chip.
 */
#define CELLULAR_CFG_UART5_DMA_ENGINE              1

/** The DMA stream for UART5 Rx: fixed in the
 * STM32F4 chip.
 */
#define CELLULAR_CFG_UART5_DMA_STREAM              0

/** The DMA channel for UART5 Rx: fixed in the
 * STM32F4 chip.
 */
#define CELLULAR_CFG_UART5_DMA_CHANNEL             4

/** The DMA engine for USART6 Rx: fixed in the
 * STM32F4 chip.
 */
#define CELLULAR_CFG_UART6_DMA_ENGINE              2

#ifndef CELLULAR_CFG_UART6_DMA_STREAM
/** The DMA stream for USART6 Rx: can also be set to 2
 * (with the same DMA engine/channel).
 */
# define CELLULAR_CFG_UART6_DMA_STREAM             1
#endif

/** The DMA channel for USART6 Rx: fixed in the
 * STM32F4 chip.
 */
#define CELLULAR_CFG_UART6_DMA_CHANNEL             5

/** The DMA engine for UART7 Rx: fixed in the
 * STM32F4 chip.
 */
#define CELLULAR_CFG_UART7_DMA_ENGINE              1

/** The DMA stream for UART7 Rx: fixed in the
 * STM32F4 chip.
 */
#define CELLULAR_CFG_UART7_DMA_STREAM              3

/** The DMA channel for UART7 Rx: fixed in the
 * STM32F4 chip.
 */
#define CELLULAR_CFG_UART7_DMA_CHANNEL             5

/** The DMA engine for UART8 Rx: fixed in the
 * STM32F4 chip.
 */
#define CELLULAR_CFG_UART8_DMA_ENGINE              1

/** The DMA stream for UART8 Rx: fixed in the
 * STM32F4 chip.
 */
#define CELLULAR_CFG_UART8_DMA_STREAM              6

/** The DMA channel for UART8 Rx: fixed in the
 * STM32F4 chip.
 */
#define CELLULAR_CFG_UART8_DMA_CHANNEL             5

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS FOR STM32F4: PINS
 * -------------------------------------------------------------- */

/** Note: on STM32F4 the pin numbering has the bank number in the
 * upper nibble and the pin number in the lower nibble, so pin 15
 * is also known as PA_15 has value 0x0f and pin 16 is also known
 * as PB_0 and has value 0x10, etc.
 */

#ifndef CELLULAR_CFG_PIN_C030_ENABLE_3V3
/** On the u-blox C030 boards it is necessary to enable
 * power to the 3.3V rail of the Arduino interface
 * by setting this pin to an open drain output and to high.
 * If you are not running on a u-blox C030 board then
 * override this pin to -1.
 */
# define CELLULAR_CFG_PIN_C030_ENABLE_3V3   0x40 // AKA PE_0
#endif

#ifndef CELLULAR_CFG_PIN_RESET
/** On the u-blox C030 boards the modem reset pin
 * is connected to the STM32F4 on this pin.
 * If you are not running on a u-blox C030 board then
 * override this pin to -1.
 */
# define CELLULAR_CFG_PIN_RESET             0x15 // AKA PB_5
#endif

#ifndef CELLULAR_CFG_PIN_ENABLE_POWER
/** The STM32F4 GPIO output that enables power to the cellular module.
 * -1 because there is no such facility on a C030 board.
 * On the u-blox C030 boards no such facility is present.
 */
# define CELLULAR_CFG_PIN_ENABLE_POWER      -1
#endif

#ifndef CELLULAR_CFG_PIN_PWR_ON
/** The STM32F4 GPIO output that that is connected to the PWR_ON pin of
 * the cellular module.
 * For the u-blox C030 boards this is 0x4e, AKA PE_14.
 */
# define CELLULAR_CFG_PIN_PWR_ON            0x4e // AKA PE_14
#endif

#ifndef CELLULAR_CFG_PIN_VINT
/** The STM32F4 GPIO input that is connected to the VInt pin of the
 * cellular module.
 * -1 is used where there is no such connection.
 * For the u-blox C030 boards this is not connected.
 */
# define CELLULAR_CFG_PIN_VINT              -1
#endif

#ifndef CELLULAR_CFG_PIN_TXD
/** The STM32F4 GPIO output pin that sends UART data to the cellular
 * module.
 * For the u-blox C030-R412M board this must be 0x09, AKA PA_9.
 * For the u-blox C030-U201 board this must be 0x39, AKA PD_5.
 */
# define CELLULAR_CFG_PIN_TXD               0x09 // AKA PA_9
#endif

#ifndef CELLULAR_CFG_PIN_RXD
/** The STM32F4 GPIO input pin that receives UART data from the cellular
 * module.
 * For the u-blox C030-R412M board this must be 0x0a, AKA PA_10.
 * For the u-blox C030-U201 board this must be 0x3a, AKA PD_6.
 */
# define CELLULAR_CFG_PIN_RXD               0x0a // AKA PA_10
#endif

#ifndef CELLULAR_CFG_PIN_CTS
/** The STM32F4 GPIO input pin that the cellular modem will use to
 * indicate that data can be sent to it.
 * -1 is used where there is no such connection.
 * For the u-blox C030-R412M board this must be 0x0b, AKA PA_11.
 * For the u-blox C030-U201 board this must be 0x33, AKA PD_3.
 */
# define CELLULAR_CFG_PIN_CTS               0x0b // AKA PA_11
#endif

#ifndef CELLULAR_CFG_PIN_RTS
/** The STM32F4 GPIO output pin that tells the cellular modem that it
 * can send more data to the STM32F4 UART.
 * -1 is used where there is no such connection.
 * If this is *not* -1 then be sure to set up
 * CELLULAR_CFG_RTS_THRESHOLD also.
 * For the u-blox C030-R412M board this must be 0x0c, AKA PA_12.
 * For the u-blox C030-U201 board this must be 0x34, AKA PD_4.
 */
# define CELLULAR_CFG_PIN_RTS               0x0c // AKA PA_12
#endif

#endif // _CELLULAR_CFG_HW_PLATFORM_SPECIFIC_H_

// End of file
