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


#ifndef _CELLULAR_MQTT_H_
#define _CELLULAR_MQTT_H_

/* No #includes allowed here */

/* This header file defines the cellular MQTT client API.  These functions
 * are thread-safe with the proviso that there is only one MQTT client
 * instance underneath.
 */

/* IMPORTANT: this API is still in the process of definition, anything
 * may change, beware!  We aim to have it settled by the middle of 2020,
 * COVID 19 permitting.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/** The default MQTT server port for unsecured operation.
 */
#define CELLULAR_MQTT_SERVER_PORT_UNSECURE 1883

/** The default MQTT server port for TLS secured operation.
 */
#define CELLULAR_MQTT_SERVER_PORT_SECURE 8883

/** The maximum length of the local client name in bytes.
 */
#ifndef CELLULAR_MQTT_CLIENT_NAME_STRING_MAX_LENGTH_BYTES
# define CELLULAR_MQTT_CLIENT_NAME_STRING_MAX_LENGTH_BYTES 64
#endif

/** The maximum length of an MQTT server address string.
 */
#ifndef CELLULAR_MQTT_SERVER_ADDRESS_STRING_MAX_LENGTH_BYTES
# define CELLULAR_MQTT_SERVER_ADDRESS_STRING_MAX_LENGTH_BYTES 256
#endif

/** The time to wait for an MQTT operation to be completed.
 */
#ifndef CELLULAR_MQTT_RESPONSE_WAIT_SECONDS
# error CELLULAR_MQTT_RESPONSE_WAIT_SECONDS must be defined in cellular_cfg_module.h.
#endif

/** The maximum length of an MQTT publish message in bytes.
 */
#ifndef CELLULAR_MQTT_PUBLISH_MAX_LENGTH_BYTES
# error CELLULAR_MQTT_PUBLISH_MAX_LENGTH_BYTES must be defined in cellular_cfg_module.h.
#endif

/** The maximum length of an MQTT read message in bytes.
 */
#ifndef CELLULAR_MQTT_READ_MAX_LENGTH_BYTES
# error CELLULAR_MQTT_READ_MAX_LENGTH_BYTES must be defined in cellular_cfg_module.h.
#endif

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/** Error codes.
 */
typedef enum {
    CELLULAR_MQTT_SUCCESS = 0,
    CELLULAR_MQTT_UNKNOWN_ERROR = -1,
    CELLULAR_MQTT_NOT_INITIALISED = -2,
    CELLULAR_MQTT_NOT_IMPLEMENTED = -3,
    CELLULAR_MQTT_NOT_RESPONDING = -4,
    CELLULAR_MQTT_INVALID_PARAMETER = -5,
    CELLULAR_MQTT_NO_MEMORY = -6,
    CELLULAR_MQTT_PLATFORM_ERROR = -7,
    CELLULAR_MQTT_AT_ERROR = -8,
    CELLULAR_MQTT_NOT_SUPPORTED = -9,
    CELLULAR_MQTT_TIMEOUT = -10,
    CELLULAR_MQTT_BAD_ADDRESS = -11,
    CELLULAR_MQTT_FORCE_32_BIT = 0x7FFFFFFF // Force this enum to be 32 bit
                                            // as it can be used as a size
                                            // also
} CellularMqttErrorCode_t;

/** MQTT QoS.
 */
typedef enum {
    CELLULAR_MQTT_AT_MOST_ONCE = 0,
    CELLULAR_MQTT_AT_LEAST_ONCE = 1,
    CELLULAR_MQTT_EXACTLY_ONCE = 2,
    MAX_NUM_CELLULAR_MQTT_QOS
} CellularMqttQos_t;

/* ----------------------------------------------------------------
 * FUNCTIONS
 * -------------------------------------------------------------- */

/** Initialise the MQTT client.  If the client is already
 * initialised then this function returns immediately. The cellular
 * module must be powered up for this function to work.
 *
 * @param pServerNameStr     the NULL terminated string that gives
 *                           the name of the server for this MQTT
 *                           session.  This may be a domain name,
 *                           or an IP address and may include a port
 *                           number.
 * @param pUserNameStr       the NULL terminated string that is the
 *                           user name required by the MQTT server.
 * @param pPasswordStr       the NULL terminated string that is the
 *                           password required by the MQTT server.
 * @param pClientNameStr     the NULL terminated string that
 *                           will be the client name for this
 *                           MQTT session.  May be NULL, in
 *                           which case the driver will provide
 *                           a name.
 * @param pKeepGoingCallback certain of the MQTT API functions
 *                           need to wait for the server to respond
 *                           and this may take some time.  Specify
 *                           a callback function here which will be
 *                           called while the API is waiting.  While
 *                           the callback function returns true the
 *                           API will continue to wait until success
 *                           or CELLULAR_MQTT_RESPONSE_WAIT_SECONDS
 *                           is reached.  If the callback function
 *                           returns false then the API will return.
 *                           Note that the thing the API was waiting for
 *                           may still succeed, this does not cancel
 *                           the operation, it simply stops waiting
 *                           for the response.  The callback function
 *                           may also be used to feed any application
 *                           watchdog timer that may be running.
 *                           May be NULL, in which case the
 *                           APIs will continue to wait until success
 *                           or CELLULAR_MQTT_RESPONSE_WAIT_SECONDS.
 * @return                   zero on success or negative error code on
 *                           failure.
 */
int32_t cellularMqttInit(const char *pServerNameStr,
                         const char *pUserNameStr,
                         const char *pPasswordStr,
                         const char *pClientNameStr,
                         bool (*pKeepGoingCallback)(void));

/** Shut-down the MQTT client.
 */
void cellularMqttDeinit();

/** Get the current MQTT client name.
 *
 * @param pClientNameStr   pointer to a place to put the client name,
 *                         which will be NULL terminated.
 * @param sizeBytes        size of the storage at pClientNameStr,
 *                         including the terminator.
 * @return                 the number of bytes written to pClientNameStr,
 *                         not including the NULL terminator (i.e. what
 *                         strlen() would return), or negative error code.
 */
int32_t cellularMqttGetClientName(char *pClientNameStr,
                                  int32_t sizeBytes);

/** Set the local port to use for the MQTT client.  If this is not
 * called the IANA assigned ports of 1883 for non-secure MQTT or 8883 
 * for TLS secured MQTT will be used.
 *
 * @param port  the port number.
 * @return      zero on success or negative error code.
 */
int32_t cellularMqttSetLocalPort(uint16_t port);

/** Get the local port used by the MQTT client.
 *
 * @return      the port number on success or negative error code.
 */
int32_t cellularMqttGetLocalPort();

/** Set the inactivity timeout used by the MQTT client.  If this
 * is not called then no inactivity timeout is used.
 *
 * @param seconds  the inactivity timeout in seconds.
 * @return         zero on success or negative error code.
 */
int32_t cellularMqttSetInactivityTimeout(int32_t seconds);

/** Get the inactivity timeout used by the MQTT client.
 *
 * @return  the inactivity timeout in seconds on success or
 *          negative error code.
 */
int32_t cellularMqttGetInactivityTimeout();

/** Switch MQTT ping or "keep alive" on.  This will send an
 * MQTT ping message to the server near the end of the 
 * inactivity timeout to keep the connection alive.
 * If this is not called no such ping is sent.
 *
 * @return zero on success or negative error code.
 */
int32_t cellularMqttSetKeepAliveOn();

/** Switch MQTT ping or "keep alive" off. See
 * cellularMqttSetKeepAliveOn() for more details.
 *
 * @return zero on success or negative error code.
 */
int32_t cellularMqttSetKeepAliveOff();

/** Determine whether MQTT ping or "keep alive" is on
 * or off.
 *
 * @return true if MQTT ping or "keep alive" is on,
 *         else false.
 */
bool cellularMqttIsKeptAlive();

/** If this function returns successfully then
 * the topic subscriptions and message queue status
 * will be remembered by both the client and the
 * server across MQTT disconnects/connects.
 * If this is not called no such data is retained.
 *
 * @return zero on success or negative error code.
 */
int32_t cellularMqttSetSessionRetentionOn();

/** Switch MQTT session retention off. See
 * cellularMqttSetSessionRetentionOn() for more details.
 *
 * @return zero on success or negative error code.
 */
int32_t cellularMqttSetSessionRetentionOff();

/** Determine whether MQTT session retention is on
 * or off.
 *
 * @return true if MQTT session retention is on else false.
 */
bool cellularMqttIsSessionRetained();

/** Switch MQTT TLS security on.  By default MQTT TLS
 * security is off.
 *
 * @param securityProfileId the security profile ID
 *                          containing the TLS security
 *                          parameters.  Specify -1
 *                          to let this be chosen
 *                          automatically.
 * @return                  zero on success or negative
 *                          error code.
 */
int32_t cellularMqttSetSecurityOn(int32_t securityProfileId);

/** Switch MQTT TLS security off.
 *
 * @return zero on success or negative error code.
 */
int32_t cellularMqttSetSecurityOff();

/** Determine whether MQTT TLS security is on or off.
 *
 * @param pSecurityProfileId a pointer to a place to put
 *                           the security profile ID that
 *                           is being used for MQTT TLS
 *                           security; may be NULL.
 * @return                   true if MQTT TLS security is
 *                           on else false.
 */
bool cellularMqttIsSecured(int32_t *pSecurityProfileId);

/** Set the MQTT "will" message that will be sent
 * by the server on uncommanded disconnect of the MQTT
 * client.
 *
 * @param qos              the MQTT QoS to use for the
 *                         "will" message.
 * @param retention        if true the "will" message will
 *                         be retained by the server across
 *                         MQTT disconnects/connects. 
 * @param pTopicNameStr    the NULL terminated topic string
 *                         for the "will" message; may be NULL.
 * @param pMessage         a pointer to the "will" message;
 *                         the "will" message is not restricted
 *                         to ASCII values. Cannot be NULL.
 * @param messageSizeBytes since pMessage may include binary
 *                         content, including NULLs, this
 *                         parameter specifies the length of
 *                         pMessage. If pMessage happens to
 *                         be an ASCII string this parameter
 *                         should be set to strlen(pMessage).
 * @return                 zero on success else negative error
 *                         code.
 */
int32_t cellularMqttSetWill(CellularMqttQos_t qos,
                            bool retention,
                            const char *pTopicNameStr,
                            const char *pMessage,
                            int32_t messageSizeBytes);

/** Get the MQTT "will" message that will be sent
 * by the server on uncommanded disconnect of the MQTT
 * client.
 *
 * @param pQos                a place to put the MQTT QoS that is
 *                            used for the "will" message. May
 *                            be NULL.
 * @param pRetention          a place to put the status of "will"
 *                            message retention. May be NULL. 
 * @param pTopicNameStr       a place to put the NULL terminated
 *                            topic string used with the "will"
 *                            message; may be NULL.
 * @param topicNameSizeBytes  the number of bytes of storage
 *                            at pTopicNameStr.  Ignored if
 *                            pTopicNameStr is NULL.
 * @param pMessage            a place to put the "will" message;
 *                            may be NULL.
 * @param pMessageSizeBytes   on entry this should point to the
 *                            number of bytes of storage at
 *                            pMessage. On return, if pMessage
 *                            is not NULL, this will be updated
 *                            to the number of bytes written
 *                            to pMessage.
 * @return                    zero on success else negative error
 *                            code.
 */
int32_t cellularMqttGetWill(CellularMqttQos_t *pQos,
                            bool *pRetention,
                            char *pTopicNameStr,
                            int32_t topicNameSizeBytes,
                            char *pMessage,
                            int32_t *pMessageSizeBytes);

/** Start an MQTT session.
 *
 * @return zero on success or negative error code.
 */
int32_t cellularMqttConnect();

/** Stop an MQTT session.
 *
 * @return zero on success or negative error code.
 */
int32_t cellularMqttDisconnect();

/** Determine whether an MQTT session is active or not.
 *
 * @return true if an MQTT session is active else false.
 */
bool cellularMqttIsConnected();

/** Publish an MQTT message.
 *
 * @param qos              the MQTT QoS to use for this message.
 * @param retention        if true the message will be retained
 *                         by the server across MQTT disconnects/
 *                         connects. 
 * @param pTopicNameStr    the NULL terminated topic string
 *                         for the message; cannot be NULL.
 * @param pMessage         a pointer to the message; the message
 *                         is not restricted to ASCII values.
 *                         Cannot be NULL.
 * @param messageSizeBytes since pMessage may include binary
 *                         content, including NULLs, this
 *                         parameter specifies the length of
 *                         pMessage. If pMessage happens to
 *                         be an ASCII string this parameter
 *                         should be set to strlen(pMessage).
 * @return                 zero on success else negative error
 *                         code.
 */
int32_t cellularMqttPublish(CellularMqttQos_t qos,
                            bool retention,
                            const char *pTopicNameStr,
                            const char *pMessage,
                            int32_t messageSizeBytes);

/** Subscribe to an MQTT topic.
 *
 * @param maxQos           the maximum MQTT message QoS to
 *                         for this subscription.
 * @param pTopicFilterStr  the NULL terminated topic string
 *                         to subscribe to; the wildcard '+'
 *                         may be used to specify "all"
 *                         at any one topic level and the 
 *                         wildcard '#' may be used at the end
 *                         of the string to indicate "everything
 *                         from here on".  Cannot be NULL.
 * @return                 success of the QoS of the subscription,
 *                         else negative error code.
 */
int32_t cellularMqttSubscribe(CellularMqttQos_t maxQos,
                             const char *pTopicFilterStr);

/** Unsubscribe from an MQTT topic.
 *
 * @param pTopicFilterStr  the NULL terminated topic string
 *                         to unsubscribe from; the wildcard '+'
 *                         may be used to specify "all"
 *                         at any one topic level and the 
 *                         wildcard '#' may be used at the end
 *                         of the string to indicate "everything
 *                         from here on".  Cannot be NULL.
 * @return                 zero on success else negative error
 *                         code.
 */
int32_t cellularMqttUnsubscribe(const char *pTopicFilterStr);

/** Set a callback to be called when new messages are
 * available to be read.
 *
 * @param pCallback       the callback. The first parameter to
 *                        the callback will be filled in with
 *                        the number of messages available to
 *                        be read. The second parameter will be
 *                        pCallbackParam. Use NULL to deregister
 *                        a previous callback.  The callback
 *                        will be run in a task with stack
 *                        size CELLULAR_CTRL_TASK_CALLBACK_STACK_SIZE_BYTES
 *                        and priority CELLULAR_CTRL_TASK_CALLBACK_PRIORITY.
 * @param pCallbackParam  this value will be passed to pCallback
 *                        as the second parameter.
 * @return                zero on success else negative error
 *                        code.
 */
int32_t cellularMqttSetMessageIndicationCallback(void (*pCallback)(int32_t, void *),
                                                 void *pCallbackParam);

/** Get the current number of unread messages.
 *
 * @return: the number of unread messages or negative error code.
 */
int32_t cellularMqttGetUnread();

/** Read an MQTT message.
 *
 * @param pTopicNameStr       a place to put the NULL terminated
 *                            topic string of the message; cannot
 *                            be NULL.
 * @param topicNameSizeBytes  the number of bytes of storage
 *                            at pTopicNameStr.
 * @param pMessage            a place to put the message; cannot
 *                            be NULL.
 * @param pMessageSizeBytes   on entry this should point to the
 *                            number of bytes of storage at
 *                            pMessage. On return, this will be
 *                            updated to the number of bytes written
 *                            to pMessage.  Cannot be NULL.
 * @param pQos                a place to put the QoS of the message;
 *                            may be NULL.
 * @return                    zero on success else negative error
 *                            code.
 */
int32_t cellularMqttMessageRead(char *pTopicNameStr,
                                int32_t topicNameSizeBytes,
                                char *pMessage,
                                int32_t *pMessageSizeBytes,
                                CellularMqttQos_t *pQos);

#ifdef __cplusplus
}
#endif

#endif // _CELLULAR_MQTT_H_

// End of file
