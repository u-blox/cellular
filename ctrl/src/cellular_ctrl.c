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
#include "cellular_port_debug.h"
#include "cellular_port_os.h"
#include "cellular_port_gpio.h"
#include "cellular_port_uart.h"
#include "cellular_ctrl_at.h"
#include "cellular_ctrl_apn_db.h"
#include "cellular_ctrl.h"

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/** The number of times to poke the module to confirm that
 * she's powered-on.
 */
#define CELLULAR_CTRL_IS_ALIVE_ATTEMPTS_POWER_ON 10

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/** A struct defining a callback plus its optional parameter.
 */
typedef struct {
    void (*pFunction)(void  *);
    void *pParam;
} CellularCtrlCallback;

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

/** Keep track of whether we've been initialised or not.
 */
static bool gInitialised = false;

/** The GPIO pin to the 3V3 supply on to the cellular module.
 */
static int32_t gPinEnablePower;

/** The GPIO pin to be used to signal power on to the cellular
 * module, i.e. the pin that is connected to the CP_ON pin of the
 * module.
 */
static int32_t gPinCpOn;

/** The GPIO pin to the VInt output on the cellular module.
 */
static int32_t gPinVInt;

/** The number of consecutive timeouts on the AT interface.
 */
static int32_t gAtNumConsecutiveTimeouts;

/** The current registration status.
 */
static CellularCtrlNetworkStatus gNetworkStatus;

/** The RSSI of the serving cell.
 */
static int32_t gRssiDbm;

/** The RSRP of the serving cell.
 */
static int32_t gRsrpDbm;

/** The RSRQ of the serving cell.
 */
static int32_t gRsrqDbm;

/** The RxQual of the serving cell.
 */
static int32_t gRxQual;

/** The ID of the serving cell.
 */
static int32_t gCellId;

/** The EARFCN of the serving cell.
 */
static int32_t gEarfcn;

/** Table to convert the 3GPP registration status from a
 * +CEREG URC to CellularCtrlNetworkStatus.
 */
static const CellularCtrlNetworkStatus gStatus3gppToCellularNetworkStatus[] =
    {CELLULAR_CTRL_NETWORK_STATUS_SEARCHING,           // 0:  searching
     CELLULAR_CTRL_NETWORK_STATUS_REGISTERED,          // 1:  registered on the home network
     CELLULAR_CTRL_NETWORK_STATUS_SEARCHING,           // 2:  searching
     CELLULAR_CTRL_NETWORK_STATUS_REGISTRATION_DENIED, // 3:  registration denied
     CELLULAR_CTRL_NETWORK_STATUS_OUT_OF_COVERAGE,     // 4:  out of coverage
     CELLULAR_CTRL_NETWORK_STATUS_REGISTERED,          // 5:  registered on a roaming network
     CELLULAR_CTRL_NETWORK_STATUS_NOT_REGISTERED,      // 6:  registered for SMS only on the home network
     CELLULAR_CTRL_NETWORK_STATUS_NOT_REGISTERED,      // 7:  registered for SMS only on a roaming network
     CELLULAR_CTRL_NETWORK_STATUS_EMERGENCY_ONLY,      // 8:  emergency service only
     CELLULAR_CTRL_NETWORK_STATUS_REGISTERED,          // 9:  registered for circuit switched fall-back on the home network
     CELLULAR_CTRL_NETWORK_STATUS_REGISTERED};         // 10: registered for circuit switched fall-back on the home network

/** Table to convert CellularCtrlRat to the value used in the module.
 */
static const uint8_t gCellularRatToLocalRat[] =
    {255, // dummy value for CELLULAR_CTRL_RAT_UNKNOWN
     7,   // CELLULAR_CTRL_RAT_CATM1
     8};  // CELLULAR_CTRL_RAT_NB1

/** Table to convert the RAT values used in the module to
 * CellularCtrlRat.
 */
static const CellularCtrlRat gLocalRatToCellularRat[] =
    {CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED,  // GSM / GPRS / eGPRS (single mode)
     CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED,  // GSM / UMTS (dual mode)
     CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED,  // UMTS (single mode)
     CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED,  // LTE (single mode)
     CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED,  // GSM / UMTS / LTE (tri mode)
     CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED,  // GSM / LTE (dual mode)
     CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED,  // UMTS / LTE (dual mode)
     CELLULAR_CTRL_RAT_CATM1,                // LTE cat.M1
     CELLULAR_CTRL_RAT_NB1,                  // LTE cat.NB1
     CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED}; // GPRS / eGPRS

/** Array to convert the LTE RSSI number from AT+CSQ into a
 * dBm value rounded up to the nearest whole number.
 */
static const int32_t gRssiConvertLte[] = {-118, -115, -113, -110, -108, -105, -103, -100,  /* 0 - 7   */
                                          -98,  -95,  -93,  -90,  -88,  -85,  -83,  -80,   /* 8 - 15  */
                                          -78,  -76,  -74,  -73,  -71,  -69,  -68,  -65,   /* 16 - 23 */
                                          -63,  -61,  -60,  -59,  -58,  -55,  -53,  -48};  /* 24 - 31 */

/** Array to convert the RAT emited by AT+COPS to one of our RATs.
 */
static const CellularCtrlRat gCopsRatToCellularRat[] = {CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED,
                                                        CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED,
                                                        CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED,
                                                        CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED,
                                                        CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED,
                                                        CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED,
                                                        CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED,
                                                        CELLULAR_CTRL_RAT_CATM1, // 7 is CATM1
                                                        CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED,
                                                        CELLULAR_CTRL_RAT_NB1};  // 9 is NB1

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: URCS AND RELATED FUNCTIONS
 * -------------------------------------------------------------- */

// Set the current network status.
// Deliberately using VERY short debug strings as this
// might be called from a URC.
static void setNetworkStatus(int32_t status)
{
    switch (status) {
        case 0:
        case 2:
            // Not (yet) registered
            cellularPortLog("NReg\n");
        break;
        case 1:
            // Registered on the home network
            cellularPortLog("RegH\n");
        break;
        case 3:
            // Registeration denied
            cellularPortLog("Deny\n");
        break;
        case 4:
            // Out of coverage
            cellularPortLog("OoC\n");
        break;
        case 5:
            // Registered on a roaming network
            cellularPortLog("RegR\n");
        break;
        case 6:
            // Registered for SMS only on the home network
            cellularPortLog("RegS\n");
        break;
        case 7:
            // Registered for SMS only on a roaming network
            cellularPortLog("RegS\n");
        break;
        case 8:
            // Registered for emergency service only
            cellularPortLog("RegE\n");
        break;
        case 9:
            // Registered for circuit switched fall-back on the home network
            cellularPortLog("RegC\n");
        break;
        case 10:
            // Registered for circuit switched fall-back on a roaming network
            cellularPortLog("RegC\n");
        break;
        default:
            // Unknown registration status
            cellularPortLog("Unk %d\n", status);
        break;
    }

    if ((status >= 0) && (status < sizeof(gStatus3gppToCellularNetworkStatus) /
                                   sizeof(gStatus3gppToCellularNetworkStatus[0]))) {
        gNetworkStatus = gStatus3gppToCellularNetworkStatus[status];
    }
}

// Registration on cat-M1/NBIoT (AT+CEREG).
static void CEREG_urc(void *pUnused)
{
    int32_t status;

    (void) pUnused;

    // Read <stat>
    status = cellular_ctrl_at_read_int();
    if (status >= 0) {
        setNetworkStatus(status);
    }
}

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: MISC
 * -------------------------------------------------------------- */

// Strip non-printable characters from an ASCII string (not very efficiently).
// stringLength is the length that strlen() would return, i.e. not including
// any final terminator.
static int32_t strip_ctrl(char *pString, int32_t stringLength)
{
    size_t numCharsRemoved = 0;

    for (size_t x = 0; x < stringLength; x++) {
        if (cellularPort_isctrl((int32_t) *(pString + x - numCharsRemoved))) {
            for (size_t y = x - numCharsRemoved; y < stringLength - numCharsRemoved - 1; y++) {
                *(pString + y) = *(pString + y + 1);
            }
            numCharsRemoved++;
        }
    }

    if (numCharsRemoved > 0) {
        *(pString + stringLength - numCharsRemoved) = 0;
    }

    return numCharsRemoved;
}

// Callback function to detect the cellular module becoming
// unresponsive.
static void atTimeoutCallback(void *pNumConsecutiveTimeouts)
{
    gAtNumConsecutiveTimeouts = *((int32_t *) pNumConsecutiveTimeouts);
}

// Set the radio parameters back to defaults.
static void clearRadioParameters()
{
    gRssiDbm = 0;
    gRsrpDbm = 0;
    gRsrqDbm = 0;
    gCellId = -1;
    gEarfcn = -1;
}

// Check that the cellular module is alive.
static CellularCtrlErrorCode moduleIsAlive(int32_t attempts)
{
    CellularCtrlErrorCode errorCode = CELLULAR_CTRL_NOT_RESPONDING;
    bool cellularIsAlive = false;

    // See if the cellular module is responding at the AT interface
    // by poking it with "AT" up to "attempts" times,
    // waiting 1 second for an "OK" response each
    // time
    for (uint32_t x = 0; !cellularIsAlive && (x < attempts); x++) {
        cellular_ctrl_at_lock();
        cellular_ctrl_at_set_at_timeout(CELLULAR_CTRL_COMMAND_MINIMUM_RESPONSE_TIME_MS, false);
        cellular_ctrl_at_cmd_start("AT");
        cellular_ctrl_at_cmd_stop_read_resp();
        cellularIsAlive = (cellular_ctrl_at_get_last_error() == 0);
        cellular_ctrl_at_clear_error();
        cellular_ctrl_at_restore_at_timeout();
        cellular_ctrl_at_unlock();
    }

    if (cellularIsAlive) {
        errorCode = CELLULAR_CTRL_SUCCESS;
    }

    return errorCode;
}

// Configure the cellular module.
static CellularCtrlErrorCode moduleConfigure()
{
    CellularCtrlErrorCode errorCode = CELLULAR_CTRL_NOT_CONFIGURED;

    // Configure the module
    cellular_ctrl_at_lock();
    cellular_ctrl_at_cmd_start("ATE0"); // Echo off
    cellular_ctrl_at_cmd_stop_read_resp();
    cellular_ctrl_at_cmd_start("AT+CMEE=2"); // Extended errors on
    cellular_ctrl_at_cmd_stop_read_resp();
    cellular_ctrl_at_cmd_start("AT&K0"); // RTS/CTS handshaking off
    cellular_ctrl_at_cmd_stop_read_resp();
    cellular_ctrl_at_cmd_start("AT&C1"); // DCD circuit (109) changes in accordance with the carrier
    cellular_ctrl_at_cmd_stop_read_resp();
    cellular_ctrl_at_cmd_start("AT&D0"); // Ignore changes to DTR
    cellular_ctrl_at_cmd_stop_read_resp();
    cellular_ctrl_at_cmd_start("AT+UCGED=5"); // Switch on channel and environment reporting for EUTRAN
    cellular_ctrl_at_cmd_stop_read_resp();
    cellular_ctrl_at_cmd_start("AT+CFUN=4"); // Stay in airplane mode until commanded to connect,
    cellular_ctrl_at_cmd_stop_read_resp();
    if (cellular_ctrl_at_unlock_return_error() == 0) {
        errorCode = CELLULAR_CTRL_SUCCESS;
    }

    return errorCode;
}

// Get an ID string from the cellular module.
static int32_t getString(const char *pCmd, char *pBuffer, size_t bufferSize)
{
    CellularCtrlErrorCode errorCodeOrSize = CELLULAR_CTRL_NOT_INITIALISED;
    int32_t bytesRead;
    int32_t atError;

    if (gInitialised) {
        errorCodeOrSize = CELLULAR_CTRL_INVALID_PARAMETER;
        if (pBuffer != NULL) {
            errorCodeOrSize = CELLULAR_CTRL_AT_ERROR;
            cellular_ctrl_at_lock();
            cellular_ctrl_at_cmd_start(pCmd);
            cellular_ctrl_at_cmd_stop();
            cellular_ctrl_at_resp_start(NULL, false);
            // Don't want characters in the string being interpreted
            // as delimiters
            cellular_ctrl_at_set_delimiter(0);
            bytesRead = cellular_ctrl_at_read_string(pBuffer, bufferSize, false);
            cellular_ctrl_at_resp_stop();
            cellular_ctrl_at_set_default_delimiter();
            atError = cellular_ctrl_at_unlock_return_error();
            if ((bytesRead >= 0) && (atError == 0)) {
                // If it is fully formed (i.e. the provided buffer was long
                // enough to hold it all) the string will have \r\n\r\n on
                // the end, remove that here
                errorCodeOrSize = bytesRead - strip_ctrl(pBuffer, cellularPort_strlen(pBuffer));
                cellularPortLog("CELLULAR_CTRL: ID string, length %d character(s), returned by %s is \"%s\".\n",
                                errorCodeOrSize, pCmd, pBuffer);
            } else {
                cellularPortLog("CELLULAR_CTRL: unable to read ID string using %s.\n", pCmd);
            }
        }
    }

    return (int32_t) errorCodeOrSize;
}

// Prepare for connection with the network.
static bool prepareConnect()
{
    bool success = false;
    int32_t status;

    cellularPortLog("CELLULAR_CTRL: preparing to connect...\n");
    // Make sure URC handler is registered
    cellular_ctrl_at_set_urc_handler("CEREG:", CEREG_urc, NULL);

    // Switch on the unsolicited result codes for registration
    // on 2G and cat-M1/NB1
    cellular_ctrl_at_lock();
    cellular_ctrl_at_cmd_start("AT+CREG=1");
    cellular_ctrl_at_cmd_stop_read_resp();
    cellular_ctrl_at_cmd_start("AT+CGREG=1");
    cellular_ctrl_at_cmd_stop_read_resp();
    cellular_ctrl_at_cmd_start("AT+CEREG=1");
    cellular_ctrl_at_cmd_stop_read_resp();
    if (cellular_ctrl_at_unlock_return_error() == 0) {
        cellular_ctrl_at_lock();
        // See if we are already in automatic mode
        cellular_ctrl_at_cmd_start("AT+COPS?");
        cellular_ctrl_at_cmd_stop();
        cellular_ctrl_at_resp_start("+COPS:", false);
        status = cellular_ctrl_at_read_int();
        cellular_ctrl_at_resp_stop();
        if (status != 0) {
            // If we aren't, set it
            cellular_ctrl_at_cmd_start("AT+COPS=0");
            cellular_ctrl_at_cmd_stop_read_resp();
        }
        if (cellular_ctrl_at_unlock_return_error() == 0) {
            success = true;
        }
    } else {
        cellularPortLog("CELLULAR_CTRL: unable to set URCs and automatic network selection mode.\n");
    }

    return success;
}

// Register with the cellular network and obtain a PDP context.
static CellularCtrlErrorCode tryConnect(bool (*pKeepGoingCallback) (void),
                                        const char *pApn,
                                        const char *pUsername,
                                        const char *pPassword)
{
    CellularCtrlErrorCode errorCode = CELLULAR_CTRL_AT_ERROR;
    bool keepGoing = true;
    bool attached = false;
    bool activated = false;
    int32_t status;
    char buffer[64];

    if (pKeepGoingCallback()) {
        // Set up context definition
        cellular_ctrl_at_lock();
        cellular_ctrl_at_cmd_start("AT+CGDCONT=");
        cellular_ctrl_at_write_int(CELLULAR_CTRL_CONTEXT_ID);
        cellular_ctrl_at_write_string("IP", true);
        if (pApn != NULL) {
            cellular_ctrl_at_write_string(pApn, true);
        }
        cellular_ctrl_at_cmd_stop_read_resp();
        if (cellular_ctrl_at_unlock_return_error() != 0) {
            cellularPortLog("CELLULAR_CTRL: unable to define context %d.\n",
                            CELLULAR_CTRL_CONTEXT_ID);
            keepGoing = false;
        }
    }

    // Set up authentication mode, if required
    if (keepGoing && pKeepGoingCallback() &&
        (pUsername != NULL) && (pPassword != NULL)) {
        cellular_ctrl_at_lock();
        cellular_ctrl_at_cmd_start("AT+UAUTHREQ=");
        cellular_ctrl_at_write_int(CELLULAR_CTRL_CONTEXT_ID);
        cellular_ctrl_at_write_int(3); // Automatic choice of authentication type
        cellular_ctrl_at_write_string(pPassword, true);
        cellular_ctrl_at_write_string(pUsername, true);
        cellular_ctrl_at_cmd_stop_read_resp();
        if (cellular_ctrl_at_unlock_return_error() != 0) {
            cellularPortLog("CELLULAR_CTRL: unable to authenticate with user name \"%s\".\n", pUsername);
            keepGoing = false;
        }
    }

    // Now come out of airplane mode and try to register
    cellular_ctrl_at_lock();
    cellular_ctrl_at_cmd_start("AT+CFUN=1");
    cellular_ctrl_at_cmd_stop_read_resp();
    cellular_ctrl_at_unlock();
    // Wait for registration to succeed
    errorCode = CELLULAR_CTRL_NOT_REGISTERED;
    while (keepGoing && pKeepGoingCallback() && (cellularGetRegisteredRan() < 0)) {
        // Prod the modem anyway, we've nout much else to do
        cellular_ctrl_at_lock();
        cellular_ctrl_at_set_at_timeout(CELLULAR_CTRL_COMMAND_MINIMUM_RESPONSE_TIME_MS, false);
        cellular_ctrl_at_cmd_start("AT+CEREG?");
        cellular_ctrl_at_cmd_stop();
        cellular_ctrl_at_resp_start("+CEREG:", false);
        // Ignore the first parameter
        cellular_ctrl_at_read_int();
        status = cellular_ctrl_at_read_int();
        if (status >= 0) {
            setNetworkStatus(status);
        }
        cellular_ctrl_at_resp_stop();
        cellular_ctrl_at_restore_at_timeout();
        if (cellular_ctrl_at_unlock_return_error() != 0) {
            keepGoing = false;
        } else {
            cellularPortTaskBlock(1000);
        }
    }

    if (keepGoing && pKeepGoingCallback()) {
        if (cellularGetRegisteredRan() >= 0) {
            if (cellularGetOperatorStr(buffer, sizeof(buffer)) >= 0) {
                cellularPortLog("Registered on \"%s\".\n", buffer);
            }
            // Now, technically speaking, EUTRAN should be good to go,
            // PDP context and everything, and we should only have
            // to activate a PDP context on GERAN.  However,
            // for reasons I don't understand, SARA-R4 can be registered
            // but not attached (i.e. AT+CGATT returns 0) on both
            // RATs (unh?).  Phil Ware, who knows about these things,
            // always goes through (a) register, (b) wait for AT+CGATT
            // to return 1 and then (c) check that a context is active
            // with AT+CGACT (even for EUTRAN). Since this sequence
            // works for both RANs, it's best to be consistent.
            // Wait for AT+CGATT to return 1
            // SARA R4/N4 AT Command Manual UBX-17003787, section 13.5
            for (size_t x = 0; !attached && pKeepGoingCallback() &&
                               (x < 10); x++) {
                cellular_ctrl_at_lock();
                cellular_ctrl_at_set_at_timeout(CELLULAR_CTRL_COMMAND_MINIMUM_RESPONSE_TIME_MS, false);
                cellular_ctrl_at_cmd_start("AT+CGATT?");
                cellular_ctrl_at_cmd_stop();
                cellular_ctrl_at_resp_start("+CGATT:", false);
                attached = (cellular_ctrl_at_read_int() == 1);
                cellular_ctrl_at_resp_stop();
                cellular_ctrl_at_restore_at_timeout();
                cellular_ctrl_at_unlock();
                if (!attached) {
                    cellularPortTaskBlock(1000);
                }
            }

            if (attached) {
                // Activate a PDP context
                errorCode = CELLULAR_CTRL_CONTEXT_ACTIVATION_FAILURE;
                for (size_t x = 0; pKeepGoingCallback() &&
                                   (errorCode != CELLULAR_CTRL_SUCCESS) &&
                                   (x < 10); x++) {
                    cellular_ctrl_at_lock();
                    cellular_ctrl_at_set_at_timeout(CELLULAR_CTRL_COMMAND_MINIMUM_RESPONSE_TIME_MS, false);
                    cellular_ctrl_at_cmd_start("AT+CGACT?");
                    cellular_ctrl_at_cmd_stop();
                    cellular_ctrl_at_resp_start("+CGACT:", false);
                    // Skip the context ID
                    cellular_ctrl_at_read_int();
                    activated = (cellular_ctrl_at_read_int() == 1);
                    cellular_ctrl_at_resp_stop();
                    if (activated) {
                        // In other u-blox modem's you'd now use AT+UPSD to
                        // map the context to an internal modem profile so that it
                        // Cell Locate could use it (AT+UPSD=0,100,1), then
                        // you would activate that profile (AT+UPSDA=0,3).
                        // However, SARA-R4 only supports a single
                        // context at any one time and so that's not required.
                        cellular_ctrl_at_restore_at_timeout();
                        if (cellular_ctrl_at_unlock_return_error() == 0) {
                            errorCode = CELLULAR_CTRL_SUCCESS;
                        }
                    } else {
                        // Help it on its way.
                        cellularPortTaskBlock(1000);
                        cellular_ctrl_at_cmd_start("AT+CGACT=");
                        cellular_ctrl_at_write_int(1);
                        cellular_ctrl_at_write_int(CELLULAR_CTRL_CONTEXT_ID);
                        cellular_ctrl_at_cmd_stop_read_resp();
                        cellular_ctrl_at_restore_at_timeout();
                        cellular_ctrl_at_unlock();
                    }
                }
                if (pKeepGoingCallback() && (errorCode != CELLULAR_CTRL_SUCCESS)) {
                    cellularPortLog("CELLULAR_CTRL: unable to activate a PDP context");
                    if (pApn != NULL) {
                        cellularPortLog(", is APN \"%s\" correct?\n", pApn);
                    } else {
                        cellularPortLog(" (no APN specified).\n");
                    }
                }
            }
        } else {
            cellularPortLog("CELLULAR_CTRL: unable to register with the network");
            if (pApn != NULL) {
                cellularPortLog(", is APN \"%s\" correct and is an antenna connected?\n", pApn);
            } else {
                cellularPortLog(", does an APN need to be specified and is an antenna connected?\n");
            }
        }
    }

    if (errorCode != CELLULAR_CTRL_SUCCESS) {
        // Switch radio off after that failure
        cellular_ctrl_at_lock();
        cellular_ctrl_at_cmd_start("AT+CFUN=4");
        cellular_ctrl_at_cmd_stop_read_resp();
        cellular_ctrl_at_unlock();
    }

    return errorCode;
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS
 * -------------------------------------------------------------- */

// Initialise the cellular control driver.
CellularCtrlErrorCode cellularCtrlInit(int32_t pinEnablePower,
                                       int32_t pinCpOn,
                                       int32_t pinVInt,
                                       bool leavePowerAlone,
                                       int32_t uart,
                                       CellularPortQueueHandle_t queueUart)
{
    CellularCtrlErrorCode errorCode = CELLULAR_CTRL_NOT_INITIALISED;
    int32_t platformError;
    CellularPortGpioConfig gpioConfig = CELLULAR_PORT_GPIO_CONFIG_DEFAULT;
    int32_t enablePowerAtStart;

    if (!gInitialised) {
        errorCode = CELLULAR_CTRL_PLATFORM_ERROR;
        cellularPortLog("CELLULAR_CTRL: initialising with enable power pin ");
        if (pinEnablePower >= 0) {
            cellularPortLog("%d, ", pinEnablePower);
        } else {
            cellularPortLog("not connected, ");
        }
        cellularPortLog("CP ON pin %d", pinCpOn);
        if (leavePowerAlone) {
            cellularPortLog(", leaving the level of both those pins alone");
        }
        if (pinVInt >= 0) {
            cellularPortLog(" and VInt pin %d.\n", pinVInt);
        } else {
            cellularPortLog(", VInt pin not connected.\n");
        }
        gpioConfig.pin = pinCpOn;
        gpioConfig.mode = CELLULAR_PORT_GPIO_MODE_OUTPUT;
        platformError = cellularPortGpioConfig(&gpioConfig);
        if (platformError == 0) {
            if (!leavePowerAlone) {
                // Set CP_ON high so that we can pull it low
                platformError = cellularPortGpioSet(pinCpOn, 1);
            }
            if (platformError == 0) {
                if (pinEnablePower >= 0) {
                    gpioConfig.pin = pinEnablePower;
                     // Input/output so we can read it as well
                    gpioConfig.mode = CELLULAR_PORT_GPIO_MODE_INPUT_OUTPUT;
                    platformError = cellularPortGpioConfig(&gpioConfig);
                    if (platformError == 0) {
                        enablePowerAtStart = cellularPortGpioGet(pinEnablePower);
                        if (!leavePowerAlone) {
                            // Make sure the default is off.
                            enablePowerAtStart = 0;
                        }
                        platformError = cellularPortGpioSet(pinEnablePower, enablePowerAtStart);
                        if (platformError != 0) {
                            cellularPortLog("CELLULAR_CTRL: cellularPortGpioSet() for enable power pin %d returned error code %d.\n",
                                            pinEnablePower, platformError);
                        }
                    } else {
                        cellularPortLog("CELLULAR_CTRL: cellularPortGpioConfig() for enable power pin %d returned error code %d.\n",
                                        pinEnablePower, platformError);
                    }
                }
                if (platformError == 0) {
                    if (pinVInt >= 0) {
                        // Set pin that monitors VINT as input
                        gpioConfig.pin = pinVInt;
                         // Input/output so we can read it as well
                        gpioConfig.mode = CELLULAR_PORT_GPIO_MODE_INPUT
                        platformError = cellularPortGpioConfig(&gpioConfig);
                        if (platformError != 0) {
                            cellularPortLog("CELLULAR_CTRL: cellularPortGpioConfig() for VInt pin %d returned error code %d.\n",
                                            pinVInt, platformError);
                        }
                    }
                    if (platformError == 0) {
                        // With that all done, initialise the AT command parser
                        errorCode = at_init(uart, queueUart);
                        if (errorCode == 0) {
                            gPinEnablePower = pinEnablePower;
                            gPinCpOn = pinCpOn;
                            gPinVInt = pinVInt;
                            gNetworkStatus = CELLULAR_NETWORK_STATUS_UNKNOWN;
                            clearRadioParameters();
                            gAtNumConsecutiveTimeouts = 0;
                            cellular_ctrl_at_set_at_timeout_callback(atTimeoutCallback);
                            gInitialised = true;
                        }
                    }
                }
            } else {
                cellularPortLog("CELLULAR_CTRL: cellularPortGpioSet() for CP_ON pin %d returned error code %d.\n",
                                pinCpOn, platformError);
            }
        } else {
            cellularPortLog("CELLULAR_CTRL: cellularPortGpioConfig() for CP_ON pin %d returned error code %d.\n",
                            pinCpOn, platformError);
        }
    } else {
        errorCode = CELLULAR_CTRL_SUCCESS;
    }

    return (int32_t) errorCode;
}

// Shut-down this cellular control driver.
void cellularCtrlDeinit()
{
    if (gInitialised) {
        // Tidy up
        cellular_ctrl_at_set_at_timeout_callback(NULL);
        gInitialised = false;
    }
}

// Determine if the module is powered by
// checking the level on the power enable pin.
bool cellularCtrlIsPowered()
{
    bool isPowered = true;

    if (gPinEnablePower >= 0) {
        isPowered = cellularPortGpioGet(gPinEnablePower);
    }

    return isPowered;
}

// Determine if the cellular module is responsive.
bool cellularCtrlIsAlive()
{
    return moduleIsAlive(1) == CELLULAR_CTRL_SUCCESS;
}

// Power the cellular module on.
int32_t cellularCtrlPowerOn(const char *pPin)
{
    CellularCtrlErrorCode errorCode = CELLULAR_CTRL_NOT_INITIALISED;
    int32_t platformError = 0;
    int32_t enablePowerAtStart = 1;

    esp_err_t espError;

    if (gInitialised) {
        if (gPinEnablePower >= 0) {
            enablePowerAtStart = cellularPortGpioGet(gPinEnablePower);
        }
        errorCode = CELLULAR_CTRL_PIN_ENTRY_NOT_SUPPORTED;
        if (pPin == NULL) {
            errorCode = CELLULAR_CTRL_PLATFORM_ERROR;
            cellularPortLog("CELLULAR_CTRL: powering on.\n");
            // First, switch on the volts
            if (gPinEnablePower >= 0) {
                platformError = cellularPortGpioSet(gPinEnablePower, 1);
            }
            if (platformError == 0) {
                // Wait for things to settle
                cellularPortTaskBlock(100);
                // SARA-R412M is powered on by holding the CP_ON pin low
                // for more than 0.15 seconds
                platformError = cellularPortGpioSet(gPinCpOn, 0);
                if (platformError == -) {
                    cellularPortTaskBlock(300);
                    // Not bothering with checking return code here
                    // as it would have barfed on the last one if
                    // it were going to
                    cellularPortGpioSet(gPinCpOn, 1);
                    cellularPortTaskBlock(CELLULAR_CTRL_BOOT_WAIT_TIME_MS);
                    // Cellular module should be up, see if it's there
                    // and, if so, configure it
                    errorCode = moduleIsAlive(CELLULAR_CTRL_IS_ALIVE_ATTEMPTS_POWER_ON);
                    // Wait for things to settle
                    if (errorCode == CELLULAR_CTRL_SUCCESS) {
                        // Configure the module
                        errorCode = moduleConfigure();
                    }
                    // If we were off at the start and power-on was
                    // unsuccessful then go back to that state
                    if ((errorCode != CELLULAR_CTRL_SUCCESS) && (enablePowerAtStart == 0)) {
                        cellularPowerOff(NULL);
                    }
                } else {
                    cellularPortLog("CELLULAR_CTRL: cellularPortGpioSet() for CP_ON pin %d returned error code %d.\n",
                                    gPinCpOn, platformError);
                }
            } else {
                cellularPortLog("CELLULAR_CTRL: cellularPortGpioSet() for enable power pin %d returned error code%d.\n",
                                gPinEnablePower, platformError);
            }
        } else {
            cellularPortLog("CELLULAR_CTRL: a SIM PIN has been set but PIN entry is not supported I'm afraid.\n");
        }
    }

    return (int32_t) errorCode;
}

// Power the cellular module off.
void cellularCtrlPowerOff(bool (*pKeepGoingCallback) (void))
{
    bool moduleIsOff;

    if (gInitialised) {
        cellularPortLog("CELLULAR_CTRL: powering off.\n");
        // Send the power off command and then pull the power
        // No error checking, we're going dowwwwwn...
        cellular_ctrl_at_lock();
        moduleIsOff = false;
        // Clear out the old RF readings
        clearRadioParameters();
        cellular_ctrl_at_cmd_start("AT+CPWROFF");
        cellular_ctrl_at_cmd_stop_read_resp();

        if (gPinVInt >= 0) {
            // If we have a VInt pin then wait until that
            // goes low
            for (size_t x = 0; (cellularPortGpioGet(gPinVInt) != 0) &&
                               (x < CELLULAR_CTRL_POWER_DOWN_WAIT_SECONDS * 4) &&
                               ((pKeepGoingCallback == NULL) ||
                                pKeepGoingCallback()); x++) {
                cellularPortTaskBlock(250);
            }
        } else {
            // Wait for the module to stop responding at the AT interface
            // by poking it with "AT"
            cellular_ctrl_at_clear_error();
            cellular_ctrl_at_set_at_timeout(CELLULAR_CTRL_COMMAND_MINIMUM_RESPONSE_TIME_MS, false);
            for (size_t x = 0; !moduleIsOff && (x < CELLULAR_CTRL_POWER_DOWN_WAIT_SECONDS * 4) &&
                               ((pKeepGoingCallback == NULL) ||
                                pKeepGoingCallback()); x++) {
                cellular_ctrl_at_cmd_start("AT");
                cellular_ctrl_at_cmd_stop_read_resp();
                moduleIsOff = (cellular_ctrl_at_get_last_error() != 0);
                cellularPortTaskBlock(250);
            }
            cellular_ctrl_at_restore_at_timeout();
        }

        if (gPinEnablePower >= 0) {
            cellularPortGpioSet(gPinEnablePower, 0);
        }
        cellularPortGpioSet(gPinCpOn, 1);
        cellular_ctrl_at_unlock();
        gAtNumConsecutiveTimeouts = 0;
    }
}

// Remove power to teh cellular module.
void cellularCtrlHardPowerOff()
{
    if (gInitialised) {
        if (gPinEnablePower >= 0) {
            cellularPortGpioSet(gPinEnablePower, 0);
        }
        cellularPortGpioSet(gPinCpOn, 1);
        cellularPortTaskBlock(100);
        gAtNumConsecutiveTimeouts = 0;
    }
}

// Get the number of consecutive AT command
// timeouts.
int32_t cellularCtrlGetConsecutiveAtTimeouts()
{
    return gAtNumConsecutiveTimeouts;
}

// Re-boot the cellular module.
int32_t cellularCtrlReboot()
{
    CellularCtrlErrorCode errorCode = CELLULAR_CTRL_NOT_INITIALISED;

    if (gInitialised) {
        errorCode = CELLULAR_CTRL_AT_ERROR;
        cellularPortLog("CELLULAR_CTRL: rebooting.\n");
        cellular_ctrl_at_lock();
        // Clear out the old RF readings
        clearRadioParameters();
        cellular_ctrl_at_cmd_start("AT+CFUN=15");
        cellular_ctrl_at_cmd_stop_read_resp();
        if (cellular_ctrl_at_unlock_return_error() == 0) {
            // Wait for the module to boot
            cellularPortTaskBlock(CELLULAR_CTRL_BOOT_WAIT_TIME_MS);
            // Wait for the module to return to life
            // and configure it
            errorCode = moduleIsAlive(CELLULAR_CTRL_IS_ALIVE_ATTEMPTS_POWER_ON);
            if (errorCode == CELLULAR_CTRL_SUCCESS) {
                // Configure the module
                errorCode = moduleConfigure();
            }
        }
        gAtNumConsecutiveTimeouts = 0;
    }

    return (int32_t) errorCode;
}

// Set the bands to be used by the cellular module.
int32_t cellularCtrlSetBandMask(CellularCtrlRat rat,
                                uint64_t bandMask)
{
    CellularCtrlErrorCode errorCode = CELLULAR_CTRL_NOT_INITIALISED;

    if (gInitialised) {
        errorCode = CELLULAR_CTRL_INVALID_PARAMETER;
        if ((rat == CELLULAR_CTRL_RAT_CATM1) ||
            (rat == CELLULAR_CTRL_RAT_NB1)) {
            errorCode = CELLULAR_CTRL_AT_ERROR;
            cellularPortLog("CELLULAR_CTRL: setting band mask for RAT %d (in module terms %d) to 0x%016llx.\n",
                            rat, gCellularRatToLocalRat[rat] -
                                 gCellularRatToLocalRat[CELLULAR_CTRL_RAT_CATM1], bandMask);
            cellular_ctrl_at_lock();
            // Note: the RAT numbering for this AT command is NOT the same
            // as the RAT numbering for all the other AT commands:
            // here CELLULAR_CTRL_RAT_CATM1 is 0 and CELLULAR_CTRL_RAT_NB1 is 1
            cellular_ctrl_at_cmd_start("AT+UBANDMASK=");
            cellular_ctrl_at_write_int(gCellularRatToLocalRat[rat] - gCellularRatToLocalRat[CELLULAR_CTRL_RAT_CATM1]);
            cellular_ctrl_at_write_uint64(bandMask);
            cellular_ctrl_at_cmd_stop_read_resp();
            if (cellular_ctrl_at_unlock_return_error() == 0) {
                errorCode = CELLULAR_CTRL_SUCCESS;
            }
        }
    }

    return (int32_t) errorCode;
}

// Get the bands being used by the cellular module.
uint64_t cellularCtrlGetBandMask(CellularCtrlRat rat)
{
    uint64_t masks[2];
    int32_t rats[2];
    bool success = true;
    uint64_t mask = 0;

    if (gInitialised) {
        if ((rat == CELLULAR_CTRL_RAT_CATM1) ||
            (rat == CELLULAR_CTRL_RAT_NB1)) {
            cellularPortLog("CELLULAR_CTRL: getting band mask for RAT %d (in module terms %d).\n",
                             rat, gCellularRatToLocalRat[rat] -
                                  gCellularRatToLocalRat[CELLULAR_CTRL_RAT_CATM1]);
            cellular_ctrl_at_lock();
            cellular_ctrl_at_cmd_start("AT+UBANDMASK?");
            cellular_ctrl_at_cmd_stop();
            cellular_ctrl_at_resp_start("+UBANDMASK:", false);
            // Read up to N integers and uint64_t representing the RATs
            // and the band masks
            // Note: the RAT numbering for this AT command is NOT the same
            // as the RAT numbering for all the other AT commands:
            // here CELLULAR_CTRL_RAT_CATM1 is 0 and CELLULAR_CTRL_RAT_NB1 is 1
            for (size_t x = 0; (x < sizeof(rats) / sizeof(rats[0])) && success; x++) {
                rats[x] = cellular_ctrl_at_read_int();
                success = (cellular_ctrl_at_read_uint64(&(masks[x])) == 0);
                if ((rats[x] >= 0) &&
                    ((rats[x] + CELLULAR_CTRL_RAT_CATM1) < sizeof(gLocalRatToCellularRat) /
                                                           sizeof(gLocalRatToCellularRat[0]))) {
                    rats[x] = gLocalRatToCellularRat[rats[x] +
                                                     gCellularRatToLocalRat[CELLULAR_CTRL_RAT_CATM1]];
                }
            }
            cellular_ctrl_at_resp_stop();
            cellular_ctrl_at_unlock();
            success = false;
            for (size_t x = 0; (x < sizeof(rats) / sizeof(rats[0])) && !success; x++) {
                if (rats[x] == rat) {
                    mask = masks[x];
                    success = true;
                    cellularPortLog("CELLULAR_CTRL: band mask for RAT %d (in module terms %d) is 0x%016llx.\n",
                                    rats[x], gCellularRatToLocalRat[rats[x]] -
                                             gCellularRatToLocalRat[CELLULAR_CTRL_RAT_CATM1], masks[x]);
                }
            }
        }
    }

    return mask;
}

// Set the sole radio access technology.
int32_t cellularCtrlSetRat(CellularCtrlRat rat)
{
    CellularCtrlErrorCode errorCode = CELLULAR_CTRL_NOT_INITIALISED;

    if (gInitialised) {
        errorCode = CELLULAR_CTRL_INVALID_PARAMETER;
        if ((rat > CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED) &&
            (rat < CELLULAR_CTRL_MAX_NUM_RATS)) {
            errorCode = CELLULAR_CTRL_AT_ERROR;
            cellularPortLog("CELLULAR_CTRL: setting sole RAT to %d (in module terms %d).\n",
                            rat, gCellularRatToLocalRat[rat]);
            cellular_ctrl_at_lock();
            cellular_ctrl_at_cmd_start("AT+URAT=");
            cellular_ctrl_at_write_int(gCellularRatToLocalRat[rat]);
            cellular_ctrl_at_cmd_stop_read_resp();
            if (cellular_ctrl_at_unlock_return_error() == 0) {
                errorCode = CELLULAR_CTRL_SUCCESS;
            }
        }
    }

    return (int32_t) errorCode;
}

// Set the radio access technology at the given rank.
int32_t cellularCtrlSetRatRank(CellularCtrlRat rat, int32_t rank)
{
    CellularCtrlErrorCode errorCode = CELLULAR_CTRL_NOT_INITIALISED;
    int32_t rats[CELLULAR_CTRL_MAX_NUM_RATS];

    if (gInitialised) {
        errorCode = CELLULAR_CTRL_INVALID_PARAMETER;
        if ((rat >= CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED) &&
            (rat < CELLULAR_CTRL_MAX_NUM_RATS)) {
            // Assume there are no RATs
            for (size_t x = 0; x < sizeof(rats) / sizeof(rats[0]); x++) {
                rats[x] = CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED;
            }
            // Get the existing RATs
            if ((rank >= 0) && (rank < CELLULAR_CTRL_MAX_NUM_RATS)) {
                errorCode = CELLULAR_CTRL_AT_ERROR;
                for (size_t x = 0; x < sizeof(rats) / sizeof(rats[0]); x++) {
                    rats[x] = cellularGetRat(x);
                    if (rats[x] == CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED) {
                        break;
                    }
                }
                // Overwrite the one we want to set
                rats[rank] = rat;

                cellularPortLog("CELLULAR_CTRL: setting the RAT at rank %d to %d (in module terms %d).\n",
                                rank, rat, gCellularRatToLocalRat[rat]);
                // Remove duplicates
                for (size_t x = 0; x < sizeof(rats) / sizeof(rats[0]); x++) {
                    for (size_t y = x + 1; y < sizeof(rats) / sizeof(rats[0]); y++) {
                        if ((rats[x] > CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED) && (rats[x] == rats[y])) {
                            rats[y] = CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED;
                        }
                    }
                }

                // Send the AT command
                cellularPortLog("CELLULAR_CTRL: RATs (removing duplicates) become:\n");
                for (size_t x = 0, y = 0; x < sizeof(rats) / sizeof(rats[0]); x++) {
                    cellularPortLog("  rank[%d]: %d (in module terms %d).\n",
                                    x, rats[x], gCellularRatToLocalRat[rats[x]]);
                    y++;
                }
                cellular_ctrl_at_lock();
                cellular_ctrl_at_cmd_start("AT+URAT=");
                for (size_t x = 0; x < sizeof(rats) / sizeof(rats[0]); x++) {
                    if (rats[x] != CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED) {
                        cellular_ctrl_at_write_int(gCellularRatToLocalRat[rats[x]]);
                    }
                }
                cellular_ctrl_at_cmd_stop_read_resp();
                if (cellular_ctrl_at_unlock_return_error() == 0) {
                    errorCode = CELLULAR_CTRL_SUCCESS;
                }
            }
        }
    }

    return (int32_t) errorCode;
}

// Get the radio access technology at the given rank.
CellularCtrlRat cellularCtrlGetRat(int32_t rank)
{
    CellularRat rats[CELLULAR_CTRL_MAX_NUM_RATS];
    int32_t rat;

    // Assume there are no RATs
    for (size_t x = 0; x < sizeof(rats) / sizeof (rats[0]); x++) {
        rats[x] = CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED;
    }

    if (gInitialised) {
        if ((rank >= 0) && (rank < CELLULAR_CTRL_MAX_NUM_RATS)) {
            // Get the RAT from the module
            cellular_ctrl_at_lock();
            cellular_ctrl_at_cmd_start("AT+URAT?");
            cellular_ctrl_at_cmd_stop();
            cellular_ctrl_at_resp_start("+URAT:", false);
            // Read up to N integers representing the RATs
            for (size_t x = 0; x < sizeof(rats) / sizeof (rats[0]); x++) {
                rat =  cellular_ctrl_at_read_int();
                if ((rat >= 0) &&
                    (rat < sizeof (gLocalRatToCellularRat) /
                           sizeof (gLocalRatToCellularRat[0]))) {
                     rats[x] = gLocalRatToCellularRat[rat];
                }
            }
            cellular_ctrl_at_resp_stop();
            cellular_ctrl_at_unlock();
            cellularPortLog("CELLULAR_CTRL: RATs are:\n");
            for (size_t x = 0; x < sizeof(rats) / sizeof(rats[0]); x++) {
                cellularPortLog("  rank[%d]: %d (in module terms %d).\n",
                                x, rats[x], gCellularRatToLocalRat[rats[x]]);
            }
        } else {
            // Set rank to 0 so that we return CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED
            rank = 0;
        }
    }

    return rats[rank];
}

// Get the rank at which the given RAT is used.
int32_t cellularCtrlGetRatRank(CellularCtrlRat rat)
{
    CellularCtrlErrorCode errorCodeOrRank = CELLULAR_CTRL_NOT_INITIALISED;
    int32_t y;

    if (gInitialised) {
        errorCodeOrRank = CELLULAR_CTRL_INVALID_PARAMETER;
        if ((rat >= CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED) &&
            (rat < CELLULAR_CTRL_MAX_NUM_RATS)) {
            errorCodeOrRank = CELLULAR_CTRL_NOT_FOUND;
            // Get the RATs from the module
            cellular_ctrl_at_lock();
            cellular_ctrl_at_cmd_start("AT+URAT?");
            cellular_ctrl_at_cmd_stop();
            cellular_ctrl_at_resp_start("+URAT:", false);
            // Read up to N integers representing the RATs
            for (size_t x = 0; (errorCodeOrRank < 0) && (x < CELLULAR_CTRL_MAX_NUM_RATS); x++) {
                y = cellular_ctrl_at_read_int();
                if ((y >= 0) &&
                    (y < sizeof(gLocalRatToCellularRat) /
                         sizeof(gLocalRatToCellularRat[0]))) {
                    if (rat == gLocalRatToCellularRat[y]) {
                        errorCodeOrRank = x;
                    }
                }
            }
            cellular_ctrl_at_resp_stop();
            cellular_ctrl_at_unlock();
            if (errorCodeOrRank >= 0) {
                cellularPortLog("CELLULAR_CTRL: rank of RAT %d (in module terms %d) is %d.\n",
                                gLocalRatToCellularRat[rat], rat, errorCodeOrRank);
            } else {
                cellularPortLog("CELLULAR_CTRL: RAT %d (in module terms %d) is not ranked.\n",
                                gLocalRatToCellularRat[rat], rat);
            }
        }
    }

    return (int32_t) errorCodeOrRank;
}

// Set the MNO Profile.
int32_t cellularCtrlSetMnoProfile(int32_t mnoProfile)
{
    CellularCtrlErrorCode errorCode = CELLULAR_CTRL_NOT_INITIALISED;

    if (gInitialised) {
        errorCode = CELLULAR_CTRL_CONNECTED;
        if (cellularGetRegisteredRat() < 0) {
            errorCode = CELLULAR_CTRL_AT_ERROR;
            cellular_ctrl_at_lock();
            cellular_ctrl_at_cmd_start("AT+UMNOPROF=");
            cellular_ctrl_at_write_int(mnoProfile);
            cellular_ctrl_at_cmd_stop_read_resp();
            if (cellular_ctrl_at_unlock_return_error() == 0) {
                errorCode = CELLULAR_CTRL_SUCCESS;
                cellularPortLog("CELLULAR_CTRL: MNO profile set to %d.\n", mnoProfile);
            } else {
                cellularPortLog("CELLULAR_CTRL: unable to set MNO profile to %d.\n", mnoProfile);
            }
        } else {
            cellularPortLog("CELLULAR_CTRL: unable to set MNO Profile as we are connected to the network.\n");
        }
    }

    return (int32_t) errorCode;
}

// Get the MNO Profile.
int32_t cellularCtrlGetMnoProfile()
{
    int32_t mnoProfile = -1;

    if (gInitialised) {
        cellular_ctrl_at_lock();
        cellular_ctrl_at_cmd_start("AT+UMNOPROF?");
        cellular_ctrl_at_cmd_stop();
        cellular_ctrl_at_resp_start("+UMNOPROF:", false);
        mnoProfile = cellular_ctrl_at_read_int();
        cellular_ctrl_at_resp_stop();
        if ((cellular_ctrl_at_unlock_return_error() == 0) && (mnoProfile >= 0)) {
            cellularPortLog("CELLULAR_CTRL: MNO profile is %d.\n", mnoProfile);
        } else {
            cellularPortLog("CELLULAR_CTRL: unable to read MNO profile.\n");
        }
    }

    return mnoProfile;
}

// Register with the cellular network and obtain a PDP context.
int32_t cellularCtrlConnect(bool (*pKeepGoingCallback) (void),
                            const char *pApn, const char *pUsername,
                            const char *pPassword)
{
    CellularCtrlErrorCode errorCode = CELLULAR_CTRL_NOT_INITIALISED;
    char imsi[CELLULAR_IMSI_SIZE];
    const char *pApnConfig = NULL;
    time_t startTime;

    if (gInitialised) {
        errorCode = CELLULAR_CTRL_INVALID_PARAMETER;
        if ((pUsername == NULL) ||
            ((pUsername != NULL) && (pPassword != NULL))) {
            errorCode = CELLULAR_CTRL_AT_ERROR;
            if (prepareConnect()) {
                // Set up the APN look-up since none is specified
                if ((pApn == NULL) && (cellularGetImsi(imsi) == 0)) {
                    pApnConfig = apnconfig(imsi);
                }
                // Now try to connect, potentially multiple times
                startTime = cellularPortGetTime();
                do {
                    if (pApnConfig != NULL) {
                        pApn = _APN_GET(pApnConfig);
                        pUsername = _APN_GET(pApnConfig);
                        pPassword = _APN_GET(pApnConfig);
                        cellularPortLog("CELLULAR_CTRL: APN from database is \"%s\".\n", pApn);
                    } else {
                        cellularPortLog("CELLULAR_CTRL: user-specified APN is \"%s\".\n", pApn);
                    }
                    // Register and activate PDP context
                    errorCode = tryConnect(pKeepGoingCallback, pApn,
                                           pUsername, pPassword);
                } while ((errorCode != CELLULAR_CTRL_SUCCESS) &&
                         (pApnConfig != NULL) &&
                         (*pApnConfig != 0) &&
                         pKeepGoingCallback());

                if (errorCode == CELLULAR_CTRL_SUCCESS) {
                    cellularPortLog("CELLULAR_CTRL: connected after %lld second(s).\n",
                                    cellularPortGetTime() - startTime);
                } else {
                    cellularPortLog("CELLULAR_CTRL: connection attempt stopped after %lld second(s).\n",
                                    cellularPortGetTime() - startTime);
                }
            }
        }
    }

    return (int32_t) errorCode;
}

// Disconnect from the cellular network.
int32_t cellularCtrlDisconnect()
{
    CellularCtrlErrorCode errorCode = CELLULAR_CTRL_NOT_INITIALISED;
    int32_t status;

    if (gInitialised) {
        errorCode = CELLULAR_CTRL_AT_ERROR;
        cellular_ctrl_at_lock();
        // Clear out the old RF readings
        clearRadioParameters();
        // See if we are already disconnected
        cellular_ctrl_at_cmd_start("AT+COPS?");
        cellular_ctrl_at_cmd_stop();
        cellular_ctrl_at_resp_start("+COPS:", false);
        status = cellular_ctrl_at_read_int();
        cellular_ctrl_at_resp_stop();
        cellular_ctrl_at_unlock();
        if (status != 2) {
            // The normal thing to do here would be
            // AT+COPS=2.  However, due to oddities
            // in the SARA-R412M firmware it is
            // recommended to set airplane mode with
            // AT+CFUN=0 or 4 (the latter being
            // persistent across reboots) and
            // then AT+CFUN=1 to bring it back again
            cellular_ctrl_at_lock();
            cellular_ctrl_at_cmd_start("AT+CFUN=4");
            cellular_ctrl_at_cmd_stop_read_resp();
            if (cellular_ctrl_at_unlock_return_error() == 0) {
                errorCode = CELLULAR_CTRL_CONNECTED;
                for (int32_t count = 10; (cellularCtrlGetActiveRat() >= 0) && (count > 0); count--) {
                    // Prod the modem to see if it's done
                    cellular_ctrl_at_lock();
                    cellular_ctrl_at_set_at_timeout(CELLULAR_CTRL_COMMAND_MINIMUM_RESPONSE_TIME_MS, false);
                    cellular_ctrl_at_cmd_start("AT+CEREG?");
                    cellular_ctrl_at_cmd_stop();
                    cellular_ctrl_at_resp_start("+CEREG:", false);
                    // Ignore the first parameter
                    cellular_ctrl_at_read_int();
                    status = cellular_ctrl_at_read_int();
                    if (status >= 0) {
                        setNetworkStatus(status);
                    }
                    cellular_ctrl_at_resp_stop();
                    cellular_ctrl_at_restore_at_timeout();
                    cellular_ctrl_at_unlock();
                    cellularPortTaskBlock(1000);
                }
                if (cellularCtrlGetActiveRat() < 0) {
                    cellular_ctrl_at_remove_urc_handler("+CEREG:");
                    errorCode = CELLULAR_CTRL_SUCCESS;
                    cellularPortLog("CELLULAR_CTRL: disconnected.\n");
                } else {
                    cellularPortLog("CELLULAR_CTRL: unable to disconnect.\n");
                }
            } else {
                cellularPortLog("CELLULAR_CTRL: unable to disconnect.\n");
            }
        } else {
            errorCode = CELLULAR_CTRL_SUCCESS;
            cellularPortLog("CELLULAR_CTRL: already disconnected.\n");
        }
    }

    return (int32_t) errorCode;
}

// Get the current network registration status.
CellularCtrlNetworkStatus cellularCtrlGetNetworkStatus()
{
    cellularPortLog("CELLULAR_CTRL: network status %d.\n", status);
    return gNetworkStatus;
}

// Get the current RAT.
int32_t cellularCtrlGetActiveRat()
{
    CellularCtrlErrorCode errorCodeOrRat = CELLULAR_CTRL_NOT_INITIALISED;

    if (gInitialised) {
        errorCodeOrRat = CELLULAR_CTRL_AT_ERROR;
        cellular_ctrl_at_lock();
        // Read the current RAT
        cellular_ctrl_at_cmd_start("AT+COPS?");
        cellular_ctrl_at_cmd_stop();
        cellular_ctrl_at_resp_start("+COPS:", false);
        // Skip past <mode>, <format> and network name
        cellular_ctrl_at_skip_param(3);
        errorCodeOrRat = cellular_ctrl_at_read_int();
        cellular_ctrl_at_resp_stop();
        cellular_ctrl_at_unlock();
        if ((errorCodeOrRat >= 0) &&
            (errorCodeOrRat < (sizeof(gCopsRatToCellularRat) / sizeof (gCopsRatToCellularRat[0])))) {
            errorCodeOrRat = gCopsRatToCellularRat[errorCodeOrRat];
            cellularPortLog("CELLULAR_CTRL: RAT is %d.\n", errorCodeOrRat);
        }
    }

    return (int32_t) errorCodeOrRat;
}

// Get the name of the operator on which the module is registered.
int32_t cellularCtrlGetOperatorStr(char *pStr, int32_t size)
{
    CellularCtrlErrorCode errorCodeOrSize = CELLULAR_CTRL_NOT_INITIALISED;
    int32_t bytesRead;
    int32_t atError;

    if (gInitialised) {
        errorCodeOrSize = CELLULAR_CTRL_INVALID_PARAMETER;
        if ((pStr != NULL) && (size > 0)) {
            errorCodeOrSize = CELLULAR_CTRL_AT_ERROR;
            cellular_ctrl_at_lock();
            // First set long alphanumeric format
            cellular_ctrl_at_cmd_start("AT+COPS=3,0");
            cellular_ctrl_at_cmd_stop();
            cellular_ctrl_at_cmd_stop_read_resp();
            // Then read the operator name
            cellular_ctrl_at_cmd_start("AT+COPS?");
            cellular_ctrl_at_cmd_stop();
            cellular_ctrl_at_resp_start("+COPS:", false);
            // Skip past <mode> and <format>
            cellular_ctrl_at_skip_param(2);
            bytesRead = cellular_ctrl_at_read_string(pStr, size, false);
            cellular_ctrl_at_resp_stop();
            atError = cellular_ctrl_at_unlock_return_error();
            if ((bytesRead >= 0) && (atError == 0)) {
                errorCodeOrSize = cellularPort_strlen(pStr);
                cellularPortLog("CELLULAR_CTRL: operator is \"%s\".\n", pStr);
            } else {
                cellularPortLog("CELLULAR_CTRL: unable to read operator name.\n");
            }
        }
    }

    return (int32_t) errorCodeOrSize;
}

// Get the MCC and MNC of the network on which the module is registered.
int32_t cellularCtrlGetMccMnc(int32_t *pMcc, int32_t *pMnc)
{
    CellularCtrlErrorCode errorCode = CELLULAR_CTRL_NOT_INITIALISED;
    char buffer[7]; // Enough room for "255255"
    int32_t bytesRead;
    int32_t atError;

    if (gInitialised) {
        errorCode = CELLULAR_CTRL_INVALID_PARAMETER;
        if ((pMcc != NULL) && (pMnc != NULL)) {
            errorCode = CELLULAR_CTRL_AT_ERROR;
            cellular_ctrl_at_lock();
            // SARA R4/N4 AT Command Manual UBX-17003787, section 7.4
            // First set numeric format
            cellular_ctrl_at_cmd_start("AT+COPS=3,2");
            cellular_ctrl_at_cmd_stop();
            cellular_ctrl_at_cmd_stop_read_resp();
            // Then read the operator MCC/MNC
            cellular_ctrl_at_cmd_start("AT+COPS?");
            cellular_ctrl_at_cmd_stop();
            cellular_ctrl_at_resp_start("+COPS:", false);
            // Skip past <mode> and <format>
            cellular_ctrl_at_skip_param(2);
            bytesRead = cellular_ctrl_at_read_string(buffer,
                                                            sizeof(buffer), false);
            cellular_ctrl_at_resp_stop();
            atError = cellular_ctrl_at_unlock_return_error();
            if ((bytesRead >= 5) && (atError == 0)) {
                // Should now have a string something like "255255"
                // The first three digits are the MCC, the next two or
                // three the MNC
                *pMnc = cellularPort_atoi(&(buffer[3]));
                buffer[3] = 0;
                *pMcc = cellularPort_atoi(buffer);
                errorCode = CELLULAR_CTRL_SUCCESS;
                cellularPortLog("CELLULAR_CTRL: MCC/MNC is %u/%u.\n",
                                (uint32_t) *pMcc, (uint32_t) *pMnc);
            } else {
                cellularPortLog("CELLULAR_CTRL: unable to read MCC/MNC.\n");
            }
        }
    }

    return (int32_t) errorCode;
}

// Get the currently allocated IP address as a string.
int32_t cellularCtrlGetIpAddressStr(char *pStr)
{
    CellularCtrlErrorCode errorCodeOrSize = CELLULAR_CTRL_NOT_INITIALISED;
    int32_t contextId;
    char buffer[CELLULAR_IP_ADDRESS_SIZE];

    if (gInitialised) {
        buffer[0] = 0;
        errorCodeOrSize = CELLULAR_CTRL_NO_CONTEXT_ACTIVATED;
        cellular_ctrl_at_lock();
        cellular_ctrl_at_cmd_start("AT+CGPADDR=");
        cellular_ctrl_at_write_int(CELLULAR_CTRL_CONTEXT_ID);
        cellular_ctrl_at_cmd_stop();
        cellular_ctrl_at_resp_start("+CGPADDR:", false);
        contextId = cellular_ctrl_at_read_int();
        cellular_ctrl_at_read_string(buffer, sizeof(buffer), false);
        cellular_ctrl_at_resp_stop();
        if (cellular_ctrl_at_unlock_return_error() == 0) {
            if (contextId == CELLULAR_CTRL_CONTEXT_ID) {
                errorCodeOrSize = cellularPort_strlen(buffer);
                if (pStr != NULL) {
                    strcpy(pStr, buffer);
                }
                if (errorCodeOrSize >= 0) {
                    cellularPortLog("CELLULAR_CTRL: IP address %.*s.\n",
                                    errorCodeOrSize, buffer);
                } else {
                    cellularPortLog("CELLULAR_CTRL: unable to read IP address.\n");
                }
            }
        }
    }

    return (int32_t) errorCodeOrSize;
}

// Get the APN currently in use.
int32_t cellularCtrlGetApnStr(char *pStr, size_t size)
{
    CellularCtrlErrorCode errorCode = CELLULAR_CTRL_NOT_INITIALISED;
    int32_t bytesRead;
    int32_t atError;

    if (gInitialised) {
        errorCode = CELLULAR_CTRL_INVALID_PARAMETER;
        if (pStr != NULL) {
            errorCode = CELLULAR_CTRL_AT_ERROR;
            cellular_ctrl_at_lock();
            cellular_ctrl_at_cmd_start("AT+CGDCONT?");
            cellular_ctrl_at_cmd_stop();
            cellular_ctrl_at_resp_start("+CGDCONT:", false);
            // Skip the "context ID" and "IP" fields
            cellular_ctrl_at_skip_param(2);
            // Read the APN field
            bytesRead = cellular_ctrl_at_read_string(pStr, size, false);
            cellular_ctrl_at_resp_stop();
            atError = cellular_ctrl_at_unlock_return_error();
            if ((bytesRead >= 0) && (atError == 0)) {
                errorCode = CELLULAR_CTRL_SUCCESS;
                cellularPortLog("CELLULAR_CTRL: APN is %s.\n", pStr);
            } else {
                cellularPortLog("CELLULAR_CTRL: unable to read APN.\n");
            }
        }
    }

    return (int32_t) errorCode;
}

// Refresh the radio parameters.
int32_t cellularCtrlRefreshRadioParameters()
{
    CellularCtrlErrorCode errorCode = CELLULAR_CTRL_NOT_INITIALISED;
    double rsrx;
    int32_t rssi;
    char buf[16];
    char *pLimit;

    if (gInitialised) {
        errorCode = CELLULAR_CTRL_NOT_REGISTERED;
        if (cellularGetRegisteredRan() >= 0) {
            errorCode = CELLULAR_CTRL_AT_ERROR;
            gRssiDbm = 0;
            gRsrpDbm = 0;
            gRsrqDbm = 0;
            // The mechanisms to get the radio information
            // are different between EUTRAN and GERAN but
            // AT+CSQ works in all cases though it sometimes
            // doesn't return a reading.
            cellular_ctrl_at_lock();
            cellular_ctrl_at_cmd_start("AT+CSQ");
            cellular_ctrl_at_cmd_stop();
            cellular_ctrl_at_resp_start("+CSQ:", false);
            rssi = cellular_ctrl_at_read_int();
            gRxQual = cellular_ctrl_at_read_int();
            if (gRxQual == 99) {
                gRxQual = -1;
            }
            cellular_ctrl_at_resp_stop();
            // AT+CSQ returns a coded RSSI value
            // The mapping is defined in the array gRssiConvertLte[].
            if (cellular_ctrl_at_unlock_return_error() == 0) {
                if ((rssi >= 0) && (rssi < sizeof(gRssiConvertLte) / sizeof(gRssiConvertLte[0]))) {
                    gRssiDbm = gRssiConvertLte[rssi];
                }
                // Note that AT+UCGED is used
                // rather than AT+CESQ as, in my experience,
                // it is more reliable in reporting answers.
                cellular_ctrl_at_lock();
                cellular_ctrl_at_cmd_start("AT+UCGED?");
                cellular_ctrl_at_cmd_stop();
                cellular_ctrl_at_resp_start("+RSRP:", false);
                gCellId = cellular_ctrl_at_read_int();
                gEarfcn = cellular_ctrl_at_read_int();
                if (cellular_ctrl_at_read_string(buf, sizeof(buf), false) > 0) {
                    rsrx = cellularPort_strtof(buf, &pLimit);
                    if (rsrx >= 0) {
                        gRsrpDbm = (int32_t) (rsrx + 0.5);
                    } else {
                        gRsrpDbm = (int32_t) (rsrx - 0.5);
                    }
                }
                cellular_ctrl_at_resp_start("+RSRQ:", false);
                // Skip past cell ID and EARFCN since they will be the same
                cellular_ctrl_at_skip_param(2);
                if (cellular_ctrl_at_read_string(buf, sizeof(buf), false) > 0) {
                    rsrx = cellularPort_strtof(buf, &pLimit);
                    if (rsrx >= 0) {
                        gRsrqDbm = (int32_t) (rsrx + 0.5);
                    } else {
                        gRsrqDbm = (int32_t) (rsrx - 0.5);
                    }
                }
                cellular_ctrl_at_resp_stop();
                if (cellular_ctrl_at_unlock_return_error() == 0) {
                    errorCode = CELLULAR_CTRL_SUCCESS;
                }
            }
        }
    }

    if (errorCode == CELLULAR_CTRL_SUCCESS) {
        cellularPortLog("CELLULAR_CTRL: radio parameters refreshed:\n");
        cellularPortLog("               RSSI:    %d dBm\n", gRssiDbm);
        cellularPortLog("               RSRP:    %d dBm\n", gRsrpDbm);
        cellularPortLog("               RSRQ:    %d dBm\n", gRsrqDbm);
        cellularPortLog("               RxQual:  %d\n", gRxQual);
        cellularPortLog("               cell ID: %d\n", gCellId);
        cellularPortLog("               EARFCN:  %d\n", gEarfcn);
    } else {
        cellularPortLog("CELLULAR_CTRL: unable to refresh radio parameters.\n");
    }

    return (int32_t) errorCode;
}

// Return the RSSI.
int32_t cellularCtrlGetRssiDbm()
{
    return gRssiDbm;
}

// Return the RSRP.
int32_t cellularCtrlGetRsrpDbm()
{
    return gRsrpDbm;
}

// Return the RSRQ.
int32_t cellularCtrlGetRsrqDbm()
{
    return gRsrqDbm;
}

// Return the RxQual.
int32_t cellularCtrlGetRxQual()
{
    return gRxQual;
}

// Work out SNR from RSSI and RSRP.
int32_t cellularCtrlGetSnrDb(int32_t *pSnrDb)
{
    CellularCtrlErrorCode errorCode = CELLULAR_CTRL_NOT_INITIALISED;
    double rssi;
    double rsrp;
    double snrDb;

    if (gInitialised) {
        errorCode = CELLULAR_CTRL_INVALID_PARAMETER;

        if ((pSnrDb != NULL) && (gRssiDbm < 0) && (gRsrpDbm < 0)) {
            // SNR = RSRP / (RSSI - RSRP).
            // First convert from dBm
            rssi = cellularPort_pow(10.0, ((double) gRssiDbm) / 10);
            rsrp = cellularPort_pow(10.0, ((double) gRsrpDbm) / 10);

            if (cellularPort_errno_get() == 0) {
                snrDb = 10 * cellularPort_log10(rsrp / (rssi - rsrp));
                if (cellularPort_errno_get() == 0) {
                    *pSnrDb = (int32_t) snrDb;
                    errorCode = CELLULAR_CTRL_SUCCESS;
                }
            }
        }
    }

    return (int32_t) errorCode;
}

// Return the cell ID.
int32_t cellularCtrlGetCellId() {
    return gCellId;
}

// Return the EARFCN.
int32_t cellularCtrlGetEarfcn() {
    return gEarfcn;
}

// Get the 15 digit IMEI of the cellular module.
int32_t cellularCtrlGetImei(char *pImei)
{
    CellularCtrlErrorCode errorCode = CELLULAR_CTRL_NOT_INITIALISED;
    int32_t bytesRead;
    int32_t atError;

    if (gInitialised) {
        errorCode = CELLULAR_CTRL_INVALID_PARAMETER;
        if (pImei != NULL) {
            errorCode = CELLULAR_CTRL_AT_ERROR;
            cellular_ctrl_at_lock();
            cellular_ctrl_at_cmd_start("AT+CGSN");
            cellular_ctrl_at_cmd_stop();
            cellular_ctrl_at_resp_start(NULL, false);
            bytesRead = cellular_ctrl_at_read_bytes((uint8_t *)pImei,
                                                           CELLULAR_CTRL_IMEI_SIZE);
            cellular_ctrl_at_resp_stop();
            atError = cellular_ctrl_at_unlock_return_error();
            if ((bytesRead == CELLULAR_IMEI_SIZE) && (atError == 0)) {
                errorCode = CELLULAR_CTRL_SUCCESS;
                cellularPortLog("CELLULAR_CTRL: IMEI is %.*s.\n",
                                CELLULAR_CTRL_IMEI_SIZE, pImei);
            } else {
                cellularPortLog("CELLULAR_CTRL: unable to read IMEI.\n");
            }
        }
    }

    return (int32_t) errorCode;
}

// Get the 15 digit IMSI of the cellular module.
int32_t cellularGetImsi(char *pImsi)
{
    CellularCtrlErrorCode errorCode = CELLULAR_CTRL_NOT_INITIALISED;
    int32_t bytesRead;
    int32_t atError;

    if (gInitialised) {
        errorCode = CELLULAR_CTRL_INVALID_PARAMETER;
        if (pImsi != NULL) {
            errorCode = CELLULAR_CTRL_AT_ERROR;
            cellular_ctrl_at_lock();
            cellular_ctrl_at_cmd_start("AT+CIMI");
            cellular_ctrl_at_cmd_stop();
            cellular_ctrl_at_resp_start(NULL, false);
            bytesRead = cellular_ctrl_at_read_bytes((uint8_t *) pImsi,
                                                           CELLULAR_CTRL_IMSI_SIZE);
            cellular_ctrl_at_resp_stop();
            atError = cellular_ctrl_at_unlock_return_error();
            if ((bytesRead == CELLULAR_CTRL_IMSI_SIZE) && (atError == 0)) {
                errorCode = CELLULAR_CTRL_SUCCESS;
                cellularPortLog("CELLULAR_CTRL: IMSI is %.*s.\n",
                                CELLULAR_CTRL_IMSI_SIZE, pImsi);
            } else {
                cellularPortLog("CELLULAR_CTRL: unable to read IMSI.\n");
            }
        }
    }

    return (int32_t) errorCode;
}

// Get the ICCID of the cellular module.
int32_t cellularGetIccidStr(char *pStr, size_t size)
{
    CellularCtrlErrorCode errorCode = CELLULAR_CTRL_NOT_INITIALISED;
    int32_t bytesRead;
    int32_t atError;

    if (gInitialised) {
        errorCode = CELLULAR_CTRL_INVALID_PARAMETER;
        if (pStr != NULL) {
            errorCode = CELLULAR_CTRL_AT_ERROR;
            cellular_ctrl_at_lock();
            cellular_ctrl_at_cmd_start("AT+CCID");
            cellular_ctrl_at_cmd_stop();
            cellular_ctrl_at_resp_start("+CCID:", false);
            bytesRead = cellular_ctrl_at_read_string(pStr, size, false);
            cellular_ctrl_at_resp_stop();
            atError = cellular_ctrl_at_unlock_return_error();
            if ((bytesRead >= 0) && (atError == 0)) {
                errorCode = CELLULAR_CTRL_SUCCESS;
                cellularPortLog("CELLULAR_CTRL: ICCID is %s.\n", pStr);
            } else {
                cellularPortLog("CELLULAR_CTRL: unable to read ICCID.\n");
            }
        }
    }

    return (int32_t) errorCode;
}

// Get the manufacturer string from the cellular module.
int32_t cellularGetManufacturerStr(char *pStr, size_t size)
{
    return getString("AT+CGMI", pStr, size);
}

// Get the model string from the cellular module.
int32_t cellularGetModelStr(char *pStr, size_t size)
{
    return getString("AT+CGMM", pStr, size);
}

// Get the firmware version string from the cellular module.
int32_t cellularGetFirmwareVersionStr(char *pStr, size_t size)
{
    return getString("AT+CGMR", pStr, size);
}

// Get the UTC time according to cellular.
int32_t cellularGetTimeUtc()
{
    CellularCtrlErrorCode errorCode = CELLULAR_CTRL_NOT_INITIALISED;
    int32_t timeUtc;
    char buffer[32];
    CellularPort_tm timeInfo;
    int32_t bytesRead;
    int32_t atError;
    int32_t offset = 0;

    if (gInitialised) {
        errorCode = CELLULAR_CTRL_AT_ERROR;
        cellular_ctrl_at_lock();
        cellular_ctrl_at_cmd_start("AT+CCLK?");
        cellular_ctrl_at_cmd_stop();
        cellular_ctrl_at_resp_start("+CCLK:", false);
        bytesRead = cellular_ctrl_at_read_string(buffer, sizeof(buffer), false);
        cellular_ctrl_at_resp_stop();
        atError = cellular_ctrl_at_unlock_return_error();
        if ((bytesRead >= 17) && (atError == 0)) {
            cellularPortLog("CELLULAR_CTRL: time is %s.\n", buffer);
            // The format of the returned string is
            // "yy/MM/dd,hh:mm:ss+TZ" but the +TZ may be omitted

            // Two-digit year converted to years since 1900
            offset = 0;
            buffer[offset + 2] = 0;
            timeInfo.tm_year = cellularPort_atoi(&(buffer[offset])) + 2000 - 1900;
            // Months converted to months since January
            offset = 3;
            buffer[offset + 2] = 0;
            timeInfo.tm_mon = cellularPort_atoi(&(buffer[offset])) - 1;
            // Day of month
            offset = 6;
            buffer[offset + 2] = 0;
            timeInfo.tm_mday = cellularPort_atoi(&(buffer[offset]));
            // Hours since midnight
            offset = 9;
            buffer[offset + 2] = 0;
            timeInfo.tm_hour = cellularPort_atoi(&(buffer[offset]));
            // Minutes after the hour
            offset = 12;
            buffer[offset + 2] = 0;
            timeInfo.tm_min = cellularPort_atoi(&(buffer[offset]));
            // Seconds after the hour
            offset = 15;
            buffer[offset + 2] = 0;
            timeInfo.tm_sec = cellularPort_atoi(&(buffer[offset]));
            // Get the time in seconds from this
            timeUtc = cellularPort_mktime(&timeInfo);
            if ((timeUtc >= 0) && (bytesRead >= 20)) {
                // There's a timezone, expressed in 15 minute intervals,
                // subtract it to get UTC
                offset = 18;
                buffer[offset + 2] = 0;
                timeUtc -= cellularPort_atoi(&(buffer[offset])) * 15 * 60;
            }

            if (timeUtc >= 0) {
                errorCode = CELLULAR_CTRL_SUCCESS;
                cellularPortLog("CELLULAR_CTRL: UTC time is %d.\n", timeUtc);
            } else {
                cellularPortLog("CELLULAR_CTRL: unable to calculate UTC time.\n");
            }
        } else {
            cellularPortLog("CELLULAR_CTRL: unable to read time with AT+CCLK.\n");
        }
    }

    return (errorCode == CELLULAR_CTRL_SUCCESS) ? timeUtc : (int32_t) errorCode;
}

// End of file
