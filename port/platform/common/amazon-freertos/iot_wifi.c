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

/**
 * @file iot_wifi.c
 * @brief Cellular (NOT Wifi!!) interface.
 */

#include "FreeRTOS.h"
#include "string.h"

/* Wifi and cellular interface includes. */
#include "iot_wifi.h"
#include "cellular_cfg_hw.h"
#include "cellular_cfg_module.h"
#include "cellular_cfg_sw.h"
#include "cellular_port_clib.h"
#include "cellular_port_debug.h"
#include "cellular_port.h"
#include "cellular_port_os.h"
#include "cellular_port_uart.h"
#include "cellular_ctrl.h"
#include "cellular_sock.h" // For cellularSockGetHostByName()

/* WiFi configuration includes. */
#include "aws_wifi_config.h"

/*-----------------------------------------------------------*/

// How long to allow for a connection to be made.
#define CELLULAR_CFG_CONNECT_TIMEOUT_SECONDS 180

// Keep track of whether we've been initialised ever.
static bool gInitialised = false;

// Used for keepGoingCallback() timeout.
static int64_t gStopTimeMS;

// UART queue.
static CellularPortQueueHandle_t gQueueHandle = NULL;

/*-----------------------------------------------------------*/

// Callback function for the cellular networkConnect process.
static bool keepGoingCallback()
{
    bool keepGoing = true;

    if (cellularPortGetTickTimeMs() > gStopTimeMS) {
        keepGoing = false;
    }

    return keepGoing;
}

/*-----------------------------------------------------------*/

// Is cellular connected?
BaseType_t WIFI_IsConnected( void )
{
    BaseType_t xRetVal = pdFALSE;

    cellularPortLog("CELLULAR_IOT_WIFI: WIFI_IsConnected() called.\n");

    if (cellularCtrlGetActiveRat() >= 0) {
        xRetVal = pdTRUE;
        cellularPortLog("CELLULAR_IOT_WIFI: ...and we are.\n");
    } else {
        cellularPortLog("CELLULAR_IOT_WIFI: ...and we are NOT.\n");
    }

    return xRetVal;
}

// Turn cellular off.
WIFIReturnCode_t WIFI_Off( void )
{
    cellularPortLog("CELLULAR_IOT_WIFI: WIFI_Off() called.\n");

    cellularCtrlPowerOff(NULL);

    // In order to ensure thread-safe operation
    // we do not deinitialise here, the
    // OS resources remain held.

    return eWiFiSuccess;
}

/*-----------------------------------------------------------*/

// Turn cellular on.  This doubles as the init function
// for cellular.
WIFIReturnCode_t WIFI_On( void )
{
    int32_t errorCode;
    WIFIReturnCode_t retVal = eWiFiFailure;

    cellularPortLog("CELLULAR_IOT_WIFI: WIFI_On() called.\n");

    if (!gInitialised) {
        errorCode = cellularPortInit();
        if (errorCode == 0) {
            errorCode = cellularPortUartInit(CELLULAR_CFG_PIN_TXD,
                                             CELLULAR_CFG_PIN_RXD,
                                             CELLULAR_CFG_PIN_CTS,
                                             CELLULAR_CFG_PIN_RTS,
                                             CELLULAR_CFG_BAUD_RATE,
                                             CELLULAR_CFG_RTS_THRESHOLD,
                                             CELLULAR_CFG_UART,
                                             &gQueueHandle);
            if (errorCode == 0) {
                errorCode = cellularCtrlInit(CELLULAR_CFG_PIN_ENABLE_POWER,
                                             CELLULAR_CFG_PIN_PWR_ON,
                                             CELLULAR_CFG_PIN_VINT,
                                             false,
                                             CELLULAR_CFG_UART,
                                             gQueueHandle);
                if (errorCode == 0) {
                    gInitialised = true;
                    cellularPortLog("CELLULAR_IOT_WIFI: WIFI_On() initialised.\n");
                } else {
                    cellularPortLog("CELLULAR_IOT_WIFI: cellularCtrlInit() failed (%d).\n",
                                    errorCode);
                }
            } else {
                cellularPortLog("CELLULAR_IOT_WIFI: cellularPortUartInit() failed (%d).\n",
                                errorCode);
            }
        } else {
            cellularPortLog("CELLULAR_IOT_WIFI: cellularPortInit() failed (%d).\n",
                            errorCode);
        }
    }

    if (gInitialised && (cellularCtrlPowerOn(NULL) == 0)) {
        cellularPortLog("CELLULAR_IOT_WIFI: WIFI_On() cellular powered on.\n");
        retVal = eWiFiSuccess;
    }

    return retVal;
}

/*-----------------------------------------------------------*/

// Make a cellular connection.
// If an SSID is given it is used as the APN of the cellular network.
// All other fields of pxNetworkParams are ignored.
WIFIReturnCode_t WIFI_ConnectAP( const WIFINetworkParams_t * const pxNetworkParams )
{
    WIFIReturnCode_t retVal = eWiFiFailure;
    const char *pApn = NULL;

    cellularPortLog("CELLULAR_IOT_WIFI: WIFI_ConnectAP() called.\n");

    if ((pxNetworkParams != NULL) && (pxNetworkParams->ucSSIDLength > 0)) {
        pApn = pxNetworkParams->pcSSID;
        cellularPortLog("CELLULAR_IOT_WIFI: connecting to APN \"%s\".\n", pApn);
    }

    gStopTimeMS = cellularPortGetTickTimeMs() + (CELLULAR_CFG_CONNECT_TIMEOUT_SECONDS * 1000);

    if (cellularCtrlConnect(keepGoingCallback, pApn, NULL, NULL) == 0) {
        retVal = eWiFiSuccess;
    }

    return retVal;
}

/*-----------------------------------------------------------*/

// Disconnect cellular.
WIFIReturnCode_t WIFI_Disconnect( void )
{
    WIFIReturnCode_t xRetVal = eWiFiFailure;

    cellularPortLog("CELLULAR_IOT_WIFI: WIFI_Disconnect() called.\n");

    if (cellularCtrlDisconnect() == 0) {
        xRetVal = eWiFiSuccess;
    }

    return xRetVal;
}

/*-----------------------------------------------------------*/

// Reset the cellular module.
WIFIReturnCode_t WIFI_Reset( void )
{
    WIFIReturnCode_t xRetVal = eWiFiFailure;

    cellularPortLog("CELLULAR_IOT_WIFI: WIFI_Reset() called.\n");

    if (cellularCtrlReboot() == 0) {
        xRetVal = eWiFiSuccess;
    }

    return xRetVal;
}

/*-----------------------------------------------------------*/

// Stub for compatibility only, simply returns
// the currently connected cellular network name and RSRP.
WIFIReturnCode_t WIFI_Scan( WIFIScanResult_t * pxBuffer,
                            uint8_t ucNumNetworks )
{
    WIFIReturnCode_t xRetVal = eWiFiFailure;
    char bssid[wificonfigMAX_BSSID_LEN + 1];
    int32_t mcc;
    int32_t mnc;

    cellularPortLog("CELLULAR_IOT_WIFI: WIFI_Scan() called.\n");

    if ((pxBuffer != NULL) && (ucNumNetworks > 0)) {
        // Zero the buffer
        memset(pxBuffer, 0, sizeof(WIFIScanResult_t) * ucNumNetworks);

        // Get the operator string
        if (cellularCtrlGetOperatorStr(pxBuffer->cSSID,
                                       sizeof(pxBuffer->cSSID)) > 0) {
            cellularPortLog("CELLULAR_IOT_WIFI: returning \"%s\" as SSID.\n",
                            pxBuffer->cSSID);
            // The BSSID field is 6 digits, so we can stuff the MCC/MNC in there
            if (cellularCtrlGetMccMnc(&mcc, &mnc) == 0) {
                // Do this in two stages as snprintf() adds a NULL
                // terminator which we don't have room for in ucBSSID
                snprintf(bssid, sizeof(bssid), "%03d%03d", mcc, mnc);
                memcpy(pxBuffer->ucBSSID, bssid, sizeof(pxBuffer->ucBSSID));
                cellularPortLog("CELLULAR_IOT_WIFI: returning \"%.6s\" (MCC/MNC) as BSSID.\n",
                                pxBuffer->ucBSSID);
            }
            // Fill in the RSSI field with the RSRP, 
            // which might just fit most of the time
            if (cellularCtrlRefreshRadioParameters() == 0) {
                pxBuffer->cRSSI = (int8_t) cellularCtrlGetRsrpDbm();
                cellularPortLog("CELLULAR_IOT_WIFI: returning %d (RSRP, which might just fit into an int8_t) as RSSI.\n",
                                pxBuffer->cRSSI);
            }
            // Can't fit anything else in: the EARFCN is larger than
            // an int8_t and xSecurity is meaningless
            pxBuffer->xSecurity  = eWiFiSecurityNotSupported;
            xRetVal = eWiFiSuccess;
        }
    }

    return xRetVal;
}

/*-----------------------------------------------------------*/

// Stub for compatibility only, returns success for AP mode,
// otherwise false.
WIFIReturnCode_t WIFI_SetMode( WIFIDeviceMode_t xDeviceMode )
{
    WIFIReturnCode_t xRetVal = eWiFiFailure;

    cellularPortLog("CELLULAR_IOT_WIFI: WIFI_SetMode() called with mode %d.\n",
                    xDeviceMode);

    if (xDeviceMode == eWiFiModeAP) {
        xRetVal = eWiFiSuccess;
    } else {
        cellularPortLog("CELLULAR_IOT_WIFI: ...which is not supported.\n");
    }

    return xRetVal;
}

/*-----------------------------------------------------------*/

// Stub for compatibility only, always returns success and
// AP mode.
WIFIReturnCode_t WIFI_GetMode( WIFIDeviceMode_t * pxDeviceMode )
{
    WIFIReturnCode_t xRetVal = eWiFiFailure;

    cellularPortLog("CELLULAR_IOT_WIFI: WIFI_GetMode() called, returning %d.\n",
                    eWiFiModeAP);

    if (pxDeviceMode != NULL)
    {
        *pxDeviceMode = eWiFiModeAP;
        xRetVal = eWiFiSuccess;
    }

    return xRetVal;
}

/*-----------------------------------------------------------*/

// Stub for compatibility only, always returns eWiFiNotSupported.
WIFIReturnCode_t WIFI_NetworkAdd( const WIFINetworkProfile_t * const pxNetworkProfile,
                                  uint16_t * pusIndex )
{
    (void) pxNetworkProfile;
    (void) pusIndex;

    cellularPortLog("CELLULAR_IOT_WIFI: WIFI_NetworkAdd() called (not supported).\n");

    return eWiFiNotSupported;
}

/*-----------------------------------------------------------*/

// Stub for compatibility only, always returns eWiFiNotSupported.
WIFIReturnCode_t WIFI_NetworkGet( WIFINetworkProfile_t * pxNetworkProfile,
                                  uint16_t usIndex )
{
    (void) pxNetworkProfile;
    (void) usIndex;

    cellularPortLog("CELLULAR_IOT_WIFI: WIFI_NetworkGet() called (not supported).\n");

    return eWiFiNotSupported;
}

/*-----------------------------------------------------------*/

// Stub for compatibility only, always returns eWiFiNotSupported.
WIFIReturnCode_t WIFI_NetworkDelete( uint16_t usIndex )
{
    (void) usIndex;

    cellularPortLog("CELLULAR_IOT_WIFI: WIFI_NetworkDelete() called (not supported).\n");

    return eWiFiNotSupported;
}

/*-----------------------------------------------------------*/

// Stub for compatibility only, always returns eWiFiNotSupported.
WIFIReturnCode_t WIFI_Ping( uint8_t * pucIPAddr,
                            uint16_t usCount,
                            uint32_t ulIntervalMS )
{
    (void) pucIPAddr;
    (void) usCount;
    (void) ulIntervalMS;

    cellularPortLog("CELLULAR_IOT_WIFI: WIFI_Ping() called (not supported).\n");

    return eWiFiNotSupported;
}

/*-----------------------------------------------------------*/

// Get the local IP address as an array of four uint8_t.
WIFIReturnCode_t WIFI_GetIP( uint8_t * pucIPAddr )
{
    WIFIReturnCode_t xRetVal = eWiFiFailure;
    char buffer[CELLULAR_CTRL_IP_ADDRESS_SIZE];
    int32_t i[4];

    cellularPortLog("CELLULAR_IOT_WIFI: WIFI_GetIP() called.\n");

    if (pucIPAddr != NULL) {
        if (cellularCtrlGetIpAddressStr(buffer) == 0) {
            if (cellularPort_sscanf(buffer, "%d.%d.%d.%d",
                                    &(i[0]), &(i[1]),
                                    &(i[2]), &(i[3])) == sizeof(i) / sizeof(i[0])) {
                for (size_t x = 0; x < sizeof(i) / sizeof(i[0]); x++) {
                    *(pucIPAddr + x) = (uint8_t) i[x];
                }
                cellularPortLog("CELLULAR_IOT_WIFI: WIFI_GetIP() returning %d.%d.%d.%d.\n",
                                i[0], i[1], i[2], i[3]);
                xRetVal = eWiFiSuccess;
            }
        }
    }

    return xRetVal;
}

/*-----------------------------------------------------------*/

// Get the last wificonfigMAX_BSSID_LEN digits of the IMEI.
WIFIReturnCode_t WIFI_GetMAC( uint8_t * pucMac )
{
    WIFIReturnCode_t xRetVal = eWiFiFailure;
    char buffer[CELLULAR_CTRL_IMEI_SIZE];

    cellularPortLog("CELLULAR_IOT_WIFI: WIFI_GetMAC() called.\n");

    if (pucMac != NULL) {
        if (cellularCtrlGetImei(buffer) == 0) {
            memcpy(pucMac,
                   buffer + CELLULAR_CTRL_IMEI_SIZE - wificonfigMAX_BSSID_LEN,
                   wificonfigMAX_BSSID_LEN);
            cellularPortLog("CELLULAR_IOT_WIFI: WIFI_GetMAC() returning last 6 digits of IMEI (\"%.6s\").\n",
                            pucMac);
            xRetVal = eWiFiSuccess;
        }
    }

    return xRetVal;
}

/*-----------------------------------------------------------*/

// Perform a DNS lookup, returning an IP address as four uint8_t.
WIFIReturnCode_t WIFI_GetHostIP( char * pcHost,
                                 uint8_t * pucIPAddr )
{
    WIFIReturnCode_t xRetVal = eWiFiFailure;
    CellularSockIpAddress_t hostIpAddress;

    cellularPortLog("CELLULAR_IOT_WIFI: WIFI_GetHostIP() called.\n");

    if ((pcHost != NULL) && (pucIPAddr != NULL)) {
        cellularPortLog("CELLULAR_IOT_WIFI: WIFI_GetHostIP() looking for \"%s\"...\n",
                        pcHost);
        if (cellularSockGetHostByName(pcHost,
                                      &hostIpAddress) == 0) {
            if (hostIpAddress.type == CELLULAR_SOCK_ADDRESS_TYPE_V4) {
                *(pucIPAddr + 0) = (uint8_t) (hostIpAddress.address.ipv4 >> 0);
                *(pucIPAddr + 1) = (uint8_t) (hostIpAddress.address.ipv4 >> 8);
                *(pucIPAddr + 2) = (uint8_t) (hostIpAddress.address.ipv4 >> 16);
                *(pucIPAddr + 3) = (uint8_t) (hostIpAddress.address.ipv4 >> 24);
                xRetVal = eWiFiSuccess;
                cellularPortLog("CELLULAR_IOT_WIFI: found %d.%d.%d.%d.\n",
                                *(pucIPAddr + 0), *(pucIPAddr + 1),
                                *(pucIPAddr + 2), *(pucIPAddr + 3));
            } else {
                cellularPortLog("CELLULAR_IOT_WIFI: but found an IPv6 address which there is no room to return.\n");
            }
        } else {
            cellularPortLog("CELLULAR_IOT_WIFI: but couldn't find it.\n");
        }
    }

    return xRetVal;
}

/*-----------------------------------------------------------*/

// Stub for compatibility only, always returns success.
WIFIReturnCode_t WIFI_StartAP( void )
{
    cellularPortLog("CELLULAR_IOT_WIFI: WIFI_StartAP() called.\n");

    return eWiFiSuccess;
}
/*-----------------------------------------------------------*/

// Stub for compatibility only, always returns success.
WIFIReturnCode_t WIFI_StopAP( void )
{
    cellularPortLog("CELLULAR_IOT_WIFI: WIFI_StopAP() called.\n");

    return eWiFiSuccess;
}

/*-----------------------------------------------------------*/

// Stub for compatibility only, always returns success.
WIFIReturnCode_t WIFI_ConfigureAP( const WIFINetworkParams_t * const pxNetworkParams )
{
    (void) pxNetworkParams;

    cellularPortLog("CELLULAR_IOT_WIFI: WIFI_ConfigureAP() called.\n");

    return eWiFiSuccess;
}

/*-----------------------------------------------------------*/

// Stub for compatibility only, always returns success.
WIFIReturnCode_t WIFI_SetPMMode( WIFIPMMode_t xPMModeType,
                                 const void * pvOptionValue )
{
    (void) xPMModeType;
    (void) pvOptionValue;

    cellularPortLog("CELLULAR_IOT_WIFI: WIFI_SetPMMode() called.\n");

    return eWiFiSuccess;
}

/*-----------------------------------------------------------*/

// Stub for compatibility only, returns success and
// eWiFiPMAlwaysOn.
WIFIReturnCode_t WIFI_GetPMMode( WIFIPMMode_t * pxPMModeType,
                                 void * pvOptionValue )
{
    WIFIReturnCode_t xRetVal = eWiFiFailure;

    (void) pvOptionValue;

    cellularPortLog("CELLULAR_IOT_WIFI: WIFI_GetPMMode() called, returning %d.\n",
                    eWiFiPMAlwaysOn);

    if (pxPMModeType != NULL) {
        *pxPMModeType = eWiFiPMAlwaysOn;
        xRetVal = eWiFiSuccess;
    }

    return xRetVal;
}

/*-----------------------------------------------------------*/

// Stub for compatibility only, always returns success.
WIFIReturnCode_t WIFI_RegisterNetworkStateChangeEventCallback( IotNetworkStateChangeEventCallback_t xCallback  )
{
    (void) xCallback;

    cellularPortLog("CELLULAR_IOT_WIFI: WIFI_RegisterNetworkStateChangeEventCallback() called.\n");

    return eWiFiSuccess;
}

/*-----------------------------------------------------------*/
