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


#ifndef _CELLULAR_CTRL_H_
#define _CELLULAR_CTRL_H_

/* No #includes allowed here */

/* This header file defines the cellular control API.  These functions
 * are thread-safe with the proviso that there is only one cellular module
 * and hence calling, for instance cellularCtrlRefreshRadioParameters()
 * will affect every thread's getting of radio parameters, or calling
 * cellularCtrlConnect() from two different threads at the same time
 * may lead to confusion.
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

/**  The bit in a CELLULAR_CTRL_SUPPORTED_RATS bitmap that denotes GPRS.
 * IMPORTANT: DO NOT CONFUSE THIS WITH CELLULAR_CTRL_RAT_GPRS,
 * it is NOT the same thing.  This value is solely for use in
 * the CELLULAR_CTRL_SUPPORTED_RATS_BITMAP value for a module.
 */
#define _CELLULAR_CTRL_SUPPORTED_RATS_BIT_GPRS 0x01

/**  The bit in a CELLULAR_CTRL_SUPPORTED_RATS bitmap that denotes UMTS.
 * IMPORTANT: DO NOT CONFUSE THIS WITH CELLULAR_CTRL_RAT_UMTS,
 * it is NOT the same thing.  This value is solely for use in
 * the CELLULAR_CTRL_SUPPORTED_RATS_BITMAP value for a module.
 */
#define _CELLULAR_CTRL_SUPPORTED_RATS_BIT_UMTS  0x02

/**  The bit in a CELLULAR_CTRL_SUPPORTED_RATS bitmap that denotes LTE.
 * IMPORTANT: DO NOT CONFUSE THIS WITH CELLULAR_CTRL_RAT_LTE,
 * it is NOT the same thing.  This value is solely for use in
 * the CELLULAR_CTRL_SUPPORTED_RATS_BITMAP value for a module.
 */
#define _CELLULAR_CTRL_SUPPORTED_RATS_BIT_LTE  0x04

/**  The bit in a CELLULAR_CTRL_SUPPORTED_RATS bitmap that denotes CATM1.
 * IMPORTANT: DO NOT CONFUSE THIS WITH CELLULAR_CTRL_RAT_CATM1,
 * it is NOT the same thing.  This value is solely for use in
 * the CELLULAR_CTRL_SUPPORTED_RATS_BITMAP value for a module.
 */
#define _CELLULAR_CTRL_SUPPORTED_RATS_BIT_CATM1 0x08

/**  The bit in a CELLULAR_CTRL_SUPPORTED_RATS bitmap that denotes NB1.
 * IMPORTANT: DO NOT CONFUSE THIS WITH CELLULAR_CTRL_RAT_NB1,
 * it is NOT the same thing.  This value is solely for use in
 * the CELLULAR_CTRL_SUPPORTED_RATS_BITMAP value for a module.
 */
#define _CELLULAR_CTRL_SUPPORTED_RATS_BIT_NB1   0x10

/** Get a CellularCtrlRat_t value based on a single bit
 * from the supported RAT bitmap.
 */
#define CELLULAR_CTRL_RAT_FROM_SUPPORTED_BITMAP(bit) ((bit & _CELLULAR_CTRL_SUPPORTED_RATS_BIT_GPRS)  ? 1 /* CELLULAR_CTRL_RAT_GPRS */  :  \
                                                      (bit & _CELLULAR_CTRL_SUPPORTED_RATS_BIT_UMTS)  ? 2 /* CELLULAR_CTRL_RAT_UMTS */  :  \
                                                      (bit & _CELLULAR_CTRL_SUPPORTED_RATS_BIT_LTE)   ? 3 /* CELLULAR_CTRL_RAT_LTE */   :  \
                                                      (bit & _CELLULAR_CTRL_SUPPORTED_RATS_BIT_CATM1) ? 4 /* CELLULAR_CTRL_RAT_CATM1 */ :  \
                                                      (bit & _CELLULAR_CTRL_SUPPORTED_RATS_BIT_NB1)   ? 5 /* CELLULAR_CTRL_RAT_NB1 */   :  \
                                                      CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED)

/** The North American bands for cat-M1, band mask bits 1 to 64.
 */
#define CELLULAR_CTRL_BAND_MASK_1_NORTH_AMERICA_CATM1_DEFAULT 0x000000400B0F189FLL

/** The North American bands for cat-M1, badn mask bits 65 to 128.
 */
#define CELLULAR_CTRL_BAND_MASK_2_NORTH_AMERICA_CATM1_DEFAULT 0LL

/** Bands 8 and 20, suitable for NB1 in Europe, band mask bits 1 to 64.
 */
#define CELLULAR_CTRL_BAND_MASK_1_EUROPE_NB1_DEFAULT 0x0000000000080080LL

/** NB1 in Europe, band mask bits 65 to 128.
 */
#define CELLULAR_CTRL_BAND_MASK_2_EUROPE_NB1_DEFAULT 0LL

/** The delay between AT commands, allowing internal cellular module
 * comms to complete before another is sent.
 */
#ifndef CELLULAR_CTRL_COMMAND_DELAY_MS
# error CELLULAR_CTRL_COMMAND_DELAY_MS must be defined in cellular_cfg_module.h.
#endif

/** The minimum reponse time one can expect with cellular module.
 * This is quite large since, if there is a URC about to come through,
 * it can delay what are normally immediate responses.
 */
#ifndef CELLULAR_CTRL_COMMAND_MINIMUM_RESPONSE_TIME_MS
# error CELLULAR_CTRL_COMMAND_MINIMUM_RESPONSE_TIME_MS must be defined in cellular_cfg_module.h.
#endif

/** The time to wait before the cellular module is ready at boot.
 */
#ifndef CELLULAR_CTRL_BOOT_WAIT_TIME_MS
# error CELLULAR_CTRL_BOOT_WAIT_TIME_MS must be defined in cellular_cfg_module.h.
#endif

/** The time to wait for an organised power off.
 */
#ifndef CELLULAR_CTRL_POWER_DOWN_WAIT_SECONDS
# error CELLULAR_CTRL_POWER_DOWN_WAIT_SECONDS must be defined in cellular_cfg_module.h.
#endif

/** The maximum number of simultaneous radio access technologies
 *  supported by the cellular module.
 */
#ifndef CELLULAR_CTRL_MAX_NUM_SIMULTANEOUS_RATS
# error CELLULAR_CTRL_MAX_NUM_SIMULTANEOUS_RATS must be defined in cellular_cfg_module.h.
#endif

/**A bitmap of the RATs supported by the cellular module.
 */
#ifndef CELLULAR_CTRL_SUPPORTED_RATS_BITMAP
# error CELLULAR_CTRL_SUPPORTED_RATS_BITMAP must be defined in cellular_cfg_module.h.
#endif

/** The PDP context ID to use.
 */
#define CELLULAR_CTRL_CONTEXT_ID 1

/** The module profile ID to use.
 */
#define CELLULAR_CTRL_PROFILE_ID 1

/** The number of digits in an IP address, including room for a
 * NULL terminator, i.e. "255.255.255.255\x00" .
 */
#define CELLULAR_CTRL_IP_ADDRESS_SIZE 16

/** The number of digits in an IMSI.
 */
#define CELLULAR_CTRL_IMSI_SIZE 15

/** The number of digits in an IMEI.
 */
#define CELLULAR_CTRL_IMEI_SIZE 15

/** The number of digits required to store an ICCID.  Note
 * that 19 digit ICCIDs also exist.  This size includes room
 * for a NULL terminator.
 */
#define CELLULAR_CTRL_ICCID_BUFFER_SIZE 21

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/** Error codes.
 */
typedef enum {
    CELLULAR_CTRL_SUCCESS = 0,
    CELLULAR_CTRL_UNKNOWN_ERROR = -1,
    CELLULAR_CTRL_NOT_INITIALISED = -2,
    CELLULAR_CTRL_NOT_IMPLEMENTED = -3,
    CELLULAR_CTRL_NOT_RESPONDING = -4,
    CELLULAR_CTRL_INVALID_PARAMETER = -5,
    CELLULAR_CTRL_NO_MEMORY = -6,
    CELLULAR_CTRL_PLATFORM_ERROR = -7,
    CELLULAR_CTRL_AT_ERROR = -8,
    CELLULAR_CTRL_NOT_CONFIGURED = -9,
    CELLULAR_CTRL_PIN_ENTRY_NOT_SUPPORTED = -10,
    CELLULAR_CTRL_NOT_REGISTERED = -11,
    CELLULAR_CTRL_CONTEXT_ACTIVATION_FAILURE = -12,
    CELLULAR_CTRL_NO_CONTEXT_ACTIVATED = -13,
    CELLULAR_CTRL_CONNECTED = -14, //!< This is an ERROR code used, for instance, to
                                   //! indicate that a disconnect attempt has failed.
    CELLULAR_CTRL_NOT_FOUND = -15,
    CELLULAR_CTRL_NOT_SUPPORTED = -16,
    CELLULAR_CTRL_SEC_SEAL_MODULE_NOT_REGISTERED = -17,
    CELLULAR_CTRL_SEC_SEAL_DEVICE_NOT_REGISTERED = -18,
    CELLULAR_CTRL_SEC_SEAL_DEVICE_NOT_ACTIVATED = -19,
    CELLULAR_CTRL_FORCE_32_BIT = 0x7FFFFFFF // Force this enum to be 32 bit
                                            // as it can be used as a size
                                            // also
} CellularCtrlErrorCode_t;

/** The possible radio access networks.
 */
typedef enum {
    CELLULAR_CTRL_RAN_UNKNOWN_OR_NOT_USED = 0,
    CELLULAR_CTRL_RAN_GERAN,
    CELLULAR_CTRL_RAN_UTRAN,
    CELLULAR_CTRL_RAN_EUTRAN,
    CELLULAR_CTRL_MAX_NUM_RANS
} CellularCtrlRan_t;

/** The possible radio access technologies.
 */
typedef enum {
    CELLULAR_CTRL_RAT_DUMMY = -1, //<! Added to ensure that the compiler
                                  //< treats values of this type as signed
                                  //< in case an error code is to be
                                  //< returned as this type.  Otherwise the
                                  //< enum could, in some cases, have an
                                  //< underlying type of unsigned and hence
                                  //< < 0 checks will always be false and you
                                  //< might not be warned of this.
    CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED = 0,
    CELLULAR_CTRL_RAT_GPRS,
    CELLULAR_CTRL_RAT_UMTS,
    CELLULAR_CTRL_RAT_LTE,
    CELLULAR_CTRL_RAT_CATM1,
    CELLULAR_CTRL_RAT_NB1,
    CELLULAR_CTRL_MAX_NUM_RATS
} CellularCtrlRat_t;

/** The current network status.  Note: if the values here
 * are changed make sure to change any .c file which may
 * implement based on the values.
 */
typedef enum {
    CELLULAR_CTRL_NETWORK_STATUS_DUMMY = -1, //<! Added to ensure that the
                                             //< compiler treats values of
                                             //< this type as signed in case
                                             //< an error code is to be
                                             //< returned as this type.
                                             //< Otherwise the enum could,
                                             //< in some cases, have an
                                             //< underlying type of unsigned
                                             //< and hence < 0 checks will
                                             //< always be false and you
                                             //< might not be warned of this.
    CELLULAR_CTRL_NETWORK_STATUS_UNKNOWN,
    CELLULAR_CTRL_NETWORK_STATUS_NOT_REGISTERED,
    CELLULAR_CTRL_NETWORK_STATUS_SEARCHING,
    CELLULAR_CTRL_NETWORK_STATUS_REGISTRATION_DENIED,
    CELLULAR_CTRL_NETWORK_STATUS_OUT_OF_COVERAGE,
    CELLULAR_CTRL_NETWORK_STATUS_EMERGENCY_ONLY,
    CELLULAR_CTRL_NETWORK_STATUS_REGISTERED,
    CELLULAR_CTRL_NETWORK_STATUS_TEMPORARY_NETWORK_BARRING,
    CELLULAR_CTRL_MAX_NUM_NETWORK_STATUS
} CellularCtrlNetworkStatus_t;

/* ----------------------------------------------------------------
 * FUNCTIONS
 * -------------------------------------------------------------- */

/** Initialise this cellular driver.  If the driver is already
 * initialised then this function returns immediately.
 *
 * @param pinEnablePower   the pin on that switches on the
 *                         power supply to the cellular module.
 *                         The sense of the pin should be such that
 *                         low means off and high means on.
 *                         Set to -1 if there is no such pin.
 * @param pinPwrOn          the pin that signals power-on to the
 *                         cellular module, i.e. the pin
 *                         that is connected to the module's PWR_ON pin.
 * @param pinVInt          the pin that can be monitored to detect
 *                         that the cellular module is powered up.
 *                         This pin should be connected to the
 *                         VInt pin of the module and is used to
 *                         make sure that the modem is truly off before
 *                         power to it is disabled.  Set to -1 if
 *                         there is no such pin.
 * @param leavePowerAlone  set this to true if initialisation should
 *                         not modify the state of pinEnablePower or 
 *                         pinPwrOn, else it will ensure that pinEnablePower
 *                         is low to disable power to the module and pinPwrOn
 *                         is high so that it can be pulled low to logically
 *                         power the module on.
 * @param uart             the UART number to use.  The uart must
 *                         already have been initialised.
 * @param queueUart        the event queue associated with the UART,
 *                         which will have been set up by the UART
 *                         initialisation function.
 * @return                 zero on success or negative error code on
 *                         failure.
 */
int32_t cellularCtrlInit(int32_t pinEnablePower,
                         int32_t pinPwrOn,
                         int32_t pinVInt,
                         bool leavePowerAlone,
                         int32_t uart,
                         CellularPortQueueHandle_t queueUart);

/** Shut-down this cellular driver.
 */
void cellularCtrlDeinit();

/** Determine if the cellular module has power.  This is done
 * by checking the level on the Enable Power pin controlling power
 * to the module.  If there is no such pin, or if the ctrl
 * module has not been initialised so that it knows about the
 * pin, then this will return true.
 *
 * @return true if power is enabled to the module, else false.
 */
bool cellularCtrlIsPowered();

/** Determine if the module is responsive.  It may happen that power
 * saving mode or some such gets out of sync, in which case this
 * can be called to see if the cellular module is responsive to
 * AT commands.
 *
 * @return true if the module is responsive, else false.
 */
bool cellularCtrlIsAlive();

/** Power the cellular module on.  If this function returns
 * success then the cellular module is ready to receive configuration
 * commands and register with the cellular network.  The caller
 * must have initialised at-client and called cellularInit()
 * before calling this function.
 *
 * @param pPin pointer to a string giving the PIN of the SIM.
 *             It is implementation dependent as to whether
 *             this can be non-NULL; if it is non-NULL and the
 *             implementation does not support PIN entry then
 *             an error code will be returned.
 * @return     zero on success or negative error code on failure.
 */
int32_t cellularCtrlPowerOn(const char *pPin);

/** Power the cellular module off.
 *
 * @param pKeepGoingCallback it is possible for power off to
 *                           take some time.  If this callback
 *                           function is non-NULL then it will
 *                           be called during the power-off
 *                           process and may be used to feed a
 *                           watchdog timer.  The callback
 *                           function should return true to
 *                           allow the power-off process to
 *                           be completed normally.  If the 
 *                           callback function returns false
 *                           then the power-off process will
 *                           be forced to completion immediately
 *                           and this function will return.
 *                           It is advisable for the callback
 *                           function to always return true,
 *                           allowing the cellular module to
 *                           power off cleanly.
 */
void cellularCtrlPowerOff(bool (*pKeepGoingCallback) (void));

/** Remove power to the cellular module using HW lines.
 *
 * @param trulyHard          if this is set to true and a
 *                           non-negative value for pinEnablePower
 *                           was supplied to cellularCtrlInit()
 *                           then just pull the power to the
 *                           cellular module.  ONLY USE IN
 *                           EMERGENCIES, IF THE CELLULAR MODULE
 *                           HAS BECOME COMPLETELY UNRESPONSIVE.
 *                           If a negative value for pinEnablePower
 *                           was supplied this value is treated
 *                           as false.
 * @param pKeepGoingCallback even with HW lines powering the
 *                           cellular module off it is possible
 *                           for power off to take some time.
 *                           If this callback function is
 *                           non-NULL then it will be called
 *                           during the power-off process and
 *                           may be used to feed a watchdog
 *                           timer.  The callback function
 *                           should return true to allow the
 *                           power-off process to be completed
 *                           normally.  If the callback function
 *                           returns false then the power-off process
 *                           will be forced to completion immediately
 *                           and this function will return.
 *                           It is advisable for the callback
 *                           function to always return true,
 *                           allowing the cellular module to
 *                           power off cleanly.
 *                           Ignored if trulyHard is true.
 */
void cellularCtrlHardPowerOff(bool trulyHard,
                              bool (*pKeepGoingCallback) (void));

/** Get the number of consecutive AT command timeouts. An excessive
 * number of these may mean that the cellular module has crashed.
 *
 * return the number of consecutive AT command timeouts
 */
int32_t cellularCtrlGetConsecutiveAtTimeouts();

/** Re-boot the cellular module.  The module will be reset with
 * a proper detach from the network and any NV parameters
 * will be saved.  If this function returns successfully
 * then the module is ready for immediate use, no call to
 * cellularPowerOn() is required (since the SIM is not reset).
 *
 * @return zero on success or negative error code on failure.
 */
int32_t cellularCtrlReboot();

/** Set the bands to be used by the cellular module.
 * The module must be powered on for this to work and the
 * module must be re-booted afterwards (with a call to
 * cellularReboot()) for it to take effect.
 *
 * @param rat       the radio access technology to obtain the
 *                  band mask for; only CELLULAR_CTRL_RAT_CATM1 and
 *                  CELLULAR_CTRL_RAT_NB1 are permitted.
 * @param bandMask1 the first band mask where bit 0 is band 1
 *                  and bit 63 is band 64.
 * @param bandMask2 the second band mask where bit 0 is band 65
 *                  and bit 63 is band 128.
 * @return          zero on success or negative error code
 *                  on failure.
 */
int32_t cellularCtrlSetBandMask(CellularCtrlRat_t rat,
                                uint64_t bandMask1,
                                uint64_t bandMask2);

/** Get the bands being used by the cellular module.
 * The module must be powered on for this to work.
 *
 * @param rat        the radio access technology to obtain the
 *                   band mask for.
 * @param pBandmask1 pointer to a place to store band mask 1,
 *                   where bit 0 is band 1 and bit 63 is band 64,
 *                   cannot be NULL.
 * @param pBandmask2 pointer to a place to store band mask 2,
 *                   where bit 0 is band 65 and bit 63 is
 *                   band 128, cannot be NULL.
 * @return           zero on succese else negative error code.
 */
int32_t cellularCtrlGetBandMask(CellularCtrlRat_t rat,
                                uint64_t *pBandMask1,
                                uint64_t *pBandMask2);

/** Set the sole radio access technology to be used by the
 * cellular module.  The module is set to use this radio
 * access technology alone and no other; use cellularSetRankRat()
 * if you want to use more than one radio access technology.
 * The module must be powered on for this to work and the
 * module must be re-booted afterwards (with a call to
 * cellularReboot()) for it to take effect.
 *
 * @param rat the radio access technology to use.
 * @return    zero on success or negative error code on failure.
 */
int32_t cellularCtrlSetRat(CellularCtrlRat_t rat);

/** Set the radio access technology to be used at the
 * given rank.  By using different ranks the module can
 * be made to support more than one radio access technology
 * at the same time but bare in mind that this can extend
 * the network search and registration time. The module must
 * be powered on for this to work and the module must be
 * re-booted afterwards (with a call to cellularReboot())
 * for it to take effect.  Setting the same RAT at two different
 * ranks will result in that RAT only being set in the higher
 * of the two ranks.  A rank may be set to
 * CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED in order to eliminate the
 * RAT at that rank but note that having no RATs will generate an
 * error and that the RATs of lower rank will be shuffled-up
 * so that there are no gaps.  In other words, with RATs at ranks
 * 0 = a and 1 = b setting the RAT at rank 0 to
 * CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED will result in 0 = b.
 *
 * @param rat  the radio access technology to use.
 * @param rank the rank at which to use the radio access technology,
 *             where 0 is the highest and the lowest is implementation
 *             dependent.
 * @return     zero on success or negative error code on failure.
 */
int32_t cellularCtrlSetRatRank(CellularCtrlRat_t rat,
                               int32_t rank);

/** Get the radio access technology that is being used by
 * the cellular module at the given rank.  Rank 0 will always
 * return a known radio access technology at all times while
 * higher ranks may return CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED.
 * As soon as CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED is returned
 * at a given rank all greater ranks can be assumed to be
 * CELLULAR_CTRL_RAT_UNKNOWN_OR_NOT_USED.
 *
 * @param rank the rank to check, where 0 is the highest and
 *             the lowest is implementation dependent.
 * @return     the radio access technology being used at that
 *             rank.
 */
int32_t cellularCtrlGetRat(int32_t rank);

/** Get the rank at which the given radio access technology
 * is being used by the cellular module.
 *
 * @param rat the radio access technology to find.
 * @return    the rank or negative error code if the radio
 *            access technology is not found in the ranked
 *            radio access technologies.
 */
int32_t cellularCtrlGetRatRank(CellularCtrlRat_t rat);

/** Set the MNO Profile use by the cellular module.  The module must
 * be powered on for this to work but must NOT be connected to the
 * cellular network (i.e. with a call to cellularDisconnect())
 * and the module must be re-booted afterwards (with a call to
 * cellularReboot()) for it to take effect.
 * IMPORTANT: the MNO profile is a kind of super-configuration,
 * which can change many things: the RAT, the bands, the APN,
 * etc.  So if you set an MNO profile you may wish to check what
 * it has done, in case you disagree with any of it.
 *
 * @param mnoProfile the MNO profile.
 * @return           zero on success or negative error code on
 *                   failure.
 */
int32_t cellularCtrlSetMnoProfile(int32_t mnoProfile);

/** Get the MNO Profile used by the cellular module.
 *
 * @return the MNO profile used by the module or negative error code
 *          on failure.
 */
int32_t cellularCtrlGetMnoProfile();

/** Register with the cellular network and obtain a PDP
 * context.
 *
 * @param pKeepGoingCallback a callback function that governs how
 *                           long registration will continue for.
 *                           This function is called while waiting
 *                           for registration to finish; registration
 *                           will only continue while it returns true.
 *                           This allows the caller to terminate
 *                           registration at their convenience.
 *                           This function may also be used to feed
 *                           any watchdog timer that might be running
 *                           during longer cat-M1/NB1 network search
 *                           periods.
 * @param pApn               pointer to a string giving the APN to
 *                           use; set to NULL if no APN is required
 *                           by the service provider.
 * @param pUsername          pointer to a string giving the user name
 *                           for PPP authentication; may be set to
 *                           NULL if no user name or password is
 *                           required.
 * @param pPassword          pointer to a string giving the password
 *                           for PPP authentication; ignored must be
 *                           non-NULL if pUsername is non-NULL.
 * @return                   zero on success or negative error code on
 *                           failure.
 */
int32_t cellularCtrlConnect(bool (*pKeepGoingCallback) (void),
                            const char *pApn, const char *pUsername,
                            const char *pPassword);

/** Disconnect the cellular module from the network.
 *
 * @return zero on success or negative error code on failure.
 */
int32_t cellularCtrlDisconnect();

/** Get the current network registration status on the given RAN.
 *
 * @param ran the Radio Access Network to check for registration status on.
 * @return    the current status or negative if there is an error determining
 *            the network status.
 */
CellularCtrlNetworkStatus_t cellularCtrlGetNetworkStatus(CellularCtrlRan_t ran);

/** Get the RAN for the given RAT.
 *
 * @param rat the Radio Access Technology.
 * @return    the corresponding Radio Access Network or negative error code.
 */
int32_t cellularCtrlGetRanForRat(CellularCtrlRat_t rat);

/** Get whether we are registered on an network.
 *
 * @return    true if registered, else false.
 */
bool cellularCtrlIsRegistered();

/** Return the RAT that is currently in use.
 *
 * @return the current RAT or -1 on failure (which means
 *         that the module is not registered on any RAT).
 */
int32_t cellularCtrlGetActiveRat();

/** Get the name of the operator on which the cellular module is
 * registered.  An error will be returned if the module is not
 * registered on the network at the time this is called.
 *
 * @param pStr    a pointer to size bytes of storage into which
 *                the operator name will be copied.  Room
 *                should be allowed for a NULL terminator, which
 *                will be added to terminate the string.  This
 *                pointer cannot be NULL.
 * @param size    the number of bytes available at pStr, including
 *                room for a NULL terminator. Must be greater
 *                than zero.
 * @return        on success, the number of characters copied into
 *                pStr NOT including the NULL terminator (i.e.
 *                as strlen() would return), on failure negative
 *                error code.
 */
int32_t cellularCtrlGetOperatorStr(char *pStr, size_t size);

/** Get the MCC/MNC of the network on which the cellular module is
 * registered.  An error will be returned if the module is not
 * registered on the network at the time this is called.
 *
 * @param pMcc    pointer to a place to store the MCC; cannot
 *                be NULL.
 * @param pMnc    pointer to a place to store the MNC; cannot
 *                be NULL.
 * @return        zero on success else negative error code.
 */
int32_t cellularCtrlGetMccMnc(int32_t *pMcc, int32_t *pMnc);

/** Return the IP address of the currently active connection.
 *
 * @param pStr    should point to storage of length at least 
 *                CELLULAR_CTRL_IP_ADDRESS_SIZE bytes in size.
 *                On return the IP address will be written to
 *                pStr as a string and a NULL terminator will
 *                be added.
 *                May be set to NULL for a simple test as to
 *                whether an IP address has been allocated or not.
 * @return        on success, the number of characters that would
 *                be copied into into pStr if it is not NULL,
 *                NOT including the terminator (i.e. as strlen()
 *                would return), on failure negative error code.
 */
int32_t cellularCtrlGetIpAddressStr(char *pStr);

/** Get the APN currently in use.
 *
 * @param pStr    a pointer to size bytes of storage into which
 *                the APN string will be copied.  Room should be
 *                allowed for a NULL terminator, which will be
 *                added to terminate the string.  This pointer
 *                cannot be NULL.
 * @param size    the number of bytes available at pStr, including
 *                room for a NULL terminator. Must be greater
 *                than zero.
 * @return        on success, the number of characters copied into
 *                pStr NOT including the NULL terminator (i.e.
 *                as strlen() would return), on failure negative
 *                error code.
 */
int32_t cellularCtrlGetApnStr(char *pStr, size_t size);

/** Refresh the RF status values.  Call this to refresh
 * RSSI, RSRP, RSRQ, Cell ID, EARFCN, etc.  This way all of the
 * values read are synchronised to a given point in time.  The
 * radio parameters stored by this function are cleared on
 * disconnect and reboot.
 *
 * @return  zero on success, negative error code on failure.
 */
int32_t cellularCtrlRefreshRadioParameters();

/** Get the RSSI that pertained after the last call to
 * cellularRefreshRadioParameters().  Note that RSSI may not
 * be available unless the module has successfully registered
 * with the cellular network.
 *
 * @return the RSSI in dBm, or zero if no RSSI measurement
 *         is currently available.
 */
int32_t cellularCtrlGetRssiDbm();

/** Get the RSRP that pertained after the last call to
 * cellularRefreshRadioParameters().  Note that RSRP may not
 * be available unless the module has successfully registered
 * with the cellular network.
 *
 * @return the RSRP in dBm, or zero if no RSRP measurement
 *         is currently available.
 */
int32_t cellularCtrlGetRsrpDbm();

/** Get the RSRQ that pertained after the last call to
 * cellularRefreshRadioParameters().  Note that RSRQ may not be
 * available unless the module has successfully registered with the
 * cellular network.
 *
 * @return the RSRQ in dB, or zero if no RSRQ measurement
 *         is currently available.
 */
int32_t cellularCtrlGetRsrqDb();

/** Get the RxQual that pertained after the last call to
 * cellularRefreshRadioParameters().  This is a number
 * from 0 to 7.  The number means different things for
 * different RATs, see the u-blox AT command manual or 
 * 3GPp specification 27.007 for detailed translation
 * tables.
 *
 * @return the RxQual, 0 to 7, or negative if no RxQual
 *         is available.
 */
int32_t cellularCtrlGetRxQual();

/** Get the SNR that pertained after the last call to
 * cellularRefreshRadioParameters(). Note that the format of
 * this call is different to that of cellularGetRssiDbm(),
 * cellularGetRsrpDbm() and cellularGetRsrqDbm() in that a pointer
 * must be passed in to obtain the result.  This is because
 * negative, positive and zero values for SNR are valid.  SNR
 * is RSRP / (RSSI - RSRP) and so if RSSI and RSRP are the same
 * a maximal integer value will be returned.
 * SNR may not be available unless the module has successfully
 * registered with the cellular network.
 *
 * @param pSnrDb a place to put the SNR measurement.  Must not
 *               be NULL.
 * @return       zero on success, negative error code on failure.
 */
int32_t cellularCtrlGetSnrDb(int32_t *pSnrDb);

/** Get the cell ID that pertained after the last call to
 * cellularRefreshRadioParameters().
 *
 * @return the cell ID, or -1 if the module is not
 *         registered with the cellular network.
 */
int32_t cellularCtrlGetCellId();

/** Get the EARFCN that pertained after the last call to
 * cellularRefreshRadioParameters().
 *
 * @return the EARFCN, or -1 if the module is not
 *         registered with the cellular network.
 */
int32_t cellularCtrlGetEarfcn();

/** Get the IMEI of the cellular module.
 *
 * @param pImei a pointer to CELLULAR_CTRL_IMEI_SIZE bytes
 *              of storage into which the IMEI will be
 *              copied; no NULL terminator is added as
 *              the IMEI is of fixed length. This pointer
 *              cannot be NULL.
 * @return      0 on success, negative error code on failure.
 */
int32_t cellularCtrlGetImei(char *pImei);

/** Get the IMSI of the SIM in the cellular module.
 *
 * @param pImsi a pointer to CELLULAR_CTRL_IMSI_SIZE bytes of
 *              storage into which the IMSI will be copied;
 *              no NULL terminator is added as the IMSI is
 *              of fixed length. This pointer cannot be NULL.
 * @return      0 on success, negative error code on failure.
 */
int32_t cellularCtrlGetImsi(char *pImsi);

/** Get the ICCID string of the SIM in the cellular module.  Note
 * that, while the ICCID is all numeric digits, like the IMEI and
 * the IMSI, the length of the ICCID can vary between 19 and 20
 * digits; it is treated as a string here because of that variable
 * length.
 *
 * @param pStr    a pointer to size bytes of storage into which
 *                the ICCID string will be copied.  Room
 *                should be allowed for a NULL terminator, which
 *                will be added to terminate the string.  This
 *                pointer cannot be NULL.
 * @param size    the number of bytes available at pStr,
 *                including room for a NULL terminator.
 *                Allocating CELLULAR_CTRL_ICCID_BUFFER_SIZE bytes of
 *                storage is safe.
 * @return        0 on success, negative error code on failure.
 */
int32_t cellularCtrlGetIccidStr(char *pStr, size_t size);

/** Get the manufacturer identification string from the cellular
 * module.
 *
 * @param pStr    a pointer to size bytes of storage into which
 *                the manufacturer string will be copied.  Room
 *                should be allowed for a NULL terminator, which
 *                will be added to terminate the string.  This
 *                pointer cannot be NULL.
 * @param size    the number of bytes available at pStr, including
 *                room for a NULL terminator. Must be greater
 *                than zero.
 * @return        on success, the number of characters copied into
 *                pStr NOT including the NULL terminator (i.e.
 *                as strlen() would return), on failure negative
 *                error code.
 */
int32_t cellularCtrlGetManufacturerStr(char *pStr, size_t size);

/** Get the model identification string from the cellular module.
 *
 * @param pStr    a pointer to size bytes of storage into which
 *                the model string will be copied.  Room should
 *                be allowed for a NULL terminator, which will be
 *                added to terminate the string.  This pointer
 *                cannot be NULL.
 * @param size    the number of bytes available at pStr, including
 *                room for a NULL terminator. Must be greater
 *                than zero.
 * @return        on success, the number of characters copied into
 *                pStr NOT including the NULL terminator (i.e.
 *                as strlen() would return), on failure negative
 *                error code.
 */
int32_t cellularCtrlGetModelStr(char *pStr, size_t size);

/** Get the firmware version string from the cellular module.
 *
 * @param pStr    a pointer to size bytes of storage into which
 *                the firmware version string will be copied.
 *                Room should be allowed for a NULL terminator,
 *                which will be added to terminate the string.
 *                This pointer cannot be NULL.
 * @param size    the number of bytes available at pStr, including
 *                room for a NULL terminator. Must be greater
 *                than zero.
 * @return        on success, the number of characters copied into
 *                pStr NOT including the NULL terminator (i.e.
 *                as strlen() would return), on failure negative
 *                error code.
 */
int32_t cellularCtrlGetFirmwareVersionStr(char *pStr, size_t size);

/** Get the UTC time according to cellular.  This feature requires
 * a connection to have been activated and support for this feature
 * is optional in the cellular network.
 *
 * @return  on success the Unix UTC time, else negative error code.
 */
int32_t cellularCtrlGetTimeUtc();

/** Request security sealing of a cellular module.  The cellular
 * module must have an active cellular connection (i.e.
 * cellularCtrlConnect() must have returned successfully) for
 * the sealing process to succeed.  Sealing may take some time,
 * hence pKeepGoingCallback is provided as a mean for the caller
 * to stop waiting for the outcome.
 * Can only be used once, will fail if the device is already security
 * sealed; use cellularCtrlGetSecuritySeal() to check the security seal
 * status.
 *
 * @param pDeviceInfoStr         the NULL terminated device
 *                               information string provided by
 *                               u-blox.
 * @param pDeviceSerialNumberStr the NULL terminated device serial
 *                               number string; you may chose what this
 *                               is, noting that there may be an upper_bound
 *                               length limit (e.g. 16 characters for 
 *                               SARA-R4/SARA-R5).
 * @param pKeepGoingCallback     a callback function that will be called
 *                               periodically while waiting for
 *                               security sealing to complete.  The
 *                               callback should return true to
 *                               continue waiting, else this function
 *                               will return.  Note that this does
 *                               not necessarily terminate the sealing
 *                               process: that may continue in the
 *                               background if there is a cellular
 *                               connection.  This callback function
 *                               may also be used to feed an
 *                               application's watchdog timer.
 *                               May be NULL, in which case this function
 *                               will not return until a succesful
 *                               security seal has been achieved.
 * @return                       zero on success, else negative
 *                               error code.
 */
int32_t cellularCtrlSetSecuritySeal(char *pDeviceInfoStr,
                                    char *pDeviceSerialNumberStr,
                                    bool (*pKeepGoingCallback) (void));

/** Get the overall security seal status of the cellular module.
 *
 * @return  zero if the cellular module has been successfully
 *          security sealed, else negative error code.
 */
int32_t cellularCtrlGetSecuritySeal();

/** Ask the cellular module to encrypt a block of data.  Force
 * this to work the cellular module must have previously been
 * security sealed.  Data encrypted in this way can be decrypted
 * on arrival at its destination by requesting the relevant
 * security keys from u-blox.
 *
 * @param pDataIn        a pointer to dataSizeBytes of data to be
 *                       encrypted, may be NULL, in which case this
 *                       function does nothing.
 * @param pDataOut       a pointer to a location dataSizeBytes in size
 *                       to store the encrypted data, can only be
 *                       NULL if pDataIn is NULL.
 * @param dataSizeBytes  the number of bytes of data to encrypt;
 *                       must be zero if pDataIn is NULL.
 * @return               on success the number of bytes encrypted
 *                       else negative error code.
 */
int32_t cellularSecurityEndToEndEncrypt(const void *pDataIn,
                                        void *pDataOut,
                                        size_t dataSizeBytes);

#ifdef __cplusplus
}
#endif

#endif // _CELLULAR_CTRL_H_

// End of file
