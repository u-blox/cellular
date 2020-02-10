/*
 * Copyright (C) u-blox Cambourne Ltd
 * u-blox Cambourne Ltd, Cambourne, UK
 *
 * All rights reserved.
 *
 * This source file is the sole property of u-blox Cambourne Ltd.
 * Reproduction or utilisation of this source in whole or part is
 * forbidden without the written consent of u-blox Cambourne Ltd.
 */

#ifndef _CELLULAR_PORT_DEBUG_H_
#define _CELLULAR_PORT_DEBUG_H_

/** Porting layer for debug functions.
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/** Define this to enable printing out of the serial port.
 */
#if defined(CONFIG_CELLULAR_ENABLE_LOGGING) && CONFIG_CELLULAR_ENABLE_LOGGING
# define cellularPortLog(format, ...) printf(format, ##__VA_ARGS__)
#else
# define cellularPortLog(...)
#endif

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * FUNCTIONS
 * -------------------------------------------------------------- */

#endif // _CELLULAR_PORT_DEBUG_H_

// End of file
