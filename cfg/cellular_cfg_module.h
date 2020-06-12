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

#ifndef _CELLULAR_CFG_MODULE_H_
#define _CELLULAR_CFG_MODULE_H_

/* No #includes allowed here */

/* This header file contains configuration information for all the
 * possible cellular module types.
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACRO CROSS CHECKING
 * -------------------------------------------------------------- */

/* #define cross-checking.
 */
#if defined(CELLULAR_CFG_MODULE_SARA_R4) && defined(CELLULAR_CFG_MODULE_SARA_R5)
# error More than one module type defined.
#endif

#if !defined(CELLULAR_CFG_MODULE_SARA_R4) && !defined(CELLULAR_CFG_MODULE_SARA_R5)
# error Must define a module type (e.g. CELLULAR_CFG_MODULE_SARA_R4 or CELLULAR_CFG_MODULE_SARA_R5).
#endif

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS COMMON TO ALL MODULES
 * -------------------------------------------------------------- */

#ifndef CELLULAR_CFG_BAUD_RATE
/** The baud rate to use on the UART interface
 */
# define CELLULAR_CFG_BAUD_RATE                      115200
#endif

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS FOR SARA-R5
 * -------------------------------------------------------------- */

#ifdef CELLULAR_CFG_MODULE_SARA_R5

/** The time within which an AT command should complete.
 */
# define CELLULAR_CTRL_COMMAND_TIMEOUT_MS 8000

/** The delay between AT commands, allowing internal cellular module
 * comms to complete before another is sent.
 */
# define CELLULAR_CTRL_COMMAND_DELAY_MS 20

/** The minimum reponse time one can expect with cellular module.
 * This is quite large since, if there is a URC about to come through,
 * it can delay what are normally immediate responses.
 */
# define CELLULAR_CTRL_COMMAND_MINIMUM_RESPONSE_TIME_MS 5000

/** The time to wait before the cellular module is ready at boot.
 */
# define CELLULAR_CTRL_BOOT_WAIT_TIME_MS 3000

/** The time to wait for an organised power off.
 * This should be used if the VInt pin is not connected:
 * lack of a response on the AT interface is NOT sufficient.
 */
# define CELLULAR_CTRL_POWER_DOWN_WAIT_SECONDS 20

/** The time to wait for an OK when the module is asked to re-boot.
 */
# define CELLULAR_CTRL_REBOOT_COMMAND_WAIT_TIME_MS 15000

/** The maximum number of simultaneous radio access technologies
 *  supported by the cellular module.
 */
# define CELLULAR_CTRL_MAX_NUM_SIMULTANEOUS_RATS 1

/** Supported RATs: a bitmap of the _CELLULAR_CTRL_RAT_BIT_x values
 * defined in cellular_ctrl.h, for this case
 * _CELLULAR_CTRL_RAT_BIT_CATM1 = 0x08.
 */
#define CELLULAR_CTRL_SUPPORTED_RATS_BITMAP 0x08UL

#endif // CELLULAR_CFG_MODULE_SARA_R5

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS FOR SARA-R4
 * -------------------------------------------------------------- */

#ifdef CELLULAR_CFG_MODULE_SARA_R4

/** The time within which an AT command should complete.
 */
# define CELLULAR_CTRL_COMMAND_TIMEOUT_MS 80000

/** The delay between AT commands, allowing internal cellular module
 * comms to complete before another is sent.
 */
# define CELLULAR_CTRL_COMMAND_DELAY_MS 100

/** The minimum reponse time one can expect with cellular module.
 * This is quite large since, if there is a URC about to come through,
 * it can delay what are normally immediate responses.
 */
# define CELLULAR_CTRL_COMMAND_MINIMUM_RESPONSE_TIME_MS 2000

/** The time to wait before the cellular module is ready at boot.
 */
# define CELLULAR_CTRL_BOOT_WAIT_TIME_MS 5000

/** The time to wait for an organised power off.
 * This should be used if the VInt pin is not connected:
 * lack of a response on the AT interface is NOT sufficient.
 */
# define CELLULAR_CTRL_POWER_DOWN_WAIT_SECONDS 35

/** The time to wait for an OK when the module is asked to re-boot.
 */
# define CELLULAR_CTRL_REBOOT_COMMAND_WAIT_TIME_MS CELLULAR_CTRL_COMMAND_TIMEOUT_MS

/** The maximum number of simultaneous radio access technologies
 *  supported by the cellular module.
 */
# define CELLULAR_CTRL_MAX_NUM_SIMULTANEOUS_RATS 3

/** Supported RATs: a bitmap of the _CELLULAR_CTRL_RAT_BIT_x values
 * defined in cellular_ctrl.h, for this case
 * _CELLULAR_CTRL_RAT_BIT_GPRS | _CELLULAR_CTRL_RAT_BIT_CATM1 |
 * _CELLULAR_CTRL_RAT_BIT_NB1 = 0x19.
 */
#define CELLULAR_CTRL_SUPPORTED_RATS_BITMAP 0x19UL

#endif // CELLULAR_CFG_MODULE_SARA_R4

#endif // _CELLULAR_CFG_MODULE_H_

// End of file
