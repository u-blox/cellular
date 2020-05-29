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
#include "cellular_ctrl_at.h"
#include "cellular_ctrl.h"
#include "cellular_sock.h" // Required for IP address manipulation
#include "cellular_mqtt.h"

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/** Return "not supported" if this module doesn't support MQTT.
 */
#if CELLULAR_MQTT_IS_SUPPORTED
# define CELLULAR_MQTT_DEFAULT_ERROR_CODE CELLULAR_MQTT_NOT_INITIALISED
#else
# define CELLULAR_MQTT_DEFAULT_ERROR_CODE CELLULAR_MQTT_NOT_SUPPORTED
#endif

/** The time to wait for a URC with information we need when
 * that information is collected locally, rather than waiting
 * on the MQTT servere
 */
#define CELLULAR_MQTT_LOCAL_URC_TIMEOUT_MS 5000

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/** Struct defining a buffer with a length, for use in
 * MqttUrcStatus_t.
 */
typedef struct {
    char *pContents;
    int32_t sizeBytes;
    bool filledIn;
} MqttBuffer_t;

/** Struct to hold all the things an MQTT URC might tell us.
 */
typedef struct {
    bool updateFlag;
    bool connected;
    bool publishSuccess;
    bool subscribeSuccess;
    CellularMqttQos_t subscribeQoS;
    bool unsubscribeSuccess;
    size_t numUnreadMessages;
#ifdef CELLULAR_CFG_MODULE_SARA_R4
    // These only required for SARA-R4
    // which sends the status back in a URC
    MqttBuffer_t clientName;
    int32_t localPortNumber;
    int32_t inactivityTimeoutSeconds;
    int32_t secured;
    int32_t securityProfileId;
    int32_t sessionRetained;
#endif
} MqttUrcStatus_t;

/** Struct to hold a message that has been read in a callback,
 * required for SARA-R4 only.
 */
typedef struct {
    bool messageRead;
    CellularMqttQos_t qos;
    char *pTopicNameStr;
    int32_t topicNameSizeBytes;
    char *pMessage;
    int32_t messageSizeBytes;
} MqttUrcMessage_t;

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

/** Callback to be called while in a function which
 * may have to wait for a server's response.
 */
static bool (*gpKeepGoingCallback)(void) = NULL;

/** Mutex protection.
 */
static CellularPortMutexHandle_t gMutex = NULL;

/** Callback to be called when an indication
 * of messages waiting to be read has been received.
 */
static void (*gpMessageIndicationCallback)(int32_t, void *);

/** User parameter to be passed to the message
 * indication callback.
 */
static void *gpMessageIndicationCallbackParam;

/** Keep track of whether "keep alive" is on or not.
 */
static bool gKeptAlive = false;

/** Store the status values from the URC.
 */
static volatile MqttUrcStatus_t gUrcStatus;

#ifdef CELLULAR_CFG_MODULE_SARA_R4
/** Storage for an MQTT message received in a
 * URC, only required for SARA-R4.
 */
static MqttUrcMessage_t gUrcMessage;
#endif

/** Hex table.
 */
static const char gHex[] = {'0', '1', '2', '3', '4', '5', '6',
                            '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: URCS AND RELATED FUNCTIONS
 * -------------------------------------------------------------- */

// A local "trampoline" for the message indication callback,
// here so that it can be called in the AT parser's
// task callback context and then hold on the mutex before
// calling gpMessageIndicationCallback with its parameters.
static void messageIndicationCallback(void *pParam)
{
    int32_t numUnreadMessages = (int32_t) pParam;

    // Lock the mutex as we'll need
    // two global variables, which could
    // never be atomic
    CELLULAR_PORT_MUTEX_LOCK(gMutex);

    if (gpMessageIndicationCallback != NULL) {
        gpMessageIndicationCallback(numUnreadMessages,
                                    gpMessageIndicationCallbackParam);
    }

    CELLULAR_PORT_MUTEX_UNLOCK(gMutex);
}

// "+UUMQTTC:" URC handler.
static void UUMQTTC_urc()
{
    int32_t urcType;
    int32_t urcParam1;
    int32_t urcParam2;

    urcType = cellular_ctrl_at_read_int();
    // All of the MQTTC URC types have at least one parameter
    urcParam1 = cellular_ctrl_at_read_int();
    switch (urcType) {
        case 0: // Logout, 1 means success
            if ((urcParam1 == 1) ||
                (urcParam1 == 100) || // SARA-R5, inactivity
                (urcParam1 == 101)) { // SARA-R5, connection lost
                // Disconnected
                gUrcStatus.connected = false;
                gUrcStatus.updateFlag = true;
            }
        break;
        case 1: // Login
#ifdef CELLULAR_CFG_MODULE_SARA_R4
            // On SARA-R4 , 0 means success, non-zero
            // values are errors
            if (urcParam1 == 0) {
#else
            if (urcParam1 == 1) {
#endif
                // Connected
                gUrcStatus.connected = true;
                gUrcStatus.updateFlag = true;
            }
        break;
        case 2: // Publish, 1 means success
            if (urcParam1 == 1) {
                // Published
                gUrcStatus.publishSuccess = true;
                gUrcStatus.updateFlag = true;
            }
        break;
        // 3 (publish file) is not used by this driver
        case 4: // Subscribe
            // Get the QoS
            urcParam2 = cellular_ctrl_at_read_int();
            // Skip the topic string
            cellular_ctrl_at_skip_param(1);
#ifdef CELLULAR_CFG_MODULE_SARA_R4
            // On SARA-R4, 0 to 2 mean success
            if ((urcParam1 >= 0) && (urcParam1 <= 2) &&
                (urcParam2 >= 0)) {
#else
            // Elsewhere 1 means success
            if ((urcParam1 == 1) && (urcParam2 >= 0)) {
#endif
                // Subscribed
                gUrcStatus.subscribeSuccess = true;
                gUrcStatus.subscribeQoS = urcParam2;
                gUrcStatus.updateFlag = true;
            }
        break;
        case 5: // Unsubscribe, 1 means success
            if (urcParam1 == 1) {
                // Unsubscribed
                gUrcStatus.unsubscribeSuccess = true;
                gUrcStatus.updateFlag = true;
            }
        break;
        case 6: // Num unread messages
            if (urcParam1 >= 0) {
                gUrcStatus.numUnreadMessages = urcParam1;
                if (gpMessageIndicationCallback != NULL) {
                    // Can't lock the mutex in here as
                    // it would block the receipt of AT
                    // commands.  Instead, launch our
                    // local callback via the AT
                    // parser's callback facility
                    cellular_ctrl_at_callback(messageIndicationCallback,
                                              (void *) (gUrcStatus.numUnreadMessages));
                }
                gUrcStatus.updateFlag = true;
            }
        break;
        default:
            // Do nothing
        break;
    }
}

#ifdef CELLULAR_CFG_MODULE_SARA_R4

// "+UUMQTTx:" URC handler, for SARA-R4 only.
// The switch statement here needs to match those in
// resetUrcStatusField() an checkUrcStatusField()
static void UUMQTTx_urc(int32_t x)
{
    // All these parameters are delimited by
    // a carriage return
    cellular_ctrl_at_set_delimiter('\r');
    switch (x) {
        case 0: // Local client name
            if (!gUrcStatus.clientName.filledIn &&
                (cellular_ctrl_at_read_string(gUrcStatus.clientName.pContents,
                                              gUrcStatus.clientName.sizeBytes,
                                              false) > 0)) {
                gUrcStatus.clientName.filledIn = true;
            }
        break;
        case 1: // Local port number
            gUrcStatus.localPortNumber = cellular_ctrl_at_read_int();
        break;
        case 2: // Server name
        case 3: // Server IP address
        case 4: // User name and password
        // Nothing to do, we never read these back
        break;
        // There is no number 5
        case 6: // Will QoS value
        case 7: // Will retention value
        case 8: // Will topic value
        case 9: // The will message
            // TODO
        break;
        case 10: // Inactivity timeout
            gUrcStatus.inactivityTimeoutSeconds = cellular_ctrl_at_read_int();
        break;
        case 11: // TLS secured
            gUrcStatus.secured = (cellular_ctrl_at_read_int() == 1);
            if (gUrcStatus.secured){
                gUrcStatus.securityProfileId = cellular_ctrl_at_read_int();
            }
        break;
        case 12: // Session retention (actually the value here is
                 // for "clean", hence the inversion)
            gUrcStatus.sessionRetained = (cellular_ctrl_at_read_int() == 0);
        break;
        default:
            // Do nothing
        break;
    }
    cellular_ctrl_at_set_default_delimiter();
}

bool _print = false;

// "+UUMQTTCM:" URC handler, for SARA-R4 only.
static void UUMQTTCM_urc()
{
    int32_t x;
    int32_t topicNameBytesRead = 0;
    int32_t messageBytesAvailable;
    char buffer[20]; // Enough room for "Len:xxxx QoS:y\r\n"

    // Skip the op code
    cellular_ctrl_at_skip_param(1);
    // Set the delimiter to '\'r to make this stop after the integer
    cellular_ctrl_at_set_delimiter('\r');
    // Read the new number of unread messages
    x = cellular_ctrl_at_read_int();
    if (x >= 0) {
        gUrcStatus.numUnreadMessages = x;
    }
    // If this URC is a result of a message
    // arriving what follows will be
    // \r\n
    // Topic:blah\r\r\n
    // Len:64 QoS:2\r\r\n
    // Msg:blah\r\n
    // ...noting no quotations marks around anything
    // Carry on with a delimiter of '\r' to wend our
    // way through this maze.

    // Switch off the stop tag and read
    // in the next 8 bytes and to see if they are "\r\nTopic:"
    cellular_ctrl_at_set_stop_tag(NULL);
    _print = true;
    x = cellular_ctrl_at_read_bytes((uint8_t *) buffer, 8);
    if ((x == 8) &&
        (cellularPort_memcmp(buffer, "\r\nTopic:", 8) == 0)) {
        if (gUrcMessage.pTopicNameStr != NULL) {
            // Read the rest of this line, which will be the topic
            topicNameBytesRead = cellular_ctrl_at_read_string(gUrcMessage.pTopicNameStr,
                                                              gUrcMessage.topicNameSizeBytes,
                                                              false);
        }
        if (topicNameBytesRead >= 0) {
            // Skip the additional '\r\n'
            cellular_ctrl_at_skip_len(2, 1);
            // Read the next line and find the length of the message
            // and the QoS from it
            x = cellular_ctrl_at_read_string(buffer, sizeof(buffer) - 1, false);
            if (x >= 0) {
                buffer[x] = '\0';
                if (cellularPort_sscanf(buffer, "Len:%d QoS:%d",
                                        &messageBytesAvailable, &(gUrcMessage.qos)) == 2) {
                    // Finally, read the next messageBytesAvailable bytes
                    if (messageBytesAvailable >= 0) {
                        // Skip the additional '\r\n'
                        cellular_ctrl_at_skip_len(2, 1);
                        // Throw away the "Msg:" bit
                        cellular_ctrl_at_read_bytes(NULL, 4);
                        // Now read the exact length of message
                        // bytes, being careful to not look for
                        // delimiters or the like as this can be
                        // a binary message
                        x = messageBytesAvailable;
                        if (x > gUrcMessage.messageSizeBytes) {
                            x = gUrcMessage.messageSizeBytes;
                        }
                        cellular_ctrl_at_set_delimiter(0);
                        if (gUrcMessage.pMessage != NULL) {
                            gUrcMessage.messageSizeBytes = 0;
                            gUrcMessage.messageSizeBytes = cellular_ctrl_at_read_bytes((uint8_t *) (gUrcMessage.pMessage),
                                                                                       x);
                            if (gUrcMessage.messageSizeBytes == x) {
                                // Done.  Phew.
                                gUrcMessage.messageRead = true;
                                // Throw away any remainder
                                if (messageBytesAvailable > x) {
                                    cellular_ctrl_at_read_bytes(NULL, messageBytesAvailable - x);
                                }
                            }
                        }
                    }
                }
            }
        }
    } else {
        // If there was no topic name this must be just an indication
        // of the number of messages read so call the callback
        if (gpMessageIndicationCallback != NULL) {
            // Can't lock the mutex in here as
            // it would block the receipt of AT
            // commands.  Instead, launch our
            // local callback via the AT
            // parser's callback facility
            cellular_ctrl_at_callback(messageIndicationCallback,
                                      (void *) (gUrcStatus.numUnreadMessages));
        }
    }
    _print = false;
    cellular_ctrl_at_set_default_delimiter();
}

#endif // CELLULAR_CFG_MODULE_SARA_R4

// MQTT URC handler, which hands
// off to the three MQTT URC types,
// "+UUMQTTx:" (where x can be a two
// digit number), "+UUMQTTC:" and
// "+UUMQTTCM:".
static void UUMQTT_urc(void *pUnused)
{
    uint8_t bytes[3];

    (void) pUnused;

    // Sort out if this is "+UUMQTTC:"
    // or "+UUMQTTx:" or [SARA-R4 only] "+UUMQTTCM:"
    if (cellular_ctrl_at_read_bytes(bytes, sizeof(bytes)) == sizeof(bytes)) {
        if (bytes[0] == 'C') {
#ifdef CELLULAR_CFG_MODULE_SARA_R4
            // Either "+UUMQTTC" or "+UUMQTTCM"
            if (bytes[1] == 'M') {
                UUMQTTCM_urc();
            } else {
                UUMQTTC_urc();
            }
        } else {
            // Probably "+UUMQTTx:"
            // Derive x as an integer, noting
            // that it can be two digits
            if (cellularPort_isdigit(bytes[0])) {
                if (cellularPort_isdigit(bytes[1])) {
                    bytes[2] = 0;
                } else {
                    bytes[1] = 0;
                }
                UUMQTTx_urc(cellularPort_atoi((char *) bytes));
            }
#else
            UUMQTTC_urc();
#endif
        }
    }
}

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: MISC
 * -------------------------------------------------------------- */

// Print the error state of MQTT
static void printErrorCodes()
{
    int32_t err1;
    int32_t err2;

    cellular_ctrl_at_lock();
    cellular_ctrl_at_cmd_start("AT+UMQTTER");
    cellular_ctrl_at_cmd_stop();
    cellular_ctrl_at_resp_start("+UMQTTER:", false);
    err1 = cellular_ctrl_at_read_int();
    err2 = cellular_ctrl_at_read_int();
    cellular_ctrl_at_resp_stop();
    cellular_ctrl_at_unlock();
    cellularPortLog("CELLULAR_MQTT: error codes %d, %d.\n", err1, err2);
}

// Process the response to an AT+MQTT command
// returning 0 for success or negative error code
static CellularMqttErrorCode_t atMqttStopCmdGetRespAndUnlock()
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_AT_ERROR;
    int32_t status = 1;

#ifdef CELLULAR_CFG_MODULE_SARA_R4
    cellular_ctrl_at_cmd_stop();
    cellular_ctrl_at_resp_start("+UMQTT:", false);
    // Skip the first parameter, which is just
    // our UMQTT command number again
    cellular_ctrl_at_skip_param(1);
    status = cellular_ctrl_at_read_int();
    cellular_ctrl_at_resp_stop();
#else
    cellular_ctrl_at_cmd_stop_read_resp();
#endif
    if ((cellular_ctrl_at_unlock_return_error() == 0) &&
        (status == 1)) {
        errorCode = CELLULAR_MQTT_SUCCESS;
    } else {
        printErrorCodes();
    }

    return errorCode;
}

// Convert a message buffer into a hex version of that buffer,
// returning the number of bytes (not hex values) in the hex buffer.
static size_t toHex(char *pHex, const char *pBinary,
                    size_t binaryLength)
{
    size_t hexLength = 0;

    for (size_t x = 0; x < (binaryLength * 2); x++) {
        if ((x & 1) == 0) {
            // Even
            *pHex = gHex[(*pBinary & 0xF0) >> 4];
        } else {
            // Odd
            *pHex = gHex[*pBinary & 0x0F];
            pBinary++;
        }
        pHex++;
        hexLength++;
    }

    return hexLength;
}

#ifdef CELLULAR_CFG_MODULE_SARA_R4
// Set the given gUrcStatus item to "not filled in".
// The switch statement here should match that in UUMQTTx_urc()
static void resetUrcStatusField(int32_t number)
{
    switch (number) {
        case 0: // Local client name
            gUrcStatus.clientName.filledIn = false;
        break;
        case 1: // Local port number
            gUrcStatus.localPortNumber = -1;
        break;
        case 2: // Server name
        case 3: // Server IP address
        case 4: // User name and password
        // Nothing to do, we never read these back
        break;
        // There is no number 5
        case 6: // Will QoS value
        case 7: // Will retention value
        case 8: // Will topic value
        case 9: // The will message
            // TODO
        break;
        case 10: // Inactivity timeout
            gUrcStatus.inactivityTimeoutSeconds = -1;
        break;
        case 11: // TLS secured
            gUrcStatus.secured = -1;
            gUrcStatus.securityProfileId = -1;
        break;
        case 12: // Session retention
            gUrcStatus.sessionRetained = -1;
        break;
        default:
            // Do nothing
        break;
    }
}

// Check if the given gUrcStatus item has been filled in.
// The switch statement here should match that in UUMQTTx_urc()
static bool checkUrcStatusField(int32_t number)
{
    bool filledIn = false;

    switch (number) {
        case 0: // Local client name
            filledIn = gUrcStatus.clientName.filledIn;
        break;
        case 1: // Local port number
            filledIn = (gUrcStatus.localPortNumber >= 0);
        break;
        case 2: // Server name
        case 3: // Server IP address
        case 4: // User name and password
        // Nothing to do, we never read these back
        break;
        // There is no number 5
        case 6: // Will QoS value
        case 7: // Will retention value
        case 8: // Will topic value
        case 9: // The will message
            // TODO
        break;
        case 10: // Inactivity timeout
            filledIn = (gUrcStatus.inactivityTimeoutSeconds >= 0);
        break;
        case 11: // TLS secured
            filledIn = (gUrcStatus.secured >= 0);
        break;
        case 12: // Session retention
            filledIn = (gUrcStatus.sessionRetained >= 0);
        break;
        default:
            // Do nothing
        break;
    }

    return filledIn;
}

// Make AT+UMQTT? happen.
// Note: caller MUST lock the mutex before calling this
// function and unlock it afterwards.
static CellularCtrlErrorCode_t doUmqttQuery(int32_t number)
{
    CellularCtrlErrorCode_t errorCode = CELLULAR_MQTT_INVALID_PARAMETER;
    char buffer[13];  // Enough room for "AT+UMQTT=x?"
    int32_t status;
    int64_t stopTimeMs;

    // The SARA-R4 AT interface gets very peculiar here.
    // Have to send in AT+UMQTT=x? and then wait for a URC
    if (number < 100) {

        // Set the relevant gUrcStatus item to "not filled in"
        resetUrcStatusField(number);

        // Now send the AT command
        errorCode = CELLULAR_MQTT_AT_ERROR;
        cellularPort_snprintf(buffer, sizeof(buffer), "AT+UMQTT=%d?", number);
        cellular_ctrl_at_lock();
        cellular_ctrl_at_cmd_start(buffer);
        cellular_ctrl_at_cmd_stop();
        cellular_ctrl_at_resp_start("+UMQTT:", false);
        // Skip the first parameter, which is just
        // our UMQTT command number again
        cellular_ctrl_at_skip_param(1);
        status = cellular_ctrl_at_read_int();
        cellular_ctrl_at_resp_stop();
        if ((cellular_ctrl_at_unlock_return_error() == 0) &&
            (status == 1)) {
            // Wait for the URC to capture the answer
            // This is just a local thing so set a short timeout
            // and don't bother with keepGoingCallback
            stopTimeMs = cellularPortGetTickTimeMs() + CELLULAR_MQTT_LOCAL_URC_TIMEOUT_MS;
            while ((!checkUrcStatusField(number)) &&
                   (cellularPortGetTickTimeMs() < stopTimeMs)) {
                cellularPortTaskBlock(250);
            }
            if (checkUrcStatusField(number)) {
                errorCode = CELLULAR_MQTT_SUCCESS;
            }
        }
    }

    return errorCode;
}
#endif

// Set MQTT ping or "keep alive" on or off.
static CellularMqttErrorCode_t setKeepAlive(bool onNotOff)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_DEFAULT_ERROR_CODE;
    int32_t status = 1;

    if (gMutex != NULL) {
        // No need to lock the mutex, the
        // mutex protection of the AT interface
        // lock is sufficient
        errorCode = CELLULAR_MQTT_AT_ERROR;
        cellular_ctrl_at_lock();
        cellular_ctrl_at_cmd_start("AT+UMQTTC=");
        // Set ping
        cellular_ctrl_at_write_int(8);
        cellular_ctrl_at_write_int(onNotOff);
#ifdef CELLULAR_CFG_MODULE_SARA_R4
        cellular_ctrl_at_cmd_stop();
        cellular_ctrl_at_resp_start("+UMQTTC:", false);
        // Skip the first parameter, which is just
        // our UMQTTC command number again
        cellular_ctrl_at_skip_param(1);
        status = cellular_ctrl_at_read_int();
        cellular_ctrl_at_resp_stop();
#else
        cellular_ctrl_at_cmd_stop_read_resp();
#endif
        if ((cellular_ctrl_at_unlock_return_error() == 0) &&
            (status == 1)) {
            // This has no URCness to it, that's it
            errorCode = CELLULAR_MQTT_SUCCESS;
            gKeptAlive = onNotOff;
        } else {
            printErrorCodes();
        }
    }

    return errorCode;
}

#ifndef CELLULAR_CFG_MODULE_SARA_R5
// Set MQTT session retention on or off.
static CellularMqttErrorCode_t setSessionRetention(bool onNotOff)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_DEFAULT_ERROR_CODE;

    if (gMutex != NULL) {
        // No need to lock the mutex, the
        // mutex protection of the AT interface
        // lock is sufficient
        errorCode = CELLULAR_MQTT_AT_ERROR;
        cellular_ctrl_at_lock();
        cellular_ctrl_at_cmd_start("AT+UMQTT=");
        // Set client clean session
        cellular_ctrl_at_write_int(12);
        // The value of clean, the opposite of retained
        cellular_ctrl_at_write_int(!onNotOff);
        errorCode = atMqttStopCmdGetRespAndUnlock();
    }

    return errorCode;
}
#endif

// Set security on or off.
static CellularMqttErrorCode_t setSecurity(bool onNotOff,
                                          int32_t securityProfileId)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_DEFAULT_ERROR_CODE;

    if (gMutex != NULL) {
        // No need to lock the mutex, the
        // mutex protection of the AT interface
        // lock is sufficient
        errorCode = CELLULAR_MQTT_AT_ERROR;
        cellular_ctrl_at_lock();
        cellular_ctrl_at_cmd_start("AT+UMQTT=");
        // Set security
        cellular_ctrl_at_write_int(11);
        cellular_ctrl_at_write_int(onNotOff);
        if (onNotOff && (securityProfileId >= 0)) {
            cellular_ctrl_at_write_int(securityProfileId);
        }
        errorCode = atMqttStopCmdGetRespAndUnlock();
    }

    return errorCode;
}

// Connect or disconnect MQTT.
static CellularMqttErrorCode_t connect(bool onNotOff)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_DEFAULT_ERROR_CODE;
    int32_t status = 1;
    int64_t stopTimeMs;

    if (gMutex != NULL) {

        // Lock the mutex as we'll be
        // setting gUrcStatus.connected]
        // and we don't want to trample
        // on anyone else
        CELLULAR_PORT_MUTEX_LOCK(gMutex);

        errorCode = CELLULAR_MQTT_AT_ERROR;
        gUrcStatus.updateFlag = false;
        cellular_ctrl_at_lock();
        // Have seen this take a little while
        cellular_ctrl_at_set_at_timeout(15000, false);
        cellular_ctrl_at_cmd_start("AT+UMQTTC=");
        // Conveniently log-in is command 0 and
        // log out is command 1
        cellular_ctrl_at_write_int(onNotOff);
#ifdef CELLULAR_CFG_MODULE_SARA_R4
        cellular_ctrl_at_cmd_stop();
        cellular_ctrl_at_resp_start("+UMQTTC:", false);
        // Skip the first parameter, which is just
        // our UMQTTC command number again
        cellular_ctrl_at_skip_param(1);
        status = cellular_ctrl_at_read_int();
        cellular_ctrl_at_resp_stop();
#else
        cellular_ctrl_at_cmd_stop_read_resp();
#endif
        cellular_ctrl_at_restore_at_timeout();
        if ((cellular_ctrl_at_unlock_return_error() == 0) &&
            (status == 1)) {
            if (onNotOff) {
                // On all platforms, when logging in,
                // we have to wait for the URC for success
                cellularPortLog("CELLULAR_MQTT: waiting for connection for up to %d"
                                " second(s)...\n",
                                CELLULAR_MQTT_SERVER_RESPONSE_WAIT_SECONDS);
                errorCode = CELLULAR_MQTT_TIMEOUT;
                stopTimeMs = cellularPortGetTickTimeMs() +
                             (CELLULAR_MQTT_SERVER_RESPONSE_WAIT_SECONDS * 1000);
                while (!gUrcStatus.updateFlag &&
                       (cellularPortGetTickTimeMs() < stopTimeMs) &&
                       ((gpKeepGoingCallback == NULL) ||
                        gpKeepGoingCallback())) {
                    cellularPortTaskBlock(1000);
                }
                if (onNotOff == gUrcStatus.connected) {
                    errorCode = CELLULAR_MQTT_SUCCESS;
                } else {
                    printErrorCodes();
                }
            } else {
                // When logging off no need to wait, that's it
                gUrcStatus.connected = false;
                errorCode = CELLULAR_MQTT_SUCCESS;
            }
        } else {
            printErrorCodes();
        }

        CELLULAR_PORT_MUTEX_UNLOCK(gMutex);

    }

    return errorCode;
}

// Determine whether MQTT TLS security is on or off.
// Note: doesn't lock the mutex, the caller has to do that.
static bool isSecured(int32_t *pSecurityProfileId)
{
    bool secured = false;

#ifdef CELLULAR_CFG_MODULE_SARA_R4
    // Lock the mutex as we'll be
    // setting gUrcStatus items
    // and we don't want to trample
    // on anyone else
    CELLULAR_PORT_MUTEX_LOCK(gMutex);

    // Run the query, answers come back in gUrcStatus
    if (doUmqttQuery(11) == 0) {
        // SARA-R4 doesn't report the security status
        // if it is the default of unsecured,
        // so if we got nothing back we are unsecured.
        if (gUrcStatus.secured >= 0) {
            secured = gUrcStatus.secured;
            if (secured && (pSecurityProfileId != NULL)) {
                *pSecurityProfileId = gUrcStatus.securityProfileId;
            }
        }
    }

    CELLULAR_PORT_MUTEX_UNLOCK(gMutex);
#else 
    // No need to lock the mutex, the
    // mutex protection of the AT interface
    // lock is sufficient
    cellular_ctrl_at_lock();
    cellular_ctrl_at_cmd_start("AT+UMQTT=");
    cellular_ctrl_at_write_int(11);
    cellular_ctrl_at_cmd_stop();
    cellular_ctrl_at_resp_start("+UMQTT:", false);
    // Skip the first parameter, which is just
    // our UMQTT command number again
    cellular_ctrl_at_skip_param(1);
    secured = (cellular_ctrl_at_read_int() == 1);
    if (secured && (pSecurityProfileId != NULL)) {
        *pSecurityProfileId = cellular_ctrl_at_read_int();
    }
    cellular_ctrl_at_resp_stop();
    cellular_ctrl_at_unlock();
#endif

    return secured;
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS
 * -------------------------------------------------------------- */

// Initialise the MQTT client.
int32_t cellularMqttInit(const char *pServerNameStr,
                         const char *pUserNameStr,
                         const char *pPasswordStr,
                         const char *pClientNameStr,
                         bool (*pKeepGoingCallback)(void))
{
    CellularCtrlErrorCode_t errorCode = CELLULAR_MQTT_NOT_SUPPORTED;

#if CELLULAR_MQTT_IS_SUPPORTED
    CellularSockAddress_t address;
    char *pAddress;
    char *pTmp;
    int32_t port;
#ifdef CELLULAR_CFG_MODULE_SARA_R4
    int32_t status = 1;
#endif
    bool keepGoing = true;

    errorCode = CELLULAR_MQTT_SUCCESS;
    if (gMutex == NULL) {
        errorCode = CELLULAR_MQTT_BAD_ADDRESS;
        // Check parameters, only pServerNameStr has to be present
        if ((pServerNameStr != NULL) &&
            (cellularPort_strlen(pServerNameStr) <= CELLULAR_MQTT_SERVER_ADDRESS_STRING_MAX_LENGTH_BYTES)) {
            // Deal with the server name string
            // Allocate space to fiddle with the
            // server address, +1 for terminator
            errorCode = CELLULAR_MQTT_NO_MEMORY;
            pAddress = (char *) pCellularPort_malloc(CELLULAR_MQTT_SERVER_ADDRESS_STRING_MAX_LENGTH_BYTES + 1);
            if (pAddress != NULL) {
                errorCode = CELLULAR_MQTT_AT_ERROR;
                // Determine if the server name given
                // is an IP address or a domain name
                // by processing it as an IP address
                pCellularPort_memset(&address, 0, sizeof(address));
                if (cellularSockStringToAddress(pServerNameStr,
                                                &address) == 0) {
                    // We have an IP address
                    // Convert the bit that isn't a port
                    // number back into a string
                    if (cellularSockIpAddressToString(&(address.ipAddress),
                                                      pAddress,
                                                      CELLULAR_MQTT_SERVER_ADDRESS_STRING_MAX_LENGTH_BYTES) == 0) {
                        cellular_ctrl_at_lock();
                        cellular_ctrl_at_cmd_start("AT+UMQTT=");
                        // Set the server IP address
                        cellular_ctrl_at_write_int(3);
                        // The address
                        cellular_ctrl_at_write_string(pAddress, true);
                        // If there's was a port number, write
                        // that also
                        if (address.port > 0) {
                            cellular_ctrl_at_write_int(address.port);
                        }
                        keepGoing = (atMqttStopCmdGetRespAndUnlock() == 0);
                    }
                } else {
                    // We must have a domain name,
                    // make a copy of it as we need to
                    // manipulate it
                    pCellularPort_strcpy(pAddress, pServerNameStr);
                    // Grab any port number off the end
                    // and then remove it from the string
                    port = cellularSockDomainGetPort(pAddress);
                    pTmp = pCellularSockDomainRemovePort(pAddress);
                    cellular_ctrl_at_lock();
                    cellular_ctrl_at_cmd_start("AT+UMQTT=");
                    // Set the server name
                    cellular_ctrl_at_write_int(2);
                    // The address
                    cellular_ctrl_at_write_string(pTmp, true);
                    // If there's was a port number, write
                    // that also
                    if (port >= 0) {
                        cellular_ctrl_at_write_int(port);
                    }
                    keepGoing = (atMqttStopCmdGetRespAndUnlock() == 0);
                }

                // Free memory
                cellularPort_free(pAddress);

                // Now deal with the credentials
                if (keepGoing && (pUserNameStr != NULL)) {
                    cellular_ctrl_at_lock();
                    cellular_ctrl_at_cmd_start("AT+UMQTT=");
                    // Set credentials
                    cellular_ctrl_at_write_int(4);
                    // The user name
                    cellular_ctrl_at_write_string(pUserNameStr, true);
                    // If there was a password, write that also
                    if (pPasswordStr != NULL) {
                        cellular_ctrl_at_write_string(pPasswordStr, true);
                    }
                    keepGoing = (atMqttStopCmdGetRespAndUnlock() == 0);
                }

                // Finally deal with the local client name
                if (keepGoing && (pClientNameStr != NULL)) {
                    cellular_ctrl_at_lock();
                    cellular_ctrl_at_cmd_start("AT+UMQTT=");
                    // Set client ID
                    cellular_ctrl_at_write_int(0);
                    // The ID
                    cellular_ctrl_at_write_string(pClientNameStr, true);
                    keepGoing = (atMqttStopCmdGetRespAndUnlock() == 0);
                }

#ifdef CELLULAR_CFG_MODULE_SARA_R4
                if (keepGoing) {
                    // If this is SARA-R4, select verbose message reads
                    cellular_ctrl_at_lock();
                    cellular_ctrl_at_cmd_start("AT+UMQTTC=");
                    // Message read format
                    cellular_ctrl_at_write_int(7);
                    // Format: verbose
                    cellular_ctrl_at_write_int(2);
                    cellular_ctrl_at_cmd_stop();
                    cellular_ctrl_at_resp_start("+UMQTTC:", false);
                    // Skip the first parameter, which is just
                    // our UMQTTC command number again
                    cellular_ctrl_at_skip_param(1);
                    status = cellular_ctrl_at_read_int();
                    cellular_ctrl_at_resp_stop();
                    keepGoing = ((cellular_ctrl_at_unlock_return_error() == 0) &&
                                 (status == 1));
                }
#endif
                // Almost done
                if (keepGoing) {
                    // Finally, create the mutex that we use for re-entrancy protection
                    if (cellularPortMutexCreate(&gMutex) == 0) {
                        pCellularPort_memset((void *) &gUrcStatus, 0, sizeof(gUrcStatus));
                        cellular_ctrl_at_set_urc_handler("+UUMQTT", UUMQTT_urc, NULL);
                        gpKeepGoingCallback = pKeepGoingCallback;
                        gpMessageIndicationCallback = NULL;
                        gpMessageIndicationCallbackParam = NULL;
                        gKeptAlive = false;
                        errorCode = CELLULAR_MQTT_SUCCESS;
                    }
                } else {
                    printErrorCodes();
                }
            }
        }
    }
#endif

    return (int32_t) errorCode;
}

// Shut-down the MQTT client.
void cellularMqttDeinit()
{
    cellular_ctrl_at_remove_urc_handler("+UUMQTT");
    cellularPortMutexDelete(gMutex);
    gMutex = NULL;
}

// Get the current MQTT client name.
int32_t cellularMqttGetClientName(char *pClientNameStr,
                                  int32_t sizeBytes)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_DEFAULT_ERROR_CODE;
#ifndef CELLULAR_CFG_MODULE_SARA_R4
    int32_t bytesRead;
#endif

    if (gMutex != NULL) {
        errorCode = CELLULAR_MQTT_INVALID_PARAMETER;
        if (pClientNameStr != NULL) {
#ifdef CELLULAR_CFG_MODULE_SARA_R4
            // Lock the mutex as we'll be
            // setting gUrcStatus items
            // and we don't want to trample
            // on anyone else
            CELLULAR_PORT_MUTEX_LOCK(gMutex);

            gUrcStatus.clientName.pContents = pClientNameStr;
            gUrcStatus.clientName.sizeBytes = sizeBytes;
            // This will fill in the string
            errorCode = doUmqttQuery(0);
            // For safety, not to leave pointers unattended
            gUrcStatus.clientName.pContents = NULL;
            gUrcStatus.clientName.sizeBytes = 0;

            CELLULAR_PORT_MUTEX_UNLOCK(gMutex);
#else
            // No need to lock the mutex, the
            // mutex protection of the AT interface
            // lock is sufficient
            errorCode = CELLULAR_MQTT_AT_ERROR;
            cellular_ctrl_at_lock();
            cellular_ctrl_at_cmd_start("AT+UMQTT=0");
            cellular_ctrl_at_cmd_stop();
            cellular_ctrl_at_resp_start("+UMQTT:", false);
            // Skip the first parameter, which is just
            // our UMQTT command number again
            cellular_ctrl_at_skip_param(1);
            bytesRead = cellular_ctrl_at_read_string(pClientNameStr,
                                                     sizeBytes,
                                                     false);
            cellular_ctrl_at_resp_stop();
            if ((cellular_ctrl_at_unlock_return_error() == 0) &&
                (bytesRead >= 0)) {
                errorCode = CELLULAR_MQTT_SUCCESS;
            }
#endif
        }
    }

    return (int32_t) errorCode;
}

// Set the local port to use for the MQTT client.
int32_t cellularMqttSetLocalPort(uint16_t port)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_DEFAULT_ERROR_CODE;

    if (gMutex != NULL) {
#ifdef CELLULAR_CFG_MODULE_SARA_R5
        errorCode = CELLULAR_MQTT_NOT_SUPPORTED;
#else
        // No need to lock the mutex, the
        // mutex protection of the AT interface
        // lock is sufficient
        cellular_ctrl_at_lock();
        cellular_ctrl_at_cmd_start("AT+UMQTT=");
        // Set the local port
        cellular_ctrl_at_write_int(1);
        cellular_ctrl_at_write_int(port);
        errorCode = atMqttStopCmdGetRespAndUnlock();
#endif
    }

    return (int32_t) errorCode;
}

// Get the local port used by the MQTT client.
int32_t cellularMqttGetLocalPort()
{
    CellularMqttErrorCode_t errorCodeOrPort = CELLULAR_MQTT_DEFAULT_ERROR_CODE;
#if !defined(CELLULAR_CFG_MODULE_SARA_R4) && !defined(CELLULAR_CFG_MODULE_SARA_R5)
    int32_t x;
#endif

    if (gMutex != NULL) {
#ifdef CELLULAR_CFG_MODULE_SARA_R4
        // Lock the mutex as we'll be
        // setting gUrcStatus items
        // and we don't want to trample
        // on anyone else
        CELLULAR_PORT_MUTEX_LOCK(gMutex);
        errorCodeOrPort = doUmqttQuery(1);
        if ((errorCodeOrPort == 0) &&
            (gUrcStatus.localPortNumber >= 0)) {
            errorCodeOrPort = gUrcStatus.localPortNumber;
        }

        CELLULAR_PORT_MUTEX_UNLOCK(gMutex);

        if (errorCodeOrPort < 0) {
            // The module doesn't respond with a port number if the
            // port number is just the default one.  Determine if
            // we are secured so that we can send back the correct
            // default port number
            errorCodeOrPort = CELLULAR_MQTT_SERVER_PORT_UNSECURE;
            if (isSecured(NULL)) {
                errorCodeOrPort = CELLULAR_MQTT_SERVER_PORT_SECURE;
            }
        }
#else
# ifdef CELLULAR_CFG_MODULE_SARA_R5
        // SARA-R5 doesn't support reading
        // the local port number, just return
        // the correct one depending on
        // whether security is on or not
        errorCodeOrPort = CELLULAR_MQTT_SERVER_PORT_UNSECURE;
        if (isSecured(NULL)) {
            errorCodeOrPort = CELLULAR_MQTT_SERVER_PORT_SECURE;
        }
# else
        // No need to lock the mutex, the
        // mutex protection of the AT interface
        // lock is sufficient
        errorCodeOrPort = CELLULAR_MQTT_AT_ERROR;
        cellular_ctrl_at_lock();
        cellular_ctrl_at_cmd_start("AT+UMQTT=");
        cellular_ctrl_at_write_int(1);
        cellular_ctrl_at_cmd_stop();
        cellular_ctrl_at_resp_start("+UMQTT:", false);
        // Skip the first parameter, which is just
        // our UMQTT command number again
        cellular_ctrl_at_skip_param(1);
        x = cellular_ctrl_at_read_int();
        cellular_ctrl_at_resp_stop();
        if ((cellular_ctrl_at_unlock_return_error() == 0) &&
            (x >= 0)) {
            errorCodeOrPort = x;
        }
# endif
#endif
    }

    return (int32_t) errorCodeOrPort;
}

// Set the inactivity timeout used by the MQTT client.
int32_t cellularMqttSetInactivityTimeout(int32_t seconds)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_DEFAULT_ERROR_CODE;

    if (gMutex != NULL) {
        // No need to lock the mutex, the
        // mutex protection of the AT interface
        // lock is sufficient
        cellular_ctrl_at_lock();
        cellular_ctrl_at_cmd_start("AT+UMQTT=");
        // Set the inactivity timeout
        cellular_ctrl_at_write_int(10);
        cellular_ctrl_at_write_int(seconds);
        errorCode = atMqttStopCmdGetRespAndUnlock();
    }

    return (int32_t) errorCode;
}

// Get the inactivity timeout used by the MQTT client.
int32_t cellularMqttGetInactivityTimeout()
{
    CellularMqttErrorCode_t errorCodeOrTimeout = CELLULAR_MQTT_DEFAULT_ERROR_CODE;
#ifndef CELLULAR_CFG_MODULE_SARA_R4
    int32_t x;
#endif

    if (gMutex != NULL) {
#ifdef CELLULAR_CFG_MODULE_SARA_R4
        // Lock the mutex as we'll be
        // setting gUrcStatus items
        // and we don't want to trample
        // on anyone else
        CELLULAR_PORT_MUTEX_LOCK(gMutex);
        errorCodeOrTimeout = doUmqttQuery(10);
        if ((errorCodeOrTimeout == 0) &&
            (gUrcStatus.inactivityTimeoutSeconds >= 0)) {
            errorCodeOrTimeout = gUrcStatus.inactivityTimeoutSeconds;
        }

        CELLULAR_PORT_MUTEX_UNLOCK(gMutex);
#else
        // No need to lock the mutex, the
        // mutex protection of the AT interface
        // lock is sufficient
        errorCodeOrTimeout = CELLULAR_MQTT_AT_ERROR;
        cellular_ctrl_at_lock();
        cellular_ctrl_at_cmd_start("AT+UMQTT=");
        cellular_ctrl_at_write_int(10);
        cellular_ctrl_at_cmd_stop();
        cellular_ctrl_at_resp_start("+UMQTT:", false);
        // Skip the first parameter, which is just
        // our UMQTT command number again
        cellular_ctrl_at_skip_param(1);
        x = cellular_ctrl_at_read_int();
        cellular_ctrl_at_resp_stop();
        if ((cellular_ctrl_at_unlock_return_error() == 0) &&
            (x >= 0)) {
            errorCodeOrTimeout = x;
        }
#endif
    }

    return (int32_t) errorCodeOrTimeout;
}

// Switch MQTT ping or "keep alive" on.
int32_t cellularMqttSetKeepAliveOn()
{
    return (int32_t) setKeepAlive(true);
}

// Switch MQTT ping or "keep alive" off.
int32_t cellularMqttSetKeepAliveOff()
{
    return (int32_t) setKeepAlive(false);
}

// Determine whether MQTT ping or "keep alive" is on.
bool cellularMqttIsKeptAlive()
{
    return gKeptAlive;
}

// Switch session retention on.
int32_t cellularMqttSetSessionRetentionOn()
{
#ifdef CELLULAR_CFG_MODULE_SARA_R5
    return CELLULAR_MQTT_NOT_SUPPORTED;
#else
    return (int32_t) setSessionRetention(true);
#endif
}

// Switch MQTT session retention off.
int32_t cellularMqttSetSessionRetentionOff()
{
#ifdef CELLULAR_CFG_MODULE_SARA_R5
    return CELLULAR_MQTT_NOT_SUPPORTED;
#else
    return (int32_t) setSessionRetention(false);
#endif
}

// Determine whether MQTT session retention is on.
bool cellularMqttIsSessionRetained()
{
    bool sessionRetained = false;

    if (gMutex != NULL) {
#ifdef CELLULAR_CFG_MODULE_SARA_R4
        // Lock the mutex as we'll be
        // setting gUrcStatus items
        // and we don't want to trample
        // on anyone else
        CELLULAR_PORT_MUTEX_LOCK(gMutex);

        // Run the query, answers come back in gUrcStatus
        if ((doUmqttQuery(12) == 0) &&
             gUrcStatus.sessionRetained >= 0) {
            sessionRetained = gUrcStatus.sessionRetained;
        }

        CELLULAR_PORT_MUTEX_UNLOCK(gMutex);
#else
    // SARA-R5 doesn't support session retention
# ifndef CELLULAR_CFG_MODULE_SARA_R5
        // No need to lock the mutex, the
        // mutex protection of the AT interface
        // lock is sufficient
        cellular_ctrl_at_lock();
        cellular_ctrl_at_cmd_start("AT+UMQTT=");
        cellular_ctrl_at_write_int(12);
        cellular_ctrl_at_cmd_stop();
        cellular_ctrl_at_resp_start("+UMQTT:", false);
        // Skip the first parameter, which is just
        // our UMQTT command number again
        cellular_ctrl_at_skip_param(1);
        // Note that what is reported is "cleaned",
        // hence the inversion here
        sessionRetained = (cellular_ctrl_at_read_int() == 0);
        cellular_ctrl_at_resp_stop();
        cellular_ctrl_at_unlock();
# endif
#endif
    }

    return sessionRetained;
}

// Switch MQTT TLS security on.
int32_t cellularMqttSetSecurityOn(int32_t securityProfileId)
{
    return (int32_t) setSecurity(true, securityProfileId);
}

// Switch MQTT TLS security off.
int32_t cellularMqttSetSecurityOff()
{
    return (int32_t) setSecurity(false, 0);
}

// Determine whether MQTT TLS security is on or off.
bool cellularMqttIsSecured(int32_t *pSecurityProfileId)
{
    bool secured = false;

    if (gMutex != NULL) {
        secured = isSecured(pSecurityProfileId);
    }

    return secured;
}

// Set the MQTT "will" message.
int32_t cellularMqttSetWill(CellularMqttQos_t qos,
                            bool retention,
                            const char *pTopicNameStr,
                            const char *pMessage,
                            int32_t messageSizeBytes)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Get the MQTT "will" message.
int32_t cellularMqttGetWill(CellularMqttQos_t *pQos,
                            bool *pRetention,
                            char *pTopicNameStr,
                            int32_t topicNameSizeBytes,
                            char *pMessage,
                            int32_t *pMessageSizeBytes)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Start an MQTT session.
int32_t cellularMqttConnect()
{
    // Deliberately don't check if we're connected
    // already: want to tickle it, have an effect,
    // just in case we're locally out of sync
    // with the MQTT stack in the module.
    return (int32_t) connect(true);
}

// Stop an MQTT session.
int32_t cellularMqttDisconnect()
{
    return (int32_t) connect(false);
}

// Determine whether an MQTT session is active or not.
bool cellularMqttIsConnected()
{
    // There is no way to ask the module about this,
    // just return our last status
    return gUrcStatus.connected;
}

// Publish an MQTT message.
int32_t cellularMqttPublish(CellularMqttQos_t qos,
                            bool retention,
                            const char *pTopicNameStr,
                            const char *pMessage,
                            int32_t messageSizeBytes)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_DEFAULT_ERROR_CODE;
    char *pHexMessage;
    int32_t status = 1;
#ifndef CELLULAR_CFG_MODULE_SARA_R4
    int64_t stopTimeMs;
#endif

    if (gMutex != NULL) {
        errorCode = CELLULAR_MQTT_INVALID_PARAMETER;
        if ((qos >= 0) &&
            (qos < MAX_NUM_CELLULAR_MQTT_QOS) &&
            (pTopicNameStr != NULL) &&
            (pMessage != NULL) &&
            (messageSizeBytes <= CELLULAR_MQTT_PUBLISH_MAX_LENGTH_BYTES)) {
            // Allocate memory to store the hex
            // version of the message
            errorCode = CELLULAR_MQTT_NO_MEMORY;
            pHexMessage = (char *) pCellularPort_malloc((messageSizeBytes * 2) + 1);
            if (pHexMessage != NULL) {
                // Convert to hex
                toHex(pHexMessage, pMessage, messageSizeBytes);
                // Add a terminator to make it a string
                *(pHexMessage + (messageSizeBytes * 2)) = '\0';

                // Lock the mutex as we'll be
                // setting gUrcStatus.publishSuccess
                // and we don't want to trample
                // on anyone else
                CELLULAR_PORT_MUTEX_LOCK(gMutex);

                errorCode = CELLULAR_MQTT_AT_ERROR;
                cellular_ctrl_at_lock();
                gUrcStatus.updateFlag = false;
                gUrcStatus.publishSuccess = false;
                cellular_ctrl_at_cmd_start("AT+UMQTTC=");
                // Publish message
                cellular_ctrl_at_write_int(2);
                // QoS
                cellular_ctrl_at_write_int(qos);
                // Retention
                cellular_ctrl_at_write_int(retention);
                // Hex mode
                cellular_ctrl_at_write_int(1);
                // Topic
                cellular_ctrl_at_write_string(pTopicNameStr, true);
                // Hex message
                cellular_ctrl_at_write_string(pHexMessage, true);
#ifdef CELLULAR_CFG_MODULE_SARA_R4
                cellular_ctrl_at_cmd_stop();
                cellular_ctrl_at_resp_start("+UMQTTC:", false);
                // Skip the first parameter, which is just
                // our UMQTTC command number again
                cellular_ctrl_at_skip_param(1);
                status = cellular_ctrl_at_read_int();
                cellular_ctrl_at_resp_stop();
#else
                cellular_ctrl_at_cmd_stop_read_resp();
#endif
                if ((cellular_ctrl_at_unlock_return_error() == 0) &&
                    (status == 1)) {
#ifdef CELLULAR_CFG_MODULE_SARA_R4
                    // For SARA-R4, that's it
                    errorCode = CELLULAR_MQTT_SUCCESS;
#else
                    // Wait for a URC to say that the publish
                    // has succeeded
                    errorCode = CELLULAR_MQTT_TIMEOUT;
                    stopTimeMs = cellularPortGetTickTimeMs() + (CELLULAR_MQTT_SERVER_RESPONSE_WAIT_SECONDS * 1000);
                    while (!gUrcStatus.updateFlag &&
                           (cellularPortGetTickTimeMs() < stopTimeMs) &&
                           ((gpKeepGoingCallback == NULL) ||
                            gpKeepGoingCallback())) {
                        cellularPortTaskBlock(1000);
                    }
                    if (gUrcStatus.publishSuccess) {
                        errorCode = CELLULAR_MQTT_SUCCESS;
                    } else {
                        printErrorCodes();
                    }
#endif
                } else {
                    printErrorCodes();
                }

                CELLULAR_PORT_MUTEX_UNLOCK(gMutex);

                // Free memory
                cellularPort_free(pHexMessage);
            }
        }
    }

    return (int32_t) errorCode;
}

// Subscribe to an MQTT topic.
int32_t cellularMqttSubscribe(CellularMqttQos_t maxQos,
                             const char *pTopicFilterStr)
{
    CellularMqttErrorCode_t errorCodeOrQos = CELLULAR_MQTT_DEFAULT_ERROR_CODE;
    int32_t status = 1;
    int64_t stopTimeMs;

    if (gMutex != NULL) {
        errorCodeOrQos = CELLULAR_MQTT_INVALID_PARAMETER;
        if ((maxQos >= 0) &&
            (maxQos < MAX_NUM_CELLULAR_MQTT_QOS) &&
            (pTopicFilterStr != NULL)) {
            // Lock the mutex as we'll be
            // setting gUrcStatus.subscribeSuccess
            // and we don't want to trample
            // on anyone else
            CELLULAR_PORT_MUTEX_LOCK(gMutex);

            errorCodeOrQos = CELLULAR_MQTT_AT_ERROR;
            cellular_ctrl_at_lock();
            gUrcStatus.updateFlag = false;
            gUrcStatus.subscribeSuccess = false;
            cellular_ctrl_at_cmd_start("AT+UMQTTC=");
            // Subscribe to a topic
            cellular_ctrl_at_write_int(4);
            // Max QoS
            cellular_ctrl_at_write_int(maxQos);
            // Topic
            cellular_ctrl_at_write_string(pTopicFilterStr, true);
#ifdef CELLULAR_CFG_MODULE_SARA_R4
            cellular_ctrl_at_cmd_stop();
            cellular_ctrl_at_resp_start("+UMQTTC:", false);
            // Skip the first parameter, which is just
            // our UMQTTC command number again
            cellular_ctrl_at_skip_param(1);
            status = cellular_ctrl_at_read_int();
            cellular_ctrl_at_resp_stop();
#else
            cellular_ctrl_at_cmd_stop_read_resp();
#endif
            if ((cellular_ctrl_at_unlock_return_error() == 0) &&
                (status == 1)) {
                // On all platforms need to wait for a URC to
                // say that the subscribe has succeeded
                errorCodeOrQos = CELLULAR_MQTT_TIMEOUT;
                stopTimeMs = cellularPortGetTickTimeMs() + (CELLULAR_MQTT_SERVER_RESPONSE_WAIT_SECONDS * 1000);
                while (!gUrcStatus.updateFlag &&
                       (cellularPortGetTickTimeMs() < stopTimeMs) &&
                       ((gpKeepGoingCallback == NULL) ||
                        gpKeepGoingCallback())) {
                    cellularPortTaskBlock(1000);
                }
                if (gUrcStatus.subscribeSuccess) {
                    errorCodeOrQos = gUrcStatus.subscribeQoS;
                } else {
                    printErrorCodes();
                }
            } else {
                printErrorCodes();
            }

            CELLULAR_PORT_MUTEX_UNLOCK(gMutex);
        }
    }

    return (int32_t) errorCodeOrQos;
}

// Unsubscribe from an MQTT topic.
int32_t cellularMqttUnsubscribe(const char *pTopicFilterStr)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_DEFAULT_ERROR_CODE;
    int32_t status = 1;
#ifndef CELLULAR_CFG_MODULE_SARA_R4
    int64_t stopTimeMs;
#endif

    if (gMutex != NULL) {
        errorCode = CELLULAR_MQTT_INVALID_PARAMETER;
        if (pTopicFilterStr != NULL) {
            // Lock the mutex as we'll be
            // setting gUrcStatus.unsubscribeSuccess
            // and we don't want to trample
            // on anyone else
            CELLULAR_PORT_MUTEX_LOCK(gMutex);

            errorCode = CELLULAR_MQTT_AT_ERROR;
            cellular_ctrl_at_lock();
            gUrcStatus.updateFlag = false;
            gUrcStatus.unsubscribeSuccess = false;
            cellular_ctrl_at_cmd_start("AT+UMQTTC=");
            // Unsubscribe from a topic
            cellular_ctrl_at_write_int(5);
            // Topic
            cellular_ctrl_at_write_string(pTopicFilterStr, true);
#ifdef CELLULAR_CFG_MODULE_SARA_R4
            cellular_ctrl_at_cmd_stop();
            cellular_ctrl_at_resp_start("+UMQTTC:", false);
            // Skip the first parameter, which is just
            // our UMQTTC command number again
            cellular_ctrl_at_skip_param(1);
            status = cellular_ctrl_at_read_int();
            cellular_ctrl_at_resp_stop();
#else
            cellular_ctrl_at_cmd_stop_read_resp();
#endif
            if ((cellular_ctrl_at_unlock_return_error() == 0) &&
                (status == 1)) {
#ifdef CELLULAR_CFG_MODULE_SARA_R4
                // For SARA-R4, that's it
                errorCode = CELLULAR_MQTT_SUCCESS;
#else
                // Wait for a URC to say that the unsubscribe
                // has succeeded
                errorCode = CELLULAR_MQTT_TIMEOUT;
                stopTimeMs = cellularPortGetTickTimeMs() + (CELLULAR_MQTT_SERVER_RESPONSE_WAIT_SECONDS * 1000);
                while (!gUrcStatus.updateFlag &&
                       (cellularPortGetTickTimeMs() < stopTimeMs) &&
                       ((gpKeepGoingCallback == NULL) ||
                        gpKeepGoingCallback())) {
                    cellularPortTaskBlock(1000);
                }
                if (gUrcStatus.unsubscribeSuccess) {
                    errorCode = CELLULAR_MQTT_SUCCESS;
                } else {
                    printErrorCodes();
                }
#endif
            } else {
                printErrorCodes();
            }

            CELLULAR_PORT_MUTEX_UNLOCK(gMutex);
        }
    }

    return (int32_t) errorCode;
}

// Set a new messages callback.
int32_t cellularMqttSetMessageIndicationCallback(void (*pCallback)(int32_t, void *),
                                                 void *pCallbackParam)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_DEFAULT_ERROR_CODE;

    if (gMutex != NULL) {
        // Lock the mutex as we're about
        // to perform a non-atomic operation
        CELLULAR_PORT_MUTEX_LOCK(gMutex);

        gpMessageIndicationCallback = pCallback;
        gpMessageIndicationCallbackParam = pCallbackParam;
        errorCode = CELLULAR_MQTT_SUCCESS;

        CELLULAR_PORT_MUTEX_UNLOCK(gMutex);
    }

    return (int32_t) errorCode;
}

// Get the number of unread messages.
int32_t cellularMqttGetUnread()
{
    return gUrcStatus.numUnreadMessages;
}

// Read an MQTT message.
// Note: message reading is completely different
// between SARA-R4 and SARA-R5
int32_t cellularMqttMessageRead(char *pTopicNameStr,
                                int32_t topicNameSizeBytes,
                                char *pMessage,
                                int32_t *pMessageSizeBytes,
                                CellularMqttQos_t *pQos)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_DEFAULT_ERROR_CODE;
    char *pMessageBuffer;
    int32_t status = 1;
#ifdef CELLULAR_CFG_MODULE_SARA_R4
    int64_t stopTimeMs;
#else
    CellularMqttQos_t qos;
    int32_t topicNameBytesRead;
    int32_t messageBytesAvailable;
    int32_t messageBytesRead;
    uint8_t quoteMark;
#endif

    if (gMutex != NULL) {
        errorCode = CELLULAR_MQTT_INVALID_PARAMETER;
        if ((pTopicNameStr != NULL) &&
            (pMessage != NULL)) {
            // We read the message into a temporary
            // buffer as it may be larger than
            // the caller has room for and we
            // have to read it all in somehow
            errorCode = CELLULAR_MQTT_NO_MEMORY;
            pMessageBuffer = (char *) pCellularPort_malloc(CELLULAR_MQTT_READ_MESSAGE_MAX_LENGTH_BYTES);
            if (pMessageBuffer != NULL) {
                // Lock the mutex as we need to
                // be sure that the URC
                // we get back was triggered
                // by us and we might in some cases
                // be going to use gUrcMessage
                CELLULAR_PORT_MUTEX_LOCK(gMutex);

                errorCode = CELLULAR_MQTT_AT_ERROR;
                cellular_ctrl_at_lock();
                cellular_ctrl_at_cmd_start("AT+UMQTTC=");
                // Read a message
                cellular_ctrl_at_write_int(6);
#ifdef CELLULAR_CFG_MODULE_SARA_R4
                // For SARA-R4 we get a standard
                // indication of success here
                // then we need to wait for a
                // URC to receive the message
                gUrcMessage.messageRead = false;
                gUrcMessage.pTopicNameStr = pTopicNameStr;
                gUrcMessage.topicNameSizeBytes = topicNameSizeBytes;
                gUrcMessage.pMessage = pMessageBuffer;
                gUrcMessage.messageSizeBytes = CELLULAR_MQTT_READ_MESSAGE_MAX_LENGTH_BYTES;
                cellular_ctrl_at_cmd_stop();
                cellular_ctrl_at_resp_start("+UMQTTC:", false);
                // Skip the first parameter, which is just
                // our UMQTTC command number again
                cellular_ctrl_at_skip_param(1);
                status = cellular_ctrl_at_read_int();
                cellular_ctrl_at_resp_stop();
                if ((cellular_ctrl_at_unlock_return_error() == 0) &&
                    (status == 1)) {
                    // Wait for a URC containing the message
                    errorCode = CELLULAR_MQTT_TIMEOUT;
                    stopTimeMs = cellularPortGetTickTimeMs() + (CELLULAR_MQTT_SERVER_RESPONSE_WAIT_SECONDS * 1000);
                    while ((!gUrcMessage.messageRead) &&
                           (cellularPortGetTickTimeMs() < stopTimeMs) &&
                           ((gpKeepGoingCallback == NULL) ||
                            gpKeepGoingCallback())) {
                        cellularPortTaskBlock(1000);
                    }
                    if (gUrcMessage.messageRead) {
                        if (gUrcStatus.numUnreadMessages > 0) {
                            gUrcStatus.numUnreadMessages--;
                        }
                        // pTopicNameStr was filled in directly,
                        // now fill in the passed-in parameters
                        // that we haven't already done
                        if (gUrcMessage.messageSizeBytes > *pMessageSizeBytes) {
                            gUrcMessage.messageSizeBytes = *pMessageSizeBytes;
                        }
                        pCellularPort_memcpy(pMessage,
                                             pMessageBuffer,
                                             gUrcMessage.messageSizeBytes);
                        *pMessageSizeBytes = gUrcMessage.messageSizeBytes;
                        if (pQos != NULL) {
                            *pQos = gUrcMessage.qos;
                        }
                        errorCode = CELLULAR_MQTT_SUCCESS;
                    }
                } else {
                    printErrorCodes();
                }
#else
                // We want just the one message
                cellular_ctrl_at_write_int(1);
                cellular_ctrl_at_cmd_stop();
                cellular_ctrl_at_resp_start("+UMQTTC:", false);
                // The message now arrives directly
                // Skip the first parameter, which is just
                // our UMQTTC command number again
                cellular_ctrl_at_skip_param(1);
                // Next comes the QoS
                qos = cellular_ctrl_at_read_int();
                // Then we can skip the length of
                // the topic and message added together,
                // and the length of the topic
                // message (which is always an
                // ASCII string so we can read it as such)
                cellular_ctrl_at_skip_param(2);
                // Now read the topic name string
                topicNameBytesRead = cellular_ctrl_at_read_string(pTopicNameStr,
                                                                  topicNameSizeBytes,
                                                                  false);
                // Read the number of message bytes to follow
                messageBytesAvailable = cellular_ctrl_at_read_int();
                if (messageBytesAvailable > CELLULAR_MQTT_READ_MESSAGE_MAX_LENGTH_BYTES) {
                    messageBytesAvailable = CELLULAR_MQTT_READ_MESSAGE_MAX_LENGTH_BYTES;
                }
                // Now read the exact length of message
                // bytes, being careful to not look for
                // delimiters or the like as this can be
                // a binary message
                cellular_ctrl_at_set_delimiter(0);
                cellular_ctrl_at_set_stop_tag(NULL);
                // Get the leading quote mark out of the way
                cellular_ctrl_at_read_bytes(&quoteMark, 1);
                // Now read the actual message data
                messageBytesRead = cellular_ctrl_at_read_bytes((uint8_t *) pMessageBuffer,
                                                               messageBytesAvailable);
                cellular_ctrl_at_resp_stop();
                cellular_ctrl_at_set_default_delimiter();
                if ((cellular_ctrl_at_unlock_return_error() == 0) &&
                    (status == 1)) {
                    // Now have all the bits, check them
                    if ((topicNameBytesRead >= 0) &&
                        (qos >= 0) &&
                        (qos < MAX_NUM_CELLULAR_MQTT_QOS) &&
                        (messageBytesRead >= 0)) {
                        // Good.  pTopicNameStr was filled in
                        // above, now fill in the other
                        // passed-in parameters
                        if (messageBytesRead > *pMessageSizeBytes) {
                            messageBytesRead = *pMessageSizeBytes;
                        }
                        pCellularPort_memcpy(pMessage,
                                             pMessageBuffer,
                                             messageBytesRead);
                        *pMessageSizeBytes = messageBytesRead;
                        if (pQos != NULL) {
                            *pQos = qos;
                        }
                        if (gUrcStatus.numUnreadMessages > 0) {
                            gUrcStatus.numUnreadMessages--;
                        }
                        errorCode = CELLULAR_MQTT_SUCCESS;
                    }
                } else {
                    printErrorCodes();
                }
#endif

                CELLULAR_PORT_MUTEX_UNLOCK(gMutex);

                // Free memory
                cellularPort_free(pMessageBuffer);
            }
        }
    }

    return (int32_t) errorCode;
}

// Get the last module-specific MQTT error code.
int32_t cellularMqttGetLastErrorCode()
{
    int32_t errorCode = 0;
    int32_t x;

    if (gMutex != NULL) {
        cellular_ctrl_at_lock();
        cellular_ctrl_at_cmd_start("AT+UMQTTER");
        cellular_ctrl_at_cmd_stop();
        cellular_ctrl_at_resp_start("+UMQTTER:", false);
        // Skip the first error code, which is a generic thing
        cellular_ctrl_at_skip_param(1);
        x = cellular_ctrl_at_read_int();
        cellular_ctrl_at_resp_stop();
        if (cellular_ctrl_at_unlock_return_error() == 0) {
            errorCode = x;
        }
    }

    return errorCode;
}

// End of file
