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
 * @brief Cellular (NOT Wifi!!) Interface.
 */

#include "FreeRTOS.h"
#include "string.h"

/* Wifi and cellular interface includes. */
#include "iot_wifi.h"
#include "cellular_cfg_hw.h"
#include "cellular_cfg_module.h"
#include "cellular_port.h"
#include "cellular_port_os.h"
#include "cellular_port_uart.h"
#include "cellular_ctrl.h"

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
static CellularPortQueueHandle_t *gpQueueHandle = NULL;

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

    if (cellularCtrlGetActiveRat() >= 0) {
        xRetVal = pdTRUE;
    }

    return xRetVal;
}

// Turn cellular off.
WIFIReturnCode_t WIFI_Off( void )
{
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
    WIFIReturnCode_t retVal = eWiFiFailure;

    if (!gInitialised) {
        if ((cellularPortInit() == 0) && 
            (cellularPortUartInit(CELLULAR_CFG_PIN_TXD,
                                  CELLULAR_CFG_PIN_RXD,
                                  CELLULAR_CFG_PIN_CTS,
                                  CELLULAR_CFG_PIN_RTS,
                                  CELLULAR_CFG_BAUD_RATE,
                                  CELLULAR_CFG_RTS_THRESHOLD,
                                  CELLULAR_CFG_UART,
                                  gpQueueHandle) == 0) &&
            (cellularCtrlInit(CELLULAR_CFG_PIN_ENABLE_POWER,
                              CELLULAR_CFG_PIN_CP_ON,
                              CELLULAR_CFG_PIN_VINT,
                              false,
                              CELLULAR_CFG_UART,
                              *gpQueueHandle) == 0)) {
            gInitialised = true;
        }
    }

    if (gInitialised && (cellularCtrlPowerOn(NULL) == 0)) {
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

    if ((pxNetworkParams != NULL) && (pxNetworkParams->ucSSIDLength > 0)) {
        pApn = pxNetworkParams->pcSSID;
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
 
    if ((pxBuffer != NULL) && (ucNumNetworks > 0)) {
        // Zero the buffer
        memset(pxBuffer, 0, sizeof(WIFIScanResult_t) * ucNumNetworks);

        // Get the operator string
        if (cellularCtrlGetOperatorStr(pxBuffer->cSSID,
                                       sizeof(pxBuffer->cSSID)) > 0) {
            // The BSSID field is 6 digits, so we can stuff the MCC/MNC in there
           if (cellularCtrlGetMccMnc(&mcc, &mnc) == 0) {
               // Do this in two stages as snprintf() adds a NULL
               // terminator which we don't have room for in ucBSSID
               snprintf(bssid, sizeof(bssid), "%03d%03d", mcc, mnc);
               memcpy(pxBuffer->ucBSSID, bssid, sizeof(pxBuffer->ucBSSID));
           }
            // Fill in the RSSI field with the RSRP, 
            // which might just fit most of the time
            if (cellularCtrlRefreshRadioParameters() == 0) {
                pxBuffer->cRSSI = (int8_t) cellularCtrlGetRsrpDbm();
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

    if (xDeviceMode == eWiFiModeAP) {
        xRetVal = eWiFiSuccess;
    }

    return xRetVal;
}

/*-----------------------------------------------------------*/

// Stub for compatibility only, always returns success and
// AP mode.
WIFIReturnCode_t WIFI_GetMode( WIFIDeviceMode_t * pxDeviceMode )
{
    WIFIReturnCode_t xRetVal = eWiFiFailure;

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

    return eWiFiNotSupported;
}

/*-----------------------------------------------------------*/

// Stub for compatibility only, always returns eWiFiNotSupported.
WIFIReturnCode_t WIFI_NetworkGet( WIFINetworkProfile_t * pxNetworkProfile,
                                  uint16_t usIndex )
{
    (void) pxNetworkProfile;
    (void) usIndex;

    return eWiFiNotSupported;
}

/*-----------------------------------------------------------*/

// Stub for compatibility only, always returns eWiFiNotSupported.
WIFIReturnCode_t WIFI_NetworkDelete( uint16_t usIndex )
{
    (void) usIndex;

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

    return eWiFiNotSupported;
}

/*-----------------------------------------------------------*/

// Get the local IP address.
WIFIReturnCode_t WIFI_GetIP( uint8_t * pucIPAddr )
{
    WIFIReturnCode_t xRetVal = eWiFiFailure;

    if (pucIPAddr != NULL) {
        if (cellularCtrlGetIpAddressStr((char *) pucIPAddr) == 0) {
            xRetVal = eWiFiSuccess;
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

    if (pucMac != NULL) {
        if (cellularCtrlGetImei(buffer) == 0) {
            memcpy(pucMac,
                   buffer + CELLULAR_CTRL_IMEI_SIZE - wificonfigMAX_BSSID_LEN,
                   wificonfigMAX_BSSID_LEN);
            xRetVal = eWiFiSuccess;
        }
    }

    return xRetVal;
}

/*-----------------------------------------------------------*/

// Stub for compatibility only, always returns eWiFiNotSupported.
WIFIReturnCode_t WIFI_GetHostIP( char * pcHost,
                                 uint8_t * pucIPAddr )
{
    (void) pcHost;
    (void) pucIPAddr;

    return eWiFiNotSupported;
}

/*-----------------------------------------------------------*/

// Stub for compatibility only, always returns success.
WIFIReturnCode_t WIFI_StartAP( void )
{
    return eWiFiSuccess;
}
/*-----------------------------------------------------------*/

// Stub for compatibility only, always returns success.
WIFIReturnCode_t WIFI_StopAP( void )
{
    return eWiFiSuccess;
}

/*-----------------------------------------------------------*/

// Stub for compatibility only, always returns success.
WIFIReturnCode_t WIFI_ConfigureAP( const WIFINetworkParams_t * const pxNetworkParams )
{
    (void) pxNetworkParams;

    return eWiFiSuccess;
}

/*-----------------------------------------------------------*/

// Stub for compatibility only, always returns success.
WIFIReturnCode_t WIFI_SetPMMode( WIFIPMMode_t xPMModeType,
                                 const void * pvOptionValue )
{
    (void) xPMModeType;
    (void) pvOptionValue;

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

    return eWiFiSuccess;
}

/*-----------------------------------------------------------*/
