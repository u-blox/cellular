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

#include "cellular_port_clib.h"
#include "cellular_port.h"

/* ----------------------------------------------------------------
 * EXTERNED FUNCTIONS
 * -------------------------------------------------------------- */

// Pull in any private functions shared between the porting .c files
// here.

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

// Initialise the porting layer.
int32_t cellularPortInit()
{
    // Nothing to do
    return CELLULAR_PORT_SUCCESS;
}

// Deinitialise the porting layer.
void cellularPortDeinit()
{
    // Nothing to do
}

// End of file
