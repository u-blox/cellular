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
#include "cellular_mqtt.h"

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
 * STATIC FUNCTIONS: URCS AND RELATED FUNCTIONS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS: MISC
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS
 * -------------------------------------------------------------- */

// Initialise the MQTT client.
int32_t cellularMqttInit(const char *pClientNameStr,
                         const char *pServerNameStr,
                         const char *pUserNameStr,
                         const char *pPasswordStr)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Shut-down the MQTT client.
void cellularMqttDeinit()
{
    // TODO
}

// Get the MQTT server name that was configured in cellularMqttInit().
int32_t cellularMqttGetServerName(char *pServerNameStr,
                                  int32_t sizeBytes)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Get the current MQTT client name.
int32_t cellularMqttGetClientName(char *pClientNameStr,
                                  int32_t sizeBytes)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Get the username that was configured in cellularMqttInit().
int32_t cellularMqttGetUserName(char *pUserNameStr,
                                int32_t sizeBytes)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Set the local port to use for the MQTT client.  If this is not
int32_t cellularMqttSetLocalPort(int32_t port)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Get the local port used by the MQTT client.
int32_t cellularMqttGetLocalPort()
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Set the inactivity timeout used by the MQTT client.
int32_t cellularMqttSetInactivityTimeout(int32_t seconds)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Get the inactivity timeout used by the MQTT client.
int32_t cellularMqttGetInactivityTimeout(int32_t seconds)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Switch MQTT ping or "keep alive" on.
int32_t cellularMqttSetKeepAliveOn()
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Switch MQTT ping or "keep alive" off.
int32_t cellularMqttSetKeepAliveOff()
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Determine whether MQTT ping or "keep alive" is on.
bool cellularMqttIsKeptAlive()
{
    // TODO
    return false;
}

// Switch session retention on.
int32_t cellularMqttSetSessionRetentionOn()
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Switch MQTT session retention off.
int32_t cellularMqttSetSessionRetentionOff()
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Determine whether MQTT session retention is on.
bool cellularMqttIsSessionRetained();

// Switch MQTT TLS security on.
int32_t cellularMqttSetSecurityOn(int32_t securityProfileId)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Switch MQTT TLS security off.
int32_t cellularMqttSetSecurityOff()
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Determine whether MQTT TLS security is on or off.
bool cellularMqttIsSecured(int32_t *pSecurityProfileId)
{
    // TODO
    return false;
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
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Stop an MQTT session.
int32_t cellularMqttDisconnect()
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Determine whether an MQTT session is active or not.
bool cellularMqttIsConnected()
{
    // TODO
    return false;
}

// Publish an MQTT message.
int32_t cellularMqttPublish(CellularMqttQos_t qos,
                            bool retention,
                            const char *pTopicNameStr,
                            const char *pMessage,
                            int32_t messageSizeBytes)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Subscribe to an MQTT topic.
int32_t cellularMqttSubscribe(CellularMqttQos_t maxQos,
                             const char *pTopicFilterStr)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Unsubscribe from an MQTT topic.
int32_t cellularMqttUnsubscribe(const char *pTopicFilterStr)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Set a new messages callback.
int32_t cellularMqttSetMessageIndicationCallback(void (*pCallback)(int32_t, void *),
                                                 void *pCallbackParam)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// Read an MQTT message.
int32_t cellularMqttMessageRead(char *pTopicNameStr,
                                int32_t topicNameSizeBytes,
                                char *pMessage,
                                int32_t *pMessageSizeBytes)
{
    CellularMqttErrorCode_t errorCode = CELLULAR_MQTT_NOT_IMPLEMENTED;

    // TODO

    return (int32_t) errorCode;
}

// End of file
