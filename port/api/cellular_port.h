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

#ifndef _CELLULAR_PORT_H_
#define _CELLULAR_PORT_H_

/** Common stuff for porting layer.
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/** Error codes.
 */
typedef enum {
    CELLULAR_PORT_SUCCESS = 0,
    CELLULAR_PORT_UNKNOWN_ERROR = -1,
    CELLULAR_PORT_NOT_INITIALISED = -2,
    CELLULAR_PORT_NOT_IMPLEMENTED = -3,
    CELLULAR_PORT_INVALID_PARAMETER = -4,
    CELLULAR_PORT_OUT_OF_MEMORY = -5,
    CELLULAR_PORT_TIMEOUT = -6,
    CELLULAR_PORT_PLATFORM_ERROR = -7
} CellularPortErrorCode;

/* ----------------------------------------------------------------
 * FUNCTIONS
 * -------------------------------------------------------------- */

/** Initialise the porting layer.
 *
 * @return zero on success else negative error code.
 */
int32_t cellularPortInit();

/** Deinitialise the porting layer.
 */
void cellularPortDeinit();

#endif // _CELLULAR_PORT_H_

// End of file
