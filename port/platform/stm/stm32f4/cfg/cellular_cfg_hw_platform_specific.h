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
 * the u-blox C030-R412M board.
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS FOR STM32F4: UART/USART
 * -------------------------------------------------------------- */

/** The STM32F4 chip has 8 UARTs.  The UARTs that may be used
 * by the UART driver should be set to 1 here.
 */
#ifndef CELLULAR_CFG_UART1_AVAILABLE
# define CELLULAR_CFG_UART1_AVAILABLE  1
#endif

#ifndef CELLULAR_CFG_UART2_AVAILABLE
# define CELLULAR_CFG_UART2_AVAILABLE  0
#endif

#ifndef CELLULAR_CFG_UART3_AVAILABLE
# define CELLULAR_CFG_UART3_AVAILABLE  0
#endif

#ifndef CELLULAR_CFG_UART4_AVAILABLE
# define CELLULAR_CFG_UART4_AVAILABLE  0
#endif

#ifndef CELLULAR_CFG_UART5_AVAILABLE
# define CELLULAR_CFG_UART5_AVAILABLE  0
#endif

#ifndef CELLULAR_CFG_UART6_AVAILABLE
# define CELLULAR_CFG_UART6_AVAILABLE  0
#endif

#ifndef CELLULAR_CFG_UART7_AVAILABLE
# define CELLULAR_CFG_UART7_AVAILABLE  0
#endif

#ifndef CELLULAR_CFG_UART8_AVAILABLE
# define CELLULAR_CFG_UART8_AVAILABLE  0
#endif

/** For the UART driver to operate it needs a DMA
 * channel (0 to 7) on a DMA stream (0 to 7) on a
 * DMA engine (1 or 2) for each UART that it is
 * requested to use through the cellular port UART API.
 * Since cellularPortUartInit() API has no concept of DMA,
 * the engine/stream/channel to use for a given UART is
 * defined here.
 * IMPORTANT: make sure that a given engine/stream
 * combination is (a) only used once and (b) not
 * used anywhere else in your code.
 */
#ifndef CELLULAR_CFG_UART1_DMA_ENGINE
# define CELLULAR_CFG_UART1_DMA_ENGINE              1
#endif
#ifndef CELLULAR_CFG_UART1_DMA_STREAM
# define CELLULAR_CFG_UART1_DMA_STREAM              0
#endif
#ifndef CELLULAR_CFG_UART1_DMA_CHANNEL
# define CELLULAR_CFG_UART1_DMA_CHANNEL             0
#endif

#ifndef CELLULAR_CFG_UART2_DMA_ENGINE
# define CELLULAR_CFG_UART2_DMA_ENGINE              1
#endif
#ifndef CELLULAR_CFG_UART2_DMA_STREAM
# define CELLULAR_CFG_UART2_DMA_STREAM              1
#endif
#ifndef CELLULAR_CFG_UART2_DMA_CHANNEL
# define CELLULAR_CFG_UART2_DMA_CHANNEL             0
#endif

#ifndef CELLULAR_CFG_UART3_DMA_ENGINE
# define CELLULAR_CFG_UART3_DMA_ENGINE              1
#endif
#ifndef CELLULAR_CFG_UART3_DMA_STREAM
# define CELLULAR_CFG_UART3_DMA_STREAM              2
#endif
#ifndef CELLULAR_CFG_UART3_DMA_CHANNEL
# define CELLULAR_CFG_UART3_DMA_CHANNEL             0
#endif

#ifndef CELLULAR_CFG_UART4_DMA_ENGINE
# define CELLULAR_CFG_UART4_DMA_ENGINE              1
#endif
#ifndef CELLULAR_CFG_UART4_DMA_STREAM
# define CELLULAR_CFG_UART4_DMA_STREAM              3
#endif
#ifndef CELLULAR_CFG_UART4_DMA_CHANNEL
# define CELLULAR_CFG_UART4_DMA_CHANNEL             0
#endif

#ifndef CELLULAR_CFG_UART5_DMA_ENGINE
# define CELLULAR_CFG_UART5_DMA_ENGINE              1
#endif
#ifndef CELLULAR_CFG_UART5_DMA_STREAM
# define CELLULAR_CFG_UART5_DMA_STREAM              4
#endif
#ifndef CELLULAR_CFG_UART5_DMA_CHANNEL
# define CELLULAR_CFG_UART5_DMA_CHANNEL             0
#endif

#ifndef CELLULAR_CFG_UART6_DMA_ENGINE
# define CELLULAR_CFG_UART6_DMA_ENGINE              1
#endif
#ifndef CELLULAR_CFG_UART6_DMA_STREAM
# define CELLULAR_CFG_UART6_DMA_STREAM              5
#endif
#ifndef CELLULAR_CFG_UART6_DMA_CHANNEL
# define CELLULAR_CFG_UART6_DMA_CHANNEL             0
#endif

#ifndef CELLULAR_CFG_UART7_DMA_ENGINE
# define CELLULAR_CFG_UART7_DMA_ENGINE              1
#endif
#ifndef CELLULAR_CFG_UART7_DMA_STREAM
# define CELLULAR_CFG_UART7_DMA_STREAM              6
#endif
#ifndef CELLULAR_CFG_UART7_DMA_CHANNEL
# define CELLULAR_CFG_UART7_DMA_CHANNEL             0
#endif

#ifndef CELLULAR_CFG_UART8_DMA_ENGINE
# define CELLULAR_CFG_UART8_DMA_ENGINE              1
#endif
#ifndef CELLULAR_CFG_UART8_DMA_STREAM
# define CELLULAR_CFG_UART8_DMA_STREAM              7
#endif
#ifndef CELLULAR_CFG_UART8_DMA_CHANNEL
# define CELLULAR_CFG_UART8_DMA_CHANNEL             0
#endif

#ifndef CELLULAR_CFG_UART
/** The UART/USART to use, a number between 1 and 8,
 * though only the USARTs (1, 2, 3 and 6) are capable of HW flow
 * control.  If you change this number you will also need to
 * make sure that the corresponding CELLULAR_CFG_UARTx_AVAILABLE
 * #define above is not set to zero.
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

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS FOR STM32F4: PINS
 * -------------------------------------------------------------- */

/** Note: on STM32F4 the pin numbering has the bank number in the
 * upper nibble and the pin number in the lower nibble, so pin 15
 * is also known as PA_15 has value 0x0f and pin 16 is also known
 * as PB_0 and has value 0x10, etc.
 */

#ifndef CELLULAR_CFG_PIN_ENABLE_POWER
/** The STM32F4 GPIO output that enables power to the cellular module.
 * -1 because there is no such facility on a C030 board.
 */
# define CELLULAR_CFG_PIN_ENABLE_POWER      -1
#endif

#ifndef CELLULAR_CFG_PIN_PWR_ON
/** The STM32F4 GPIO output that that is connected to the PWR_ON pin of
 * the cellular module.
 */
# define CELLULAR_CFG_PIN_PWR_ON            0x4e // AKA PE_14
#endif

#ifndef CELLULAR_CFG_PIN_VINT
/** The STM32F4 GPIO input that is connected to the VInt pin of the
 * cellular module.
 * -1 is used where there is no such connection.
 */
# define CELLULAR_CFG_PIN_VINT              -1
#endif

#ifndef CELLULAR_CFG_PIN_TXD
/** The STM32F4 GPIO output pin that sends UART data to the cellular
 * module.
 */
# define CELLULAR_CFG_PIN_TXD               0x09 // AKA PA_9
#endif

#ifndef CELLULAR_CFG_PIN_RXD
/** The STM32F4 GPIO input pin that receives UART data from the cellular
 * module.
 */
# define CELLULAR_CFG_PIN_RXD               0x0a // AKA PA_10
#endif

#ifndef CELLULAR_CFG_PIN_CTS
/** The STM32F4 GPIO input pin that the cellular modem will use to
 * indicate that data can be sent to it.
 * -1 is used where there is no such connection.
 */
# define CELLULAR_CFG_PIN_CTS               0x0b // AKA PA_11
#endif

#ifndef CELLULAR_CFG_PIN_RTS
/** The STM32F4 GPIO output pin that tells the cellular modem that it
 * can send more data to the STM32F4 UART.
 * -1 is used where there is no such connection.
 * If this is *not* -1 then be sure to set up
 * CELLULAR_CFG_RTS_THRESHOLD also.
 */
# define CELLULAR_CFG_PIN_RTS               0x0c // AKA PA_12
#endif

#endif // _CELLULAR_CFG_HW_PLATFORM_SPECIFIC_H_

// End of file
