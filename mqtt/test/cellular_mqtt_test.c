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
#include "cellular_cfg_hw_platform_specific.h"
#include "cellular_cfg_sw.h"
#include "cellular_cfg_module.h"
#include "cellular_port_clib.h"
#include "cellular_port.h"
#include "cellular_port_debug.h"
#include "cellular_port_os.h"
#include "cellular_port_uart.h"
#include "cellular_port_test_platform_specific.h"
#include "cellular_ctrl.h"
#include "cellular_mqtt.h"
#include "cellular_cfg_test.h"

#if CELLULAR_MQTT_IS_SUPPORTED

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// The Thingstream server without security.
#define CELLULAR_MQTT_THINGSTREAM_SERVER_UNSECURE "mqtt.thingstream.io:1883"

// A message to send encrypted.
#define CELLULAR_MQTT_TEST_MESSAGE "Hello World!"

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// Used for keepGoingCallback() timeout.
static int64_t gStopTimeMs;

// The UART queue handle: kept as a global variable
// because if a test fails init will have run but
// deinit will have been skipped.  With this as a global,
// when the inits skip doing their thing because
// things are already init'ed, the subsequent
// functions will continue to use this valid queue
// handle.
static CellularPortQueueHandle_t gUartQueueHandle = NULL;

// Place to store the original RAT settings of the module.
static CellularCtrlRat_t gOriginalRats[CELLULAR_CTRL_MAX_NUM_SIMULTANEOUS_RATS];

// Place to store the original band mask settings of the module.
static uint64_t gOriginalMask1;
static uint64_t gOriginalMask2;

// A place to put the IMEI which is used by various of the tests.
static char gImei[CELLULAR_CTRL_IMEI_SIZE + 1];

// A string of all possible characters, including strings
// that might appear as terminators in the AT interface
static const char gAllChars[] = "the quick brown fox jumps over the lazy dog "
                                "THE QUICK BROWN FOX JUMPS OVER THE LAZY DOG 0123456789 "
                                "\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e"
                                "\x0f\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1a\x1b\x1c"
                                "\x1d\x1e!\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~\x7f"
                                "\r\nOK\r\n \r\nERROR\r\n";

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

// Callback function for the cellular networkConnect process.
static bool keepGoingCallback()
{
    bool keepGoing = true;

    if (cellularPortGetTickTimeMs() > gStopTimeMs) {
        keepGoing = false;
    }

    return keepGoing;
}

// Connect to the network, saving existing settings first.
static void networkConnect(const char *pApn,
                           const char *pUsername,
                           const char *pPassword)
{
    for (size_t x = 0; x < sizeof (gOriginalRats) / sizeof (gOriginalRats[0]); x++) {
        gOriginalRats[x] = CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED;
    }

    cellularPortLog("CELLULAR_MQTT_TEST: saving existing settings...\n");
    // First, read out the existing RATs so that we can put them back
    for (size_t x = 0; x < sizeof (gOriginalRats) / sizeof (gOriginalRats[0]); x++) {
        gOriginalRats[x] = cellularCtrlGetRat(x);
    }
    if ((CELLULAR_CFG_TEST_RAT == CELLULAR_CTRL_RAT_CATM1) || (CELLULAR_CFG_TEST_RAT == CELLULAR_CTRL_RAT_NB1)) {
        // Then read out the existing band masks
       CELLULAR_PORT_TEST_ASSERT(cellularCtrlGetBandMask(CELLULAR_CFG_TEST_RAT,
                                                         &gOriginalMask1, &gOriginalMask2) == 0);
    }
    cellularPortLog("CELLULAR_MQTT_TEST: setting sole RAT to %d...\n", CELLULAR_CFG_TEST_RAT);
    CELLULAR_PORT_TEST_ASSERT(cellularCtrlSetRat(CELLULAR_CFG_TEST_RAT) == 0);
    if ((CELLULAR_CFG_TEST_RAT == CELLULAR_CTRL_RAT_CATM1) || (CELLULAR_CFG_TEST_RAT == CELLULAR_CTRL_RAT_NB1)) {
        cellularPortLog("CELLULAR_CTRL_TEST: setting bandmask to 0x%08x%08x %08x%08x...\n",
                        (uint32_t) (CELLULAR_CFG_TEST_BANDMASK2 >> 32),
                        (uint32_t) CELLULAR_CFG_TEST_BANDMASK2,
                        (uint32_t) (CELLULAR_CFG_TEST_BANDMASK1 >> 32),
                        (uint32_t) CELLULAR_CFG_TEST_BANDMASK1);
        CELLULAR_PORT_TEST_ASSERT(cellularCtrlSetBandMask(CELLULAR_CFG_TEST_RAT,
                                                          CELLULAR_CFG_TEST_BANDMASK1,
                                                          CELLULAR_CFG_TEST_BANDMASK2) == 0);
    }
    CELLULAR_PORT_TEST_ASSERT(cellularCtrlReboot() == 0);

    cellularPortLog("CELLULAR_MQTT_TEST: connecting...\n");
    gStopTimeMs = cellularPortGetTickTimeMs() + (CELLULAR_CFG_TEST_CONNECT_TIMEOUT_SECONDS * 1000);
    CELLULAR_PORT_TEST_ASSERT(cellularCtrlConnect(keepGoingCallback,
                                                  pApn, pUsername, pPassword) == 0);
    cellularPortLog("CELLULAR_MQTT_TEST: RAT %d, cellularCtrlGetNetworkStatus() %d.\n",
                    CELLULAR_CFG_TEST_RAT, cellularCtrlGetNetworkStatus(cellularCtrlGetRanForRat(CELLULAR_CFG_TEST_RAT)));
    CELLULAR_PORT_TEST_ASSERT(cellularCtrlGetNetworkStatus(cellularCtrlGetRanForRat(CELLULAR_CFG_TEST_RAT)) == CELLULAR_CTRL_NETWORK_STATUS_REGISTERED);
    CELLULAR_PORT_TEST_ASSERT(cellularCtrlIsRegistered());
}

// Disconnect from the network and restore teh saved settings.
static void networkDisconnect()
{
    bool screwy = false;

    cellularPortLog("CELLULAR_MQTT_TEST: disconnecting...\n");
    CELLULAR_PORT_TEST_ASSERT(cellularCtrlDisconnect() == 0);
    CELLULAR_PORT_TEST_ASSERT(cellularCtrlGetNetworkStatus(cellularCtrlGetRanForRat(CELLULAR_CFG_TEST_RAT)) != CELLULAR_CTRL_NETWORK_STATUS_REGISTERED);
    CELLULAR_PORT_TEST_ASSERT(!cellularCtrlIsRegistered());

    cellularPortLog("CELLULAR_MQTT_TEST: restoring saved settings...\n");
    if ((CELLULAR_CFG_TEST_RAT == CELLULAR_CTRL_RAT_CATM1) || (CELLULAR_CFG_TEST_RAT == CELLULAR_CTRL_RAT_NB1)) {
        // No asserts here, we need it to plough on and succeed
        if (cellularCtrlSetBandMask(CELLULAR_CFG_TEST_RAT, gOriginalMask1,
                                                           gOriginalMask2) != 0) {
            cellularPortLog("CELLULAR_MQTT_TEST: !!! ATTENTION: the band mask for RAT %d on the module under test may have been left screwy, please check!!!\n", CELLULAR_CFG_TEST_RAT);
        }
    }
    for (size_t x = 0; x < sizeof (gOriginalRats) / sizeof (gOriginalRats[0]); x++) {
        cellularCtrlSetRatRank(gOriginalRats[x], x);
    }
    for (size_t x = 0; x < sizeof (gOriginalRats) / sizeof (gOriginalRats[0]); x++) {
        if (cellularCtrlGetRat(x) != gOriginalRats[x]) {
            screwy = true;
        }
    }
    if (screwy) {
        cellularPortLog("CELLULAR_MQTT_TEST: !!! ATTENTION: the RAT settings of the module under test may have been left screwy, please check!!!\n");
    }
    cellularCtrlReboot();
}

// Callback for unread message indications
static void messageIndicationCallback(int32_t numUnread, void *pParam)
{
    int32_t *pNumUnread = (int32_t *) pParam;

    cellularPortLog("messageIndicationCallback() called.\n");
    cellularPortLog("%d message(s) unread.\n", numUnread);
    *pNumUnread = numUnread;
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: TESTS
 * -------------------------------------------------------------- */

/** Basic test: initialise and then deinitialise everything.
 */
CELLULAR_PORT_TEST_FUNCTION(void cellularMqttTestInitialisation(),
                            "mqttInitialisation",
                            "mqtt")
{
    char buffer[32];

    CELLULAR_PORT_TEST_ASSERT(cellularPortInit() == 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPortUartInit(CELLULAR_CFG_PIN_TXD,
                                                   CELLULAR_CFG_PIN_RXD,
                                                   CELLULAR_CFG_PIN_CTS,
                                                   CELLULAR_CFG_PIN_RTS,
                                                   CELLULAR_CFG_BAUD_RATE,
                                                   CELLULAR_CFG_RTS_THRESHOLD,
                                                   CELLULAR_CFG_UART,
                                                   &gUartQueueHandle) == 0);
    CELLULAR_PORT_TEST_ASSERT(cellularCtrlInit(CELLULAR_CFG_PIN_ENABLE_POWER,
                                               CELLULAR_CFG_PIN_PWR_ON,
                                               CELLULAR_CFG_PIN_VINT,
                                               false,
                                               CELLULAR_CFG_UART,
                                               gUartQueueHandle) == 0);

    CELLULAR_PORT_TEST_ASSERT(cellularCtrlPowerOn(NULL) == 0);

    // Get the IMEI for use later
    CELLULAR_PORT_TEST_ASSERT(cellularCtrlGetImei(gImei) == 0);
    gImei[sizeof(gImei) - 1] = 0;

    // Use the IP address here so that we don't have to be
    // connected
    CELLULAR_PORT_TEST_ASSERT(cellularMqttInit(CELLULAR_CFG_TEST_MQTT_SERVER_IP_ADDRESS,
                                               NULL,
                                               CELLULAR_CFG_TEST_MQTT_USERNAME,
                                               CELLULAR_CFG_TEST_MQTT_PASSWORD,
                                               keepGoingCallback) == 0);

    // For information only
    cellularPortLog("CELLULAR_MQTT_TEST: getting local MQTT client ID...\n");
    CELLULAR_PORT_TEST_ASSERT(cellularMqttGetClientId(buffer, sizeof(buffer)) == 0);

    cellularCtrlPowerOff(NULL);
    cellularMqttDeinit();

    cellularCtrlDeinit();
    CELLULAR_PORT_TEST_ASSERT(cellularPortUartDeinit(CELLULAR_CFG_UART) == 0);
    cellularPortDeinit();
}

/** Connect to an MQTT server.
 */
CELLULAR_PORT_TEST_FUNCTION(void cellularMqttTestConnectDisconnect(),
                            "mqttConnectDisconnect",
                            "mqtt")
{
    char buffer[32];
    int32_t y;
    int64_t startTimeMs;

    CELLULAR_PORT_TEST_ASSERT(cellularPortInit() == 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPortUartInit(CELLULAR_CFG_PIN_TXD,
                                                   CELLULAR_CFG_PIN_RXD,
                                                   CELLULAR_CFG_PIN_CTS,
                                                   CELLULAR_CFG_PIN_RTS,
                                                   CELLULAR_CFG_BAUD_RATE,
                                                   CELLULAR_CFG_RTS_THRESHOLD,
                                                   CELLULAR_CFG_UART,
                                                   &gUartQueueHandle) == 0);
    CELLULAR_PORT_TEST_ASSERT(cellularCtrlInit(CELLULAR_CFG_PIN_ENABLE_POWER,
                                               CELLULAR_CFG_PIN_PWR_ON,
                                               CELLULAR_CFG_PIN_VINT,
                                               false,
                                               CELLULAR_CFG_UART,
                                               gUartQueueHandle) == 0);

    // Call this first in case a previous failed test left things initialised
    cellularMqttDeinit();

    CELLULAR_PORT_TEST_ASSERT(cellularCtrlPowerOn(NULL) == 0);

    networkConnect(CELLULAR_CFG_TEST_APN,
                   CELLULAR_CFG_TEST_USERNAME,
                   CELLULAR_CFG_TEST_PASSWORD);

    cellularPortLog("CELLULAR_MQTT_TEST: initialising MQTT with server \"%s\"...\n",
                    CELLULAR_CFG_TEST_MQTT_SERVER_DOMAIN_NAME);
    CELLULAR_PORT_TEST_ASSERT(cellularMqttInit(CELLULAR_CFG_TEST_MQTT_SERVER_DOMAIN_NAME,
                                               "bong",
                                               CELLULAR_CFG_TEST_MQTT_USERNAME,
                                               CELLULAR_CFG_TEST_MQTT_PASSWORD,
                                               keepGoingCallback) == 0);

    cellularPortLog("CELLULAR_MQTT_TEST: getting local MQTT client ID...\n");
    CELLULAR_PORT_TEST_ASSERT(cellularMqttGetClientId(buffer, sizeof(buffer)) == 0);
    cellularPortLog("CELLULAR_MQTT_TEST: local MQTT client ID is \"%s\".\n", buffer);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_strcmp(buffer, "bong") == 0);

    cellularPortLog("CELLULAR_MQTT_TEST: getting local MQTT port...\n");
    y = cellularMqttGetLocalPort();
    cellularPortLog("CELLULAR_MQTT_TEST: local MQTT port is %d.\n", y);
    CELLULAR_PORT_TEST_ASSERT(y == CELLULAR_MQTT_SERVER_PORT_UNSECURE);

    cellularPortLog("CELLULAR_MQTT_TEST: getting inactivity timeout...\n");
    y = cellularMqttGetInactivityTimeout();
    cellularPortLog("CELLULAR_MQTT_TEST: inactivity timeout is %d.\n", y);
    CELLULAR_PORT_TEST_ASSERT(y == 0);

    cellularPortLog("CELLULAR_MQTT_TEST: getting keep-alive value...\n");
    y = cellularMqttIsKeptAlive();
    cellularPortLog("CELLULAR_MQTT_TEST: keep-alive value is %d.\n", y);
    CELLULAR_PORT_TEST_ASSERT(y == 0);

    cellularPortLog("CELLULAR_MQTT_TEST: getting session clean value...\n");
    y = cellularMqttIsSessionClean();
    cellularPortLog("CELLULAR_MQTT_TEST: session clean value is %d.\n", y);
    CELLULAR_PORT_TEST_ASSERT(y == 1);

    cellularPortLog("CELLULAR_MQTT_TEST: getting security value...\n");
    y = cellularMqttIsSecured(NULL);
    cellularPortLog("CELLULAR_MQTT_TEST: security value is %d.\n", y);
    CELLULAR_PORT_TEST_ASSERT(y == 0);

    startTimeMs = cellularPortGetTickTimeMs();
    cellularPortLog("CELLULAR_MQTT_TEST: connecting to \"%s\"...\n",
                    CELLULAR_CFG_TEST_MQTT_SERVER_DOMAIN_NAME);
    gStopTimeMs = cellularPortGetTickTimeMs() + (CELLULAR_CFG_TEST_MQTT_SERVER_TIMEOUT_SECONDS * 1000);
    y = cellularMqttConnect();
    if (y == 0) {
        cellularPortLog("CELLULAR_MQTT_TEST: connected after %d seconds.\n",
                        ((int32_t) (cellularPortGetTickTimeMs() - startTimeMs)) / 1000);
        CELLULAR_PORT_TEST_ASSERT(cellularMqttIsConnected());
    } else {
        cellularPortLog("CELLULAR_MQTT_TEST: not connected after %d seconds, module error %d.\n",
                        ((int32_t) (cellularPortGetTickTimeMs() - startTimeMs)) / 1000,
                        cellularMqttGetLastErrorCode());
        CELLULAR_PORT_TEST_ASSERT(!cellularMqttIsConnected());
        CELLULAR_PORT_TEST_ASSERT(false);
    }

    cellularPortLog("CELLULAR_MQTT_TEST: disconnecting again...\n");
    gStopTimeMs = cellularPortGetTickTimeMs() + (CELLULAR_CFG_TEST_MQTT_SERVER_TIMEOUT_SECONDS * 1000);
    CELLULAR_PORT_TEST_ASSERT(cellularMqttDisconnect() == 0);
    CELLULAR_PORT_TEST_ASSERT(!cellularMqttIsConnected());

    // Disconnect from the cellular network and tidy up
    networkDisconnect();

    cellularCtrlPowerOff(NULL);
    cellularMqttDeinit();

    cellularCtrlDeinit();
    CELLULAR_PORT_TEST_ASSERT(cellularPortUartDeinit(CELLULAR_CFG_UART) == 0);
    cellularPortDeinit();
}

/** Subscribe/publish messages with an MQTT server.
 */
CELLULAR_PORT_TEST_FUNCTION(void cellularMqttTestSubscribePublish(),
                            "mqttSubscribePublish",
                            "mqtt")
{
    char buffer[32];
    int32_t y;
    int32_t z;
    int32_t numPublished = 0;
    int32_t numUnread = 0;
    int64_t startTimeMs;
    char *pTopicOut;
    char *pTopicIn;
    char *pMessageOut;
    char *pMessageIn;
    CellularMqttQos_t qos;

    CELLULAR_PORT_TEST_ASSERT(cellularPortInit() == 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPortUartInit(CELLULAR_CFG_PIN_TXD,
                                                   CELLULAR_CFG_PIN_RXD,
                                                   CELLULAR_CFG_PIN_CTS,
                                                   CELLULAR_CFG_PIN_RTS,
                                                   CELLULAR_CFG_BAUD_RATE,
                                                   CELLULAR_CFG_RTS_THRESHOLD,
                                                   CELLULAR_CFG_UART,
                                                   &gUartQueueHandle) == 0);
    CELLULAR_PORT_TEST_ASSERT(cellularCtrlInit(CELLULAR_CFG_PIN_ENABLE_POWER,
                                               CELLULAR_CFG_PIN_PWR_ON,
                                               CELLULAR_CFG_PIN_VINT,
                                               false,
                                               CELLULAR_CFG_UART,
                                               gUartQueueHandle) == 0);

    // Malloc space to read messages and topics into
    pTopicOut = (char *) pCellularPort_malloc(CELLULAR_MQTT_READ_TOPIC_MAX_LENGTH_BYTES);
    CELLULAR_PORT_TEST_ASSERT(pTopicOut != NULL);
    pTopicIn = (char *) pCellularPort_malloc(CELLULAR_MQTT_READ_TOPIC_MAX_LENGTH_BYTES);
    CELLULAR_PORT_TEST_ASSERT(pTopicIn != NULL);
    pMessageOut = (char *) pCellularPort_malloc(CELLULAR_MQTT_PUBLISH_MAX_LENGTH_BYTES);
    CELLULAR_PORT_TEST_ASSERT(pMessageOut != NULL);
    pMessageIn = (char *) pCellularPort_malloc(CELLULAR_MQTT_READ_MESSAGE_MAX_LENGTH_BYTES);
    CELLULAR_PORT_TEST_ASSERT(pMessageIn != NULL);

    // Make a unique topic name to stop different boards colliding
    cellularPort_snprintf(pTopicOut, CELLULAR_MQTT_READ_TOPIC_MAX_LENGTH_BYTES,
                          "ubx_test/%s", gImei);

    // Call this first in case a previous failed test left things initialised
    cellularMqttDeinit();

    CELLULAR_PORT_TEST_ASSERT(cellularCtrlPowerOn(NULL) == 0);

    networkConnect(CELLULAR_CFG_TEST_APN,
                   CELLULAR_CFG_TEST_USERNAME,
                   CELLULAR_CFG_TEST_PASSWORD);

    cellularPortLog("CELLULAR_MQTT_TEST: initialising MQTT with server \"%s\"...\n",
                    CELLULAR_CFG_TEST_MQTT_SERVER_DOMAIN_NAME);
    CELLULAR_PORT_TEST_ASSERT(cellularMqttInit(CELLULAR_CFG_TEST_MQTT_SERVER_DOMAIN_NAME,
                                               gImei,
                                               CELLULAR_CFG_TEST_MQTT_USERNAME,
                                               CELLULAR_CFG_TEST_MQTT_PASSWORD,
                                               keepGoingCallback) == 0);

    cellularPortLog("CELLULAR_MQTT_TEST: getting local MQTT client ID...\n");
    CELLULAR_PORT_TEST_ASSERT(cellularMqttGetClientId(buffer, sizeof(buffer)) == 0);
    cellularPortLog("CELLULAR_MQTT_TEST: local MQTT client ID is \"%s\".\n", buffer);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_strcmp(buffer, gImei) == 0);

#ifdef CELLULAR_CFG_MODULE_SARA_R5
    CELLULAR_PORT_TEST_ASSERT(cellularMqttSetLocalPort(10) == CELLULAR_MQTT_NOT_SUPPORTED);
#else
    cellularPortLog("CELLULAR_MQTT_TEST: setting local MQTT port to %d...\n", 10);
    CELLULAR_PORT_TEST_ASSERT(cellularMqttSetLocalPort(10) == 0);
    y = cellularMqttGetLocalPort();
    cellularPortLog("CELLULAR_MQTT_TEST: local MQTT port is %d.\n", y);
    CELLULAR_PORT_TEST_ASSERT(y == 10);
    cellularPortLog("CELLULAR_MQTT_TEST: setting local MQTT port to %d...\n",
                    CELLULAR_MQTT_SERVER_PORT_UNSECURE);
    CELLULAR_PORT_TEST_ASSERT(cellularMqttSetLocalPort(CELLULAR_MQTT_SERVER_PORT_UNSECURE) == 0);
#endif
    y = cellularMqttGetLocalPort();
    cellularPortLog("CELLULAR_MQTT_TEST: local MQTT port is %d.\n", y);
    CELLULAR_PORT_TEST_ASSERT(y == CELLULAR_MQTT_SERVER_PORT_UNSECURE);

    cellularPortLog("CELLULAR_MQTT_TEST: setting inactivity timeout to %d"
                    " second(s)...\n", 360);
    CELLULAR_PORT_TEST_ASSERT(cellularMqttSetInactivityTimeout(360) == 0);
    y = cellularMqttGetInactivityTimeout();
    cellularPortLog("CELLULAR_MQTT_TEST: inactivity timeout is %d.\n", y);
    CELLULAR_PORT_TEST_ASSERT(y == 360);

    cellularPortLog("CELLULAR_MQTT_TEST: switching keep-alive on...\n");
    CELLULAR_PORT_TEST_ASSERT(cellularMqttSetKeepAliveOn() == 0);
    cellularPortLog("CELLULAR_MQTT_TEST: getting keep-alive value...\n");
    y = cellularMqttIsKeptAlive();
    cellularPortLog("CELLULAR_MQTT_TEST: keep-alive value is %d.\n", y);
    CELLULAR_PORT_TEST_ASSERT(y == 1);
    cellularPortLog("CELLULAR_MQTT_TEST: switching keep-alive off again...\n");
    CELLULAR_PORT_TEST_ASSERT(cellularMqttSetKeepAliveOff() == 0);
    cellularPortLog("CELLULAR_MQTT_TEST: getting keep-alive value...\n");
    y = cellularMqttIsKeptAlive();
    cellularPortLog("CELLULAR_MQTT_TEST: keep-alive value is %d.\n", y);
    CELLULAR_PORT_TEST_ASSERT(y == 0);

#ifdef CELLULAR_CFG_MODULE_SARA_R5
    CELLULAR_PORT_TEST_ASSERT(cellularMqttSetSessionCleanOn() == CELLULAR_MQTT_NOT_SUPPORTED);
    CELLULAR_PORT_TEST_ASSERT(cellularMqttSetSessionCleanOff() == CELLULAR_MQTT_NOT_SUPPORTED);
#else
    cellularPortLog("CELLULAR_MQTT_TEST: switching session clean off...\n");
    CELLULAR_PORT_TEST_ASSERT(cellularMqttSetSessionCleanOff() == 0);
    cellularPortLog("CELLULAR_MQTT_TEST: getting session clean value...\n");
    y = cellularMqttIsSessionClean();
    cellularPortLog("CELLULAR_MQTT_TEST: session clean value is %d.\n", y);
    CELLULAR_PORT_TEST_ASSERT(y == 0);
    cellularPortLog("CELLULAR_MQTT_TEST: switching session clean on again...\n");
    CELLULAR_PORT_TEST_ASSERT(cellularMqttSetSessionCleanOn() == 0);
#endif
    y = cellularMqttIsSessionClean();
    cellularPortLog("CELLULAR_MQTT_TEST: session clean value is %d.\n", y);
    CELLULAR_PORT_TEST_ASSERT(y == 1);

    cellularPortLog("CELLULAR_MQTT_TEST: getting security value...\n");
    y = cellularMqttIsSecured(NULL);
    cellularPortLog("CELLULAR_MQTT_TEST: security value is %d.\n", y);
    CELLULAR_PORT_TEST_ASSERT(y == 0);

    startTimeMs = cellularPortGetTickTimeMs();
    cellularPortLog("CELLULAR_MQTT_TEST: connecting to \"%s\"...\n",
                    CELLULAR_CFG_TEST_MQTT_SERVER_DOMAIN_NAME);
    gStopTimeMs = cellularPortGetTickTimeMs() +
                  (CELLULAR_CFG_TEST_MQTT_SERVER_TIMEOUT_SECONDS * 1000);
    y = cellularMqttConnect();
    if (y == 0) {
        cellularPortLog("CELLULAR_MQTT_TEST: connected after %d seconds.\n",
                        ((int32_t) (cellularPortGetTickTimeMs() - startTimeMs)) / 1000);
        CELLULAR_PORT_TEST_ASSERT(cellularMqttIsConnected());
    } else {
        cellularPortLog("CELLULAR_MQTT_TEST: not connected after %d seconds, module error %d.\n",
                        ((int32_t) (cellularPortGetTickTimeMs() - startTimeMs)) / 1000,
                        cellularMqttGetLastErrorCode());
        CELLULAR_PORT_TEST_ASSERT(!cellularMqttIsConnected());
        CELLULAR_PORT_TEST_ASSERT(false);
    }

    // Set the callback
    CELLULAR_PORT_TEST_ASSERT(cellularMqttSetMessageIndicationCallback(messageIndicationCallback,
                                                                       &numUnread) == 0);

    cellularPortLog("CELLULAR_MQTT_TEST: subscribing to topic \"%s\"...\n", pTopicOut);
    startTimeMs = cellularPortGetTickTimeMs();
    gStopTimeMs = cellularPortGetTickTimeMs() +
                  (CELLULAR_CFG_TEST_MQTT_SERVER_TIMEOUT_SECONDS * 1000);
    y = cellularMqttSubscribe(CELLULAR_MQTT_EXACTLY_ONCE, pTopicOut);
    if (y >= 0) {
        cellularPortLog("CELLULAR_MQTT_TEST: subscribe successful after %d ms, QoS %d.\n",
                        (int32_t) (cellularPortGetTickTimeMs() - startTimeMs), y);
    } else {
        cellularPortLog("CELLULAR_MQTT_TEST: subscribe returned error %d after %d ms,"
                        " module error %d.\n",
                        y, (int32_t) (cellularPortGetTickTimeMs() - startTimeMs),
                        cellularMqttGetLastErrorCode());
        CELLULAR_PORT_TEST_ASSERT(false);
    }

    // There may be unread messages sitting on the server from a previous test run,
    // read them off here.
    y = cellularMqttGetUnread();
    for (size_t x = 0; x < y; x++) {
        while (cellularMqttGetUnread() > 0) {
            cellularPortLog("CELLULAR_MQTT_TEST: reading existing unread message %d of %d.\n",
                            x + 1, y);
            CELLULAR_PORT_TEST_ASSERT(cellularMqttMessageRead(pTopicIn,
                                                              CELLULAR_MQTT_READ_TOPIC_MAX_LENGTH_BYTES,
                                                              pMessageIn, &z,
                                                              NULL) == 0);
            CELLULAR_PORT_TEST_ASSERT(cellularPort_strcmp(pTopicIn, pTopicOut) == 0);
            // Let everyone sort themselves out
            cellularPortTaskBlock(5000);
        }
    }

    CELLULAR_PORT_TEST_ASSERT(cellularMqttGetUnread() == 0);

    cellularPortLog("CELLULAR_MQTT_TEST: publishing %d byte(s) to topic \"%s\"...\n",
                    CELLULAR_MQTT_PUBLISH_MAX_LENGTH_BYTES, pTopicOut);
    startTimeMs = cellularPortGetTickTimeMs();
    gStopTimeMs = cellularPortGetTickTimeMs() +
                  (CELLULAR_CFG_TEST_MQTT_SERVER_TIMEOUT_SECONDS * 1000);
    // Fill in the outgoing message buffer with all possible things
    y = CELLULAR_MQTT_PUBLISH_MAX_LENGTH_BYTES;
    while (y > 0) {
        z = sizeof(gAllChars);
        if (z > y) {
            z = y;
        }
        pCellularPort_memcpy(pMessageOut, gAllChars, z);
        y -= z;
    }
    y = cellularMqttPublish(CELLULAR_MQTT_EXACTLY_ONCE, false, pTopicOut,
                            pMessageOut, CELLULAR_MQTT_PUBLISH_MAX_LENGTH_BYTES);
    if (y == 0) {
        cellularPortLog("CELLULAR_MQTT_TEST: publish successful after %d ms.\n",
                        (int32_t) (cellularPortGetTickTimeMs() - startTimeMs));
        numPublished++;
    } else {
        cellularPortLog("CELLULAR_MQTT_TEST: publish returned error %d after %d ms, module"
                        " error %d.\n",
                        y, (int32_t) (cellularPortGetTickTimeMs() - startTimeMs),
                        cellularMqttGetLastErrorCode());
        CELLULAR_PORT_TEST_ASSERT(false);
    }

    cellularPortLog("CELLULAR_MQTT_TEST: waiting for an unread message indication...\n");
    startTimeMs = cellularPortGetTickTimeMs();
    while ((numUnread == 0) &&
           (cellularPortGetTickTimeMs() < startTimeMs +
                                         (CELLULAR_CFG_TEST_MQTT_SERVER_TIMEOUT_SECONDS * 1000))) {
        cellularPortTaskBlock(1000);
    }

    if (numUnread > 0) {
        cellularPortLog("CELLULAR_MQTT_TEST: %d message(s) unread.\n", numUnread);
    } else {
        cellularPortLog("CELLULAR_MQTT_TEST: no messages unread after %d ms.\n",
                        (int32_t) (cellularPortGetTickTimeMs() - startTimeMs));
        CELLULAR_PORT_TEST_ASSERT(false);
    }

    CELLULAR_PORT_TEST_ASSERT(numUnread == 1);
    CELLULAR_PORT_TEST_ASSERT(cellularMqttGetUnread() == numUnread);

    cellularPortLog("CELLULAR_MQTT_TEST: reading the message...\n");
    qos = -1;
    y = CELLULAR_MQTT_READ_MESSAGE_MAX_LENGTH_BYTES;
    CELLULAR_PORT_TEST_ASSERT(cellularMqttMessageRead(pTopicIn,
                                                      CELLULAR_MQTT_READ_TOPIC_MAX_LENGTH_BYTES,
                                                      pMessageIn, &y,
                                                      &qos) == 0);
    cellularPortLog("CELLULAR_MQTT_TEST: read %d byte(s)...\n", y);
    CELLULAR_PORT_TEST_ASSERT(qos == CELLULAR_MQTT_EXACTLY_ONCE);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_strcmp(pTopicIn, pTopicOut) == 0);
    CELLULAR_PORT_TEST_ASSERT(y == CELLULAR_MQTT_PUBLISH_MAX_LENGTH_BYTES);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_memcmp(pMessageIn, pMessageOut, y) == 0);

    CELLULAR_PORT_TEST_ASSERT(cellularMqttGetUnread() == 0);

    // Cancel the subscribe
    cellularPortLog("CELLULAR_MQTT_TEST: unsubscribing from topic \"%s\"...\n",
                    pTopicOut);
    gStopTimeMs = cellularPortGetTickTimeMs() +
                  (CELLULAR_CFG_TEST_MQTT_SERVER_TIMEOUT_SECONDS * 1000);
    CELLULAR_PORT_TEST_ASSERT(cellularMqttUnsubscribe(pTopicOut) == 0);

    // Remove the callback
    CELLULAR_PORT_TEST_ASSERT(cellularMqttSetMessageIndicationCallback(NULL, NULL) == 0);

    cellularPortLog("CELLULAR_MQTT_TEST: disconnecting again...\n");
    gStopTimeMs = cellularPortGetTickTimeMs() +
                  (CELLULAR_CFG_TEST_MQTT_SERVER_TIMEOUT_SECONDS * 1000);
    CELLULAR_PORT_TEST_ASSERT(cellularMqttDisconnect() == 0);
    CELLULAR_PORT_TEST_ASSERT(!cellularMqttIsConnected());

    // Disconnect from the cellular network and tidy up
    networkDisconnect();

    cellularCtrlPowerOff(NULL);
    cellularMqttDeinit();

    cellularPort_free(pMessageIn);
    cellularPort_free(pMessageOut);
    cellularPort_free(pTopicIn);
    cellularPort_free(pTopicOut);

    cellularCtrlDeinit();
    CELLULAR_PORT_TEST_ASSERT(cellularPortUartDeinit(CELLULAR_CFG_UART) == 0);
    cellularPortDeinit();
}

# ifdef CELLULAR_CFG_TEST_THINGSTREAM_CLIENT_ID

# ifndef CELLULAR_CFG_TEST_THINGSTREAM_USERNAME
#  error Must specify a username for your Thingstream account.
# endif

# ifndef CELLULAR_CFG_TEST_THINGSTREAM_PASSWORD
#  error Must specify a password for your Thingstream account.
# endif

/** Subscribe/publish messages with a Thingstream MQTT server.
 */
CELLULAR_PORT_TEST_FUNCTION(void cellularMqttTestThingstreamBasic(),
                            "mqttThingstreamBasic",
                            "mqtt")
{
    int32_t y;
    int32_t z;
    int32_t numPublished = 0;
    int32_t numUnread = 0;
    int64_t startTimeMs;
    char *pTopicOut;
    char *pTopicIn;
    char *pMessageOut;
    char *pMessageIn;
    CellularMqttQos_t qos;

    CELLULAR_PORT_TEST_ASSERT(cellularPortInit() == 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPortUartInit(CELLULAR_CFG_PIN_TXD,
                                                   CELLULAR_CFG_PIN_RXD,
                                                   CELLULAR_CFG_PIN_CTS,
                                                   CELLULAR_CFG_PIN_RTS,
                                                   CELLULAR_CFG_BAUD_RATE,
                                                   CELLULAR_CFG_RTS_THRESHOLD,
                                                   CELLULAR_CFG_UART,
                                                   &gUartQueueHandle) == 0);
    CELLULAR_PORT_TEST_ASSERT(cellularCtrlInit(CELLULAR_CFG_PIN_ENABLE_POWER,
                                               CELLULAR_CFG_PIN_PWR_ON,
                                               CELLULAR_CFG_PIN_VINT,
                                               false,
                                               CELLULAR_CFG_UART,
                                               gUartQueueHandle) == 0);

    // Malloc space to read messages and topics into
    pTopicOut = (char *) pCellularPort_malloc(CELLULAR_MQTT_READ_TOPIC_MAX_LENGTH_BYTES);
    CELLULAR_PORT_TEST_ASSERT(pTopicOut != NULL);
    pTopicIn = (char *) pCellularPort_malloc(CELLULAR_MQTT_READ_TOPIC_MAX_LENGTH_BYTES);
    CELLULAR_PORT_TEST_ASSERT(pTopicIn != NULL);
    pMessageOut = (char *) pCellularPort_malloc(CELLULAR_MQTT_PUBLISH_MAX_LENGTH_BYTES);
    CELLULAR_PORT_TEST_ASSERT(pMessageOut != NULL);
    pMessageIn = (char *) pCellularPort_malloc(CELLULAR_MQTT_READ_MESSAGE_MAX_LENGTH_BYTES);
    CELLULAR_PORT_TEST_ASSERT(pMessageIn != NULL);

    // Make a unique topic name to stop different boards colliding
    cellularPort_snprintf(pTopicOut, CELLULAR_MQTT_READ_TOPIC_MAX_LENGTH_BYTES,
                          "ubx_test/%s", gImei);

    // Call this first in case a previous failed test left things initialised
    cellularMqttDeinit();

    CELLULAR_PORT_TEST_ASSERT(cellularCtrlPowerOn(NULL) == 0);

    networkConnect(CELLULAR_CFG_TEST_APN,
                   CELLULAR_CFG_TEST_USERNAME,
                   CELLULAR_CFG_TEST_PASSWORD);

    cellularPortLog("CELLULAR_MQTT_TEST: initialising MQTT with server \"%s\"...\n",
                    CELLULAR_MQTT_THINGSTREAM_SERVER_UNSECURE);
    CELLULAR_PORT_TEST_ASSERT(cellularMqttInit(CELLULAR_MQTT_THINGSTREAM_SERVER_UNSECURE,
                                               CELLULAR_PORT_STRINGIFY_QUOTED(CELLULAR_CFG_TEST_THINGSTREAM_CLIENT_ID),
                                               CELLULAR_PORT_STRINGIFY_QUOTED(CELLULAR_CFG_TEST_THINGSTREAM_USERNAME),
                                               CELLULAR_PORT_STRINGIFY_QUOTED(CELLULAR_CFG_TEST_THINGSTREAM_PASSWORD),
                                               keepGoingCallback) == 0);

    startTimeMs = cellularPortGetTickTimeMs();
    cellularPortLog("CELLULAR_MQTT_TEST: connecting to %s...\n",
                    CELLULAR_MQTT_THINGSTREAM_SERVER_UNSECURE);
    gStopTimeMs = cellularPortGetTickTimeMs() +
                  (CELLULAR_CFG_TEST_MQTT_SERVER_TIMEOUT_SECONDS * 1000);
    y = cellularMqttConnect();
    if (y == 0) {
        cellularPortLog("CELLULAR_MQTT_TEST: connected after %d seconds.\n",
                        ((int32_t) (cellularPortGetTickTimeMs() - startTimeMs)) / 1000);
        CELLULAR_PORT_TEST_ASSERT(cellularMqttIsConnected());
    } else {
        cellularPortLog("CELLULAR_MQTT_TEST: not connected after %d seconds, module error %d.\n",
                        ((int32_t) (cellularPortGetTickTimeMs() - startTimeMs)) / 1000,
                        cellularMqttGetLastErrorCode());
        CELLULAR_PORT_TEST_ASSERT(!cellularMqttIsConnected());
        CELLULAR_PORT_TEST_ASSERT(false);
    }

    // Set the callback
    CELLULAR_PORT_TEST_ASSERT(cellularMqttSetMessageIndicationCallback(messageIndicationCallback,
                                                                       &numUnread) == 0);

    cellularPortLog("CELLULAR_MQTT_TEST: subscribing to topic \"%s\"...\n", pTopicOut);
    startTimeMs = cellularPortGetTickTimeMs();
    gStopTimeMs = cellularPortGetTickTimeMs() +
                  (CELLULAR_CFG_TEST_MQTT_SERVER_TIMEOUT_SECONDS * 1000);
    y = cellularMqttSubscribe(CELLULAR_MQTT_EXACTLY_ONCE, pTopicOut);
    if (y >= 0) {
        cellularPortLog("CELLULAR_MQTT_TEST: subscribe successful after %d ms, QoS %d.\n",
                        (int32_t) (cellularPortGetTickTimeMs() - startTimeMs), y);
    } else {
        cellularPortLog("CELLULAR_MQTT_TEST: subscribe returned error %d after %d ms,"
                        " module error %d.\n",
                        y, (int32_t) (cellularPortGetTickTimeMs() - startTimeMs),
                        cellularMqttGetLastErrorCode());
        CELLULAR_PORT_TEST_ASSERT(false);
    }

    // There may be unread messages sitting on the server from a previous test run,
    // read them off here.
    y = cellularMqttGetUnread();
    for (size_t x = 0; x < y; x++) {
        while (cellularMqttGetUnread() > 0) {
            cellularPortLog("CELLULAR_MQTT_TEST: reading existing unread message %d of %d.\n",
                            x + 1, y);
            CELLULAR_PORT_TEST_ASSERT(cellularMqttMessageRead(pTopicIn,
                                                              CELLULAR_MQTT_READ_TOPIC_MAX_LENGTH_BYTES,
                                                              pMessageIn, &z,
                                                              NULL) == 0);
            CELLULAR_PORT_TEST_ASSERT(cellularPort_strcmp(pTopicIn, pTopicOut) == 0);
            // Let everyone sort themselves out
            cellularPortTaskBlock(5000);
        }
    }

    CELLULAR_PORT_TEST_ASSERT(cellularMqttGetUnread() == 0);

    cellularPortLog("CELLULAR_MQTT_TEST: publishing %d byte(s) to topic \"%s\"...\n",
                    CELLULAR_MQTT_PUBLISH_MAX_LENGTH_BYTES, pTopicOut);
    startTimeMs = cellularPortGetTickTimeMs();
    gStopTimeMs = cellularPortGetTickTimeMs() +
                  (CELLULAR_CFG_TEST_MQTT_SERVER_TIMEOUT_SECONDS * 1000);
    // Fill in the outgoing message buffer with all possible things
    y = CELLULAR_MQTT_PUBLISH_MAX_LENGTH_BYTES;
    while (y > 0) {
        z = sizeof(gAllChars);
        if (z > y) {
            z = y;
        }
        pCellularPort_memcpy(pMessageOut, gAllChars, z);
        y -= z;
    }
    y = cellularMqttPublish(CELLULAR_MQTT_EXACTLY_ONCE, false, pTopicOut,
                            pMessageOut, CELLULAR_MQTT_PUBLISH_MAX_LENGTH_BYTES);
    if (y == 0) {
        cellularPortLog("CELLULAR_MQTT_TEST: publish successful after %d ms.\n",
                        (int32_t) (cellularPortGetTickTimeMs() - startTimeMs));
        numPublished++;
    } else {
        cellularPortLog("CELLULAR_MQTT_TEST: publish returned error %d after %d ms, module"
                        " error %d.\n",
                        y, (int32_t) (cellularPortGetTickTimeMs() - startTimeMs),
                        cellularMqttGetLastErrorCode());
        CELLULAR_PORT_TEST_ASSERT(false);
    }

    cellularPortLog("CELLULAR_MQTT_TEST: waiting for an unread message indication...\n");
    startTimeMs = cellularPortGetTickTimeMs();
    while ((numUnread == 0) &&
           (cellularPortGetTickTimeMs() < startTimeMs +
                                         (CELLULAR_CFG_TEST_MQTT_SERVER_TIMEOUT_SECONDS * 1000))) {
        cellularPortTaskBlock(1000);
    }

    if (numUnread > 0) {
        cellularPortLog("CELLULAR_MQTT_TEST: %d message(s) unread.\n", numUnread);
    } else {
        cellularPortLog("CELLULAR_MQTT_TEST: no messages unread after %d ms.\n",
                        (int32_t) (cellularPortGetTickTimeMs() - startTimeMs));
        CELLULAR_PORT_TEST_ASSERT(false);
    }

    CELLULAR_PORT_TEST_ASSERT(numUnread == 1);
    CELLULAR_PORT_TEST_ASSERT(cellularMqttGetUnread() == numUnread);

    cellularPortLog("CELLULAR_MQTT_TEST: reading the message...\n");
    qos = -1;
    y = CELLULAR_MQTT_READ_MESSAGE_MAX_LENGTH_BYTES;
    CELLULAR_PORT_TEST_ASSERT(cellularMqttMessageRead(pTopicIn,
                                                      CELLULAR_MQTT_READ_TOPIC_MAX_LENGTH_BYTES,
                                                      pMessageIn, &y,
                                                      &qos) == 0);
    cellularPortLog("CELLULAR_MQTT_TEST: read %d byte(s)...\n", y);
    CELLULAR_PORT_TEST_ASSERT(qos == CELLULAR_MQTT_EXACTLY_ONCE);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_strcmp(pTopicIn, pTopicOut) == 0);
    CELLULAR_PORT_TEST_ASSERT(y == CELLULAR_MQTT_PUBLISH_MAX_LENGTH_BYTES);
    CELLULAR_PORT_TEST_ASSERT(cellularPort_memcmp(pMessageIn, pMessageOut, y) == 0);

    CELLULAR_PORT_TEST_ASSERT(cellularMqttGetUnread() == 0);

    // Cancel the subscribe
    cellularPortLog("CELLULAR_MQTT_TEST: unsubscribing from topic \"%s\"...\n",
                    pTopicOut);
    gStopTimeMs = cellularPortGetTickTimeMs() +
                  (CELLULAR_CFG_TEST_MQTT_SERVER_TIMEOUT_SECONDS * 1000);
    CELLULAR_PORT_TEST_ASSERT(cellularMqttUnsubscribe(pTopicOut) == 0);

    // Remove the callback
    CELLULAR_PORT_TEST_ASSERT(cellularMqttSetMessageIndicationCallback(NULL, NULL) == 0);

    cellularPortLog("CELLULAR_MQTT_TEST: disconnecting again...\n");
    gStopTimeMs = cellularPortGetTickTimeMs() +
                  (CELLULAR_CFG_TEST_MQTT_SERVER_TIMEOUT_SECONDS * 1000);
    CELLULAR_PORT_TEST_ASSERT(cellularMqttDisconnect() == 0);
    CELLULAR_PORT_TEST_ASSERT(!cellularMqttIsConnected());

    // Disconnect from the cellular network and tidy up
    networkDisconnect();

    cellularCtrlPowerOff(NULL);
    cellularMqttDeinit();

    cellularPort_free(pMessageIn);
    cellularPort_free(pMessageOut);
    cellularPort_free(pTopicIn);
    cellularPort_free(pTopicOut);

    cellularCtrlDeinit();
    CELLULAR_PORT_TEST_ASSERT(cellularPortUartDeinit(CELLULAR_CFG_UART) == 0);
    cellularPortDeinit();
}

#  if CELLULAR_CTRL_SECURITY_ROOT_OF_TRUST

/** Subscribe/publish encypted messages with a Thingstream MQTT server.
 */
CELLULAR_PORT_TEST_FUNCTION(void cellularMqttTestThingstreamEncrypted(),
                            "mqttThingstreamEncrypted",
                            "mqtt")
{
    int32_t y;
    int32_t z;
    int32_t numPublished = 0;
    int32_t numUnread = 0;
    int64_t startTimeMs;
    char *pTopicOut;
    char *pTopicIn;
    char *pMessageOut;
    char *pMessageIn;
    CellularMqttQos_t qos;

    CELLULAR_PORT_TEST_ASSERT(cellularPortInit() == 0);
    CELLULAR_PORT_TEST_ASSERT(cellularPortUartInit(CELLULAR_CFG_PIN_TXD,
                                                   CELLULAR_CFG_PIN_RXD,
                                                   CELLULAR_CFG_PIN_CTS,
                                                   CELLULAR_CFG_PIN_RTS,
                                                   CELLULAR_CFG_BAUD_RATE,
                                                   CELLULAR_CFG_RTS_THRESHOLD,
                                                   CELLULAR_CFG_UART,
                                                   &gUartQueueHandle) == 0);
    CELLULAR_PORT_TEST_ASSERT(cellularCtrlInit(CELLULAR_CFG_PIN_ENABLE_POWER,
                                               CELLULAR_CFG_PIN_PWR_ON,
                                               CELLULAR_CFG_PIN_VINT,
                                               false,
                                               CELLULAR_CFG_UART,
                                               gUartQueueHandle) == 0);

    CELLULAR_PORT_TEST_ASSERT(cellularCtrlPowerOn(NULL) == 0);

    cellularPortLog("CELLULAR_MQTT_TEST: getting status of security seal...\n");
    y = cellularCtrlGetSecuritySeal();
    cellularPortLog("CELLULAR_MQTT_TEST: security seal status is %d.\n", y);
    if (y == CELLULAR_CTRL_SUCCESS) {

        CELLULAR_PORT_TEST_ASSERT(sizeof(CELLULAR_MQTT_TEST_MESSAGE) - 1 +
                                  CELLULAR_CTRL_END_TO_END_ENCRYPT_HEADER_SIZE_BYTES <= CELLULAR_MQTT_PUBLISH_MAX_LENGTH_BYTES);
        CELLULAR_PORT_TEST_ASSERT(sizeof(CELLULAR_MQTT_TEST_MESSAGE) - 1 + 
                                  CELLULAR_CTRL_END_TO_END_ENCRYPT_HEADER_SIZE_BYTES <= CELLULAR_MQTT_READ_MESSAGE_MAX_LENGTH_BYTES);

        // Malloc space to read messages and topics into
        pTopicOut = (char *) pCellularPort_malloc(CELLULAR_MQTT_READ_TOPIC_MAX_LENGTH_BYTES);
        CELLULAR_PORT_TEST_ASSERT(pTopicOut != NULL);
        pTopicIn = (char *) pCellularPort_malloc(CELLULAR_MQTT_READ_TOPIC_MAX_LENGTH_BYTES);
        CELLULAR_PORT_TEST_ASSERT(pTopicIn != NULL);
        pMessageOut = (char *) pCellularPort_malloc(CELLULAR_MQTT_PUBLISH_MAX_LENGTH_BYTES);
        CELLULAR_PORT_TEST_ASSERT(pMessageOut != NULL);
        pMessageIn = (char *) pCellularPort_malloc(CELLULAR_MQTT_READ_MESSAGE_MAX_LENGTH_BYTES);
        CELLULAR_PORT_TEST_ASSERT(pMessageIn != NULL);

        // Make a unique topic name to stop different boards colliding
        cellularPort_snprintf(pTopicOut, CELLULAR_MQTT_READ_TOPIC_MAX_LENGTH_BYTES,
                              "ubx_test/%s", gImei);

        cellularPortLog("CELLULAR_CTRL_TEST: requesting end to end encryption...\n");
        z = cellularSecurityEndToEndEncrypt(CELLULAR_MQTT_TEST_MESSAGE,
                                            pMessageOut,
                                            sizeof(CELLULAR_MQTT_TEST_MESSAGE) - 1);
        CELLULAR_PORT_TEST_ASSERT(z <= sizeof(CELLULAR_MQTT_TEST_MESSAGE) - 1 + 
                                       CELLULAR_CTRL_END_TO_END_ENCRYPT_HEADER_SIZE_BYTES);

        // Call this first in case a previous failed test left things initialised
        cellularMqttDeinit();

        networkConnect(CELLULAR_CFG_TEST_APN,
                       CELLULAR_CFG_TEST_USERNAME,
                       CELLULAR_CFG_TEST_PASSWORD);

        cellularPortLog("CELLULAR_MQTT_TEST: initialising MQTT with server \"%s\"...\n",
                        CELLULAR_MQTT_THINGSTREAM_SERVER_UNSECURE);
        CELLULAR_PORT_TEST_ASSERT(cellularMqttInit(CELLULAR_MQTT_THINGSTREAM_SERVER_UNSECURE,
                                                   CELLULAR_PORT_STRINGIFY_QUOTED(CELLULAR_CFG_TEST_THINGSTREAM_CLIENT_ID),
                                                   CELLULAR_PORT_STRINGIFY_QUOTED(CELLULAR_CFG_TEST_THINGSTREAM_USERNAME),
                                                   CELLULAR_PORT_STRINGIFY_QUOTED(CELLULAR_CFG_TEST_THINGSTREAM_PASSWORD),
                                                   keepGoingCallback) == 0);

        startTimeMs = cellularPortGetTickTimeMs();
        cellularPortLog("CELLULAR_MQTT_TEST: connecting to %s...\n",
                        CELLULAR_MQTT_THINGSTREAM_SERVER_UNSECURE);
        gStopTimeMs = cellularPortGetTickTimeMs() +
                      (CELLULAR_CFG_TEST_MQTT_SERVER_TIMEOUT_SECONDS * 1000);
        y = cellularMqttConnect();
        if (y == 0) {
            cellularPortLog("CELLULAR_MQTT_TEST: connected after %d seconds.\n",
                            ((int32_t) (cellularPortGetTickTimeMs() - startTimeMs)) / 1000);
            CELLULAR_PORT_TEST_ASSERT(cellularMqttIsConnected());
        } else {
            cellularPortLog("CELLULAR_MQTT_TEST: not connected after %d seconds, module error %d.\n",
                            ((int32_t) (cellularPortGetTickTimeMs() - startTimeMs)) / 1000,
                            cellularMqttGetLastErrorCode());
            CELLULAR_PORT_TEST_ASSERT(!cellularMqttIsConnected());
            CELLULAR_PORT_TEST_ASSERT(false);
        }

        // Set the callback
        CELLULAR_PORT_TEST_ASSERT(cellularMqttSetMessageIndicationCallback(messageIndicationCallback,
                                                                           &numUnread) == 0);

        cellularPortLog("CELLULAR_MQTT_TEST: subscribing to topic \"%s\"...\n", pTopicOut);
        startTimeMs = cellularPortGetTickTimeMs();
        gStopTimeMs = cellularPortGetTickTimeMs() +
                      (CELLULAR_CFG_TEST_MQTT_SERVER_TIMEOUT_SECONDS * 1000);
        y = cellularMqttSubscribe(CELLULAR_MQTT_EXACTLY_ONCE, pTopicOut);
        if (y >= 0) {
            cellularPortLog("CELLULAR_MQTT_TEST: subscribe successful after %d ms, QoS %d.\n",
                            (int32_t) (cellularPortGetTickTimeMs() - startTimeMs), y);
        } else {
            cellularPortLog("CELLULAR_MQTT_TEST: subscribe returned error %d after %d ms,"
                            " module error %d.\n",
                            y, (int32_t) (cellularPortGetTickTimeMs() - startTimeMs),
                            cellularMqttGetLastErrorCode());
            CELLULAR_PORT_TEST_ASSERT(false);
        }

        // There may be unread messages sitting on the server from a previous test run,
        // read them off here.
        y = cellularMqttGetUnread();
        for (size_t x = 0; x < y; x++) {
            while (cellularMqttGetUnread() > 0) {
                cellularPortLog("CELLULAR_MQTT_TEST: reading existing unread message %d of %d.\n",
                                x + 1, y);
                CELLULAR_PORT_TEST_ASSERT(cellularMqttMessageRead(pTopicIn,
                                                                  CELLULAR_MQTT_READ_TOPIC_MAX_LENGTH_BYTES,
                                                                  pMessageIn, &z,
                                                                  NULL) == 0);
                CELLULAR_PORT_TEST_ASSERT(cellularPort_strcmp(pTopicIn, pTopicOut) == 0);
                // Let everyone sort themselves out
                cellularPortTaskBlock(5000);
            }
        }

        CELLULAR_PORT_TEST_ASSERT(cellularMqttGetUnread() == 0);

        cellularPortLog("CELLULAR_MQTT_TEST: publishing %d byte(s) to topic \"%s\"...\n",
                        z, pTopicOut);
        startTimeMs = cellularPortGetTickTimeMs();
        gStopTimeMs = cellularPortGetTickTimeMs() +
                      (CELLULAR_CFG_TEST_MQTT_SERVER_TIMEOUT_SECONDS * 1000);
        y = cellularMqttPublish(CELLULAR_MQTT_EXACTLY_ONCE, false, pTopicOut,
                                pMessageOut, z);
        if (y == 0) {
            cellularPortLog("CELLULAR_MQTT_TEST: publish successful after %d ms.\n",
                            (int32_t) (cellularPortGetTickTimeMs() - startTimeMs));
            numPublished++;
        } else {
            cellularPortLog("CELLULAR_MQTT_TEST: publish returned error %d after %d ms, module"
                            " error %d.\n",
                            y, (int32_t) (cellularPortGetTickTimeMs() - startTimeMs),
                            cellularMqttGetLastErrorCode());
            CELLULAR_PORT_TEST_ASSERT(false);
        }

        cellularPortLog("CELLULAR_MQTT_TEST: waiting for an unread message indication...\n");
        startTimeMs = cellularPortGetTickTimeMs();
        while ((numUnread == 0) &&
               (cellularPortGetTickTimeMs() < startTimeMs +
                                             (CELLULAR_CFG_TEST_MQTT_SERVER_TIMEOUT_SECONDS * 1000))) {
            cellularPortTaskBlock(1000);
        }

        if (numUnread > 0) {
            cellularPortLog("CELLULAR_MQTT_TEST: %d message(s) unread.\n", numUnread);
        } else {
            cellularPortLog("CELLULAR_MQTT_TEST: no messages unread after %d ms.\n",
                            (int32_t) (cellularPortGetTickTimeMs() - startTimeMs));
            CELLULAR_PORT_TEST_ASSERT(false);
        }

        CELLULAR_PORT_TEST_ASSERT(numUnread == 1);
        CELLULAR_PORT_TEST_ASSERT(cellularMqttGetUnread() == numUnread);

        cellularPortLog("CELLULAR_MQTT_TEST: reading the message...\n");
        qos = -1;
        y = CELLULAR_MQTT_READ_MESSAGE_MAX_LENGTH_BYTES;
        CELLULAR_PORT_TEST_ASSERT(cellularMqttMessageRead(pTopicIn,
                                                          CELLULAR_MQTT_READ_TOPIC_MAX_LENGTH_BYTES,
                                                          pMessageIn, &y,
                                                          &qos) == 0);
        cellularPortLog("CELLULAR_MQTT_TEST: read %d byte(s)...\n", y);
        CELLULAR_PORT_TEST_ASSERT(qos == CELLULAR_MQTT_EXACTLY_ONCE);
        CELLULAR_PORT_TEST_ASSERT(cellularPort_strcmp(pTopicIn, pTopicOut) == 0);
        CELLULAR_PORT_TEST_ASSERT(y == z);
        CELLULAR_PORT_TEST_ASSERT(cellularPort_memcmp(pMessageIn, pMessageOut, z) == 0);

        CELLULAR_PORT_TEST_ASSERT(cellularMqttGetUnread() == 0);

        // Cancel the subscribe
        cellularPortLog("CELLULAR_MQTT_TEST: unsubscribing from topic \"%s\"...\n",
                        pTopicOut);
        gStopTimeMs = cellularPortGetTickTimeMs() +
                      (CELLULAR_CFG_TEST_MQTT_SERVER_TIMEOUT_SECONDS * 1000);
        CELLULAR_PORT_TEST_ASSERT(cellularMqttUnsubscribe(pTopicOut) == 0);

        // Remove the callback
        CELLULAR_PORT_TEST_ASSERT(cellularMqttSetMessageIndicationCallback(NULL, NULL) == 0);

        cellularPortLog("CELLULAR_MQTT_TEST: disconnecting again...\n");
        gStopTimeMs = cellularPortGetTickTimeMs() +
                      (CELLULAR_CFG_TEST_MQTT_SERVER_TIMEOUT_SECONDS * 1000);
        CELLULAR_PORT_TEST_ASSERT(cellularMqttDisconnect() == 0);
        CELLULAR_PORT_TEST_ASSERT(!cellularMqttIsConnected());

        // Disconnect from the cellular network and tidy up
        networkDisconnect();

        cellularPort_free(pMessageIn);
        cellularPort_free(pMessageOut);
        cellularPort_free(pTopicIn);
        cellularPort_free(pTopicOut);
    } else {
        cellularPortLog("CELLULAR_MQTT_TEST: NOT RUNNING test of encrypted "
                        "MQTT as the cellular module is not security "
                        "sealed.\n");
    }

    cellularCtrlPowerOff(NULL);
    cellularMqttDeinit();

    cellularCtrlDeinit();
    CELLULAR_PORT_TEST_ASSERT(cellularPortUartDeinit(CELLULAR_CFG_UART) == 0);
    cellularPortDeinit();
}
#  endif // CELLULAR_CTRL_SECURITY_ROOT_OF_TRUST

# endif // CELLULAR_CFG_TEST_THINGSTREAM_USERNAME

/** Clean-up to be run at the end of this round of tests, just
 * in case there were test failures which would have resulted
 * in the deinitialisation being skipped.
 */
CELLULAR_PORT_TEST_FUNCTION(void cellularMqttTestCleanUp(),
                            "mqttCleanUp",
                            "mqtt")
{
    cellularMqttDeinit();
    cellularCtrlDeinit();
    CELLULAR_PORT_TEST_ASSERT(cellularPortUartDeinit(CELLULAR_CFG_UART) == 0);
    cellularPortDeinit();
}

#endif // CELLULAR_MQTT_IS_SUPPORTED

// End of file
