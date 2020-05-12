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

/* Only #includes of cellular_* are allowed here, no C lib,
 * no platform stuff and no OS stuff.  Anything required from
 * the platform/C library/OS must be brought in through
 * cellular_port* to maintain portability.
 */

#ifdef CELLULAR_CFG_OVERRIDE
# include "cellular_cfg_override.h" // For a customer's configuration override
#endif
#include "cellular_cfg_sw.h"
#include "cellular_cfg_module.h"
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
    void (*pFunction) (void *);
    void *pParam;
} CellularCtrlCallback_t;

/** Type to accommodate the types of registration query.
 */
typedef struct {
    CellularCtrlRan_t ran;
    const char *pQueryStr;
    const char *pResponseStr;
} CellularCtrlRegTypes_t;

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
 * module, i.e. the pin that is connected to the PWR_ON pin of the
 * module.
 */
static int32_t gPinPwrOn;

/** The GPIO pin to the VInt output on the cellular module.
 */
static int32_t gPinVInt;

/** The UART number we will be using to talk to the
 * cellular module.
 */
static int32_t gUart;

/** The number of consecutive timeouts on the AT interface.
 */
static int32_t gAtNumConsecutiveTimeouts;

/** The current registration statuses, one for each RAN.
 */
static CellularCtrlNetworkStatus_t gNetworkStatus[CELLULAR_CTRL_MAX_NUM_RANS];

/** The RSSI of the serving cell.
 */
static int32_t gRssiDbm;

/** The RSRP of the serving cell.
 */
static int32_t gRsrpDbm;

/** The RSRQ of the serving cell.
 */
static int32_t gRsrqDb;

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
 * +CEREG URC to CellularCtrlNetworkStatus_t.
 */
static const CellularCtrlNetworkStatus_t gStatus3gppToCellularNetworkStatus[] =
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

/** Table to convert CellularCtrlRat_t to the value used in the module.
 */
static const uint8_t gCellularRatToLocalRat[] =
    {255, // dummy value for CELLULAR_CTRL_RAT_UNKNOWN
     9,   // CELLULAR_CTRL_RAT_GPRS  - GPRS single mode (TODO: only correct for SARA-R4)
     2,   // CELLULAR_CTRL_RAT_UMTS  - UMTS single mode
     3,   // CELLULAR_CTRL_RAT_LTE   - LTE single mode
     7,   // CELLULAR_CTRL_RAT_CATM1 - LTE cat.M1
     8};  // CELLULAR_CTRL_RAT_NB1   - LTE cat.NB1

/** Table to convert the RAT values used in the module to
 * CellularCtrlRat_t.
 */
static const CellularCtrlRat_t gLocalRatToCellularRat[] =
    {CELLULAR_CTRL_RAT_GPRS,   // 0: GSM / GPRS / eGPRS (single mode)
     CELLULAR_CTRL_RAT_UMTS,   // 1: GSM / UMTS (dual mode)
     CELLULAR_CTRL_RAT_UMTS,   // 2: UMTS (single mode)
     CELLULAR_CTRL_RAT_LTE,    // 3: LTE (single mode)
     CELLULAR_CTRL_RAT_LTE,    // 4: GSM / UMTS / LTE (tri mode)
     CELLULAR_CTRL_RAT_LTE,    // 5: GSM / LTE (dual mode)
     CELLULAR_CTRL_RAT_LTE,    // 6: UMTS / LTE (dual mode)
     CELLULAR_CTRL_RAT_CATM1,  // 7: LTE cat.M1
     CELLULAR_CTRL_RAT_NB1,    // 8: LTE cat.NB1
     CELLULAR_CTRL_RAT_GPRS};  // 9: GPRS / eGPRS

/** Array to convert the LTE RSSI number from AT+CSQ into a
 * dBm value rounded up to the nearest whole number.
 */
static const int32_t gRssiConvertLte[] = {-118, -115, -113, -110, -108, -105, -103, -100,  /* 0 - 7   */
                                          -98,  -95,  -93,  -90,  -88,  -85,  -83,  -80,   /* 8 - 15  */
                                          -78,  -76,  -74,  -73,  -71,  -69,  -68,  -65,   /* 16 - 23 */
                                          -63,  -61,  -60,  -59,  -58,  -55,  -53,  -48};  /* 24 - 31 */

/** Array to convert the RAT emitted by AT+COPS to one of our RATs.
 */
static const CellularCtrlRat_t gCopsRatToCellularRat[] = {CELLULAR_CTRL_RAT_GPRS,                // 0: GSM
                                                          CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED, // 1: GSM Compact
                                                          CELLULAR_CTRL_RAT_UMTS,                // 2: UTRAN
                                                          CELLULAR_CTRL_RAT_GPRS,                // 3: EDGE
                                                          CELLULAR_CTRL_RAT_UMTS,                // 4: UTRAN with HSDPA
                                                          CELLULAR_CTRL_RAT_UMTS,                // 5: UTRAN with HSUPA
                                                          CELLULAR_CTRL_RAT_UMTS,                // 6: UTRAN with HSDPA and HSUPA
                                                          CELLULAR_CTRL_RAT_LTE,                 // 7: LTE or CAT-M1
                                                          CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED, // 8: EC_GSM
                                                          CELLULAR_CTRL_RAT_NB1};                // 9: NB1

/** The possible registration query strings.
 */
static const CellularCtrlRegTypes_t gRegTypes[] = {{CELLULAR_CTRL_RAN_GERAN, "AT+CREG?", "+CREG"},
                                                   {CELLULAR_CTRL_RAN_GERAN, "AT+CGREG?", "+CGREG"},
                                                   {CELLULAR_CTRL_RAN_EUTRAN, "AT+CEREG?", "+CEREG"},
};

// Return the RAN for the given RAT
static const CellularCtrlRan_t gRanForRat[] = {CELLULAR_CTRL_RAN_UNKNOWN_OR_NOT_USED, // CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED
                                               CELLULAR_CTRL_RAN_GERAN,               // CELLULAR_CTRL_RAT_GPRS
                                               CELLULAR_CTRL_RAN_UTRAN,               // CELLULAR_CTRL_RAT_UMTS
                                               CELLULAR_CTRL_RAN_EUTRAN,              // CELLULAR_CTRL_RAT_LTE
                                               CELLULAR_CTRL_RAN_EUTRAN,              // CELLULAR_CTRL_RAT_CATM1
                                               CELLULAR_CTRL_RAN_EUTRAN};             // CELLULAR_CTRL_RAT_NB1

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: URCS AND RELATED FUNCTIONS
 * -------------------------------------------------------------- */

// Set the current network status.
// Deliberately using VERY short debug strings as this
// might be called from a URC.
static void setNetworkStatus(int32_t status, CellularCtrlRan_t ran)
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
        gNetworkStatus[ran] = gStatus3gppToCellularNetworkStatus[status];
    }
}

// Registration on a network (AT+CREG/CGREG/CEREG).
static inline void CXREG_urc(CellularCtrlRan_t ran)
{
    int32_t status;

    // Read status
    status = cellular_ctrl_at_read_int();
    // Check that status was there AND there is no
    // subsequent character: if there is this wasn't a URC
    // it was the +CxREG:x,y response to an AT+CxREG?,
    // which got into here by mistake
    if ((status >= 0) && (cellular_ctrl_at_read_int() < 0)) {
        setNetworkStatus(status, ran);
    }
}

// Registration on a GSM network (AT+CREG).
static void CREG_urc(void *pUnused)
{
    (void) pUnused;

    CXREG_urc(CELLULAR_CTRL_RAN_GERAN);
}

// Registration on a GPRS network (AT+CGREG).
static void CGREG_urc(void *pUnused)
{
    (void) pUnused;

    CXREG_urc(CELLULAR_CTRL_RAN_GERAN);
}

// Registration on an EUTRAN (LTE) network (AT+CEREG).
static void CEREG_urc(void *pUnused)
{
    (void) pUnused;

    CXREG_urc(CELLULAR_CTRL_RAN_EUTRAN);
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
        if (cellularPort_iscntrl((int32_t) *(pString + x - numCharsRemoved))) {
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
    gRsrqDb = 0;
    gCellId = -1;
    gEarfcn = -1;
}

// Check that the cellular module is alive.
static CellularCtrlErrorCode_t moduleIsAlive(int32_t attempts)
{
    CellularCtrlErrorCode_t errorCode = CELLULAR_CTRL_NOT_RESPONDING;
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

// Configure one item in the cellular module.
static bool moduleConfigureOne(int32_t uart,
                               char *pAtString)
{
    bool success = false;

    cellular_ctrl_at_lock();
    cellular_ctrl_at_cmd_start(pAtString);
    cellular_ctrl_at_cmd_stop_read_resp();
    if (cellular_ctrl_at_unlock_return_error() == 0) {
        success = true;
    }

    return success;
}

// Configure the cellular module.
static CellularCtrlErrorCode_t moduleConfigure(int32_t uart)
{
    CellularCtrlErrorCode_t errorCode = CELLULAR_CTRL_NOT_CONFIGURED;

    // Configure the module
    if (moduleConfigureOne(uart, "ATE0") && // Echo off
        // Extended errors on
        moduleConfigureOne(uart, "AT+CMEE=2") &&
        // DCD circuit (109) changes in accordance with the carrier
        moduleConfigureOne(uart, "AT&C1") &&
        // Ignore changes to DTR
        moduleConfigureOne(uart, "AT&D0") &&
#ifdef CELLULAR_CFG_MODULE_SARA_R4
        // Switch on channel and environment reporting for EUTRAN
        moduleConfigureOne(uart, "AT+UCGED=5") &&
#endif
        // TODO switch off power saving until it is integrated into this API
        moduleConfigureOne(uart, "AT+CPSMS=0") && 
        // Stay in airplane mode until commanded to connect
        moduleConfigureOne(uart, "AT+CFUN=4")) {
        // TODO: check if AT&K3 requires both directions
        // of flow control to be on or just one of them
        if (cellularPortIsRtsFlowControlEnabled(uart) &&
            cellularPortIsCtsFlowControlEnabled(uart)) {
            if (moduleConfigureOne(uart, "AT&K3")) { // RTS/CTS handshaking on
                errorCode = CELLULAR_CTRL_SUCCESS;
            }
        } else {
            if (moduleConfigureOne(uart, "AT&K0")) { // RTS/CTS handshaking off
                errorCode = CELLULAR_CTRL_SUCCESS;
            }
        }
    }

    return errorCode;
}

// Get an ID string from the cellular module.
static int32_t getString(const char *pCmd, char *pBuffer, size_t bufferSize)
{
    CellularCtrlErrorCode_t errorCodeOrSize = CELLULAR_CTRL_NOT_INITIALISED;
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
            bytesRead = cellular_ctrl_at_read_string(pBuffer, bufferSize, true);
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
    cellular_ctrl_at_set_urc_handler("+CREG:", CREG_urc, NULL);
    cellular_ctrl_at_set_urc_handler("+CGREG:", CGREG_urc, NULL);
    cellular_ctrl_at_set_urc_handler("+CEREG:", CEREG_urc, NULL);

    // Switch on the unsolicited result codes for registration
    cellular_ctrl_at_lock();
    cellular_ctrl_at_cmd_start("AT+CREG=1");
    cellular_ctrl_at_cmd_stop_read_resp();
    if (cellular_ctrl_at_unlock_return_error() == 0) {
        cellular_ctrl_at_lock();
        cellular_ctrl_at_cmd_start("AT+CGREG=1");
        cellular_ctrl_at_cmd_stop_read_resp();
        if (cellular_ctrl_at_unlock_return_error() == 0) {
            cellular_ctrl_at_lock();
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
                } else {
                    cellularPortLog("CELLULAR_CTRL: unable to set automatic network selection mode.\n");
                }
            } else {
                cellularPortLog("CELLULAR_CTRL: unable to set +CEREG URCs.\n");
            }
        } else {
            cellularPortLog("CELLULAR_CTRL: unable to set +CGREG URC.\n");
        }
    } else {
        cellularPortLog("CELLULAR_CTRL: unable to set +CREG URC.\n");
    }

    return success;
}

// Register with the cellular network and obtain a PDP context.
static CellularCtrlErrorCode_t tryConnect(bool (*pKeepGoingCallback) (void),
                                          const char *pApn,
                                          const char *pUsername,
                                          const char *pPassword)
{
    CellularCtrlErrorCode_t errorCode = CELLULAR_CTRL_AT_ERROR;
    bool keepGoing = true;
    bool attached = false;
    bool activated = false;
    int32_t regType;
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
    regType = 0;
    while (keepGoing && pKeepGoingCallback() && !cellularCtrlIsRegistered()) {
        // Prod the modem anyway, we've nout much else to do
        cellular_ctrl_at_lock();
        cellular_ctrl_at_set_at_timeout(CELLULAR_CTRL_COMMAND_MINIMUM_RESPONSE_TIME_MS, false);
        cellular_ctrl_at_cmd_start(gRegTypes[regType].pQueryStr);
        cellular_ctrl_at_cmd_stop();
        cellular_ctrl_at_resp_start(gRegTypes[regType].pResponseStr, false);
        // Ignore the first parameter
        cellular_ctrl_at_read_int();
        status = cellular_ctrl_at_read_int();
        if (status >= 0) {
            setNetworkStatus(status, gRegTypes[regType].ran);
        } else {
            // Dodge for SARA-R4: it's not supposed to
            // but SARA-R4 can spit-out a "+CxREG: y" URC
            // while we're waiting for the "+CxREG: x,y"
            // response from the AT+CxREG command.
            // If that happens status will be -1 'cos
            // there's only a single integer in the URC.
            // So now wait for the actual response
            cellular_ctrl_at_resp_start(gRegTypes[regType].pResponseStr, false);
            cellular_ctrl_at_read_int();
            status = cellular_ctrl_at_read_int();
            if (status >= 0) {
                setNetworkStatus(status, gRegTypes[regType].ran);
            }
        }
        cellular_ctrl_at_resp_stop();
        cellular_ctrl_at_restore_at_timeout();
        if (cellular_ctrl_at_unlock_return_error() != 0) {
            keepGoing = false;
        } else {
            cellularPortTaskBlock(300);
        }
        regType++;
        if (regType >= sizeof(gRegTypes) / sizeof(gRegTypes[0])) {
            regType = 0;
        }
    }

    if (keepGoing && pKeepGoingCallback()) {
        if (cellularCtrlIsRegistered()) {
            if (cellularCtrlGetOperatorStr(buffer, sizeof(buffer)) >= 0) {
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
                        cellular_ctrl_at_restore_at_timeout();
                        if (cellular_ctrl_at_unlock_return_error() == 0) {
#ifdef CELLULAR_CFG_MODULE_SARA_R4
                            // SARA-R4 only supports a single context at any
                            // one time and so doesn't require that.
                            errorCode = CELLULAR_CTRL_SUCCESS;
#else
                            // Use AT+UPSD to map the context to an internal
                            // modem profile e.g. AT+UPSD=0,100,1, then
                            // activate that profile e.g. AT+UPSDA=0,3.
                            cellular_ctrl_at_lock();
                            // Map profile ID CELLULAR_CTRL_PROFILE_ID to
                            // context ID CELLULAR_CTRL_CONTEXT_ID
                            cellular_ctrl_at_cmd_start("AT+UPSD=");
                            cellular_ctrl_at_write_int(CELLULAR_CTRL_PROFILE_ID);
                            cellular_ctrl_at_write_int(100);
                            cellular_ctrl_at_write_int(CELLULAR_CTRL_CONTEXT_ID);
                            cellular_ctrl_at_cmd_stop_read_resp();
                            // Activate profile ID CELLULAR_CTRL_PROFILE_ID
                            cellular_ctrl_at_cmd_start("AT+UPSDA=");
                            cellular_ctrl_at_write_int(CELLULAR_CTRL_PROFILE_ID);
                            cellular_ctrl_at_write_int(3);
                            cellular_ctrl_at_cmd_stop_read_resp();
                            if (cellular_ctrl_at_unlock_return_error() == 0) {
                                errorCode = CELLULAR_CTRL_SUCCESS;
                            }
#endif
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

// Wait for power off to complete
void waitForPowerOff(bool (*pKeepGoingCallback) (void),
                     int32_t pinVInt,
                     int32_t timeoutSeconds)
{
    bool moduleIsOff = false;
    int64_t endTimeMs = cellularPortGetTickTimeMs() + (timeoutSeconds * 1000);

    while (!moduleIsOff &&
           (cellularPortGetTickTimeMs() < endTimeMs) &&
           ((pKeepGoingCallback == NULL) ||
            pKeepGoingCallback())) {
        if (pinVInt >= 0) {
            // If we have a VInt pin then wait until that
            // goes low
            moduleIsOff = (cellularPortGpioGet(pinVInt) == 0);
        } else {
            // Wait for the module to stop responding at the AT interface
            // by poking it with "AT"
            cellular_ctrl_at_lock();
            cellular_ctrl_at_set_at_timeout(CELLULAR_CTRL_COMMAND_MINIMUM_RESPONSE_TIME_MS, false);
            cellular_ctrl_at_cmd_start("AT");
            cellular_ctrl_at_cmd_stop_read_resp();
            moduleIsOff = (cellular_ctrl_at_get_last_error() != 0);
            cellular_ctrl_at_restore_at_timeout();
            cellular_ctrl_at_unlock();
        }
        // Relax a bit
        cellularPortTaskBlock(1000);
    }
}

#ifdef CELLULAR_CFG_MODULE_SARA_R5

// Convert RSRP in 36.133 format to dBm.
// Returns 0 if the number is not known.
// 0: -141 dBm or less,
// 1..96: from -140 dBm to -45 dBm with 1 dBm steps,
// 97: -44 dBm or greater,
// 255: not known or not detectable.
static int32_t rsrpToDbm(int32_t rsrp)
{
    int32_t rsrpDbm = 0;

    if (rsrp <= 97) {
        rsrpDbm = rsrp - 97 - 44;
        if (rsrpDbm < -141) {
            rsrpDbm = -141;
        }
    }

    return rsrpDbm;
}

// Convert RSRQ in 36.133 format to dB.
// Returns 0 if the number is not known.
// 0: less than -19.5 dB
// 1..33: from -19.5 dB to -3.5 dB with 0.5 dB steps
// 34: -3 dB or greater
// 255: not known or not detectable.
static int32_t rsrqToDb(int32_t rsrq)
{
    int32_t rsrqDb = 0;

    if (rsrq <= 34) {
        rsrqDb = (rsrq - 34 - 6) / 2;
        if (rsrqDb < -19) {
            rsrqDb = -19;
        }
    }

    return rsrqDb;
}

#endif

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS
 * -------------------------------------------------------------- */

// Initialise the cellular control driver.
int32_t cellularCtrlInit(int32_t pinEnablePower,
                         int32_t pinPwrOn,
                         int32_t pinVInt,
                         bool leavePowerAlone,
                         int32_t uart,
                         CellularPortQueueHandle_t queueUart)
{
    CellularCtrlErrorCode_t errorCode = CELLULAR_CTRL_NOT_INITIALISED;
    int32_t platformError = 0;
    CellularPortGpioConfig_t gpioConfig = CELLULAR_PORT_GPIO_CONFIG_DEFAULT;
    int32_t enablePowerAtStart;

    if (!gInitialised) {
        errorCode = CELLULAR_CTRL_PLATFORM_ERROR;
        cellularPortLog("CELLULAR_CTRL: initialising with enable power pin ");
        if (pinEnablePower >= 0) {
            cellularPortLog("%d, ", pinEnablePower);
        } else {
            cellularPortLog("not connected, ");
        }
        cellularPortLog("PWR_ON pin %d", pinPwrOn);
        if (leavePowerAlone) {
            cellularPortLog(", leaving the level of both those pins alone");
        }
        if (pinVInt >= 0) {
            cellularPortLog(" and VInt pin %d.\n", pinVInt);
        } else {
            cellularPortLog(", VInt pin not connected.\n");
        }
        gpioConfig.pin = pinPwrOn;
        if (!leavePowerAlone) {
            // Set PWR_ON high so that we can pull it low
            platformError = cellularPortGpioSet(pinPwrOn, 1);
        }
        if (platformError == 0) {
            // PWR_ON open drain so that we can pull it low and then let it
            // float afterwards since it is pulled-up by the cellular module
            // TODO: the u-blox C030-R412M board requires a pull-up here.
            gpioConfig.pullMode = CELLULAR_PORT_GPIO_PULL_MODE_PULL_UP;
            gpioConfig.driveMode = CELLULAR_PORT_GPIO_DRIVE_MODE_OPEN_DRAIN;
            gpioConfig.direction = CELLULAR_PORT_GPIO_DIRECTION_OUTPUT;
            platformError = cellularPortGpioConfig(&gpioConfig);
            if (platformError == 0) {
                gpioConfig.pullMode = CELLULAR_PORT_GPIO_PULL_MODE_NONE;
                if (pinEnablePower >= 0) {
                    gpioConfig.driveMode = CELLULAR_PORT_GPIO_DRIVE_MODE_NORMAL;
                    gpioConfig.pin = pinEnablePower;
                     // Input/output so we can read it as well
                    gpioConfig.direction = CELLULAR_PORT_GPIO_DIRECTION_INPUT_OUTPUT;
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
                        gpioConfig.direction = CELLULAR_PORT_GPIO_DIRECTION_INPUT;
                        platformError = cellularPortGpioConfig(&gpioConfig);
                        if (platformError != 0) {
                            cellularPortLog("CELLULAR_CTRL: cellularPortGpioConfig() for VInt pin %d returned error code %d.\n",
                                            pinVInt, platformError);
                        }
                    }
                    if (platformError == 0) {
                        // With that all done, initialise the AT command parser
                        errorCode = cellular_ctrl_at_init(uart, queueUart);
                        if (errorCode == 0) {
                            cellular_ctrl_at_set_at_timeout(CELLULAR_CTRL_COMMAND_TIMEOUT_MS, true);
                            gPinEnablePower = pinEnablePower;
                            gPinPwrOn = pinPwrOn;
                            gPinVInt = pinVInt;
                            gUart = uart;
                            for (size_t x = 0; x < sizeof(gNetworkStatus) / sizeof(gNetworkStatus[0]); x++) {
                                gNetworkStatus[x] = CELLULAR_CTRL_NETWORK_STATUS_UNKNOWN;
                            }
                            clearRadioParameters();
                            gAtNumConsecutiveTimeouts = 0;
                            cellular_ctrl_at_set_at_timeout_callback(atTimeoutCallback);
                            gInitialised = true;
                        }
                    }
                }
            } else {
                cellularPortLog("CELLULAR_CTRL: cellularPortGpioConfig() for PWR_ON pin %d returned error code %d.\n",
                                pinPwrOn, platformError);
            }
        } else {
            cellularPortLog("CELLULAR_CTRL: cellularPortGpioSet() for PWR_ON pin %d returned error code %d.\n",
                            pinPwrOn, platformError);
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
        cellular_ctrl_at_deinit(gUart);
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
    bool isAlive = false;

    if (gInitialised) {
        isAlive = (moduleIsAlive(1) == CELLULAR_CTRL_SUCCESS);
    }

    return isAlive;
}

// Power the cellular module on.
int32_t cellularCtrlPowerOn(const char *pPin)
{
    CellularCtrlErrorCode_t errorCode = CELLULAR_CTRL_NOT_INITIALISED;
    int32_t platformError = 0;
    int32_t enablePowerAtStart = 1;

    if (gInitialised) {
        if (gPinEnablePower >= 0) {
            enablePowerAtStart = cellularPortGpioGet(gPinEnablePower);
        }
        errorCode = CELLULAR_CTRL_PIN_ENTRY_NOT_SUPPORTED;
        if (pPin == NULL) {
            errorCode = CELLULAR_CTRL_PLATFORM_ERROR;
            // For some modules the power-on pulse on PWR_ON and the
            // power-off pulse on PWR_ON are the the same duration,
            // in effect a toggle.  To avoid accidentally powering
            // the module off, check if it is already on.
            // Note: doing this even if there is an enable power
            // pin for safety sake
            if (((gPinVInt >= 0) && cellularPortGpioGet(gPinVInt)) ||
                (moduleIsAlive(1) == CELLULAR_CTRL_SUCCESS)) {
                cellularPortLog("CELLULAR_CTRL: powering on, module is already on, flushing...\n");
                // Configure the module
                errorCode = moduleConfigure(gUart);
            } else {
                cellularPortLog("CELLULAR_CTRL: powering on.\n");
                // First, switch on the volts
                if (gPinEnablePower >= 0) {
                    platformError = cellularPortGpioSet(gPinEnablePower, 1);
                }
                if (platformError == 0) {
                    // Wait for things to settle
                    cellularPortTaskBlock(100);
                    platformError = cellularPortGpioSet(gPinPwrOn, 0);
                    if (platformError == 0) {
#ifdef CELLULAR_CFG_MODULE_SARA_R4
                        // SARA-R412M is powered on by holding the PWR_ON pin low
                        // for more than 0.15 seconds
                        cellularPortTaskBlock(300);
#endif
#ifdef CELLULAR_CFG_MODULE_SARA_R5
                        // SARA-R5 is powered on by holding the PWR_ON pin low
                        // for more than 1 second
                        cellularPortTaskBlock(1200);
#endif
                        // Not bothering with checking return code here
                        // as it would have barfed on the last one if
                        // it were going to
                        cellularPortGpioSet(gPinPwrOn, 1);
                        cellularPortTaskBlock(CELLULAR_CTRL_BOOT_WAIT_TIME_MS);
#ifdef CELLULAR_CFG_MODULE_SARA_R5
                        // SARA-R5 chucks out a load of stuff after
                        // boot at the moment: flush it away
                        char buffer[8];
                        while (cellularPortUartRead(gUart, buffer, sizeof(buffer)) > 0) {}
#endif
                        // Cellular module should be up, see if it's there
                        // and, if so, configure it
                        errorCode = moduleIsAlive(CELLULAR_CTRL_IS_ALIVE_ATTEMPTS_POWER_ON);
                        if (errorCode == CELLULAR_CTRL_SUCCESS) {
                            // Configure the module
                            errorCode = moduleConfigure(gUart);
                        }
                        // If we were off at the start and power-on was
                        // unsuccessful then go back to that state
                        if ((errorCode != CELLULAR_CTRL_SUCCESS) && (enablePowerAtStart == 0)) {
                            cellularCtrlPowerOff(NULL);
                        }
                    } else {
                        cellularPortLog("CELLULAR_CTRL: cellularPortGpioSet() for PWR_ON pin %d returned error code %d.\n",
                                        gPinPwrOn, platformError);
                    }
                } else {
                    cellularPortLog("CELLULAR_CTRL: cellularPortGpioSet() for enable power pin %d returned error code%d.\n",
                                    gPinEnablePower, platformError);
                }
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
    if (gInitialised) {
        cellularPortLog("CELLULAR_CTRL: powering off with AT command.\n");
        // Send the power off command and then pull the power
        // No error checking, we're going dowwwwwn...
        cellular_ctrl_at_lock();
        // Clear out the old RF readings
        clearRadioParameters();
        cellular_ctrl_at_cmd_start("AT+CPWROFF");
        cellular_ctrl_at_cmd_stop_read_resp();
        cellular_ctrl_at_unlock();
        // Wait for the module to power down
        waitForPowerOff(pKeepGoingCallback, gPinVInt,
                        CELLULAR_CTRL_POWER_DOWN_WAIT_SECONDS);
        // Now switch off power if possible
        if (gPinEnablePower >= 0) {
            cellularPortGpioSet(gPinEnablePower, 0);
        }
        cellularPortGpioSet(gPinPwrOn, 1);
        gAtNumConsecutiveTimeouts = 0;
    }
}

// Remove power to the cellular module.
void cellularCtrlHardPowerOff(bool trulyHard, bool (*pKeepGoingCallback) (void))
{
    if (gInitialised) {
        // If we have control of power and the user
        // wants a truly hard power off then just do it.
        if (trulyHard && (gPinEnablePower > 0)) {
           cellularPortLog("CELLULAR_CTRL: powering off by pulling the power.\n");
            cellularPortGpioSet(gPinEnablePower, 0);
        } else {
            cellularPortLog("CELLULAR_CTRL: powering off using the PWR_ON pin.\n");
            cellularPortGpioSet(gPinPwrOn, 0);
            // Both SARA-R412M and SARA-R5 are powered off by
            // holding the PWR_ON pin low for more than 1.5 seconds
            cellularPortTaskBlock(2000);
            cellularPortGpioSet(gPinPwrOn, 1);
            // Clear out the old RF readings
            clearRadioParameters();
            // Wait for the module to power down
            waitForPowerOff(pKeepGoingCallback, gPinVInt,
                            CELLULAR_CTRL_POWER_DOWN_WAIT_SECONDS);
            // Now switch off power if possible
            if (gPinEnablePower > 0) {
                cellularPortGpioSet(gPinEnablePower, 0);
            }
        }
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
    CellularCtrlErrorCode_t errorCode = CELLULAR_CTRL_NOT_INITIALISED;

    if (gInitialised) {
        errorCode = CELLULAR_CTRL_AT_ERROR;
        cellularPortLog("CELLULAR_CTRL: rebooting.\n");
        cellular_ctrl_at_lock();
        cellular_ctrl_at_set_at_timeout(CELLULAR_CTRL_REBOOT_COMMAND_WAIT_TIME_MS,
                                        false);
        // Clear out the old RF readings
        clearRadioParameters();
#ifdef CELLULAR_CFG_MODULE_SARA_R5
        // SARA-R5 doesn't support 15 (which doesn't reset the SIM)
        cellular_ctrl_at_cmd_start("AT+CFUN=16");
#else
        cellular_ctrl_at_cmd_start("AT+CFUN=15");
#endif
        cellular_ctrl_at_cmd_stop_read_resp();
        cellular_ctrl_at_restore_at_timeout();
        if (cellular_ctrl_at_unlock_return_error() == 0) {
            // Wait for the module to boot
            cellularPortTaskBlock(CELLULAR_CTRL_BOOT_WAIT_TIME_MS);
#ifdef CELLULAR_CFG_MODULE_SARA_R5
            // SARA-R5 chucks out a load of stuff after
            // boot at the moment: flush it away
            char buffer[8];
            while (cellularPortUartRead(gUart, buffer, sizeof(buffer)) > 0) {}
#endif
            // Wait for the module to return to life
            // and configure it
            errorCode = moduleIsAlive(CELLULAR_CTRL_IS_ALIVE_ATTEMPTS_POWER_ON);
            if (errorCode == CELLULAR_CTRL_SUCCESS) {
                // Configure the module
                errorCode = moduleConfigure(gUart);
            }
            gAtNumConsecutiveTimeouts = 0;
        }
    }

    return (int32_t) errorCode;
}

// Set the bands to be used by the cellular module.
int32_t cellularCtrlSetBandMask(CellularCtrlRat_t rat,
                                uint64_t bandMask1,
                                uint64_t bandMask2)
{
    CellularCtrlErrorCode_t errorCode = CELLULAR_CTRL_NOT_INITIALISED;

    if (gInitialised) {
        errorCode = CELLULAR_CTRL_INVALID_PARAMETER;
        if ((rat == CELLULAR_CTRL_RAT_CATM1) ||
            (rat == CELLULAR_CTRL_RAT_NB1)) {
            errorCode = CELLULAR_CTRL_AT_ERROR;
            cellularPortLog("CELLULAR_CTRL: setting band mask for RAT %d (in module terms %d) to 0x%08x%08x %08x%08x.\n",
                            rat, gCellularRatToLocalRat[rat] -
                                 gCellularRatToLocalRat[CELLULAR_CTRL_RAT_CATM1],
                                 (uint32_t) (bandMask2 >> 32), (uint32_t) bandMask2,
                                 (uint32_t) (bandMask1 >> 32), (uint32_t) bandMask1);
            cellular_ctrl_at_lock();
            // Note: the RAT numbering for this AT command is NOT the same
            // as the RAT numbering for all the other AT commands:
            // here CELLULAR_CTRL_RAT_CATM1 is 0 and CELLULAR_CTRL_RAT_NB1 is 1
            cellular_ctrl_at_cmd_start("AT+UBANDMASK=");
            cellular_ctrl_at_write_int(gCellularRatToLocalRat[rat] - gCellularRatToLocalRat[CELLULAR_CTRL_RAT_CATM1]);
            cellular_ctrl_at_write_uint64(bandMask1);
            cellular_ctrl_at_write_uint64(bandMask2);
            cellular_ctrl_at_cmd_stop_read_resp();
            if (cellular_ctrl_at_unlock_return_error() == 0) {
                errorCode = CELLULAR_CTRL_SUCCESS;
            }
        }
    }

    return (int32_t) errorCode;
}

// Get the bands being used by the cellular module.
int32_t cellularCtrlGetBandMask(CellularCtrlRat_t rat,
                                uint64_t *pBandMask1,
                                uint64_t *pBandMask2)
{
    CellularCtrlErrorCode_t errorCode = CELLULAR_CTRL_NOT_INITIALISED;
    uint64_t i[6];
    uint64_t masks[2][2];
    int32_t rats[2];
    bool success = true;
    size_t count = 0;

    if (gInitialised) {
        errorCode = CELLULAR_CTRL_INVALID_PARAMETER;
        if (((rat == CELLULAR_CTRL_RAT_CATM1) ||
             (rat == CELLULAR_CTRL_RAT_NB1)) &&
            (pBandMask1 != NULL) &&
            (pBandMask2 != NULL)) {

            errorCode = CELLULAR_CTRL_AT_ERROR;

            // Initialise locals
            for (size_t x = 0; x < sizeof(i) / sizeof(i[0]); x++) {
                i[x] = -1;
            }
            pCellularPort_memset(masks, 0, sizeof(masks));
            for (size_t x = 0; x < sizeof(rats) / sizeof(rats[0]); x++) {
                rats[x] = -1;
            }

            cellularPortLog("CELLULAR_CTRL: getting band mask for RAT %d (in module terms %d).\n",
                             rat, gCellularRatToLocalRat[rat] -
                                  gCellularRatToLocalRat[CELLULAR_CTRL_RAT_CATM1]);
            cellular_ctrl_at_lock();
            cellular_ctrl_at_cmd_start("AT+UBANDMASK?");
            cellular_ctrl_at_cmd_stop();
            cellular_ctrl_at_resp_start("+UBANDMASK:", false);
            // The AT response here can be any one of the following:
            //    0        1             2             3           4                 5
            // <rat_a>,<bandmask_a0>
            // <rat_a>,<bandmask_a0>,<bandmask_a1>
            // <rat_a>,<bandmask_a0>,<rat_b>,      <bandmask_b0>
            // <rat_a>,<bandmask_a0>,<bandmask_a1>,<rat_b>,      <bandmask_b0>
            // <rat_a>,<bandmask_a0>,<rat_b>,      <bandmask_b0>,<bandmask_b1>                  <-- ASSUMED THIS CANNOT HAPPEN!!!
            // <rat_a>,<bandmask_a0>,<bandmask_a1>,<rat_b>,      <bandmask_b0>,  <bandmask_b1>
            //
            // Since each entry is just a decimal number, how to tell which format
            // is being used?
            //
            // Here's my algorithm:
            // i.   Read i0 and i1, <rat_a> and <bandmask_a0>.
            // ii.  Attempt to read i2: if is present it could be
            //      <bandmask_a1> or <rat_b>, if not FINISH.
            // iii. Attempt to read i3: if it is present then it is
            //      either <bandmask_b0> or <rat_b>, if it
            //      is not present then the i2 was <bandmask_a1> FINISH.
            // iv.  Attempt to read i4 : if it is present then i2
            //      was <bandmask_a1>, i3 was <rat_b> and i4 is
            //      <bandmask_b0>, if it is not present then i2 was
            //      <rat_b> and i3 was <bandmask_b0> FINISH.
            // v.   Attempt to read i5: if it is present then it is
            //      <bandmask_b1>.

            // Read all the numbers in
            for (size_t x = 0; (x < sizeof(i) / sizeof(i[0])) && success; x++) {
                success = (cellular_ctrl_at_read_uint64(&(i[x])) == 0);
                if (success) {
                    count++;
                }
            }
            cellular_ctrl_at_resp_stop();
            cellular_ctrl_at_unlock();

            // Point i, nice and simple, <rat_a> and <bandmask_a0>.
            if (count >= 2) {
                rats[0] = i[0];
                masks[0][0] = i[1];
            }
            if (count >= 3) {
                // Point ii, the "present" part.
                if (count >= 4) {
                    // Point iii, the "present" part.
                    if (count >= 5) {
                        // Point iv, the "present" part, <bandmask_a1>,
                        // <rat_b> and <bandmask_b1>.
                        masks[0][1] = i[2];
                        rats[1] = i[3];
                        masks[1][0] = i[4];
                        if (count >= 6) {
                            // Point v, <bandmask_b1>.
                            masks[1][1] = i[5];
                        }
                    } else {
                        // Point iv, the "not present" part, <rat_b>
                        // and <bandmask_b0>.
                        rats[1] = i[2];
                        masks[1][0] = i[3];
                    }
                } else {
                    // Point iii, the "not present" part, <bandmask_a1>.
                    masks[0][1] = i[2];
                }
            } else {
                // Point ii, the "not present" part, FINISH.
            }

            // Note: the RAT numbering for this AT command is NOT the same
            // as the RAT numbering for all the other AT commands:
            // here CELLULAR_CTRL_RAT_CATM1 is 0 and CELLULAR_CTRL_RAT_NB1 is 1
            // Convert the RAT numbering to keep things simple on the brain
            for (size_t x = 0; x < sizeof(rats) / sizeof(rats[0]); x++) {
                if ((rats[x] >= 0) &&
                    ((rats[x] + gCellularRatToLocalRat[CELLULAR_CTRL_RAT_CATM1]) < sizeof(gLocalRatToCellularRat) /
                                                                                   sizeof(gLocalRatToCellularRat[0]))) {
                    rats[x] = gLocalRatToCellularRat[rats[x] +
                                                     gCellularRatToLocalRat[CELLULAR_CTRL_RAT_CATM1]];
                }
            }

            // Fill in the answers
            for (size_t x = 0; x < sizeof(rats) / sizeof(rats[0]); x++) {
                if (rats[x] == rat) {
                    *pBandMask1 = masks[x][0];
                    *pBandMask2 = masks[x][1];
                    cellularPortLog("CELLULAR_CTRL: band mask for RAT %d (in module terms %d) is 0x%08x%08x %08x%08x.\n",
                                    rat, gCellularRatToLocalRat[rat] -
                                         gCellularRatToLocalRat[CELLULAR_CTRL_RAT_CATM1],
                                         (uint32_t) (*pBandMask2 >> 32), (uint32_t) (*pBandMask2),
                                         (uint32_t) (*pBandMask1 >> 32), (uint32_t) (*pBandMask1));
                    errorCode = CELLULAR_CTRL_SUCCESS;
                }
            }
        }
    }

    return (int32_t) errorCode;
}

// Set the sole radio access technology.
int32_t cellularCtrlSetRat(CellularCtrlRat_t rat)
{
    CellularCtrlErrorCode_t errorCode = CELLULAR_CTRL_NOT_INITIALISED;

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
// TODO: this code works for SARA-R4 and SARA-R5 but it
// won't work for modules which have more of a "mode"
// select URAT command where the RAT in the AT+URAT
// command is multiple, e.g. TOBY where you can select
// GSM / UMTS / LTE (tri mode).  For those kinds of
// modules this code will need revisiting: will need
// to read out what's there and make the RAT/rank
// selection based on that knowledge.  Somehow.
int32_t cellularCtrlSetRatRank(CellularCtrlRat_t rat, int32_t rank)
{
    CellularCtrlErrorCode_t errorCode = CELLULAR_CTRL_NOT_INITIALISED;
    int32_t rats[CELLULAR_CTRL_MAX_NUM_SIMULTANEOUS_RATS];

    if (gInitialised) {
        errorCode = CELLULAR_CTRL_INVALID_PARAMETER;
        // Allow unknown RAT here in order that the caller
        // can remove a RAT from the list
        if ((rat >= CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED) &&
            (rat < CELLULAR_CTRL_MAX_NUM_RATS)) {
            if ((rank >= 0) &&
                (rank < CELLULAR_CTRL_MAX_NUM_SIMULTANEOUS_RATS)) {
                // Assume there are no RATs
                for (size_t x = 0; x < sizeof(rats) / sizeof(rats[0]); x++) {
                    rats[x] = CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED;
                }
                // Get the existing RATs
                errorCode = CELLULAR_CTRL_AT_ERROR;
                for (size_t x = 0; x < sizeof(rats) / sizeof(rats[0]); x++) {
                    rats[x] = cellularCtrlGetRat(x);
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
int32_t cellularCtrlGetRat(int32_t rank)
{
    CellularCtrlErrorCode_t errorCodeOrRat = CELLULAR_CTRL_NOT_INITIALISED;
    CellularCtrlRat_t rats[CELLULAR_CTRL_MAX_NUM_SIMULTANEOUS_RATS];
    int32_t rat;

    // Assume there are no RATs
    for (size_t x = 0; x < sizeof(rats) / sizeof (rats[0]); x++) {
        rats[x] = CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED;
    }

    if (gInitialised) {
        errorCodeOrRat = CELLULAR_CTRL_INVALID_PARAMETER;
        if ((rank >= 0) && (rank < CELLULAR_CTRL_MAX_NUM_SIMULTANEOUS_RATS)) {
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
            errorCodeOrRat = rats[rank];
            cellularPortLog("CELLULAR_CTRL: RATs are:\n");
            for (size_t x = 0; x < sizeof(rats) / sizeof(rats[0]); x++) {
                cellularPortLog("  rank[%d]: %d (in module terms %d).\n",
                                x, rats[x], gCellularRatToLocalRat[rats[x]]);
            }
        }
    }

    return (int32_t) errorCodeOrRat;
}

// Get the rank at which the given RAT is used.
int32_t cellularCtrlGetRatRank(CellularCtrlRat_t rat)
{
    CellularCtrlErrorCode_t errorCodeOrRank = CELLULAR_CTRL_NOT_INITIALISED;
    int32_t y;

    if (gInitialised) {
        errorCodeOrRank = CELLULAR_CTRL_INVALID_PARAMETER;
        if ((rat > CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED) &&
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
                                rat, gCellularRatToLocalRat[rat], errorCodeOrRank);
            } else {
                cellularPortLog("CELLULAR_CTRL: RAT %d (in module terms %d) is not ranked.\n",
                                rat, gCellularRatToLocalRat[rat]);
            }
        }
    }

    return (int32_t) errorCodeOrRank;
}

// Set the MNO Profile.
int32_t cellularCtrlSetMnoProfile(int32_t mnoProfile)
{
    CellularCtrlErrorCode_t errorCode = CELLULAR_CTRL_NOT_INITIALISED;

    if (gInitialised) {
        errorCode = CELLULAR_CTRL_CONNECTED;
        if (!cellularCtrlIsRegistered()) {
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
    CellularCtrlErrorCode_t errorCode = CELLULAR_CTRL_NOT_INITIALISED;
    char imsi[CELLULAR_CTRL_IMSI_SIZE];
    const char *pApnConfig = NULL;
    int64_t startTime;

    if (gInitialised) {
        errorCode = CELLULAR_CTRL_INVALID_PARAMETER;
        if ((pUsername == NULL) ||
            ((pUsername != NULL) && (pPassword != NULL))) {
            errorCode = CELLULAR_CTRL_AT_ERROR;
            if (prepareConnect()) {
                // Set up the APN look-up since none is specified
                if ((pApn == NULL) && (cellularCtrlGetImsi(imsi) == 0)) {
                    pApnConfig = apnconfig(imsi);
                }
                // Now try to connect, potentially multiple times
                startTime = cellularPortGetTickTimeMs();
                do {
                    if (pApnConfig != NULL) {
                        pApn = _APN_GET(pApnConfig);
                        pUsername = _APN_GET(pApnConfig);
                        pPassword = _APN_GET(pApnConfig);
                        cellularPortLog("CELLULAR_CTRL: APN from database is \"%s\".\n", pApn);
                    } else {
                        if (pApn != NULL) {
                            cellularPortLog("CELLULAR_CTRL: user-specified APN is \"%s\".\n", pApn);
                        } else {
                            cellularPortLog("CELLULAR_CTRL: default APN will be used by network.\n");
                        }
                    }
                    // Register and activate PDP context
                    errorCode = tryConnect(pKeepGoingCallback, pApn,
                                           pUsername, pPassword);
                } while ((errorCode != CELLULAR_CTRL_SUCCESS) &&
                         (pApnConfig != NULL) &&
                         (*pApnConfig != 0) &&
                         pKeepGoingCallback());

                if (errorCode == CELLULAR_CTRL_SUCCESS) {
                    cellularPortLog("CELLULAR_CTRL: connected after %d second(s).\n",
                                    (int32_t) ((cellularPortGetTickTimeMs() - startTime) / 1000));
                } else {
                    cellularPortLog("CELLULAR_CTRL: connection attempt stopped after %d second(s).\n",
                                    (int32_t) ((cellularPortGetTickTimeMs() - startTime) / 1000));
                }
                // This to avoid warnings about unused variables when 
                // cellularPortLog() is compiled out
                (void) startTime;
            }
        }
    }

    return (int32_t) errorCode;
}

// Disconnect from the cellular network.
int32_t cellularCtrlDisconnect()
{
    CellularCtrlErrorCode_t errorCode = CELLULAR_CTRL_NOT_INITIALISED;
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
#ifndef CELLULAR_CFG_MODULE_SARA_R4
            // TODO: is this required?
            // Deactivate profile ID CELLULAR_CTRL_PROFILE_ID
            cellular_ctrl_at_lock();
            cellular_ctrl_at_cmd_start("AT+UPSDA=");
            cellular_ctrl_at_write_int(CELLULAR_CTRL_PROFILE_ID);
            cellular_ctrl_at_write_int(4);
            cellular_ctrl_at_cmd_stop_read_resp();
            cellular_ctrl_at_unlock();
            // No need to deactivate the context, it will
            // go when we deregister.
#endif
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
                for (int32_t count = 10;
                     cellularCtrlIsRegistered() && (count > 0);
                     count--) {
                    for (int32_t x = 0; x < sizeof(gRegTypes) / sizeof(gRegTypes[0]); x++) {
                        // Prod the modem to see if it's done
                        cellular_ctrl_at_lock();
                        cellular_ctrl_at_set_at_timeout(CELLULAR_CTRL_COMMAND_MINIMUM_RESPONSE_TIME_MS, false);
                        cellular_ctrl_at_cmd_start(gRegTypes[x].pQueryStr);
                        cellular_ctrl_at_cmd_stop();
                        cellular_ctrl_at_resp_start(gRegTypes[x].pResponseStr, false);
                        // Ignore the first parameter
                        cellular_ctrl_at_read_int();
                        status = cellular_ctrl_at_read_int();
                        if (status >= 0) {
                            setNetworkStatus(status, gRegTypes[x].ran);
                        }
                        cellular_ctrl_at_resp_stop();
                        cellular_ctrl_at_restore_at_timeout();
                        cellular_ctrl_at_unlock();
                        cellularPortTaskBlock(300);
                    }
                }
                if (!cellularCtrlIsRegistered()) {
                    cellular_ctrl_at_remove_urc_handler("+CREG:");
                    cellular_ctrl_at_remove_urc_handler("+CGREG:");
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

// Get the current network registration status on a given RAN.
CellularCtrlNetworkStatus_t cellularCtrlGetNetworkStatus(CellularCtrlRan_t ran)
{
    CellularCtrlErrorCode_t errorCodeOrNetworkStatus = CELLULAR_CTRL_NOT_INITIALISED;

    if (gInitialised) {
        errorCodeOrNetworkStatus = CELLULAR_CTRL_INVALID_PARAMETER;
        if ((ran > 0) && (ran < sizeof(gNetworkStatus) / sizeof(gNetworkStatus[0]))) {
            errorCodeOrNetworkStatus = gNetworkStatus[ran];
            cellularPortLog("CELLULAR_CTRL: network status on RAN %d is %d.\n",
                            ran, errorCodeOrNetworkStatus);
        }
    }

    return errorCodeOrNetworkStatus;
}

// Get the RAN for the given RAT.
int32_t cellularCtrlGetRanForRat(CellularCtrlRat_t rat)
{
    CellularCtrlErrorCode_t errorCodeOrRan = CELLULAR_CTRL_INVALID_PARAMETER;

    if ((rat > 0) && (rat < sizeof(gRanForRat) / sizeof(gRanForRat[0]))) {
        errorCodeOrRan = gRanForRat[rat];
    }

    return (int32_t) errorCodeOrRan;
}

// Get whether we are registered on any RAN or not.
bool cellularCtrlIsRegistered()
{
    bool isRegistered = false;
    size_t x;

    if (gInitialised) {
        for (x = 0; !isRegistered &&
                    (x < sizeof(gNetworkStatus) / sizeof(gNetworkStatus[0])); x++) {
            isRegistered = (gNetworkStatus[x] == CELLULAR_CTRL_NETWORK_STATUS_REGISTERED);
        }
        if (isRegistered) {
            cellularPortLog("CELLULAR_CTRL: registered on RAN %d.\n", x - 1);
        } else {
            cellularPortLog("CELLULAR_CTRL: not registered.\n");
        }
    }

    return isRegistered;
}

// Get the current RAT.
int32_t cellularCtrlGetActiveRat()
{
    CellularCtrlErrorCode_t errorCodeOrRat = CELLULAR_CTRL_NOT_INITIALISED;

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
int32_t cellularCtrlGetOperatorStr(char *pStr, size_t size)
{
    CellularCtrlErrorCode_t errorCodeOrSize = CELLULAR_CTRL_NOT_INITIALISED;
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
    CellularCtrlErrorCode_t errorCode = CELLULAR_CTRL_NOT_INITIALISED;
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
    CellularCtrlErrorCode_t errorCodeOrSize = CELLULAR_CTRL_NOT_INITIALISED;
    int32_t contextId;
    char buffer[CELLULAR_CTRL_IP_ADDRESS_SIZE];

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
                    pCellularPort_strcpy(pStr, buffer);
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
    CellularCtrlErrorCode_t errorCodeOrSize = CELLULAR_CTRL_NOT_INITIALISED;
    int32_t bytesRead;
    int32_t atError;

    if (gInitialised) {
        errorCodeOrSize = CELLULAR_CTRL_INVALID_PARAMETER;
        if (pStr != NULL) {
            errorCodeOrSize = CELLULAR_CTRL_AT_ERROR;
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
                errorCodeOrSize = bytesRead;
                cellularPortLog("CELLULAR_CTRL: APN is %s.\n", pStr);
            } else {
                cellularPortLog("CELLULAR_CTRL: unable to read APN.\n");
            }
        }
    }

    return (int32_t) errorCodeOrSize;
}

// Refresh the radio parameters.
int32_t cellularCtrlRefreshRadioParameters()
{
    CellularCtrlErrorCode_t errorCode = CELLULAR_CTRL_NOT_INITIALISED;
    int32_t x;
#ifdef CELLULAR_CFG_MODULE_SARA_R4
    double rsrx;
    char buf[16];
#endif
#ifdef CELLULAR_CFG_MODULE_SARA_R5
    int32_t bytesRead;
    int32_t atError;
    char *pBuffer;
    char *pStr;
    char *pSave;
#endif

    if (gInitialised) {
        errorCode = CELLULAR_CTRL_NOT_REGISTERED;
        if (cellularCtrlIsRegistered()) {
            errorCode = CELLULAR_CTRL_AT_ERROR;
            gRssiDbm = 0;
            gRsrpDbm = 0;
            gRsrqDb = 0;
            // The mechanisms to get the radio information
            // are different between EUTRAN and GERAN but
            // AT+CSQ works in all cases though it sometimes
            // doesn't return a reading.
            cellular_ctrl_at_lock();
            cellular_ctrl_at_cmd_start("AT+CSQ");
            cellular_ctrl_at_cmd_stop();
            cellular_ctrl_at_resp_start("+CSQ:", false);
            x = cellular_ctrl_at_read_int();
            gRxQual = cellular_ctrl_at_read_int();
            if (gRxQual == 99) {
                gRxQual = -1;
            }
            cellular_ctrl_at_resp_stop();
            // AT+CSQ returns a coded RSSI value
            // The mapping is defined in the array gRssiConvertLte[].
            if (cellular_ctrl_at_unlock_return_error() == 0) {
                if ((x >= 0) && (x < sizeof(gRssiConvertLte) / sizeof(gRssiConvertLte[0]))) {
                    gRssiDbm = gRssiConvertLte[x];
                }
                // Note that AT+UCGED is used
                // rather than AT+CESQ as, in my experience,
                // it is more reliable in reporting answers.
#ifdef CELLULAR_CFG_MODULE_SARA_R5
                // For UCGED=2, which is what SARA-R5
                // supports, the response is a multi-line one:
                // +UCGED: 2
                // <rat>,<svc>,<MCC>,<MNC>
                // <earfcn>,<Lband>,<ul_BW>,<dl_BW>,<tac>,<LcellId>,<PCID>,<mTmsi>,<mmeGrId>,<mmeCode>, <rsrp>,<rsrq>,<Lsinr>,<Lrrc>,<RI>,<CQI>,<avg_rsrp>,<totalPuschPwr>,<avgPucchPwr>,<drx>, <l2w>,<volte_mode>[,<meas_gap>,<tti_bundling>]
                // e.g.
                // 6,4,001,01
                // 2525,5,50,50,e8fe,1a2d001,1,d60814d1,8001,01,28,31,13.75,3,1,10,28,-50,-6,0,255,255,0
                // Malloc some memory to read the whole thing into
                pBuffer = (char *) pCellularPort_malloc(128);
                if (pBuffer != NULL) {
                    cellular_ctrl_at_lock();
                    cellular_ctrl_at_cmd_start("AT+UCGED?");
                    cellular_ctrl_at_cmd_stop();
                    cellular_ctrl_at_resp_start("+UCGED:", false);
                    cellular_ctrl_at_skip_param(1);
                    // Next two lines of response
                    // Don't want characters in the string being interpreted
                    // as delimiters
                    cellular_ctrl_at_set_delimiter(0);
                    cellular_ctrl_at_resp_start(NULL, false);
                    // Read beyond stop tag to ignore \r\n
                    bytesRead = cellular_ctrl_at_read_string(pBuffer, 128, true);
                    cellular_ctrl_at_resp_stop();
                    cellular_ctrl_at_set_default_delimiter();
                    atError = cellular_ctrl_at_unlock_return_error();
                    if ((bytesRead > 0) && (atError == 0)) {
                        // Find the '\r' at the end of the first line and replace it
                        // with ','
                        pStr = pCellularPort_strchr(pBuffer, '\r');
                        if (pStr != NULL) {
                            *pStr = ',';
                            // Remove all the other control characters
                            bytesRead -= strip_ctrl(pBuffer, cellularPort_strlen(pBuffer));
                            if (bytesRead > 0) {
                                // Now find all the bits we want
                                pStr = pCellularPort_strtok_r(pBuffer, ",", &pSave);
                                for (x = 1; pStr != NULL; x++) {
                                    if (x == 5) {
                                        // EARFCN is element 5
                                        gEarfcn = cellularPort_strtol(pStr, NULL, 10);
                                    } else if (x == 11) {
                                        // Physical Cell ID is element 11
                                        gCellId = cellularPort_strtol(pStr, NULL, 10);
                                    } else if (x == 15) {
                                        // RSRP is element 15,
                                        // coded as specified in TS 36.133
                                        gRsrpDbm = rsrpToDbm(cellularPort_strtol(pStr, NULL, 10));
                                    } else if (x == 16) {
                                        // RSRQ is element 16
                                        // coded as specified in TS 36.133
                                        gRsrqDb = rsrqToDb(cellularPort_strtol(pStr, NULL, 10));
                                        errorCode = CELLULAR_CTRL_SUCCESS;
                                   }
                                   pStr = pCellularPort_strtok_r(NULL, ",", &pSave);
                                }
                            }
                        }
                    }
                    // Free memory again
                    cellularPort_free(pBuffer);
                }
#endif
#ifdef CELLULAR_CFG_MODULE_SARA_R4
                // SARA-R4 only supports UCGED=5, and it only
                // supports UCGED at all in EUTRAN mode
                if (cellularCtrlGetNetworkStatus(CELLULAR_CTRL_RAN_EUTRAN) == CELLULAR_CTRL_NETWORK_STATUS_REGISTERED) {
                    cellular_ctrl_at_lock();
                    cellular_ctrl_at_cmd_start("AT+UCGED?");
                    cellular_ctrl_at_cmd_stop();
                    cellular_ctrl_at_resp_start("+RSRP:", false);
                    gCellId = cellular_ctrl_at_read_int();
                    gEarfcn = cellular_ctrl_at_read_int();
                    if (cellular_ctrl_at_read_string(buf, sizeof(buf), false) > 0) {
                        rsrx = cellularPort_strtof(buf, NULL);
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
                        rsrx = cellularPort_strtof(buf, NULL);
                        if (rsrx >= 0) {
                            gRsrqDb = (int32_t) (rsrx + 0.5);
                        } else {
                            gRsrqDb = (int32_t) (rsrx - 0.5);
                        }
                    }
                    cellular_ctrl_at_resp_stop();
                    if (cellular_ctrl_at_unlock_return_error() == 0) {
                        errorCode = CELLULAR_CTRL_SUCCESS;
                    }
                } else {
                    // Can't use AT+UCGED, that's all we can get
                     errorCode = CELLULAR_CTRL_SUCCESS;
                }
#endif
            }
        }
    }

    if (errorCode == CELLULAR_CTRL_SUCCESS) {
        cellularPortLog("CELLULAR_CTRL: radio parameters refreshed:\n");
        cellularPortLog("               RSSI:    %d dBm\n", gRssiDbm);
        cellularPortLog("               RSRP:    %d dBm\n", gRsrpDbm);
        cellularPortLog("               RSRQ:    %d dB\n", gRsrqDb);
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
int32_t cellularCtrlGetRsrqDb()
{
    return gRsrqDb;
}

// Return the RxQual.
int32_t cellularCtrlGetRxQual()
{
    return gRxQual;
}

// Work out SNR from RSSI and RSRP.
int32_t cellularCtrlGetSnrDb(int32_t *pSnrDb)
{
    CellularCtrlErrorCode_t errorCode = CELLULAR_CTRL_NOT_INITIALISED;
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
    CellularCtrlErrorCode_t errorCode = CELLULAR_CTRL_NOT_INITIALISED;
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
            if ((bytesRead == CELLULAR_CTRL_IMEI_SIZE) && (atError == 0)) {
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
int32_t cellularCtrlGetImsi(char *pImsi)
{
    CellularCtrlErrorCode_t errorCode = CELLULAR_CTRL_NOT_INITIALISED;
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
int32_t cellularCtrlGetIccidStr(char *pStr, size_t size)
{
    CellularCtrlErrorCode_t errorCode = CELLULAR_CTRL_NOT_INITIALISED;
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
int32_t cellularCtrlGetManufacturerStr(char *pStr, size_t size)
{
    return getString("AT+CGMI", pStr, size);
}

// Get the model string from the cellular module.
int32_t cellularCtrlGetModelStr(char *pStr, size_t size)
{
    return getString("AT+CGMM", pStr, size);
}

// Get the firmware version string from the cellular module.
int32_t cellularCtrlGetFirmwareVersionStr(char *pStr, size_t size)
{
    return getString("AT+CGMR", pStr, size);
}

// Get the UTC time according to cellular.
int32_t cellularCtrlGetTimeUtc()
{
    CellularCtrlErrorCode_t errorCode = CELLULAR_CTRL_NOT_INITIALISED;
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
