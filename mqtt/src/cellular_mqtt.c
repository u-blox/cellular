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

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/** Struct to hold all the things an MQTT URC might tell us.
 */
typedef struct {
    bool connected;
    bool publishSuccess;
    bool subscribeSuccess;
    CellularMqttQos_t subscribeQoS;
    bool unsubscribeSuccess;
    size_t numUnreadMessages;
    bool memoryFull;
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

/** Keep track of whether we've been initialised or not.
 */
static bool gInitialised = false;

/** Callback to be called while in a function which
 * may have to wait for a server's response.
 */
static bool (*gpKeepGoingCallback)(void) = NULL;

/** Mutex protection.
 */
static CellularPortMutexHandle_t gMutex;

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
static MqttUrcStatus_t gUrcStatus;

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

// MQTT URC handler.
// Note: on SARA-R4 the URC has just a small number of uses
// while on SARA-R5 most of the AT+UMQTTC commands simply return
// OK and it is necessary to wait for a URC to determine if they
// worked or not.
static void UUMQTTC_urc(void *pUnused)
{
    int32_t urcType;
    int32_t urcParam1;
    int32_t urcParam2;

    (void) pUnused;

    urcType = cellular_ctrl_at_read_int();
    // All of the MQTT URC types have at least one parameter
    urcParam1 = cellular_ctrl_at_read_int();
    switch (urcType) {
        case 0: // Logout, 1 means success
            if ((urcParam1 == 1) ||
                (urcParam1 == 100) || // SARA-R5, inactivity
                (urcParam1 == 101)) { // SARA-R5, connection lost
                // Disconnected
                gUrcStatus.connected = false;
            }
        break;
        case 1: // Login, 0 means success
            if (urcParam1 == 0) {
                // Connected
                gUrcStatus.connected = true;
            }
        break;
        case 2: // Publish, 1 means success
            if (urcParam1 == 1) {
                // Published
                gUrcStatus.publishSuccess = true;
            }
        break;
        // 3 (publish file) is not used by this driver
        case 4: // Subscribe, 1 means success
            if (urcParam1 == 1) {
                urcParam2 = cellular_ctrl_at_read_int();
                if (urcParam2 >= 0) {
                    // Skip the topic string
                    cellular_ctrl_at_skip_param(1);
                    // Subscribed
                    gUrcStatus.subscribeQoS = urcParam2;
                    gUrcStatus.subscribeSuccess = true;
                }
            }
        break;
        case 5: // Unsubscribe, 1 means success
            if (urcParam1 == 1) {
                // Unsubscribed
                gUrcStatus.unsubscribeSuccess = true;
            }
        break;
        case 6: // Num unread messages
            if (urcParam1 >= 0) {
                urcParam2 = cellular_ctrl_at_read_int();
                if (urcParam2 >= 0) {
                    gUrcStatus.numUnreadMessages = urcParam1;
                    gUrcStatus.memoryFull = (urcParam2 == 1);
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
            }
        break;
        default:
            // Do nothing
        break;
    }
}

#ifdef CELLULAR_CFG_MODULE_SARA_R4

// MQTT message URC handler, for SARA-R4 only.
static void UUMQTTCM_urc(void *pParam)
{
    MqttUrcMessage_t *pUrcMessage = (MqttUrcMessage_t *) pParam;
    int32_t param;
    int32_t topicNameBytesRead;
    int32_t messageBytesAvailable;
    uint8_t quoteMark;

    // Skip the op code
    cellular_ctrl_at_skip_param(1);
    // Read the new number of unread messages
    param = cellular_ctrl_at_read_int();
    if (param >= 0) {
        gUrcStatus.numUnreadMessages = param;
    }
    // Read the topic name
    topicNameBytesRead = cellular_ctrl_at_read_string(pUrcMessage->pTopicNameStr,
                                                      pUrcMessage->topicNameSizeBytes,
                                                      false);
    // Read the message length
    messageBytesAvailable = cellular_ctrl_at_read_int();
    if (messageBytesAvailable > CELLULAR_MQTT_READ_MAX_LENGTH_BYTES) {
        messageBytesAvailable = CELLULAR_MQTT_READ_MAX_LENGTH_BYTES;
    }
    // Read the QoS
    pUrcMessage->qos= cellular_ctrl_at_read_int();
    // Now read the message
    if (messageBytesAvailable >= 0) {
        // Now read the exact length of message
        // bytes, being careful to not look for
        // delimiters or the like as this can be
        // a binary message
        cellular_ctrl_at_set_delimiter(0);
        cellular_ctrl_at_set_stop_tag(NULL);
        // Get the leading quote mark out of the way
        cellular_ctrl_at_read_bytes(&quoteMark, 1);
        // Now read the actual data
        pUrcMessage->messageSizeBytes = cellular_ctrl_at_read_bytes((uint8_t *) (pUrcMessage->pMessage),
                                                                    messageBytesAvailable);
        // Get the trailing quote mark out of the way
        cellular_ctrl_at_read_bytes(&quoteMark, 1);
        cellular_ctrl_at_set_default_delimiter();
    }

    // Now have all the bits, so if all look good say that
    // the message has been read
    if ((topicNameBytesRead >= 0) &&
        (pUrcMessage->qos >= 0) &&
        (pUrcMessage->qos < MAX_NUM_CELLULAR_MQTT_QOS) &&
        (pUrcMessage->messageSizeBytes >= 0)) {
        pUrcMessage->messageRead = true;
    }
}

#endif // CELLULAR_CFG_MODULE_SARA_R4

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: MISC
 * -------------------------------------------------------------- */

// Convert a message buffer into a hex version of that buffer,
// returning the number of bytes (not hex values) in the hex buffer.
static size_t toHex(char *pHex, const char *pBinary,
                    size_t binaryLength)
{
    size_t hexLength = 0;

    for (size_t x = 0; x < (binaryLength << 2); x++) {
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

// Set MQTT ping or "keep alive" on or off.
static CellularMqttErrorCode_t setKeepAlive(bool onNotOff)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_DEFAULT_ERROR_CODE;
    int32_t status = 1;

    if (gInitialised) {
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
        }
    }

    return errorCode;
}

// Set MQTT session retention on or off.
static CellularMqttErrorCode_t setSessionRetention(bool onNotOff)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_DEFAULT_ERROR_CODE;

    if (gInitialised) {
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
        cellular_ctrl_at_cmd_stop_read_resp();
        if (cellular_ctrl_at_unlock_return_error() == 0) {
            errorCode = CELLULAR_MQTT_SUCCESS;
        }
    }

    return errorCode;
}

// Set security on or off.
static CellularMqttErrorCode_t setSecurity(bool onNotOff,
                                          int32_t securityProfileId)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_DEFAULT_ERROR_CODE;

    if (gInitialised) {
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
        cellular_ctrl_at_cmd_stop_read_resp();
        if (cellular_ctrl_at_unlock_return_error() == 0) {
            errorCode = CELLULAR_MQTT_SUCCESS;
        }
    }

    return errorCode;
}

// Connect or disconnect MQTT.
static CellularMqttErrorCode_t connect(bool onNotOff)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_DEFAULT_ERROR_CODE;
    int32_t status = 1;
    int64_t stopTimeMs;

    if (gInitialised) {

        // Lock the mutex as we'll be
        // setting gUrcStatus.connected]
        // and we don't want to trample
        // on anyone else
        CELLULAR_PORT_MUTEX_LOCK(gMutex);

        errorCode = CELLULAR_MQTT_AT_ERROR;
        cellular_ctrl_at_lock();
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
        if ((cellular_ctrl_at_unlock_return_error() == 0) &&
            (status == 1)) {
            errorCode = CELLULAR_MQTT_TIMEOUT;
            // On all platforms we have to wait for the URC for success
            stopTimeMs = cellularPortGetTickTimeMs() + (CELLULAR_MQTT_RESPONSE_WAIT_SECONDS * 1000);
            while ((onNotOff != gUrcStatus.connected) &&
                   (cellularPortGetTickTimeMs() < stopTimeMs) &&
                   ((gpKeepGoingCallback == NULL) ||
                    gpKeepGoingCallback())) {
                cellularPortTaskBlock(1000);
            }
            if (onNotOff == gUrcStatus.connected) {
                errorCode = CELLULAR_MQTT_SUCCESS;
            }
        }

        CELLULAR_PORT_MUTEX_UNLOCK(gMutex);

    }

    return errorCode;
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
    int32_t port;
    int32_t status = 1;
    bool keepGoing = true;

    errorCode = CELLULAR_MQTT_SUCCESS;
    if (!gInitialised) {
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
                        cellular_ctrl_at_cmd_stop();
                        cellular_ctrl_at_resp_start("+UMQTT:", false);
                        // Skip the first parameter, which is just
                        // our UMQTT command number again
                        cellular_ctrl_at_skip_param(1);
                        status = cellular_ctrl_at_read_int();
                        cellular_ctrl_at_resp_stop();
                        keepGoing = ((cellular_ctrl_at_unlock_return_error() == 0) &&
                                     (status == 1));
                    }
                } else {
                    // We must have a domain name,
                    // make a copy of it as we need to
                    // manipulate it
                    pCellularPort_strcpy(pAddress, pServerNameStr);
                    // Grab any port number off the end
                    // and then remove it from the string
                    port = cellularSockDomainGetPort(pAddress);
                    pCellularSockDomainRemovePort(pAddress);
                    cellular_ctrl_at_lock();
                    cellular_ctrl_at_cmd_start("AT+UMQTT=");
                    // Set the server name
                    cellular_ctrl_at_write_int(2);
                    // The address
                    cellular_ctrl_at_write_string(pAddress, true);
                    // If there's was a port number, write
                    // that also
                    if (port >= 0) {
                        cellular_ctrl_at_write_int(port);
                    }
                    cellular_ctrl_at_cmd_stop_read_resp();
                    keepGoing = (cellular_ctrl_at_unlock_return_error() == 0);
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
                    // If there's was a password, write that also
                    if (pPasswordStr != NULL) {
                        cellular_ctrl_at_write_string(pPasswordStr, true);
                    }
                    cellular_ctrl_at_cmd_stop();
                    cellular_ctrl_at_resp_start("+UMQTT:", false);
                    // Skip the first parameter, which is just
                    // our UMQTT command number again
                    cellular_ctrl_at_skip_param(1);
                    status = cellular_ctrl_at_read_int();
                    cellular_ctrl_at_resp_stop();
                    keepGoing = ((cellular_ctrl_at_unlock_return_error() == 0) &&
                                 (status == 1));
                }

                // Finally deal with the local client name
                if (keepGoing && (pClientNameStr != NULL)) {
                    cellular_ctrl_at_lock();
                    cellular_ctrl_at_cmd_start("AT+UMQTT=");
                    // Set client ID
                    cellular_ctrl_at_write_int(0);
                    // The ID
                    cellular_ctrl_at_write_string(pClientNameStr, true);
                    cellular_ctrl_at_cmd_stop();
                    cellular_ctrl_at_resp_start("+UMQTT:", false);
                    // Skip the first parameter, which is just
                    // our UMQTT command number again
                    cellular_ctrl_at_skip_param(1);
                    status = cellular_ctrl_at_read_int();
                    cellular_ctrl_at_resp_stop();
                    keepGoing = ((cellular_ctrl_at_unlock_return_error() == 0) &&
                                 (status == 1));
                }

#ifdef CELLULAR_CFG_MODULE_SARA_R4
                if (keepGoing) {
                    // If this is SARA-R4, select verbose message reads
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
                        pCellularPort_memset(&gUrcStatus, 0, sizeof(gUrcStatus));
                        cellular_ctrl_at_set_urc_handler("+UUMQTTC:", UUMQTTC_urc, NULL);
                        gpKeepGoingCallback = pKeepGoingCallback;
                        gpMessageIndicationCallback = NULL;
                        gpMessageIndicationCallbackParam = NULL;
                        gKeptAlive = false;
                        errorCode = CELLULAR_MQTT_SUCCESS;
                        gInitialised = true;
                    }
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
    cellular_ctrl_at_remove_urc_handler("+UUMQTTC:");
    cellularPortMutexDelete(gMutex);
    gInitialised = false;
}

// Get the current MQTT client name.
int32_t cellularMqttGetClientName(char *pClientNameStr,
                                  int32_t sizeBytes)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_DEFAULT_ERROR_CODE;
    int32_t bytesRead;

    if (gInitialised) {
        errorCode = CELLULAR_MQTT_INVALID_PARAMETER;
        if (pClientNameStr != NULL) {
            // No need to lock the mutex, the
            // mutex protection of the AT interface
            // lock is sufficient
            errorCode = CELLULAR_MQTT_AT_ERROR;
            cellular_ctrl_at_lock();
            cellular_ctrl_at_cmd_start("AT+UMQTT?");
            cellular_ctrl_at_cmd_stop();
            // The response is multi-line and we want
            // just the first one
            cellular_ctrl_at_resp_start("+UMQTT:0,", false);
            bytesRead = cellular_ctrl_at_read_string(pClientNameStr,
                                                     sizeBytes,
                                                     false);
            cellular_ctrl_at_resp_stop();
            cellular_ctrl_at_unlock();
            if (bytesRead >= 0) {
                errorCode = CELLULAR_MQTT_SUCCESS;
            }
        }
    }

    return (int32_t) errorCode;
}

// Set the local port to use for the MQTT client.
int32_t cellularMqttSetLocalPort(uint16_t port)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_DEFAULT_ERROR_CODE;

    if (gInitialised) {
        // No need to lock the mutex, the
        // mutex protection of the AT interface
        // lock is sufficient
        errorCode = CELLULAR_MQTT_AT_ERROR;
        cellular_ctrl_at_lock();
        cellular_ctrl_at_cmd_start("AT+UMQTT=");
        // Set the local port
        cellular_ctrl_at_write_int(1);
        cellular_ctrl_at_write_int(port);
        cellular_ctrl_at_cmd_stop_read_resp();
        if (cellular_ctrl_at_unlock_return_error() == 0) {
            errorCode = CELLULAR_MQTT_SUCCESS;
        }
    }

    return (int32_t) errorCode;
}

// Get the local port used by the MQTT client.
int32_t cellularMqttGetLocalPort()
{
    CellularMqttErrorCode_t errorCodeOrPort = CELLULAR_MQTT_DEFAULT_ERROR_CODE;
    uint16_t port;

    if (gInitialised) {
        // No need to lock the mutex, the
        // mutex protection of the AT interface
        // lock is sufficient
        errorCodeOrPort = CELLULAR_MQTT_AT_ERROR;
        cellular_ctrl_at_lock();
        cellular_ctrl_at_cmd_start("AT+UMQTT?");
        cellular_ctrl_at_cmd_stop();
        // The response is multi-line and we want
        // just the second one
        cellular_ctrl_at_resp_start("+UMQTT:1,", false);
        port = cellular_ctrl_at_read_int();
        cellular_ctrl_at_resp_stop();
        cellular_ctrl_at_unlock();
        if ((port >= 0) && (port <= USHRT_MAX)) {
            errorCodeOrPort = port;
        }
    }

    return (int32_t) errorCodeOrPort;
}

// Set the inactivity timeout used by the MQTT client.
int32_t cellularMqttSetInactivityTimeout(int32_t seconds)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_DEFAULT_ERROR_CODE;

    if (gInitialised) {
        // No need to lock the mutex, the
        // mutex protection of the AT interface
        // lock is sufficient
        errorCode = CELLULAR_MQTT_AT_ERROR;
        cellular_ctrl_at_lock();
        cellular_ctrl_at_cmd_start("AT+UMQTT=");
        // Set the inactivity timeout
        cellular_ctrl_at_write_int(10);
        cellular_ctrl_at_write_int(seconds);
        cellular_ctrl_at_cmd_stop_read_resp();
        if (cellular_ctrl_at_unlock_return_error() == 0) {
            errorCode = CELLULAR_MQTT_SUCCESS;
        }
    }

    return (int32_t) errorCode;
}

// Get the inactivity timeout used by the MQTT client.
int32_t cellularMqttGetInactivityTimeout()
{
    CellularMqttErrorCode_t errorCodeOrTimeout = CELLULAR_MQTT_DEFAULT_ERROR_CODE;
    int32_t timeout;

    if (gInitialised) {
        // No need to lock the mutex, the
        // mutex protection of the AT interface
        // lock is sufficient
        errorCodeOrTimeout = CELLULAR_MQTT_AT_ERROR;
        cellular_ctrl_at_lock();
        cellular_ctrl_at_cmd_start("AT+UMQTT?");
        cellular_ctrl_at_cmd_stop();
        // The response is multi-line and we want
        // number 10
        cellular_ctrl_at_resp_start("+UMQTT:10,", false);
        timeout = cellular_ctrl_at_read_int();
        cellular_ctrl_at_resp_stop();
        cellular_ctrl_at_unlock();
        if (timeout >= 0) {
            errorCodeOrTimeout = timeout;
        }
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
    return (int32_t) setSessionRetention(true);
}

// Switch MQTT session retention off.
int32_t cellularMqttSetSessionRetentionOff()
{
    return (int32_t) setSessionRetention(false);
}

// Determine whether MQTT session retention is on.
bool cellularMqttIsSessionRetained()
{
    bool sessionRetained = false;
    int32_t x;

    if (gInitialised) {
        // No need to lock the mutex, the
        // mutex protection of the AT interface
        // lock is sufficient
        cellular_ctrl_at_lock();
        cellular_ctrl_at_cmd_start("AT+UMQTT?");
        cellular_ctrl_at_cmd_stop();
        // The response is multi-line and we want
        // number 12
        cellular_ctrl_at_resp_start("+UMQTT:12,", false);
        x = cellular_ctrl_at_read_int();
        cellular_ctrl_at_resp_stop();
        cellular_ctrl_at_unlock();
        if (x == 1) {
            sessionRetained = true;
        }
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
    int32_t x;
    int32_t y;

    if (gInitialised) {
        // No need to lock the mutex, the
        // mutex protection of the AT interface
        // lock is sufficient
        cellular_ctrl_at_lock();
        cellular_ctrl_at_cmd_start("AT+UMQTT?");
        cellular_ctrl_at_cmd_stop();
        // The response is multi-line and we want
        // number 11
        cellular_ctrl_at_resp_start("+UMQTT:11,", false);
        x = cellular_ctrl_at_read_int();
        if (x == 1) {
            y = cellular_ctrl_at_read_int();
        }
        cellular_ctrl_at_resp_stop();
        cellular_ctrl_at_unlock();
        if (x == 1) {
            secured = true;
            if (pSecurityProfileId != NULL) {
                *pSecurityProfileId = -1;
                if (y >= 0) {
                    *pSecurityProfileId = y;
                }
            }
        }
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

    if (gInitialised) {
        errorCode = CELLULAR_MQTT_INVALID_PARAMETER;
        if ((qos >= 0) &&
            (qos < MAX_NUM_CELLULAR_MQTT_QOS) &&
            (pTopicNameStr != NULL) &&
            (pMessage != NULL) &&
            (messageSizeBytes <= CELLULAR_MQTT_PUBLISH_MAX_LENGTH_BYTES)) {
            // Allocate memory to store the hex
            // version of the message
            errorCode = CELLULAR_MQTT_NO_MEMORY;
            pHexMessage = (char *) pCellularPort_malloc(messageSizeBytes * 2);
            if (pHexMessage != NULL) {
                // Convert to hex
                toHex(pHexMessage, pMessage, messageSizeBytes);

                // Lock the mutex as we'll be
                // setting gUrcStatus.publishSuccess
                // and we don't want to trample
                // on anyone else
                CELLULAR_PORT_MUTEX_LOCK(gMutex);

                errorCode = CELLULAR_MQTT_AT_ERROR;
                cellular_ctrl_at_lock();
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
                    stopTimeMs = cellularPortGetTickTimeMs() + (CELLULAR_MQTT_RESPONSE_WAIT_SECONDS * 1000);
                    while ((!gUrcStatus.publishSuccess) &&
                           (cellularPortGetTickTimeMs() < stopTimeMs) &&
                           ((gpKeepGoingCallback == NULL) ||
                            gpKeepGoingCallback())) {
                        cellularPortTaskBlock(1000);
                    }
                    if (gUrcStatus.publishSuccess) {
                        errorCode = CELLULAR_MQTT_SUCCESS;
                    }
#endif
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

    if (gInitialised) {
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
                stopTimeMs = cellularPortGetTickTimeMs() + (CELLULAR_MQTT_RESPONSE_WAIT_SECONDS * 1000);
                while ((!gUrcStatus.subscribeSuccess) &&
                       (cellularPortGetTickTimeMs() < stopTimeMs) &&
                       ((gpKeepGoingCallback == NULL) ||
                        gpKeepGoingCallback())) {
                    cellularPortTaskBlock(1000);
                }
                if (gUrcStatus.subscribeSuccess) {
                    errorCodeOrQos = gUrcStatus.subscribeQoS;
                }
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

    if (gInitialised) {
        errorCode = CELLULAR_MQTT_INVALID_PARAMETER;
        if (pTopicFilterStr != NULL) {
            // Lock the mutex as we'll be
            // setting gUrcStatus.unsubscribeSuccess
            // and we don't want to trample
            // on anyone else
            CELLULAR_PORT_MUTEX_LOCK(gMutex);

            errorCode = CELLULAR_MQTT_AT_ERROR;
            cellular_ctrl_at_lock();
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
                stopTimeMs = cellularPortGetTickTimeMs() + (CELLULAR_MQTT_RESPONSE_WAIT_SECONDS * 1000);
                while ((!gUrcStatus.unsubscribeSuccess) &&
                       (cellularPortGetTickTimeMs() < stopTimeMs) &&
                       ((gpKeepGoingCallback == NULL) ||
                        gpKeepGoingCallback())) {
                    cellularPortTaskBlock(1000);
                }
                if (gUrcStatus.unsubscribeSuccess) {
                    errorCode = CELLULAR_MQTT_SUCCESS;
                }
#endif
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

    // Lock the mutex as we're about
    // to perform a non-atomic operation
    CELLULAR_PORT_MUTEX_LOCK(gMutex);

    gpMessageIndicationCallback = pCallback;
    gpMessageIndicationCallbackParam = pCallbackParam;
    errorCode = CELLULAR_MQTT_SUCCESS;

    CELLULAR_PORT_MUTEX_UNLOCK(gMutex);

    return (int32_t) errorCode;
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
    MqttUrcMessage_t urcMessage;
#else
    CellularMqttQos_t qos;
    int32_t topicNameBytesRead;
    int32_t messageBytesAvailable;
    int32_t messageBytesRead;
    uint8_t quoteMark;
#endif

    if (gInitialised) {
        errorCode = CELLULAR_MQTT_INVALID_PARAMETER;
        if ((pTopicNameStr != NULL) &&
            (pMessage != NULL)) {
            // We read the message into a temporary
            // buffer as it may be larger than
            // the caller has room for and we
            // have to read it all in somehow
            errorCode = CELLULAR_MQTT_NO_MEMORY;
            pMessageBuffer = (char *) pCellularPort_malloc(CELLULAR_MQTT_READ_MAX_LENGTH_BYTES);
            if (pMessageBuffer != NULL) {
                // Lock the mutex as we'll be
                // setting gUrcStatus.messageRead
                // and we don't want to trample
                // on anyone else
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
                urcMessage.messageRead = false;
                urcMessage.pTopicNameStr = pTopicNameStr;
                urcMessage.topicNameSizeBytes = topicNameSizeBytes;
                urcMessage.pMessage = pMessageBuffer;
                urcMessage.messageSizeBytes = CELLULAR_MQTT_READ_MAX_LENGTH_BYTES;
                cellular_ctrl_at_set_urc_handler("+UUMQTTCM:", UUMQTTCM_urc, &urcMessage);
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
                    stopTimeMs = cellularPortGetTickTimeMs() + (CELLULAR_MQTT_RESPONSE_WAIT_SECONDS * 1000);
                    while ((!urcMessage.messageRead) &&
                           (cellularPortGetTickTimeMs() < stopTimeMs) &&
                           ((gpKeepGoingCallback == NULL) ||
                            gpKeepGoingCallback())) {
                        cellularPortTaskBlock(1000);
                    }
                    if (urcMessage.messageRead) {
                        // pTopicNameStr was filled in directly,
                        // now fill in the passed-in parameters
                        // that we haven't already done
                        pCellularPort_memcpy(pMessage,
                                             pMessageBuffer,
                                             urcMessage.messageSizeBytes);
                        *pMessageSizeBytes = urcMessage.messageSizeBytes;
                        if (pQos != NULL) {
                            *pQos = urcMessage.qos;
                        }
                        errorCode = CELLULAR_MQTT_SUCCESS;
                    }
                }
                cellular_ctrl_at_remove_urc_handler("+UUMQTTCM:");
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
                if (messageBytesAvailable > CELLULAR_MQTT_READ_MAX_LENGTH_BYTES) {
                    messageBytesAvailable = CELLULAR_MQTT_READ_MAX_LENGTH_BYTES;
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
                        errorCode = CELLULAR_MQTT_SUCCESS;
                    }
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

// End of file
