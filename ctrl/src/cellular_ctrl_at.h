/*
 * Copyright (c) 2017, Arm Limited and affiliates.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef _CELLULAR_CTRL_AT_H_
#define _CELLULAR_CTRL_AT_H_

/* No #includes allowed here */

/* This header file defines the cellular AT client API.  These functions
 * are thread-safe with the proviso that there can be only a single
 * UART in use at any one time.
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/** The default AT command time-out.
 */
#ifndef CELLULAR_CTRL_AT_COMMAND_DEFAULT_TIMEOUT_MS
# define CELLULAR_CTRL_AT_COMMAND_DEFAULT_TIMEOUT_MS 8000
#endif

/** The task priority for the URC handler.
 */
#ifndef CELLULAR_CTRL_AT_TASK_URC_HANDLER_PRIORITY
# define CELLULAR_CTRL_AT_TASK_URC_HANDLER_PRIORITY 12
#endif

/** The task priority for any callback made via
 * cellular_ctrl_at_urc_callback().
 */
#ifndef CELLULAR_CTRL_AT_TASK_URC_CALLBACK_PRIORITY
# ifdef CELLULAR_CTRL_CALLBACK_PRIORITY
#  if (CELLULAR_CTRL_CALLBACK_PRIORITY >= CELLULAR_CTRL_AT_TASK_URC_HANDLER_PRIORITY)
#   error CELLULAR_CTRL_CALLBACK_PRIORITY must be less than CELLULAR_CTRL_AT_TASK_URC_HANDLER_PRIORITY
#  else
#   define CELLULAR_CTRL_AT_TASK_URC_CALLBACK_PRIORITY CELLULAR_CTRL_CALLBACK_PRIORITY
#  endif
# else 
#  define CELLULAR_CTRL_AT_TASK_URC_CALLBACK_PRIORITY 15
# endif
#endif

/** The stack size of the task in the context of which the callbacks
 * of URCs will be run.  5 kbytes should be plenty of room.
 */
#ifndef CELLULAR_CTRL_AT_TASK_STACK_CALLBACK_SIZE_BYTES
# ifdef CELLULAR_CTRL_CALLBACK_STACK_SIZE_BYTES
#  define CELLULAR_CTRL_AT_TASK_STACK_CALLBACK_SIZE_BYTES CELLULAR_CTRL_CALLBACK_STACK_SIZE_BYTES
# else 
#  define CELLULAR_CTRL_AT_TASK_STACK_CALLBACK_SIZE_BYTES (1024 * 5)
# endif
#endif

/** The stack size for the OOB task.
 * Note: this size worst case for unoptimised compilation
 * (so that a debugger can be used sensibly) under the worst compiler.
 */
#ifndef CELLULAR_CTRL_OOB_TASK_STACK_CALLBACK_SIZE_BYTES
# define CELLULAR_CTRL_OOB_TASK_STACK_CALLBACK_SIZE_BYTES (1024 * 5)
#endif

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/** AT Error types enumeration */
typedef enum {
    CELLULAR_CTRL_AT_DEVICE_ERROR_TYPE_NO_ERROR = 0,
    CELLULAR_CTRL_AT_DEVICE_ERROR_TYPE_ERROR, // AT ERROR
    CELLULAR_CTRL_AT_DEVICE_ERROR_TYPE_CMS,   // AT ERROR CMS
    CELLULAR_CTRL_AT_DEVICE_ERROR_TYPE_CME    // AT ERROR CME
} cellular_ctrl_at_device_error_type_t;

/** AT response error with error code and type */
typedef struct {
    cellular_ctrl_at_device_error_type_t errType;
    int32_t errCode;
} cellular_ctrl_at_device_err_t ;

/** Error codes.
 */
typedef enum {
    CELLULAR_CTRL_AT_SUCCESS = 0,
    CELLULAR_CTRL_AT_UNKNOWN_ERROR = -1,
    CELLULAR_CTRL_AT_NOT_INITIALISED = -2,
    CELLULAR_CTRL_AT_NOT_IMPLEMENTED = -3,
    CELLULAR_CTRL_AT_INVALID_PARAMETER = -4,
    CELLULAR_CTRL_AT_OUT_OF_MEMORY = -5,
    CELLULAR_CTRL_AT_DEVICE_ERROR = -6
} cellular_ctrl_at_error_code_t;

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * FUNCTIONS
 * -------------------------------------------------------------- */

/** Initialise the cellular AT client.
 *
 * @param uart             the UART to use; this must have
 *                         already been configured by the caller.
 * @param queue_uart       the event queue associated with the UART,
 *                         which must have already been set up by
 *                         the caller.
 * @return                 zero on success, otherwise negative error
 *                         code.
 */
cellular_ctrl_at_error_code_t cellular_ctrl_at_init(int32_t uart,
                                                    CellularPortQueueHandle_t queue_uart);

/** Shut down the cellular AT client.
 */
void cellular_ctrl_at_deinit();

/** Get whether debug prints are on or off.
 *
 * @return  true if debug prints are on, else false.
 */
bool cellular_ctrl_at_debug_get();

/** Switch debug prints on or off.
 *
 * @param onNotOff  set to true to cause debug prints, else false.
 */
void cellular_ctrl_at_debug_set(bool onNotOff);

/** Set the URC callback for URC. If URC is found when parsing AT
 * responses, then call if called.  If URC is already set then it's
 * not set twice.
 * IMPORTANT: don't do anything heavy in a URC, e.g. don't printf() or,
 * at most, print a few characters; URCs have to run quickly as they
 * are interleaved with everything else.
 *
 * @param prefix          register URC prefix for callback. URC
 *                        could be for example "+CMTI: ".
 * @param callback        callback, which is called if URC is found in
 *                        AT response.
 * @param callback_param  parameter to be passed to the callback
 * @return                AT_SUCCESS or AT_NO_MEMORY if
 *                        no memory.
 */
cellular_ctrl_at_error_code_t
cellular_ctrl_at_set_urc_handler(const char *prefix, void (callback)(void *),
                                 void *callback_param);

/** Remove URC handler from linked list of URC's.
 *
 * @param prefix register URC prefix for callback. URC could be
 *               for example "+CMTI: "
 */
void cellular_ctrl_at_remove_urc_handler(const char *prefix);

/** Make a callback resulting from a URC.  This should be used
 * in preference to making a direct call to the callback as it
 * queues the callback neatly, allowing other URC handlers
 * to run without blocking the UART.  The callback is
 * executed in a task with
 * CELLULAR_CTRL_AT_TASK_STACK_CALLBACK_SIZE_BYTES of stack running
 * at priority CELLULAR_CTRL_AT_TASK_URC_CALLBACK_PRIORITY.
 *
 * @param callback        the callback function.
 * @param callback_param  the single callback parametrers.
 * @return                true on success, else false.
 */
bool cellular_ctrl_at_urc_callback(void (callback)(void *),
                                   void *callback_param);

/** returns the last error while parsing AT responses.
 *
 * @return last error.
 */
cellular_ctrl_at_error_code_t
cellular_ctrl_at_get_last_error();

/** return the last device error while parsing AT responses.
 *         Actually AT error (CME/CMS).
 *
 * @return last error struct at_device_err_t.
 */
cellular_ctrl_at_device_err_t
cellular_ctrl_at_get_last_device_error();

/** Lock the UART stream in order that the user
 * can control it and start the AT timeout running.
 */
void cellular_ctrl_at_lock();

/** Unlock the UART stream after at_lock_stream().
 */
void cellular_ctrl_at_unlock();

/** Unlock the UART stream and return the last error.
 *
 * @return last error that happened when parsing AT responses.
 * */
cellular_ctrl_at_error_code_t
cellular_ctrl_at_unlock_return_error();

/** Set timeout in milliseconds for AT commands.
 *
 * @param timeout_milliseconds  timeout in milliseconds.
 * @param default_timeout       store as default timeout.
 */
void cellular_ctrl_at_set_at_timeout(uint32_t timeout_milliseconds,
                                     bool default_timeout);

/** Set a callback that will be called when there has been
 * one or more consecutive timeouts.
 *
 * @param callback        the callback, which will take as
 *                        a parameter a single void * that is
 *                        a pointer to an int32_t giving
 *                        the number of consecutive timeouts.
 *                        Use NULL to cancel a previous callback.
 */
void cellular_ctrl_at_set_at_timeout_callback(void (callback)(void *));

/** Restore timeout to previous timeout. Handy if there is
 * a need to change timeout temporarily.
 */
void cellular_ctrl_at_restore_at_timeout();

/** Clear pending error flag. By default, error is cleared
 * only in at_lock().
 */
void cellular_ctrl_at_clear_error();

/** Flushes the underlying stream.
 */
void cellular_ctrl_at_flush();

/** Synchronize AT command and response handling to modem.
 * Note that this locks the UART stream all by itself, do
 * NOT call at_lock()/at_unlock() around it.
 * Also, if a synchronisation error should occur the
 * error code in the AT client is NOT cleared, it is up
 * to the user to clear it with a call to at_clear_error().
 *
 * @param timeout_ms ATHandler timeout when trying to sync.
 *                   Will be restored when function returns.
 * @return           true if synchronization was successful,
 *                   false in case of failure.
 */
bool cellular_ctrl_at_sync(int32_t timeout_ms);

/** Starts the command writing by clearing the last error and
 * writing the given command. In case of failure when writing,
 * the last error is set to AT_DEVICE_ERROR.
 *
 * @param cmd  AT command to be written to modem.
 */
void cellular_ctrl_at_cmd_start(const char *cmd);

/** Writes integer type AT command sub-parameter. Starts with
 * the delimiter if not the first param after cmd_start.
 * In case of failure when writing, the last error is set to
 * AT_DEVICE_ERROR.
 *
 * @param param integer to be written to modem as AT command
 *              sub-parameter.
 */
void cellular_ctrl_at_write_int(int32_t param);

/** Writes unsigned 64-bit integer type AT command sub-parameter.
 * Starts with the delimiter if not the first param after
 * cmd_start.* In case of failure when writing, the last error
 * is set to AT_DEVICE_ERROR.
 *
 * @param param uint64_t to be written to modem as AT command
 *              sub-parameter.
 */
void cellular_ctrl_at_write_uint64(uint64_t param);

/** Writes string type AT command sub-parameter. Quotes are
 * added to surround the given string.  Starts with the delimiter
 * if not the first param after cmd_start.  In case of failure
 * when writing, the last error is set to AT_DEVICE_ERROR.
 *
 * @param param         string to be written to modem as AT command
 *                      sub-parameter.
 * @param useQuotes     flag indicating whether the string
 *                      should be included in quotation marks.
 */
void cellular_ctrl_at_write_string(const char *param, bool useQuotes);

/** Stops the AT command by writing command-line terminator CR to
 * mark command as finished.
 */
void cellular_ctrl_at_cmd_stop();

/** Stops the AT command by writing command-line terminator CR to
 * mark command as finished and reads the OK/ERROR response.
 */
void cellular_ctrl_at_cmd_stop_read_resp();

/** Write bytes without any sub-parameter delimiters, such as comma.
 *  In case of failure when writing, the last error is set to
 *  AT_DEVICE_ERROR.
 *
 * @param data bytes to be written to modem.
 * @param len  length of data string.
 * @return     number of characters successfully written.
 */
size_t cellular_ctrl_at_write_bytes(const uint8_t *data, size_t len);

/** Sets the stop tag for the current scope (response/information
 * response/element).
 * Parameter's reading routines will stop the reading when such tag
 * is found and will set the found flag.
 * Consume routines will read everything until such tag is found.
 *
 * @param stop_tag_seq string to be set as stop tag.
 */
void cellular_ctrl_at_set_stop_tag(const char *stop_tag_seq);

/** Sets the delimiter between parameters or between elements of
 * the information response. Parameter's reading routines will stop
 * when such char is read.
 *
 * @param delimiter char to be set as _delimiter.
 */
void cellular_ctrl_at_set_delimiter(char delimiter);

/** Sets the delimiter to default value defined by AT_DEFAULT_DELIMITER.
 */
void cellular_ctrl_at_set_default_delimiter();

/** Defines behaviour for using or ignoring the delimiter within
 * an AT command.
 *
 * @param use_delimiter indicating if delimiter should be used or not.
 */
void cellular_ctrl_at_use_delimiter(bool use_delimiter);

/** Consumes the given length from the reading buffer.
 *
 * @param len   length to be consumed.
 * @param count number of times to consume the given length.
 */
void cellular_ctrl_at_skip_len(int32_t len, uint32_t count);

/** Consumes the given number of parameters from the reading buffer.
 *
 * @param count number of parameters to be skipped.
 */
void cellular_ctrl_at_skip_param(uint32_t count);

/** Reads given number of bytes from receiving buffer.  Delimiters
 * and stop-tags are obeyed if they are found outside quotes
 * so if you want to stop them getting in the way call
 * cellular_ctrl_at_set_delimiter(0) and
 * cellular_ctrl_at_set_stop_tag(NULL), before calling this.
 *
 * @param buf output buffer for the read.
 * @param len maximum number of bytes to read.
 * @return    number of successfully read bytes or -1 in
 *            case of error.
 */
int32_t cellular_ctrl_at_read_bytes(uint8_t *buf, size_t len);

/** Reads chars from reading buffer. Terminates with NULL. Skips
 * the quotation marks. Stops on delimiter or stop tag.
 *
 * @param str                output buffer for the read.
 * @param size               maximum number of chars to output
 *                           including NULL.
 * @param read_even_stop_tag if true then try to read even if
 *                           the stop tag was found previously;
 *                           set this to read a multi-line response.
 * @return                   length of output string (as in
 *                           the value that strlen() would
 *                           return) or -1 in case of read
 *                           timeout before delimiter or stop
 8                           tag is found.
 */
int32_t cellular_ctrl_at_read_string(char *str, size_t size,
                                     bool read_even_stop_tag);

/** Reads chars representing hex ascii values and converts them
 * to the corresponding chars.  For example: "4156" to "AV".
 * Terminates with null. Skips the quotation marks.
 * Stops on delimiter or stop tag.
 *
 * @param str  output buffer for the read.
 * @param size maximum number of chars to output.
 * @return     length of output string or -1 in case of read
 *             timeout before delimiter or stop tag is found.
 */
int32_t cellular_ctrl_at_read_hex_string(char *str, size_t size);

/** Reads as string and converts result to integer. Supports
 * only positive integers.
 *
 * @return the positive integer or -1 in case of error.
 */
int32_t cellular_ctrl_at_read_int();

/** Reads as string and converts result to uint64_t. Supports
 * only positive integers.
 *
 * @param uint64 a place to put the uint64_t.
 * @return       zero on success, -1 in case of error.
 */
int32_t cellular_ctrl_at_read_uint64(uint64_t *uint64);

/** This looks for necessary matches: prefix, OK, ERROR, URCs
 * and sets the correct scope.
 *
 * @param prefix string to be matched from receiving buffer.
 *               If not NULL and match succeeds, then scope
 *               will be set as information response(info_type).
 * @param stop   flag to indicate if we go to information
 *               response scope or not (needed when nothing is
 *               expected to be received anymore after the
 *               prefix match: SMS case: "> ", bc95 reboot case).
 */
void cellular_ctrl_at_resp_start(const char *prefix, bool stop);

/**  Ends all scopes starting from current scope.
 *   Consumes everything until the scope's stop tag is found,
 *   then goes to next scope until response scope is ending.
 *   Possible sequence:
 *   element scope -> information response scope -> response scope.
 */
void cellular_ctrl_at_resp_stop();

/** Looks for matching the prefix given to resp_start() call.
 * If needed, it ends the scope of a previous information
 * response. Sets the information response scope if new prefix is
 * found and response scope if prefix is not found.
 *
 * @return true if new information response is found, false
 *         otherwise.
 */
bool cellular_ctrl_at_info_resp();

/** Looks for matching the start tag.
 * If needed, it ends the scope of a previous element.
 * Sets the element scope if start tag is found and information
 * response scope if start tag is not found.
 *
 * @param start_tag tag to be matched to begin parsing an
 *                  element of an information response.
 * @return          true if new element is found, false
 *                  otherwise.
 */
bool cellular_ctrl_at_info_elem(char start_tag);

/** Consumes the received content until current stop tag
 * is found.
 *
 * @return true if stop tag is found, false otherwise.
 */
bool cellular_ctrl_at_consume_to_stop_tag();

/** Special case: wait for a single character to
 * arrive.  This can be used without starting
 * a command or response, it doesn't care.
 * The character is consumed.
 *
 * @param chr the character that is expected.
 * @return    true if the character that arrived
 *            matched chr, else false.
 */
bool cellular_ctrl_at_wait_char(char chr);

/** Return the last 3GPP error code.
 *
 * @return last 3GPP error code.
 */
int32_t cellular_ctrl_at_get_3gpp_error();

#endif // _CELLULAR_CTRL_AT_H_

// End of file
