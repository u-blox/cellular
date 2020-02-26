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

#ifndef _CELLULAR_PORT_OS_H_
#define _CELLULAR_PORT_OS_H_

/* No #includes allowed here */

/** Porting layer for OS functions.  These functions are
 * thread-safe.
 */

/* ----------------------------------------------------------------
 * COMPILE-TIME MACROS
 * -------------------------------------------------------------- */

/** Helper to make sure that lock/unlock pairs are always balanced.
 */
#define CELLULAR_PORT_MUTEX_LOCK(x)      { cellularPortMutexLock(x)

/** Helper to make sure that lock/unlock pairs are always balanced.
 */
#define CELLULAR_PORT_MUTEX_UNLOCK(x)    } cellularPortMutexUnlock(x)

/* ----------------------------------------------------------------
 * TYPES
 * -------------------------------------------------------------- */

/** Mutex handle.
 */
typedef void * CellularPortMutexHandle_t;

/** Queue handle.
 */
typedef void * CellularPortQueueHandle_t;

/** Task handle.
 */
typedef void * CellularPortTaskHandle_t;

/** struct timeval.
 */
typedef struct timeval {
   int32_t tv_sec;    //<! seconds. */
   int32_t tv_usec;   //<! microseconds. */
} CellularPort_timeval;

/* ----------------------------------------------------------------
 * FUNCTIONS: TASKS
 * -------------------------------------------------------------- */

/** Create a task.
 *
 * @param pFunction      the function that forms the task.
 * @param pName          a NULL-terminated string naming the task,
 *                       may be NULL.
 * @param stackSizeBytes the number of bytes of memory to dynamically
 *                       allocate for stack.
 * @param ppParameter    a pointer to the pointer that will be passed
 *                       to pFunction when the task is started.
 *                       The thing at the end of this pointer must be
 *                       there for the lifetime of the task.
 *                       May be NULL.
 * @param priority       the priority at which to run the task,
 *                       the meaning of which is platform dependent
 * @param pTaskHandle    a place to put the handle of the created
 *                       task.
 * @return               zero on success else negative error code.
 */
int32_t cellularPortTaskCreate(void (*pFunction)(void *),
                               const char *pName,
                               size_t stackSizeBytes,
                               void **ppParameter,
                               int32_t priority,
                               CellularPortTaskHandle_t *pTaskHandle);

/** Delete the given task.
 *
 * @param taskHandle  the handle of the task to be deleted.
 *                    Use NULL to delete the current task.
 * @return            zero on success else negative error code.
 */
int32_t cellularPortTaskDelete(const CellularPortTaskHandle_t taskHandle);

/** Check if the current task handle is equal to the given task handle.
 *
 * @param taskHandle  the task handle to check against.
 * @return            true if the task handle pointed to by
 *                    pTaskHandle is the current task handle,
 *                    otherwise false
 */
bool cellularPortTaskIsThis(const CellularPortTaskHandle_t taskHandle);

/** Block the current task for a time.
 *
 * @param delayMs the amount of time to block for in milliseconds.
 */
void cellularPortTaskBlock(int32_t delayMs);

/* ----------------------------------------------------------------
 * FUNCTIONS: QUEUES
 * -------------------------------------------------------------- */

/** Create a queue.
 *
 * @param queueLength    the maximum length of the queue in units
 *                       of itemSizeBytes.
 * @param itemSizeBytes  the size of each item on the queue.
 * @param pQueueHandle   a place to put the handle of the queue.
 * @return               zero on success else negative error code.
 */
int32_t cellularPortQueueCreate(size_t queueLength,
                                size_t itemSizeBytes,
                                CellularPortQueueHandle_t *pQueueHandle);

/** Delete the given queue.
 *
 * @param queueHandle  the handle of the queue to be deleted.
 * @return             zero on success else negative error code.
 */
int32_t cellularPortQueueDelete(const CellularPortQueueHandle_t queueHandle);

/** Send to the given queue.
 *
 * @param queueHandle  the handle of the queue.
 * @param pEventData   pointer to the data to send.  The data will
 *                     be copied into the queue and hence can be
 *                     destroyed by the caller once this functions
 *                     returns.
 * @return             zero on success else negative error code.
 */
int32_t cellularPortQueueSend(const CellularPortQueueHandle_t queueHandle,
                              const void *pEventData);

/** Receive from the given queue.
 *
 * @param queueHandle the handle of the queue.
 * @param pEventData  pointer to a place to put incoming data.
 * @return            zero on success else negative error code.
 */
int32_t cellularPortQueueReceive(const CellularPortQueueHandle_t queueHandle,
                                 void *pEventData);

/* ----------------------------------------------------------------
 * FUNCTIONS: MUTEXES
 * -------------------------------------------------------------- */

/** Create a mutex.
 *
 * @param pMutexHandle a place to put the mutex handle.
 * @return             zero on success else negative error code.
 */
int32_t cellularPortMutexCreate(CellularPortMutexHandle_t *pMutexHandle);

/** Destroy a mutex.
 *
 * @param mutexHandle the handle of the mutex.
 * @return            zero on success else negative error code.
 */
int32_t cellularPortMutexDelete(const CellularPortMutexHandle_t mutexHandle);

/** Lock the given mutex, waiting until it is available if
 * it is already locked.
 *
 * @param mutexHandle  the handle of the mutex.
 * @return             zero on success else negative error code.
 */
int32_t cellularPortMutexLock(const CellularPortMutexHandle_t mutexHandle);

/** Try to lock the given mutex, waiting up to delayMs
 * if it is currently locked.
 *
 * @param mutexHandle  the handle of the mutex.
 * @param delayMs      the maximum time to wait in milliseconds.
 * @return             zero on success else negative error code.
 */
int32_t cellularPortMutexTryLock(const CellularPortMutexHandle_t mutexHandle,
                                 int32_t delayMs);

/** Unlock the given mutex.
 *
 * @param mutexHandle   the handle of the mutex.
 * @return              zero on success else negative error code.
 */
int32_t cellularPortMutexUnlock(const CellularPortMutexHandle_t mutexHandle);

/** Get the task handle of the task that has the given mutex locked.
 *
 * @param mutexHandle   the handle of the mutex.
 * @return              the handle of the task that holds the mutex
 *                      or zero if the mutex is not currently held.
 */
CellularPortTaskHandle_t cellularPortMutexGetLocker(const CellularPortMutexHandle_t mutexHandle);

/* ----------------------------------------------------------------
 * FUNCTIONS: TIME
 * -------------------------------------------------------------- */

/** Get the current OS tick converted to a time in milliseconds.
 * This is guaranteed to be unaffected by any time setting activity
 *
 * @return the current OS tick converted to milliseconds.
 */
int64_t cellularPortGetTickTimeMs();

#endif // _CELLULAR_PORT_OS_H_

// End of file
