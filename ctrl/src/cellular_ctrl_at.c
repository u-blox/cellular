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

// Define this to print AT strings out in full always
#define DEBUG_PRINT_FULL_AT_STRING

/* Only #includes of cellular_* are allowed here, no C lib,
 * no platform stuff and no OS stuff.  Anything required from
 * the platform/C library/OS must be brought in through
 * cellular_port* to maintain portability.
 */

// Note: no dependency here on HW or module type
#ifdef CELLULAR_CFG_OVERRIDE
# include "cellular_cfg_override.h" // For a customer's configuration override
#endif
#include "cellular_cfg_sw.h"
#include "cellular_cfg_os_platform_specific.h"
#include "cellular_port_clib.h"
#include "cellular_port.h"
#include "cellular_port_debug.h"
#include "cellular_port_os.h"
#include "cellular_port_gpio.h"
#include "cellular_port_uart.h"
#include "cellular_ctrl_at.h"

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

// URCs should be handled fast, if you add debug traces within URC
// processing then you also need to increase this time.
#define CELLULAR_CTRL_AT_URC_TIMEOUT_MS   100

// The minimum delay between the end of the last response and
// sending a new AT command.
#define CELLULAR_CTRL_AT_SEND_DELAY       25

// Suppress logging of very big packet payloads, maxlen is approximate
// due to write/read are cached.
#define CELLULAR_CTRL_AT_DEBUG_MAXLEN     80

// Big enough for the biggest thing that pops out without a read, which
// is a LWM2M read of a very large object, up to the limit of the AT
// interface
#define CELLULAR_CTRL_AT_BUFF_SIZE        1024

// A marker to check for buffer overruns
#define CELLULAR_CTRL_AT_MARKER           "DEADBEEF"

// Size of the marker: do NOT use sizeof(CELLULAR_CTRL_AT_MARKER), since
// we don't want the easy-peasy terminator in the marker string,
// we only want interesting stuff
#define CELLULAR_CTRL_AT_MARKER_SIZE      8

// A macro to check that the guard is present.
#define CELLULAR_CTRL_AT_GUARD_CHECK_ONE(marker) ((*((marker) + 0) == 'D') && \
                                                  (*((marker) + 1) == 'E') && \
                                                  (*((marker) + 2) == 'A') && \
                                                  (*((marker) + 3) == 'D') && \
                                                  (*((marker) + 4) == 'B') && \
                                                  (*((marker) + 5) == 'E') && \
                                                  (*((marker) + 6) == 'E') && \
                                                  (*((marker) + 7) == 'F') ? true : false)

// Macro to check buf
#define CELLULAR_CTRL_AT_GUARD_CHECK(buf_struct) (CELLULAR_CTRL_AT_GUARD_CHECK_ONE(buf_struct.mk0) && \
                                                  CELLULAR_CTRL_AT_GUARD_CHECK_ONE(buf_struct.mk1))

// Various well-defined strings.
#define CELLULAR_CTRL_AT_OK                      "OK\r\n"
#define CELLULAR_CTRL_AT_OK_LENGTH               4
#define CELLULAR_CTRL_AT_CRLF                    "\r\n"
#define CELLULAR_CTRL_AT_CRLF_LENGTH             2
#define CELLULAR_CTRL_AT_CME_ERROR               "+CME ERROR:"
#define CELLULAR_CTRL_AT_CME_ERROR_LENGTH        11
#define CELLULAR_CTRL_AT_CMS_ERROR               "+CMS ERROR:"
#define CELLULAR_CTRL_AT_CMS_ERROR_LENGTH        11
#define CELLULAR_CTRL_AT_ERROR_                  "ERROR\r\n"
#define CELLULAR_CTRL_AT_ERROR_LENGTH            7
#define CELLULAR_CTRL_AT_MAX_RESP_LENGTH         64
#define CELLULAR_CTRL_AT_OUTPUT_DELIMITER        "\r"
#define CELLULAR_CTRL_AT_OUTPUT_DELIMITER_LENGTH 1

// The default list delimiter on the AT interface.
#define CELLULAR_CTRL_AT_DEFAULT_DELIMITER ','

// The maximum length of the callback queue.  Each item in the queue
// will be sizeof(cellular_ctrl_at_callback_t) bytes big.
#define CELLULAR_CTRL_AT_CALLBACK_QUEUE_LENGTH 10

/** The stack size for the URC task.
 */
#ifndef CELLULAR_CTRL_AT_TASK_URC_STACK_SIZE_BYTES
# error CELLULAR_CTRL_AT_TASK_URC_STACK_SIZE_BYTES must be defined in cellular_cfg_os_platform_specific.h
#endif

/** The task priority for the URC handler.
 */
#ifndef CELLULAR_CTRL_AT_TASK_URC_PRIORITY
# error CELLULAR_CTRL_AT_TASK_URC_PRIORITY must be defined in cellular_cfg_os_platform_specific.h
#endif

/** The stack size of the task in the context of which callbacks
 * will be run.  5 kbytes should be plenty of room.
 */
#ifndef CELLULAR_CTRL_TASK_CALLBACK_STACK_SIZE_BYTES
# error CELLULAR_CTRL_TASK_CALLBACK_STACK_SIZE_BYTES must be defined in cellular_cfg_os_platform_specific.h
#endif

/** The task priority for any callback made via
 * cellular_ctrl_at_callback().
 */
#ifndef CELLULAR_CTRL_TASK_CALLBACK_PRIORITY
# error CELLULAR_CTRL_TASK_CALLBACK_PRIORITY must be defined in cellular_cfg_os_platform_specific.h
#endif

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

// Scope.
typedef enum {
    CELLULAR_CTRL_AT_SCOPE_TYPE_RESP, //<! the part of the response that doesn't include
                                      //<  the information response (+CMD1,+CMD2..)
                                      //<  ends with OK or (CME)(CMS)ERROR.
    CELLULAR_CTRL_AT_SCOPE_TYPE_INFO, //<! the information response part of the response,
                                      //<  starts with +CMD1 and ends with CELLULAR_CTRL_AT_CRLF
                                      //<  information response contains parameters or
                                      //<  subsets of parameters (elements), both separated
                                      //<  by comma.
    CELLULAR_CTRL_AT_SCOPE_TYPE_ELEM, //<! subsets of parameters that are part of information
                                      //< response, its parameters are separated by comma.
    CELLULAR_CTRL_AT_SCOPE_TYPE_NOT_SET
} cellular_ctrl_at_scope_type;

// Definition of a URC.
typedef struct cellular_ctrl_at_urc_t {
    const char *prefix;
    int prefix_len;
    void (*cb) (void *);
    void *cb_param;
    struct cellular_ctrl_at_urc_t *next;
} cellular_ctrl_at_urc_t;

// Definition of a tag.
typedef struct {
    char tag[7];
    size_t len;
    bool found;
} cellular_ctrl_at_tag_t;

// Definition of the receive buffer
typedef struct {
    char mk0[CELLULAR_CTRL_AT_MARKER_SIZE];
    char recv_buff[CELLULAR_CTRL_AT_BUFF_SIZE];
    char mk1[CELLULAR_CTRL_AT_MARKER_SIZE];
    // reading position
    size_t recv_len;
    // reading length
    size_t recv_pos;
} cellular_ctrl_at_buf_t;

// A struct defining a callback plus its optional parameter.
typedef struct {
    void(*function)(void  *);
    void *param;
} cellular_ctrl_at_callback_t;

/* ----------------------------------------------------------------
 * VARIABLES
 * -------------------------------------------------------------- */

static const uint8_t _map_3gpp_errors[][2] =  {
    { 103, 3 },  { 106, 6 },  { 107, 7 },  { 108, 8 },  { 111, 11 }, { 112, 12 }, { 113, 13 }, { 114, 14 },
    { 115, 15 }, { 122, 22 }, { 125, 25 }, { 172, 95 }, { 173, 96 }, { 174, 97 }, { 175, 99 }, { 176, 111 },
    { 177, 8 },  { 126, 26 }, { 127, 27 }, { 128, 28 }, { 129, 29 }, { 130, 30 }, { 131, 31 }, { 132, 32 },
    { 133, 33 }, { 134, 34 }, { 140, 40 }, { 141, 41 }, { 142, 42 }, { 143, 43 }, { 144, 44 }, { 145, 45 },
    { 146, 46 }, { 178, 65 }, { 179, 66 }, { 180, 48 }, { 181, 83 }, { 171, 49 },
};

// Mutex to control access to the UART stream.
static CellularPortMutexHandle_t _mtx_stream;

// Task buffer and task stack for the URC task.
static CellularPortTaskHandle_t _task_handle_urc;

// Mutex to determine whether the URC task is running.
static CellularPortMutexHandle_t _mtx_urc_task_running;

// Task buffer and task stack for call-backs.
static CellularPortTaskHandle_t _task_handle_callbacks;

// Mutex to determine whether the call-backs task is running.
static CellularPortMutexHandle_t _mtx_callbacks_task_running;

// Queue to feed the call-backs task.
static CellularPortQueueHandle_t _queue_callbacks;

// Queue to feed the URC task.
static CellularPortQueueHandle_t _queue_uart;

// The UART port to use, -1 means not initialised.
static int32_t _uart = -1;

static cellular_ctrl_at_error_code_t _last_error;
static int32_t _last_3gpp_error;
static cellular_ctrl_at_device_err_t  _last_at_error;
static uint16_t _urc_string_max_length;

// Linked-list anchor for URC handlers
static cellular_ctrl_at_urc_t *_urcs;

static uint32_t _at_timeout_ms;
static uint32_t _previous_at_timeout;
static int32_t _at_num_consecutive_timeouts;

static void(*_at_timeout_callback)(void *);

static uint32_t _at_send_delay_ms;
static int64_t _last_response_stop_ms;

// The buffer
static cellular_ctrl_at_buf_t _buf;

static cellular_ctrl_at_scope_type _current_scope;

// tag to stop response scope
static cellular_ctrl_at_tag_t _resp_stop;
// tag to stop information response scope
static cellular_ctrl_at_tag_t _info_stop;
// tag to stop element scope
static cellular_ctrl_at_tag_t _elem_stop;
// reference to the stop tag of current scope (resp/info/elem)
static cellular_ctrl_at_tag_t *_stop_tag;

// delimiter between parameters and also used for delimiting
// elements of information response
static char _delimiter;
// set true on prefix match -> indicates start of an information
// response or of an element
static bool _prefix_matched;
// set true on URC match
static bool _urc_matched;
// set true on (CME)(CMS)ERROR
static bool _error_found;
// Max length of OK,(CME)(CMS)ERROR and URCs
static size_t _max_resp_length;

// prefix set during resp_start and used to try matching
// possible information responses
static char _info_resp_prefix[CELLULAR_CTRL_AT_BUFF_SIZE];
static bool _cmd_start;
static bool _use_delimiter;

// time when a command or an URC processing was started
static int64_t _start_time_ms;

static bool _debug_on = false;

/* ----------------------------------------------------------------
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

int32_t hex_str_to_int(const char *hex_string, int32_t hex_string_length)
{
    const int32_t base = 16;
    int32_t character_as_integer, integer_output = 0;

    for (size_t i = 0; i < hex_string_length && hex_string[i] != '\0'; i++) {
        if (hex_string[i] >= '0' && hex_string[i] <= '9') {
            character_as_integer = hex_string[i] - '0';
        } else if (hex_string[i] >= 'A' && hex_string[i] <= 'F') {
            character_as_integer = hex_string[i] - 'A' + 10;
        } else {
            character_as_integer = hex_string[i] - 'a' + 10;
        }
        integer_output *= base;
        integer_output += character_as_integer;
    }

    return integer_output;
}

int32_t hex_str_to_char_str(const char *str, int32_t len, char *buf)
{
    int32_t str_count = 0;
    for (size_t i = 0; i + 1 < len; i += 2) {
        int32_t upper = hex_str_to_int(str + i, 1);
        int32_t lower = hex_str_to_int(str + i + 1, 1);
        buf[str_count] = ((upper << 4) & 0xF0) | (lower & 0x0F);
        str_count++;
    }

    return str_count;
}

// Copy content of one char buffer to another
// buffer and set NULL terminator.
static void set_string(char *dest, const char *src,
                       size_t src_len)
{
    pCellularPort_memcpy(dest, src, src_len);
    dest[src_len] = '\0';
}

// Find occurrence of one char buffer inside
// another char buffer.
static const char *mem_str(const char *dest,
                              size_t dest_len,
                              const char *src,
                              size_t src_len)
{
    if (dest_len >= src_len) {
        for (size_t i = 0; i < dest_len - src_len + 1; ++i) {
            if (cellularPort_memcmp(dest + i, src, src_len) == 0) {
                return dest + i;
            }
        }
    }

    return NULL;
}

// Local debug.
static void debug_print(const char *p, int len)
{
    if (_debug_on) {
        for (int32_t i = 0; i < len; i++) {
            char c = *p++;
            if (!cellularPort_isprint((int32_t) c)) {
                if (c == '\r') {
                    cellularPortLog("%c", '\n');
                } else if (c == '\n') {
                    // Do nothing
                } else {
                    cellularPortLog("[%d]", c);
                }
            } else {
                cellularPortLog("%c", c);
            }
        }
    }
}

// Set last error.
static void set_error(cellular_ctrl_at_error_code_t error)
{
    if (error != CELLULAR_CTRL_AT_SUCCESS) {
        cellularPortLog("CELLULAR_AT: AT error %d\n", error);
    }
    if (_last_error == CELLULAR_CTRL_AT_SUCCESS) {
        _last_error = error;
    }
}

// Sets to 0 the reading position, reading length and the whole
// buffer content.
static void reset_buffer()
{
    _buf.recv_pos = 0;
    _buf.recv_len = 0;
}

// Reading position set to 0 and buffer's unread content moved
// to beginning.
static void rewind_buffer()
{
    if (_buf.recv_pos > 0 && _buf.recv_len >= _buf.recv_pos) {
        _buf.recv_len -= _buf.recv_pos;
        // move what is not read to beginning of buffer
        pCellularPort_memmove(_buf.recv_buff, _buf.recv_buff + _buf.recv_pos, _buf.recv_len);
        _buf.recv_pos = 0;
    }
}

// Calculate remaining time for polling based on request start
// time and AT timeout.
// Returns 0 or time in ms for polling.
static int32_t poll_timeout(int32_t at_timeout)
{
    int64_t timeout;

    if (at_timeout >= 0) {
        // No need to worry about overflow here, we're never awake
        // for long enough
        int64_t now_ms = cellularPortGetTickTimeMs();
        if (now_ms >= _start_time_ms + at_timeout) {
            timeout = 0;
        } else if (_start_time_ms + at_timeout - now_ms > INT_MAX) {
            timeout = INT_MAX;
        } else {
            timeout = _start_time_ms + at_timeout - now_ms;
        }
    } else {
        timeout = 0;
    }

    return timeout;
}

// Reads from serial to receiving buffer.
// Returns true on successful read OR false on timeout.
static bool fill_buffer(bool wait_for_timeout)
{
    int32_t at_timeout = -1;

    if (wait_for_timeout) {
        at_timeout = _at_timeout_ms;
        if (cellularPortTaskIsThis(_task_handle_urc)) {
            at_timeout = CELLULAR_CTRL_AT_URC_TIMEOUT_MS;
        }
    }
    // Reset buffer when full
    if (sizeof(_buf.recv_buff) == _buf.recv_len) {
        cellularPortLog("CELLULAR_CTRL: !!! overflow.\n");
        debug_print((char *) (_buf.recv_buff), _buf.recv_len);
        reset_buffer();
    }

    while (poll_timeout(at_timeout) > 0) {
        int32_t len = cellularPortUartRead(_uart,
                                           _buf.recv_buff + _buf.recv_len,
                                           sizeof(_buf.recv_buff) - _buf.recv_len);
        if (len > 0) {
            debug_print((char *) (_buf.recv_buff) + _buf.recv_len, len);
            _buf.recv_len += len;
            return true;
        }
    }

    cellularPort_assert(CELLULAR_CTRL_AT_GUARD_CHECK(_buf));

    return false;
}

// Gets char from receiving buffer.
// Resets and fills the buffer if all are already read
// (receiving position equals receiving length).
// Returns a next char or -1 on failure (also sets error flag).
static int32_t get_char()
{
    cellular_ctrl_at_callback_t cb;

    if (_buf.recv_pos == _buf.recv_len) {
        reset_buffer(); // try to read as much as possible
        if (!fill_buffer(true)) {
            cellularPortLog("CELLULAR_AT: timeout.\n");
            _at_num_consecutive_timeouts++;
            if (_at_timeout_callback != NULL) {
                cb.function = _at_timeout_callback;
                cb.param = &_at_num_consecutive_timeouts;
                cellularPortQueueSend(_queue_callbacks, &cb);
            }
            set_error(CELLULAR_CTRL_AT_DEVICE_ERROR);
            return -1; // timeout to read
        } else {
            _at_num_consecutive_timeouts = 0;
        }
    }

    return _buf.recv_buff[_buf.recv_pos++];
}

static void set_tag(cellular_ctrl_at_tag_t *tag_dst, const char *tag_seq)
{
    if (tag_seq) {
        size_t tag_len = cellularPort_strlen(tag_seq);
        set_string(tag_dst->tag, tag_seq, tag_len);
        tag_dst->len = tag_len;
        tag_dst->found = false;
    } else {
        _stop_tag = NULL;
    }
}

// Checks if current char in buffer matches ch and
// consumes it, if no match leaves the buffer unchanged.
static bool consume_char(char ch)
{
    int32_t read_char = get_char();
    if (read_char == -1) {
        return false;
    }
    // If we read something else than ch, recover it
    if (read_char != ch) {
        _buf.recv_pos--;
        return false;
    }

    return true;
}

// Consumes the received content until tag is found.
// Consumes the tag only if consume_tag flag is true.
static bool consume_to_tag(const char *tag, bool consume_tag)
{
    size_t match_pos = 0;
    size_t tag_length = cellularPort_strlen(tag);

    while (true) {
        int32_t c = get_char();
        if (c == -1) {
            return false;
        }
        if (c == tag[match_pos]) {
            match_pos++;
        } else if (match_pos != 0) {
            match_pos = 0;
            if (c == tag[match_pos]) {
                match_pos++;
            }
        }
        if (match_pos == tag_length) {
            break;
        }
    }

    if (!consume_tag) {
        _buf.recv_pos -= tag_length;
    }

    return true;
}

// Set scope.
static void set_scope(cellular_ctrl_at_scope_type scope_type)
{
    if (_current_scope != scope_type) {
        _current_scope = scope_type;
        switch (_current_scope) {
            case CELLULAR_CTRL_AT_SCOPE_TYPE_RESP:
                _stop_tag = &_resp_stop;
                _stop_tag->found = false;
                break;
            case CELLULAR_CTRL_AT_SCOPE_TYPE_INFO:
                _stop_tag = &_info_stop;
                _stop_tag->found = false;
                consume_char(' ');
                break;
            case CELLULAR_CTRL_AT_SCOPE_TYPE_ELEM:
                _stop_tag = &_elem_stop;
                _stop_tag->found = false;
                break;
            case CELLULAR_CTRL_AT_SCOPE_TYPE_NOT_SET:
                _stop_tag = NULL;
                return;
            default:
                break;
        }
    }
}

static cellular_ctrl_at_scope_type get_scope()
{
    return _current_scope;
}

// Consumes to information response stop tag which is CELLULAR_CTRL_AT_CRLF.
// Sets scope to response.
static void information_response_stop()
{
    if (cellular_ctrl_at_consume_to_stop_tag()) {
        set_scope(CELLULAR_CTRL_AT_SCOPE_TYPE_RESP);
    }
}

// Consumes to element stop tag. Sets scope to
// information response.
static void information_response_element_stop()
{
    if (cellular_ctrl_at_consume_to_stop_tag()) {
        set_scope(CELLULAR_CTRL_AT_SCOPE_TYPE_INFO);
    }
}

// Rewinds the receiving buffer and compares it against given str.
static bool match(const char *str, size_t size)
{
    rewind_buffer();

    if ((_buf.recv_len - _buf.recv_pos) < size) {
        return false;
    }

    if (str &&
        (cellularPort_memcmp(_buf.recv_buff + _buf.recv_pos, str, size) == 0)) {
        // consume matching part
        _buf.recv_pos += size;
        return true;
    }

    return false;
}

// Iterates URCs and checks if they match the receiving
// buffer content. If URC match sets the scope to information
// response and after URC's cb returns finishes the information
// response scope (consumes to CELLULAR_CTRL_AT_CRLF).
static bool match_urc()
{
    rewind_buffer();
    size_t prefix_len = 0;
    for (cellular_ctrl_at_urc_t *urc = _urcs; urc; urc = urc->next) {
        prefix_len = urc->prefix_len;
        if (_buf.recv_len >= prefix_len) {
            if (match(urc->prefix, prefix_len)) {
                set_scope(CELLULAR_CTRL_AT_SCOPE_TYPE_INFO);
                int64_t now_ms = cellularPortGetTickTimeMs();
                if (urc->cb) {
                    urc->cb(urc->cb_param);
                }
                information_response_stop();
                // Add the amount of time spent in the URC
                // world to the start time
                _start_time_ms += cellularPortGetTickTimeMs() - now_ms;

                return true;
            }
        }
    }
    return false;
}

// Convert AT error code from CME/CMS ERROR responses
// to 3GPP error code.
static void set_3gpp_error(int32_t error,
                           cellular_ctrl_at_device_error_type_t error_type)
{
    if (_last_3gpp_error) { // don't overwrite likely root cause error
        return;
    }

    if ((error_type == CELLULAR_CTRL_AT_DEVICE_ERROR_TYPE_CMS) && (error < 128)) {
        // CMS errors 0-127 maps straight to 3GPP errors
        _last_3gpp_error = error;
    } else {
        for (size_t i = 0; i < sizeof(_map_3gpp_errors) /
                               sizeof(_map_3gpp_errors[0]); i++) {
            if (_map_3gpp_errors[i][0] == error) {
                _last_3gpp_error = _map_3gpp_errors[i][1];
                cellularPortLog("CELLULAR_AT: 3GPP error code %d.\n",
                                cellular_ctrl_at_get_3gpp_error());
                break;
            }
        }
    }
}

// Reads the error code if expected and sets it as
// last error.
static void at_error(bool error_code_expected,
                     cellular_ctrl_at_device_error_type_t error_type)
{
    if (error_code_expected &&
        (error_type == CELLULAR_CTRL_AT_DEVICE_ERROR_TYPE_CMS ||
         error_type == CELLULAR_CTRL_AT_DEVICE_ERROR_TYPE_CME)) {
        set_scope(CELLULAR_CTRL_AT_SCOPE_TYPE_INFO);
        int32_t error = cellular_ctrl_at_read_int();

        if (error != -1) {
            set_3gpp_error(error, error_type);
            _last_at_error.errCode = error;
            _last_at_error.errType = error_type;
            cellularPortLog("CELLULAR_AT: AT error code %d.\n", error);
        } else {
            cellularPortLog("CELLULAR_AT: ERROR reading failed\n");
        }
    }

    set_error(CELLULAR_CTRL_AT_DEVICE_ERROR);
}

// Checks if any of the error strings are matching the
// receiving buffer content.
static bool match_error()
{
    if (match(CELLULAR_CTRL_AT_CME_ERROR, CELLULAR_CTRL_AT_CME_ERROR_LENGTH)) {
        at_error(true, CELLULAR_CTRL_AT_DEVICE_ERROR_TYPE_CME);
        return true;
    } else if (match(CELLULAR_CTRL_AT_CMS_ERROR, CELLULAR_CTRL_AT_CMS_ERROR_LENGTH)) {
        at_error(true, CELLULAR_CTRL_AT_DEVICE_ERROR_TYPE_CMS);
        return true;
    } else if (match(CELLULAR_CTRL_AT_ERROR_, CELLULAR_CTRL_AT_ERROR_LENGTH)) {
        at_error(false, CELLULAR_CTRL_AT_DEVICE_ERROR_TYPE_NO_ERROR);
        return true;
    }
    return false;
}

// Checks if receiving buffer contains OK, ERROR,
// URC or given prefix.
static void resp(const char *prefix, bool crLfFirst, bool check_urc)
{
    _prefix_matched = false;
    _urc_matched = false;
    _error_found = false;

    while (cellular_ctrl_at_get_last_error() == CELLULAR_CTRL_AT_SUCCESS) {
        if (crLfFirst) {
            match(CELLULAR_CTRL_AT_CRLF, CELLULAR_CTRL_AT_CRLF_LENGTH);
        }

        if (match(CELLULAR_CTRL_AT_OK, CELLULAR_CTRL_AT_OK_LENGTH)) {
            set_scope(CELLULAR_CTRL_AT_SCOPE_TYPE_RESP);
            _stop_tag->found = true;
            return;
        }

        if (match_error()) {
            _error_found = true;
            return;
        }

        if (prefix && match(prefix, cellularPort_strlen(prefix))) {
            _prefix_matched = true;
            return;
        }

        if (check_urc && match_urc()) {
            _urc_matched = true;
            cellular_ctrl_at_clear_error();
            continue;
        }

        // If no match found, look for CELLULAR_CTRL_AT_CRLF and consume
        // everything up to and including CELLULAR_CTRL_AT_CRLF
        if (mem_str(_buf.recv_buff, _buf.recv_len, CELLULAR_CTRL_AT_CRLF,
                    CELLULAR_CTRL_AT_CRLF_LENGTH)) {
            // If no prefix, return on CELLULAR_CTRL_AT_CRLF - means data to read
            if (!prefix) {
                return;
            }
            consume_to_tag(CELLULAR_CTRL_AT_CRLF, true);
        } else {
            // If no prefix, no CELLULAR_CTRL_AT_CRLF and no more chance to
            // match for OK, ERROR or URC (since max resp
            // length is already in buffer) return so data
            // could be read
            if (!prefix &&
                ((_buf.recv_len - _buf.recv_pos) >= _max_resp_length)) {
                return;
            }
            if (!fill_buffer(true)) {
                // if we don't get any match and no data
                // within timeout, set an error to indicate
                // need for recovery
                set_error(CELLULAR_CTRL_AT_DEVICE_ERROR);
            }
        }
    }

    // something went wrong, application needs to
    // recover and retry
}

static size_t write(const void *data, size_t len)
{
    size_t write_len = 0;
    bool debug_on = _debug_on;

    for (; write_len < len;) {
        int32_t ret = cellularPortUartWrite(_uart,
                                            (const char *) data + write_len,
                                            len - write_len);
        if (ret < 0) {
            set_error(CELLULAR_CTRL_AT_DEVICE_ERROR);
            _debug_on = debug_on;
            return 0;
        }
#ifdef DEBUG_PRINT_FULL_AT_STRING
        debug_print((const char *) data + write_len, ret);
#else
        if (_debug_on && (write_len < CELLULAR_CTRL_AT_DEBUG_MAXLEN)) {
            if (write_len + ret < CELLULAR_CTRL_AT_DEBUG_MAXLEN) {
                debug_print((const char *) data + write_len, ret);
            } else {
                debug_print("...", sizeof("..."));
                _debug_on = false;
            }
        }
#endif
        write_len += (size_t) ret;
    }

    _debug_on = debug_on;

    return write_len;
}

// Do common checks before sending sub-parameters
static bool check_cmd_send()
{
    if ((_last_error != CELLULAR_CTRL_AT_SUCCESS)) {
        return false;
    }

    // Don't write delimiter if flag was set so
    if (!_use_delimiter) {
        return true;
    }

    // Don't write delimiter if this is the first sub-parameter
    if (_cmd_start) {
        _cmd_start = false;
    } else {
        if (write(&_delimiter, 1) != 1) {
            // Writing of delimiter failed, return.
            // write() will already have set _last_error
            return false;
        }
    }

    return true;
}

static bool find_urc_handler(const char *prefix)
{
    cellular_ctrl_at_urc_t *urc = _urcs;
    while (urc) {
        if (cellularPort_strcmp(prefix, urc->prefix) == 0) {
            return true;
        }
        urc = urc->next;
    }

    return false;
}

// Just unlock the UART stream, don't kick off
// any further data receipt.  This is used in
// task_urc to avoid recursion.
static void cellular_ctrl_at_unlock_no_data_check()
{
    cellularPortMutexUnlock(_mtx_stream);
}

// Convert a string which should contain
// something like "7587387289371387" (and
// be NULL terminated) into a uint64_t
// Any leading crap is ignored and conversion
// stops when a non-numeric character is reached.
static uint64_t strToUint64(char *buf)
{
    uint64_t uint64 = 0;
    int32_t length;
    const char *numerals = "0123456789";
    uint64_t multiplier;

    // Skip things that aren't numerals
    buf += cellularPort_strcspn(buf, numerals);
    // Determine the length of the numerals part
    length = cellularPort_strspn(buf, numerals);
    while (length > 0) {
        multiplier = (uint64_t) cellularPort_pow(10, (length - 1));
        uint64 += (*buf - '0') * multiplier;
        length--;
        buf++;
    }

    return uint64;
}

// Convert a uint64_t into a string,
// returning the length of string that
// would be required even if bufLen were
// too small (i.e. just like snprintf() would).
static int32_t uint64ToStr(char *buf, size_t bufLen,
                           uint64_t uint64)
{
    int32_t sizeOrError = -1;
    int32_t x;
    // Max value of a uint64_t is
    // 18,446,744,073,709,551,616,
    // so maximum divisor is
    // 10,000,000,00,000,000,000.
    uint64_t divisor = 10000000000000000000U;

    if (bufLen > 0) {
        sizeOrError = 0;
        // Cut the divisor down to size
        while (uint64 < divisor) {
            divisor /= 10;
        }
        if (divisor == 0) {
            divisor = 1;
        }

        // Reduce bufLen by 1 to allow for the terminator
        bufLen--;
        // Now write the numerals
        while (divisor > 0) {
            x = uint64 / divisor;
            if (bufLen > 0) {
                *buf = x + '0';
            }
            uint64 -= x * divisor;
            sizeOrError++;
            bufLen--;
            buf++;
            divisor /= 10;
        }
        // Add the terminator
        *buf = '\0';
    }

    return sizeOrError;
}

// Task to find urc's from the AT response, triggered through
// something being written to _queue_uart.
// If an invalid event (e.g. a negative size) is received, the
// task will exit in an orderly fashion.
static void task_urc(void *parameters)
{
    int32_t data_size_or_error = 0;

    CELLULAR_PORT_MUTEX_LOCK(_mtx_urc_task_running);

    (void) parameters;

    cellularPortLog("CELLULAR_AT: task_urc() started.\n");

    while (data_size_or_error >= 0) {
        data_size_or_error = cellularPortUartEventReceive(_queue_uart);
        if (data_size_or_error >= 0) {

            cellular_ctrl_at_lock();

            if (((data_size_or_error > 0) || (_buf.recv_pos < _buf.recv_len))) {
                if (_debug_on) {
                    cellularPortLog("CELLULAR_AT: OoB readable %d, already buffered %u.\n", data_size_or_error,
                                    _buf.recv_len - _buf.recv_pos);
                }
                _current_scope = CELLULAR_CTRL_AT_SCOPE_TYPE_NOT_SET;
                while (true) {
                    if (match_urc()) {
                        data_size_or_error = cellularPortUartGetReceiveSize(_uart);
                        if (!(data_size_or_error > 0) ||
                            (_buf.recv_pos < _buf.recv_len)) {
                            break; // we have nothing to read anymore
                        }
                    // If no match found, look for CELLULAR_CTRL_AT_CRLF and consume everything up to CELLULAR_CTRL_AT_CRLF
                    } else if (mem_str(_buf.recv_buff,
                                       _buf.recv_len, CELLULAR_CTRL_AT_CRLF, CELLULAR_CTRL_AT_CRLF_LENGTH)) {
                        consume_to_tag(CELLULAR_CTRL_AT_CRLF, true);
                    } else {
                        if (!fill_buffer(true)) {
                            reset_buffer(); // consume anything that could not be handled
                            break;
                        }
                        // No need to worry about overflow here, we're never awake
                        // for long enough
                        _start_time_ms = cellularPortGetTickTimeMs();
                    }
                }
                if (_debug_on) {
                    cellularPortLog("CELLULAR_AT: OoB done.\n");
                }
            }

            // Just unlock the UART stream without
            // checking for more data, which would try
            // to queue stuff on this task and I'm not
            // sure that's safe
            cellular_ctrl_at_unlock_no_data_check();
        }
    }

    CELLULAR_PORT_MUTEX_UNLOCK(_mtx_urc_task_running);

    // Delete ourself: only valid way out in Free RTOS
    cellularPortTaskDelete(NULL);

    cellularPortLog("CELLULAR_AT: task_urc() ended.\n");
}

// Dummy function that should never be called, just used in the
// implementation of taskCallbacks().
static void dummy(void *param) {
    (void) param;
    cellularPort_assert(false);
}

// Task in the context of which call-backs are called.
// If a callback structure is received with a NULL function
// pointer then this task exits in an orderly fashion.
static void task_callbacks(void *parameters)
{
    cellular_ctrl_at_callback_t cb;

    CELLULAR_PORT_MUTEX_LOCK(_mtx_callbacks_task_running);

    (void) parameters;

    cellularPortLog("CELLULAR_AT: task_callbacks() started.\n");

    cb.function = dummy;
    cb.param = NULL;

    while (cb.function != NULL) {
        if (cellularPortQueueReceive(_queue_callbacks, &cb) == 0) {
            if (cb.function != NULL) {
                cb.function(cb.param);
            }
        }
    }

    CELLULAR_PORT_MUTEX_UNLOCK(_mtx_callbacks_task_running);

    // Delete ourself
    cellularPortTaskDelete(NULL);

    cellularPortLog("CELLULAR_AT: task_callbacks() ended.\n");
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS
 * -------------------------------------------------------------- */

// Initialise the AT client.
cellular_ctrl_at_error_code_t cellular_ctrl_at_init(int32_t uart,
                                                    CellularPortQueueHandle_t queue_uart)
{
    if (_uart >= 0) {
        return CELLULAR_CTRL_AT_SUCCESS;
    }

    if (queue_uart == NULL) {
        return CELLULAR_CTRL_AT_INVALID_PARAMETER;
    }

    _queue_uart = queue_uart;
    _at_timeout_ms = CELLULAR_CTRL_AT_COMMAND_DEFAULT_TIMEOUT_MS;
    _at_timeout_callback = NULL;
    _at_num_consecutive_timeouts = 0;
    _at_send_delay_ms = CELLULAR_CTRL_AT_SEND_DELAY,
    _last_error = CELLULAR_CTRL_AT_SUCCESS;
    _last_3gpp_error = 0;
    _urc_string_max_length = 0;
    _urcs = NULL;
    _previous_at_timeout = _at_timeout_ms;
    _last_response_stop_ms = 0;
    _stop_tag = NULL;
    _delimiter = CELLULAR_CTRL_AT_DEFAULT_DELIMITER;
    _prefix_matched = false;
    _urc_matched = false;
    _error_found = false;
    _max_resp_length = CELLULAR_CTRL_AT_MAX_RESP_LENGTH;
    _debug_on = true;
    _cmd_start = false;
    _use_delimiter = true;
    _start_time_ms = 0;
    cellular_ctrl_at_clear_error();

    // Set up the buffer and it's protection markers
    reset_buffer();
    pCellularPort_memset(_buf.recv_buff, 0, sizeof(_buf.recv_buff));
    pCellularPort_memcpy(_buf.mk0, CELLULAR_CTRL_AT_MARKER,
                         CELLULAR_CTRL_AT_MARKER_SIZE);
    pCellularPort_memcpy(_buf.mk1, CELLULAR_CTRL_AT_MARKER,
                         CELLULAR_CTRL_AT_MARKER_SIZE);
    pCellularPort_memset(_info_resp_prefix, 0, sizeof(_info_resp_prefix));

    _current_scope = CELLULAR_CTRL_AT_SCOPE_TYPE_NOT_SET;
    set_tag(&_resp_stop, CELLULAR_CTRL_AT_OK);
    set_tag(&_info_stop, CELLULAR_CTRL_AT_CRLF);
    set_tag(&_elem_stop, ")");

    // Mutex protections for the data stream and the tasks
    if (cellularPortMutexCreate(&_mtx_stream) != 0) {
        return CELLULAR_CTRL_AT_OUT_OF_MEMORY;
    }
    if (cellularPortMutexCreate(&_mtx_urc_task_running) != 0) {
        cellularPortMutexDelete(_mtx_stream);
        return CELLULAR_CTRL_AT_OUT_OF_MEMORY;
    }
    if (cellularPortMutexCreate(&_mtx_callbacks_task_running) != 0) {
        cellularPortMutexDelete(_mtx_stream);
        cellularPortMutexDelete(_mtx_urc_task_running);
        return CELLULAR_CTRL_AT_OUT_OF_MEMORY;
    }

    // Start a queue to feed the callbacks task
    if (cellularPortQueueCreate(CELLULAR_CTRL_AT_CALLBACK_QUEUE_LENGTH,
                                sizeof(cellular_ctrl_at_callback_t),
                                &_queue_callbacks) != 0) {
        cellularPortMutexDelete(_mtx_stream);
        cellularPortMutexDelete(_mtx_urc_task_running);
        cellularPortMutexDelete(_mtx_callbacks_task_running);
        return CELLULAR_CTRL_AT_OUT_OF_MEMORY;
    }

    // Start a task to handle out of band responses
    if (cellularPortTaskCreate(task_urc, "at_task_urc",
                               CELLULAR_CTRL_AT_TASK_URC_STACK_SIZE_BYTES,
                               NULL,
                               CELLULAR_CTRL_AT_TASK_URC_PRIORITY,
                               &_task_handle_urc) != 0) {
        cellularPortMutexDelete(_mtx_stream);
        cellularPortMutexDelete(_mtx_urc_task_running);
        cellularPortMutexDelete(_mtx_callbacks_task_running);
        cellularPortMutexDelete(_queue_callbacks);
        return CELLULAR_CTRL_AT_OUT_OF_MEMORY;
    }

    // Pause here to allow the task creation that was
    // requested above to actually occur in the idle thread,
    // required by some RTOSs (e.g. FreeRTOS).
    cellularPortTaskBlock(100);

    // Start a task to run callbacks and a queue to feed it
    if (cellularPortTaskCreate(task_callbacks, "at_callbacks",
                               CELLULAR_CTRL_TASK_CALLBACK_STACK_SIZE_BYTES,
                               NULL,
                               CELLULAR_CTRL_TASK_CALLBACK_PRIORITY,
                               &_task_handle_callbacks) != 0) {
        // Get urc task to exit
        cellularPortUartEventSend(_queue_uart, -1);
        CELLULAR_PORT_MUTEX_LOCK(_mtx_urc_task_running);
        CELLULAR_PORT_MUTEX_UNLOCK(_mtx_urc_task_running);
        cellularPortMutexDelete(_mtx_stream);
        cellularPortMutexDelete(_mtx_urc_task_running);
        cellularPortMutexDelete(_mtx_callbacks_task_running);
        cellularPortMutexDelete(_queue_callbacks);
        // Pause here to allow the task deletion that was
        // requested above to actually occur in the idle thread,
        // required by some RTOSs (e.g. FreeRTOS)
        cellularPortTaskBlock(100);
        return CELLULAR_CTRL_AT_OUT_OF_MEMORY;
    }

    // Pause here to allow the task creation that was
    // requested above to actually occur in the idle thread,
    // required by some RTOSs (e.g. FreeRTOS)
    cellularPortTaskBlock(100);

    // Set _uart now that all is good
    _uart = uart;

    return CELLULAR_CTRL_AT_SUCCESS;
}

// Deinitialise the AT client.
void cellular_ctrl_at_deinit()
{
    cellular_ctrl_at_callback_t cb;

    if (_uart >= 0) {
        // The caller needs to make sure that no read/write
        // is in progress when this function is called.

        // Get urc task to exit
        cellularPortUartEventSend(_queue_uart, -1);
        CELLULAR_PORT_MUTEX_LOCK(_mtx_urc_task_running);
        CELLULAR_PORT_MUTEX_UNLOCK(_mtx_urc_task_running);

        // Get callbacks task to exit
        cb.function = NULL;
        cb.param = NULL;
        cellularPortQueueSend(_queue_callbacks, (void *) &cb);
        CELLULAR_PORT_MUTEX_LOCK(_mtx_callbacks_task_running);
        CELLULAR_PORT_MUTEX_UNLOCK(_mtx_callbacks_task_running);

         // Free memory
        while (_urcs) {
            cellular_ctrl_at_urc_t *urc = _urcs;
            _urcs = urc->next;
            cellularPort_free(urc);
        }

        // Tidy up
        cellularPortMutexDelete(_mtx_stream);
        cellularPortMutexDelete(_mtx_urc_task_running);
        cellularPortMutexDelete(_mtx_callbacks_task_running);
        cellularPortQueueDelete(_queue_callbacks);
        cellularPort_assert(CELLULAR_CTRL_AT_GUARD_CHECK(_buf));

        // Pause here to allow the tidy-up to occur in the idle thread,
        // required by some RTOSs (e.g. FreeRTOS).
        cellularPortTaskBlock(100);

        _uart = -1;
    }
}

bool cellular_ctrl_at_debug_get()
{
    return _debug_on;
}

void cellular_ctrl_at_debug_set(bool onNotOff)
{
    _debug_on = onNotOff;
}

cellular_ctrl_at_error_code_t cellular_ctrl_at_set_urc_handler(const char *prefix,
                                                               void (callback) (void *),
                                                               void *callback_param)
{
    if (_uart < 0) {
        return  CELLULAR_CTRL_AT_NOT_INITIALISED;
    } else {
        if (find_urc_handler(prefix)) {
            cellularPortLog("CELLULAR_AT: URC already added with prefix \"%s\".\n", prefix);
            return CELLULAR_CTRL_AT_SUCCESS;
        }

        cellular_ctrl_at_urc_t *urc = (cellular_ctrl_at_urc_t *) pCellularPort_malloc(sizeof(cellular_ctrl_at_urc_t));
        if (!urc) {
            return CELLULAR_CTRL_AT_OUT_OF_MEMORY;
        } else {
            size_t prefix_len = cellularPort_strlen(prefix);
            if (prefix_len > _urc_string_max_length) {
                _urc_string_max_length = prefix_len;
                if (_urc_string_max_length > _max_resp_length) {
                    _max_resp_length = _urc_string_max_length;
                }
            }

            urc->prefix = prefix;
            urc->prefix_len = prefix_len;
            urc->cb = callback;
            urc->cb_param = callback_param;
            urc->next = _urcs;
            _urcs = urc;
        }
    }

    return CELLULAR_CTRL_AT_SUCCESS;
}

void cellular_ctrl_at_remove_urc_handler(const char *prefix)
{
    cellular_ctrl_at_urc_t *current = _urcs;
    cellular_ctrl_at_urc_t *prev = NULL;

    if (_uart >= 0) {
        while (current) {
            if (cellularPort_strcmp(prefix, current->prefix) == 0) {
                if (prev) {
                    prev->next = current->next;
                } else {
                    _urcs = current->next;
                }
                cellularPort_free(current);
                break;
            }
            prev = current;
            current = prev->next;
        }
    }
}

// Make a callback resulting from a URC
bool cellular_ctrl_at_callback(void (callback)(void *),
                                   void *callback_param)
{
    cellular_ctrl_at_callback_t cb;

    cb.function = callback;
    cb.param = callback_param;

    return cellularPortQueueSend(_queue_callbacks, &cb) == 0;
}

// Lock the UART stream.
void cellular_ctrl_at_lock()
{
    if (_uart >= 0) {
        cellularPortMutexLock(_mtx_stream);
        cellular_ctrl_at_clear_error();
        // No need to worry about overflow here, we're never awake
        // for long enough
        _start_time_ms = cellularPortGetTickTimeMs();
    }
}

// Unlock the UART stream and kick off a receive
// if one was lounging around.
void cellular_ctrl_at_unlock()
{
    int32_t sizeBytes = 0;

    if (_uart >= 0) {
        cellular_ctrl_at_unlock_no_data_check();
        sizeBytes = cellularPortUartGetReceiveSize(_uart);
        if ((sizeBytes > 0) ||
            (_buf.recv_pos < _buf.recv_len)) {
            cellularPortUartEventSend(_queue_uart, sizeBytes);
        }
        cellularPort_assert(CELLULAR_CTRL_AT_GUARD_CHECK(_buf));
    }
}

// Unlock the UART stream and return the last error.
cellular_ctrl_at_error_code_t cellular_ctrl_at_unlock_return_error()
{
    cellular_ctrl_at_error_code_t error = _last_error;
    cellular_ctrl_at_unlock();
    return error;
}

void cellular_ctrl_at_set_at_timeout(uint32_t timeout_milliseconds,
                                     bool default_timeout)
{
    if (_uart >= 0) {
        if (default_timeout) {
            _previous_at_timeout = timeout_milliseconds;
            _at_timeout_ms = timeout_milliseconds;
        } else if (timeout_milliseconds != _at_timeout_ms) {
            _previous_at_timeout = _at_timeout_ms;
            _at_timeout_ms = timeout_milliseconds;
        }
    }
}

void cellular_ctrl_at_set_at_timeout_callback(void (callback)(void *))
{
    if (_uart >= 0) {
        _at_timeout_callback = callback;
    }
}


void cellular_ctrl_at_restore_at_timeout()
{
    if (_uart >= 0) {
        if (_previous_at_timeout != _at_timeout_ms) {
            _at_timeout_ms = _previous_at_timeout;
        }
    }
}

void cellular_ctrl_at_skip_len(int32_t len, uint32_t count)
{
    if (_uart >= 0) {
        if ((_last_error != CELLULAR_CTRL_AT_SUCCESS) ||
            !_stop_tag || _stop_tag->found) {
            return;
        }

        for (uint32_t i = 0; i < count; i++) {
            int32_t read_len = 0;
            while (read_len < len) {
                int32_t c = get_char();
                if (c == -1) {
                    set_error(CELLULAR_CTRL_AT_DEVICE_ERROR);
                    return;
                }
                read_len++;
            }
        }
    }
}

void cellular_ctrl_at_skip_param(uint32_t count)
{
    if (_uart >= 0) {
        if ((_last_error != CELLULAR_CTRL_AT_SUCCESS) ||
            !_stop_tag || _stop_tag->found) {
            return;
        }

        for (uint32_t i = 0; (i < count) && !_stop_tag->found; i++) {
            size_t match_pos = 0;
            while (true) {
                int c = get_char();
                if (c == -1) {
                    set_error(CELLULAR_CTRL_AT_DEVICE_ERROR);
                    return;
                } else if (c == _delimiter) {
                    break;
                } else if (_stop_tag->len && c == _stop_tag->tag[match_pos]) {
                    match_pos++;
                    if (match_pos == _stop_tag->len) {
                        _stop_tag->found = true;
                        break;
                    }
                } else if (match_pos) {
                    match_pos = 0;
                }
            }
        }
    }
}

int32_t cellular_ctrl_at_read_bytes(uint8_t *buf, size_t len)
{
    size_t read_len = 0;
    size_t match_pos = 0;

    if ((_uart < 0) || (_last_error != CELLULAR_CTRL_AT_SUCCESS) ||
        (_stop_tag && _stop_tag->found)) {
        return -1;
    }

    bool debug_on = _debug_on;
    for (; read_len < (len + match_pos); read_len++) {
        int32_t c = get_char();
        if (c == -1) {
            set_error(CELLULAR_CTRL_AT_DEVICE_ERROR);
            _debug_on = debug_on;
            return -1;
        } else if (_stop_tag &&
                   _stop_tag->len &&
                   (c == _stop_tag->tag[match_pos])) {
            match_pos++;
            if (match_pos == _stop_tag->len) {
                _stop_tag->found = true;
                // remove tag from string if it was matched
                read_len -= _stop_tag->len - 1;
                break;
            }
        } else if (match_pos) {
            match_pos = 0;
        }
        buf[read_len] = c;
#ifndef DEBUG_PRINT_FULL_AT_STRING
        if (_debug_on && (read_len >= CELLULAR_CTRL_AT_DEBUG_MAXLEN)) {
            debug_print("...", sizeof("..."));
            _debug_on = false;
        }
#endif
    }

    _debug_on = debug_on;
    return read_len;
}

int32_t cellular_ctrl_at_read_string(char *buf, size_t size,
                                     bool read_even_stop_tag)
{
    if ((_uart < 0) || (_last_error != CELLULAR_CTRL_AT_SUCCESS) ||
        !_stop_tag || (_stop_tag->found &&
                       (read_even_stop_tag == false))) {
        return -1;
    }

    uint32_t len = 0;
    size_t match_pos = 0;
    bool delimiter_found = false;
    bool in_quotes = false;

    for (; len < (size - 1 + match_pos); len++) {
        int32_t c = get_char();
        if (c == -1) {
            set_error(CELLULAR_CTRL_AT_DEVICE_ERROR);
            return -1;
        } else if (!in_quotes && (c == _delimiter)) {
            buf[len] = '\0';
            delimiter_found = true;
            break;
        } else if (c == '\"') {
            match_pos = 0;
            len--;
            in_quotes = !in_quotes;
            continue;
        } else if (!in_quotes &&
                   _stop_tag->len &&
                   (c == _stop_tag->tag[match_pos])) {
            match_pos++;
            if (match_pos == _stop_tag->len) {
                _stop_tag->found = true;
                // remove tag from string if it was matched
                len -= _stop_tag->len - 1;
                buf[len] = '\0';
                break;
            }
        } else if (match_pos) {
            match_pos = 0;
        }

        buf[len] = c;
    }

    if (len && (len == size - 1 + match_pos)) {
        buf[len] = '\0';
    }

    // Consume to delimiter or stop_tag
    if (!delimiter_found && !_stop_tag->found) {
        // Note:  match_pos was being reset to zero here but
        // that means that if half of the tag was matched in the
        // for() loop above it will be missed here
        while (1) {
            int32_t c = get_char();
            if (c == -1) {
                set_error(CELLULAR_CTRL_AT_DEVICE_ERROR);
                break;
            } else if (c == _delimiter) {
                break;
            } else if (_stop_tag->len &&
                       (c == _stop_tag->tag[match_pos])) {
                match_pos++;
                if (match_pos == _stop_tag->len) {
                    _stop_tag->found = true;
                    break;
                }
            }
        }
    }

    return len;
}

int32_t cellular_ctrl_at_read_hex_string(char *buf, size_t size)
{
    if ((_uart < 0) || (_last_error != CELLULAR_CTRL_AT_SUCCESS) || !_stop_tag || _stop_tag->found) {
        return -1;
    }

    size_t match_pos = 0;

    consume_char('\"');

    if ((_last_error != CELLULAR_CTRL_AT_SUCCESS)) {
        return -1;
    }

    size_t read_idx = 0;
    size_t buf_idx = 0;
    char hexbuf[2];

    for (; read_idx < size * 2 + match_pos; read_idx++) {
        int32_t c = get_char();

        if (match_pos) {
            buf_idx++;
        } else {
            buf_idx = read_idx / 2;
        }

        if (c == -1) {
            set_error(CELLULAR_CTRL_AT_DEVICE_ERROR);
            return -1;
        }
        if (c == _delimiter) {
            break;
        } else if (c == '\"') {
            match_pos = 0;
            read_idx--;
            continue;
        } else if (_stop_tag->len &&
                   (c == _stop_tag->tag[match_pos])) {
            match_pos++;
            if (match_pos == _stop_tag->len) {
                _stop_tag->found = true;
                // remove tag from string if it was matched
                buf_idx -= _stop_tag->len - 1;
                break;
            }
        } else if (match_pos) {
            match_pos = 0;
        }

        if (match_pos) {
            buf[buf_idx] = c;
        } else {
            hexbuf[read_idx % 2] = c;
            if (read_idx % 2 == 1) {
                hex_str_to_char_str(hexbuf, 2, buf + buf_idx);
            }
        }
    }

    if (read_idx && (read_idx == size * 2 + match_pos)) {
        buf_idx++;
    }

    return buf_idx;
}

int32_t cellular_ctrl_at_read_int()
{
    if ((_uart < 0) || (_last_error != CELLULAR_CTRL_AT_SUCCESS) || !_stop_tag || _stop_tag->found) {
        return -1;
    }

    char buff[32]; // enough for an integer
    char *first_no_digit;

    if (cellular_ctrl_at_read_string(buff,
                              (size_t) sizeof(buff),
                              false) == 0) {
        return -1;
    }

    return cellularPort_strtol(buff, &first_no_digit, 10);
}

int32_t cellular_ctrl_at_read_uint64(uint64_t *uint64)
{
    if ((_uart < 0) || (_last_error != CELLULAR_CTRL_AT_SUCCESS) || !_stop_tag || _stop_tag->found) {
        return -1;
    }

    char buff[32]; // enough for an integer

    if (cellular_ctrl_at_read_string(buff,
                                     (size_t) sizeof(buff),
                                     false) == 0) {
        return -1;
    } else {
        // Would use sscanf() here but we cannot
        // rely on there being 64 bit sscanf() support
        // in the underlying library, hence
        // we do our own thing
        *uint64 = strToUint64(buff);
    }

    return 0;
}

void cellular_ctrl_at_set_delimiter(char delimiter)
{
    _delimiter = delimiter;
}

void cellular_ctrl_at_set_default_delimiter()
{
    _delimiter = CELLULAR_CTRL_AT_DEFAULT_DELIMITER;
}

void cellular_ctrl_at_use_delimiter(bool use_delimiter)
{
    _use_delimiter = use_delimiter;
}

void cellular_ctrl_at_set_stop_tag(const char *stop_tag_seq)
{
    if ((_uart < 0) || (_last_error != CELLULAR_CTRL_AT_SUCCESS) ||
        !_stop_tag) {
        return;
    }

    set_tag(_stop_tag, stop_tag_seq);
}

void cellular_ctrl_at_clear_error()
{
    if (_uart >= 0) {
        _last_error = CELLULAR_CTRL_AT_SUCCESS;
        _last_at_error.errCode = 0;
        _last_at_error.errType = CELLULAR_CTRL_AT_DEVICE_ERROR_TYPE_NO_ERROR;
        _last_3gpp_error = 0;
    }
}

cellular_ctrl_at_error_code_t cellular_ctrl_at_get_last_error()
{
    return _last_error;
}

cellular_ctrl_at_device_err_t cellular_ctrl_at_get_last_device_error()
{
    return _last_at_error;
}

int32_t cellular_ctrl_at_get_3gpp_error()
{
    return _last_3gpp_error;
}

void cellular_ctrl_at_resp_start(const char *prefix, bool stop)
{
    if ((_uart < 0) || (_last_error != CELLULAR_CTRL_AT_SUCCESS)) {
        return;
    }

    set_scope(CELLULAR_CTRL_AT_SCOPE_TYPE_NOT_SET);
    // Try get as much data as possible
    rewind_buffer();
    (void) fill_buffer(false);

    if (prefix) {
        cellularPort_assert(cellularPort_strlen(prefix) < CELLULAR_CTRL_AT_BUFF_SIZE);
        // copy prefix so we can later use it without having
        // to provide again for info_resp
        pCellularPort_strcpy(_info_resp_prefix, prefix);
    }

    set_scope(CELLULAR_CTRL_AT_SCOPE_TYPE_RESP);

    resp(prefix, true, true);

    if (!stop && prefix && _prefix_matched) {
        set_scope(CELLULAR_CTRL_AT_SCOPE_TYPE_INFO);
    }
}

// Check URC because of error as URC
bool cellular_ctrl_at_info_resp()
{
    if ((_uart < 0) || (_last_error != CELLULAR_CTRL_AT_SUCCESS) ||
        _resp_stop.found) {
        return false;
    }

    if (_prefix_matched) {
        _prefix_matched = false;
        return true;
    }

    // If coming here after another info response was
    // started (looping), stop the previous one.
    // Trying to handle stopping in this level instead
    // of doing it in upper level.
    if (get_scope() == CELLULAR_CTRL_AT_SCOPE_TYPE_INFO) {
        information_response_stop();
    }

    resp(_info_resp_prefix, true, false);

    if (_prefix_matched) {
        set_scope(CELLULAR_CTRL_AT_SCOPE_TYPE_INFO);
        _prefix_matched = false;
        return true;
    }

    // On mismatch go to response scope
    set_scope(CELLULAR_CTRL_AT_SCOPE_TYPE_RESP);

    return false;
}

bool cellular_ctrl_at_info_elem(char start_tag)
{
    if ((_uart < 0) || (_last_error != CELLULAR_CTRL_AT_SUCCESS)) {
        return false;
    }

    // If coming here after another info response
    // element was started (looping), stop the
    // previous one.
    // Trying to handle stopping in this level
    // instead of doing it in upper level.
    if (get_scope() == CELLULAR_CTRL_AT_SCOPE_TYPE_ELEM) {
        information_response_element_stop();
    }

    consume_char(_delimiter);

    if (consume_char(start_tag)) {
        _prefix_matched = true;
        set_scope(CELLULAR_CTRL_AT_SCOPE_TYPE_ELEM);
        return true;
    }

    // On mismatch go to information response scope
    set_scope(CELLULAR_CTRL_AT_SCOPE_TYPE_INFO);

    return false;
}

bool cellular_ctrl_at_consume_to_stop_tag()
{
    if ((_uart < 0) || !_stop_tag ||
        (_stop_tag && _stop_tag->found) || _error_found) {
        return true;
    }

    if (consume_to_tag((const char *) _stop_tag->tag, true)) {
        return true;
    }

    cellularPortLog("CELLULAR_AT: stop tag not found.\n");

    set_error(CELLULAR_CTRL_AT_DEVICE_ERROR);

    return false;
}

void cellular_ctrl_at_resp_stop()
{
    if (_uart >= 0) {
        // Do not return on error so that we
        // can consume whatever there is in the buffer
        if (_current_scope == CELLULAR_CTRL_AT_SCOPE_TYPE_ELEM) {
            information_response_element_stop();
            set_scope(CELLULAR_CTRL_AT_SCOPE_TYPE_INFO);
        }

        if (_current_scope == CELLULAR_CTRL_AT_SCOPE_TYPE_INFO) {
            information_response_stop();
        }

        // Go for response stop_tag
        if (cellular_ctrl_at_consume_to_stop_tag()) {
            set_scope(CELLULAR_CTRL_AT_SCOPE_TYPE_NOT_SET);
        }

        // Restore stop tag to OK
        set_tag(&_resp_stop, CELLULAR_CTRL_AT_OK);
        // Reset info resp prefix
        pCellularPort_memset(_info_resp_prefix, 0, sizeof(_info_resp_prefix));

        // No need to worry about overflow here, we're never awake
        // for long enough
        _last_response_stop_ms = cellularPortGetTickTimeMs();
    }
}

void cellular_ctrl_at_cmd_start(const char *cmd)
{
    int32_t delay_ms;

    if (_uart >= 0) {
        if (_at_send_delay_ms) {
            delay_ms = (_last_response_stop_ms + _at_send_delay_ms) - cellularPortGetTickTimeMs();
            if (delay_ms > 0) {
                cellularPortTaskBlock(delay_ms);
            }
        }

        if (_last_error != CELLULAR_CTRL_AT_SUCCESS) {
            return;
        }

        (void) write(cmd, cellularPort_strlen(cmd));

        _cmd_start = true;
    }
}

void cellular_ctrl_at_write_uint64(uint64_t param)
{
    // do common checks before sending sub-parameter
    if ((_uart < 0) || (check_cmd_send() == false)) {
        return;
    }

    // write the integer sub-parameter
    const int32_t str_len = 24;
    char number_string[str_len];
    int32_t result = uint64ToStr(number_string, str_len, param);
    if (result > 0 && result < str_len) {
        (void) write(number_string, cellularPort_strlen(number_string));
    }
}

void cellular_ctrl_at_write_int(int32_t param)
{
    // do common checks before sending sub-parameter
    if ((_uart < 0) || (check_cmd_send() == false)) {
        return;
    }

    // write the integer sub-parameter
    const uint32_t str_len = 12;
    char number_string[str_len];
    int32_t result = cellularPort_sprintf(number_string, "%d", param);
    if (result > 0 && result < str_len) {
        (void) write(number_string, cellularPort_strlen(number_string));
    }
}

void cellular_ctrl_at_write_string(const char *param,
                                   bool useQuotations)
{
    // do common checks before sending sub-parameter
    if ((_uart < 0) || (check_cmd_send() == false)) {
        return;
    }

    // we are writing string, surround it with quotes
    if (useQuotations && (write("\"", 1) != 1)) {
        return;
    }

    (void) write(param, cellularPort_strlen(param));

    if (useQuotations) {
        // we are writing string, surround it with quotes
        (void) write("\"", 1);
    }
}

void cellular_ctrl_at_cmd_stop()
{
    if ((_uart < 0) || (_last_error != CELLULAR_CTRL_AT_SUCCESS)) {
        return;
    }

    // Finish with delimiter
    (void) write(CELLULAR_CTRL_AT_OUTPUT_DELIMITER,
                 CELLULAR_CTRL_AT_OUTPUT_DELIMITER_LENGTH);
}

void cellular_ctrl_at_cmd_stop_read_resp()
{
    if (_uart >= 0) {
        cellular_ctrl_at_cmd_stop();
        cellular_ctrl_at_resp_start(NULL, false);
        cellular_ctrl_at_resp_stop();
    }
}

size_t cellular_ctrl_at_write_bytes(const uint8_t *data, size_t len)
{
    if ((_uart < 0) || (_last_error != CELLULAR_CTRL_AT_SUCCESS)) {
        return 0;
    }

    return write(data, len);
}

void cellular_ctrl_at_flush()
{
    if (_uart >= 0) {
        cellularPortLog("CELLULAR_AT: flush.\n");
        reset_buffer();
        while (fill_buffer(false)) {
            reset_buffer();
        }
    }
}

bool cellular_ctrl_at_sync(int32_t timeout_ms)
{
    if (_uart >= 0) {
        cellularPortLog("CELLULAR_AT: sync.\n");
        // poll for 10 seconds
        for (int i = 0; i < 10; i++) {
            cellular_ctrl_at_lock();
            cellular_ctrl_at_set_at_timeout(timeout_ms, false);
            // For sync use an AT command that is supported
            // by all modems and likely not used frequently,
            // especially a common response like OK could
            // be response to previous request.
            cellular_ctrl_at_cmd_start("AT+CMEE?");
            cellular_ctrl_at_cmd_stop();
            cellular_ctrl_at_resp_start("+CMEE:", false);
            cellular_ctrl_at_resp_stop();
            cellular_ctrl_at_restore_at_timeout();
            // TODO: the original code didn't clear the
            // error, so I've left that the same here,
            // though it seems odd to do so
            if (cellular_ctrl_at_unlock_return_error() == CELLULAR_CTRL_AT_SUCCESS) {
                return true;
            }
        }
        cellularPortLog("CELLULAR_AT: sync failed.\n");
    }

    return false;
}

// Wait for a single character to arrive.
bool cellular_ctrl_at_wait_char(char chr)
{
    int32_t c;

    _error_found = false;

    if (_uart >= 0) {
        while (cellular_ctrl_at_get_last_error() == CELLULAR_CTRL_AT_SUCCESS) {
            c = get_char();
            // Continue to look for URCs,
            // you never know when the sneaky
            // buggers might turn up
            match_urc();
            if (match_error()) {
                _error_found = true;
                return false;
            }

            if (c == chr) {
                return true;
            }
        }
    }

    return false;
}

// End of file
