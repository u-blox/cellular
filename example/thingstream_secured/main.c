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

/* This file demonstrates the use of u-blox end-to-end security
 * with the Thingstream service using a u-blox cellular module.
 * The choice of module and the choice of platform on which this
 * code runs is made at build time, see the README.md for
 * instructions.
 */

#ifdef CELLULAR_CFG_OVERRIDE
# include "cellular_cfg_override.h" // For a customer's configuration override
#endif
#include "cellular_cfg_hw_platform_specific.h"
#include "cellular_cfg_os_platform_specific.h"
#include "cellular_cfg_sw.h"
#include "cellular_cfg_module.h"
#include "cellular_port_clib.h"
#include "cellular_port.h"
#include "cellular_port_debug.h"
#include "cellular_port_os.h"
#include "cellular_port_uart.h"
#include "cellular_ctrl.h"
#include "cellular_mqtt.h"
#ifdef CELLULAR_CFG_UBLOX_TEST
// This purely for internal u-blox testing
# include "cellular_port_test_platform_specific.h"
# include "cellular_cfg_test.h"
#endif

// Only include this example if root of trust and
// MQTT are both supported by the cellular module.
#if CELLULAR_CTRL_SECURITY_ROOT_OF_TRUST && CELLULAR_MQTT_IS_SUPPORTED && \
    (defined(MY_THINGSTREAM_CLIENT_ID) ||                                 \
     defined(CELLULAR_CFG_TEST_THINGSTREAM_CLIENT_ID))

#ifndef CELLULAR_PORT_TEST_FUNCTION
# error if you are not using the unit test framework to run this code you must ensure that the platform clocks/RTOS are set up and either define CELLULAR_PORT_TEST_FUNCTION yourself or replace it as necessary.
#endif

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// Please replace the lines below with your Thingstream
// Client ID, e.g. something like:
// #define MY_THINGSTREAM_CLIENT_ID "device:521b5a33-2374-4547-8edc-50743cf45709"
// The CELLULAR_CFG setting below is simply used during
// internal u-blox testing of this example.
#if !defined(MY_THINGSTREAM_CLIENT_ID) && defined(CELLULAR_CFG_TEST_THINGSTREAM_CLIENT_ID)
# define MY_THINGSTREAM_CLIENT_ID CELLULAR_PORT_STRINGIFY_QUOTED(CELLULAR_CFG_TEST_THINGSTREAM_CLIENT_ID)
#endif

// Please replace the lines below with your Thingstream
// user name, e.g. something like:
// #define MY_THINGSTREAM_USERNAME "WF782XXWUQ10212JUU6P"
// The CELLULAR_CFG setting below is simply used during
// internal u-blox testing of this example.
#if !defined(MY_THINGSTREAM_USERNAME) && defined(CELLULAR_CFG_TEST_THINGSTREAM_USERNAME)
# define MY_THINGSTREAM_USERNAME CELLULAR_PORT_STRINGIFY_QUOTED(CELLULAR_CFG_TEST_THINGSTREAM_USERNAME)
#endif

// Please replace the lines below with your Thingstream
// password, e.g. something like:
// #define MY_THINGSTREAM_PASSWORD "cef059L/PRVXntInvcBpQVXyz7peZ/xpt3X9tw01"
// The CELLULAR_CFG setting below is simply used during
// internal u-blox testing of this example.
#if !defined(MY_THINGSTREAM_PASSWORD) && defined(CELLULAR_CFG_TEST_THINGSTREAM_PASSWORD)
# define MY_THINGSTREAM_PASSWORD CELLULAR_PORT_STRINGIFY_QUOTED(CELLULAR_CFG_TEST_THINGSTREAM_PASSWORD)
#endif

// If you want this example to run in a particular RAT
// e.g. CatM1, NB1, etc., define it either by replacing
// the lines below with, for instance:
//
// #define MY_RAT CELLULAR_CTRL_RAT_CATM1
//
// or by setting MY_RAT to your chosen RAT outside this
// build.  The CELLULAR_CFG setting below is simply
// used during internal u-blox testing of this example.
#if !defined(MY_RAT) && defined(CELLULAR_CFG_TEST_RAT)
# define MY_RAT CELLULAR_CFG_TEST_RAT
#endif

// If you chose CATM1 or NB1 as your RAT, you may
// wish to chose the bands to use by editing
// the lines below to set both MY_BANDMASK1 and
// MY_BANDMASK2 (both must be set) or by setting
// both MY_BANDMASK1 and MY_BANDMASK2 outside this build.
// The CELLULAR_CFG setting below is simply used during
// internal u-blox testing of this example.
#if !defined(MY_BANDMASK1) && defined(CELLULAR_CFG_TEST_BANDMASK1)
# define MY_BANDMASK1 CELLULAR_CFG_TEST_BANDMASK1
#endif
#if !defined(MY_BANDMASK2) && defined(CELLULAR_CFG_TEST_BANDMASK2)
# define MY_BANDMASK2 CELLULAR_CFG_TEST_BANDMASK2
#endif

// The Thingstream server URL.
#define THINGSTREAM_SERVER "mqtt.thingstream.io"

// A topic to publish to.
#define MY_TOPIC "test/ubx_1"

// A message to send.
#define MY_MESSAGE "Hello World!"

// If your cellular subscription needs an APN, possibly
// with an associated username or password, then set it here.
#define MY_APN      NULL
#define MY_USERNAME NULL
#define MY_PASSWORD NULL

// For u-blox internal testing only
#ifdef CELLULAR_PORT_TEST_ASSERT
# define EXAMPLE_FINAL_STATE(x) CELLULAR_PORT_TEST_ASSERT(x);
#else 
# define EXAMPLE_FINAL_STATE(x)
#endif

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

// Buffer in which to store a received topic.
static char gTopic[CELLULAR_MQTT_READ_TOPIC_MAX_LENGTH_BYTES];

// Buffer in which to store our outgoing encrypted message.
static char gMessageOut[sizeof(MY_MESSAGE) - 1 +
                        CELLULAR_CTRL_END_TO_END_ENCRYPT_HEADER_SIZE_BYTES];

// Buffer in which to store a received message.
static char gMessageIn[CELLULAR_MQTT_READ_MESSAGE_MAX_LENGTH_BYTES];

// Used for keepGoingCallback() timeout.
static int64_t gStopTimeMs;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

// Callback function for the cellular connection process,
// return true while the connection process should continue,
// false to stop it.
static bool keepGoingCallback()
{
    bool keepGoing = true;

    if (cellularPortGetTickTimeMs() > gStopTimeMs) {
        keepGoing = false;
    }

    return keepGoing;
}

// Print a buffer in hex
static void printHex(char *pBuffer, char bufferLength)
{
    for (size_t x = 0; x < bufferLength; x++) {
        cellularPortLog("%02x", *pBuffer);
        pBuffer++;
    }
}

// Connect to the cellular network, returning 0 on success
// else negative error code.
static int32_t connect()
{
#if defined(MY_RAT) && defined(MY_BANDMASK1) && defined(MY_BANDMASK2)
    bool rebootRequired = false;
    uint64_t bandMask1;
    uint64_t bandMask2;
#endif

#ifdef MY_RAT
    // Check if we're on the correct RAT
    if (cellularCtrlGetRat(0) != MY_RAT) {
        cellularCtrlSetRatRank(MY_RAT, 0);
        rebootRequired = true;
    }
#endif

#if defined(MY_RAT) && defined(MY_BANDMASK1) && defined(MY_BANDMASK2)
    // If we're using CAT-M1 or NB1 RAT make sure that the band mask is correct
    if ((MY_RAT == CELLULAR_CTRL_RAT_CATM1) || (MY_RAT == CELLULAR_CTRL_RAT_NB1)) {
        cellularCtrlGetBandMask(MY_RAT, &bandMask1, &bandMask2);
        if ((bandMask1 != MY_BANDMASK1) || (bandMask2 != MY_BANDMASK2)) {
            cellularCtrlSetBandMask(MY_RAT, bandMask1, bandMask2);
            rebootRequired = true;
        }
    }
#endif

#ifdef MY_RAT
    // If we made a change above, the cellular module must be rebooted
    // for it to take effect
    if (rebootRequired) {
        cellularCtrlReboot();
    }
#endif

    // Now connect to the network
    gStopTimeMs = cellularPortGetTickTimeMs() + (180 * 1000);
    return cellularCtrlConnect(keepGoingCallback, MY_APN, MY_USERNAME, MY_PASSWORD);
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: THE EXAMPLE
 * -------------------------------------------------------------- */

// The entry point: before this is called the system clocks have
// been started and the RTOS is running; we are in task space.
CELLULAR_PORT_TEST_FUNCTION(void cellularExampleThingstreamSecured(),
                            "exampleThingstreamSecured",
                            "mqtt")
{
    CellularPortQueueHandle_t uartQueueHandle;
    int32_t x;
    bool success = false;

    // Initialise the underlying cellular module drivers
    cellularPortInit();
    cellularPortUartInit(CELLULAR_CFG_PIN_TXD, CELLULAR_CFG_PIN_RXD,
                         CELLULAR_CFG_PIN_CTS, CELLULAR_CFG_PIN_RTS,
                         CELLULAR_CFG_BAUD_RATE,
                         CELLULAR_CFG_RTS_THRESHOLD,
                         CELLULAR_CFG_UART, &uartQueueHandle);
    cellularCtrlInit(CELLULAR_CFG_PIN_ENABLE_POWER,
                     CELLULAR_CFG_PIN_PWR_ON, CELLULAR_CFG_PIN_VINT,
                     false, CELLULAR_CFG_UART, uartQueueHandle);

    cellularPortLog("EXAMPLE: powering on cellular module...\n");
    if (cellularCtrlPowerOn(NULL) == 0) {

        // Continue only if we have a security seal
        cellularPortLog("EXAMPLE: checking security seal of "
                        " cellular module...\n");
        if (cellularCtrlGetSecuritySeal() == 0) {

            // Have our message encrypted by the cellular module
            // and returned to us in gMessageOut.
            cellularPortLog("EXAMPLE: encrypting %d byte message \"%s\"...\n",
                           cellularPort_strlen(MY_MESSAGE), MY_MESSAGE);
            x = cellularSecurityEndToEndEncrypt(MY_MESSAGE, gMessageOut,
                                                cellularPort_strlen(MY_MESSAGE));
            cellularPortLog("EXAMPLE: got back %d byte(s) of encrypted"
                            " message: 0x", x);
            printHex(gMessageOut, x);
            cellularPortLog("\n");

            // Connect to the network
            cellularPortLog("EXAMPLE: connecting to the cellular network...\n");
            if (connect() == 0) {

                // Initialise MQTT
                cellularPortLog("EXAMPLE: initialising with client ID"
                                " \"%s\"...\n", MY_THINGSTREAM_CLIENT_ID);
                cellularMqttInit(THINGSTREAM_SERVER,
                                 MY_THINGSTREAM_CLIENT_ID,
                                 MY_THINGSTREAM_USERNAME,
                                 MY_THINGSTREAM_PASSWORD,
                                 NULL);

                // Connect to the MQTT server
                cellularPortLog("EXAMPLE: connecting to %s...\n",
                                THINGSTREAM_SERVER);
                if (cellularMqttConnect() == 0) {

                    // Subscribe to our topic, just so that we can check that
                    // the message we are about to publish has indeed been published
                    cellularPortLog("EXAMPLE: subscribing to topic \"%s\"...\n",
                                    MY_TOPIC);
                    if (cellularMqttSubscribe(CELLULAR_MQTT_AT_MOST_ONCE,
                                              MY_TOPIC) == 0) {

                        // Publish our encrypted message to the topic
                        cellularPortLog("EXAMPLE: publishing %d byte(s) to"
                                        " topic \"%s\"...\n", x, MY_TOPIC);
                        if (cellularMqttPublish(CELLULAR_MQTT_AT_MOST_ONCE,
                                                false, MY_TOPIC,
                                                gMessageOut, x) == 0) {

                            // Wait for an indication that a message has arrived
                            // back because of our subscription to the topic
                            cellularPortLog("EXAMPLE: waiting up to 20 second(s)"
                                            " for unread message indication...\n");
                            for (x = 0; (cellularMqttGetUnread() == 0) &&
                                        (x < 20); x++) {
                                cellularPortTaskBlock(1000);
                            }

                            // Read the message
                            if (cellularMqttGetUnread() > 0) {
                                cellularPortLog("EXAMPLE: message arrived in less"
                                                " than %d second(s).\n", x);
                                cellularPortLog("EXAMPLE: reading message...\n");
                                x = CELLULAR_MQTT_READ_MESSAGE_MAX_LENGTH_BYTES;
                                if (cellularMqttMessageRead(gTopic, sizeof(gTopic),
                                                            gMessageIn, &x,
                                                            NULL) == 0) {
                                    cellularPortLog("EXAMPLE: message %d byte(s):"
                                                    " 0x:", x);
                                    printHex(gMessageIn, x);
                                    cellularPortLog("\nEXAMPLE: ...from topic"
                                                    " \"%s\".\n", gTopic);
                                    cellularPortLog("EXAMPLE: done.\n");
                                    success = true;
                                } else {
                                    cellularPortLog("EXAMPLE ERROR: unable to"
                                                    " read message.\n");
                                }
                            } else {
                                cellularPortLog("EXAMPLE ERROR: no unread"
                                                " message indication was"
                                                " received, the message"
                                                " may not have been"
                                                " published.\n");
                            }
                        } else {
                            cellularPortLog("EXAMPLE ERROR: unable to publish"
                                            " %d bytes(s) to topic \"%s\".\n",
                                            x, MY_TOPIC);
                        }
                    } else {
                        cellularPortLog("EXAMPLE ERROR: unable to subscribe to"
                                        " topic \"%s\".\n", MY_TOPIC);
                    }

                    // Tidy up: unsubscribe from the topic and log out of the server
                    cellularMqttUnsubscribe(MY_TOPIC);
                    cellularMqttDisconnect();

                } else {
                    cellularPortLog("EXAMPLE ERROR: unable to connect to %s with"
                                    " client ID \"%s\", please check that your"
                                    " client ID, username and password are"
                                    " correct.  The cellular module reported"
                                    " error %d which may help diagnose the"
                                    " problem, please look it up in the MQTT"
                                    " error codes annex for the cellular"
                                    " module you are using.\n",
                                    THINGSTREAM_SERVER, MY_THINGSTREAM_CLIENT_ID,
                                    cellularMqttGetLastErrorCode());
                }

                // Deinit MQTT
                cellularMqttDeinit();

                // Disconnect from the cellular network
                cellularCtrlDisconnect();
            } else {
                cellularPortLog("EXAMPLE ERROR: unable to connect to the cellular network.\n");
            }
        } else {
            cellularPortLog("EXAMPLE: unable to run this example of end to"
                            " end security as this cellular module is not"
                            " security sealed.\n");
            success = true;
        }
        cellularCtrlPowerOff(NULL);
    } else {
        cellularPortLog("EXAMPLE ERROR: unable to power-up the cellular module.\n");
    }

    cellularPortLog("EXAMPLE: finished.\n");

    cellularCtrlDeinit();
    cellularPortUartDeinit(CELLULAR_CFG_UART);
    cellularPortDeinit();

    // For u-blox internal testing only
    EXAMPLE_FINAL_STATE(success);
}

#endif // CELLULAR_CTRL_SECURITY_ROOT_OF_TRUST && CELLULAR_MQTT_IS_SUPPORTED &&
       // (defined(MY_THINGSTREAM_CLIENT_ID) || defined(CELLULAR_CFG_TEST_THINGSTREAM_CLIENT_ID))

// End of file
