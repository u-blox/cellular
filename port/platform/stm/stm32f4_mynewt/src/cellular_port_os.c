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

#ifdef CELLULAR_CFG_OVERRIDE
# include "cellular_cfg_override.h" // For a customer's configuration override
#endif
#include "cellular_cfg_sw.h"
#include "cellular_port_debug.h"
#include "cellular_port_clib.h"
#include "cellular_port.h"
#include "cellular_port_os.h"

#if (MYNEWT == 1)
#include "os/os.h"
#include "assert.h"

struct cellular_port_queue_msg
{
    STAILQ_ENTRY(cellular_port_queue_msg) next;
    uint8_t msg_databuf[0];
};

/*
 * Cellular port queue wrapper.
 *
 */
struct cellular_port_queue_wrapper
{
    size_t item_length;
    struct os_sem send_sem;
    struct os_sem rx_sem;
    struct os_mempool msgpool;
    STAILQ_HEAD(, cellular_port_queue_msg) msgq;
};

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
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: TASKS
 * -------------------------------------------------------------- */

// Create a task.
int32_t cellularPortTaskCreate(void (*pFunction)(void *),
                               const char *pName,
                               size_t stackSizeBytes,
                               void *pParameter,
                               int32_t priority,
                               CellularPortTaskHandle_t *pTaskHandle)
{
    int rc;
    struct os_task *tp;
    os_stack_t *sp;
    os_stack_t os_stack_size;
    CellularPortErrorCode_t errorCode;

    /* Better have a function and task handle */
    if ((pFunction == NULL) || (pTaskHandle == NULL)) {
        return CELLULAR_PORT_INVALID_PARAMETER;
    }

    /* Allocate task */
    tp = pCellularPort_malloc(sizeof(struct os_task));
    if (!tp) {
        return CELLULAR_PORT_OUT_OF_MEMORY;
    }

    /* Clear the memory as task init assumes some values are zero to start */
    memset(tp, 0, sizeof(struct os_task));

    /*
     * Allocate stack. The OS requires the stack to be in units of os_stack_t.
     */
    os_stack_size = (stackSizeBytes + (OS_STACK_ALIGNMENT - 1)) / sizeof(os_stack_t);
    sp = pCellularPort_malloc(os_stack_size * sizeof(os_stack_t));
    if (!sp) {
        cellularPort_free(tp);
        return CELLULAR_PORT_OUT_OF_MEMORY;
    }

    /* XXX: we should have alignment */
    assert(((uint32_t)sp & (OS_STACK_ALIGNMENT - 1)) == 0);

    /* Initialize the task */
    rc = os_task_init(tp, pName, pFunction, NULL, priority, OS_WAIT_FOREVER,
                      sp, os_stack_size);
    if (rc == OS_OK) {
        *pTaskHandle = (CellularPortMutexHandle_t)tp;
        errorCode = CELLULAR_PORT_SUCCESS;
    } else {
        cellularPort_free(tp);
        cellularPort_free(sp);
        errorCode = CELLULAR_PORT_PLATFORM_ERROR;
    }

    return (int32_t) errorCode;
}

// Delete the given task.
int32_t cellularPortTaskDelete(const CellularPortTaskHandle_t taskHandle)
{
    /*
     * XXX: need to determine what we want to do here.
     */
    return CELLULAR_PORT_SUCCESS;
}

// Check if the current task handle is equal to the given task handle.
bool cellularPortTaskIsThis(const CellularPortTaskHandle_t taskHandle)
{
    return ((struct os_task *)taskHandle == os_sched_get_current_task());
}

// Block the current task for a time.
void cellularPortTaskBlock(int32_t delayMs)
{
    os_time_delay(os_time_ms_to_ticks32(delayMs));
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: QUEUES
 * -------------------------------------------------------------- */

// Create a queue.
// Note: CMSIS-OS has osMessage which, in the case
// of the STM32F4 platform, maps to FreeRTOS queues,
// however an osMessage is fixed at 32 bits in size.
// Could use osMail but that would result in lots
// of malloc()/free() operations which is undesirable
// hence we go straight to the underlying FreeRTOS
// xQueue interface here.
int32_t cellularPortQueueCreate(size_t queueLength,
                                size_t itemSizeBytes,
                                CellularPortQueueHandle_t *pQueueHandle)
{
    void *msgbuf;
    uint32_t poolsize;
    size_t bufsize;
    os_error_t err;
    struct cellular_port_queue_wrapper *cpqw;

    /* We need to allocate a queue wrapper for the freertos queue */
    cpqw = pCellularPort_malloc(sizeof(struct cellular_port_queue_wrapper));
    if (!cpqw) {
        return CELLULAR_PORT_OUT_OF_MEMORY;
    }

    /*
     * We need to allocate the queue message buffers. This message buffer needs
     * to include the cellular_port_queue_msg structure. This is just the list
     * pointer needed to enqueue the element.
     */
    cpqw->item_length = itemSizeBytes;
    bufsize = itemSizeBytes + sizeof(struct cellular_port_queue_msg);
    poolsize = OS_MEMPOOL_BYTES(queueLength, bufsize);
    msgbuf = pCellularPort_malloc(poolsize);
    if (!msgbuf) {
        cellularPort_free(cpqw);
        return CELLULAR_PORT_OUT_OF_MEMORY;
    }

    err = os_mempool_init(&cpqw->msgpool, queueLength, bufsize, msgbuf,
                          "cpqw");
    if (err != OS_OK) {
        cellularPort_free(msgbuf);
        cellularPort_free(cpqw);
        return CELLULAR_PORT_PLATFORM_ERROR;
    }

    /*
     * Initialize the queue wrapper. The send queue gets initialized to the
     * number of elements in the memory pool. The number of tokens in the
     * semaphore
     */
    os_sem_init(&cpqw->rx_sem, 0);
    os_sem_init(&cpqw->send_sem, queueLength);
    STAILQ_INIT(&cpqw->msgq);
    *pQueueHandle = cpqw;

    return CELLULAR_PORT_SUCCESS;
}

// Delete the given queue.
int32_t cellularPortQueueDelete(const CellularPortQueueHandle_t queueHandle)
{
    struct cellular_port_queue_wrapper *cpqw;

    cpqw = (struct cellular_port_queue_wrapper *)queueHandle;

    if (cpqw == NULL) {
        return CELLULAR_PORT_PLATFORM_ERROR;
    }

    /* Un-register the mempool and free allocated memory */
    os_mempool_unregister(&cpqw->msgpool);
    cellularPort_free((uint32_t *)cpqw->msgpool.mp_membuf_addr);
    cellularPort_free(cpqw);

    /* XXX: for now, assume no outstanding semaphores... queue not in use */
    return CELLULAR_PORT_SUCCESS;
}

// Send to the given queue.
int32_t cellularPortQueueSend(const CellularPortQueueHandle_t queueHandle,
                              const void *pEventData)
{
    os_sr_t sr;
    os_error_t err;
    struct cellular_port_queue_msg *msg;
    struct cellular_port_queue_wrapper *cpqw;

    if ((queueHandle == NULL) || (pEventData == NULL)) {
        return CELLULAR_PORT_INVALID_PARAMETER;
    }

    /* Wait for semaphore to send */
    cpqw = (struct cellular_port_queue_wrapper *)queueHandle;
    err = os_sem_pend(&cpqw->send_sem, OS_WAIT_FOREVER);
    if (err != OS_OK) {
        return CELLULAR_PORT_PLATFORM_ERROR;
    }

    /* There has to be a free element! */
    msg = os_memblock_get(&cpqw->msgpool);
    assert(msg != NULL);

    /* Copy the data into the message buffer */
    memcpy(&msg->msg_databuf[0], pEventData, cpqw->item_length);

    /* Add to queue */
    OS_ENTER_CRITICAL(sr);
    STAILQ_INSERT_TAIL(&cpqw->msgq, msg, next);
    OS_EXIT_CRITICAL(sr);

    /* Release a semaphore to rx sem */
    os_sem_release(&cpqw->rx_sem);

    return CELLULAR_PORT_SUCCESS;
}

// Send to the given queue from an ISR.
int32_t cellularPortQueueSendFromISR(const CellularPortQueueHandle_t queueHandle,
                                     const void *pEventData)
{
    uint16_t tokens;
    struct cellular_port_queue_msg *msg;
    struct cellular_port_queue_wrapper *cpqw;

    if ((queueHandle == NULL) || (pEventData == NULL)) {
        return CELLULAR_PORT_INVALID_PARAMETER;
    }

    /* Cannot wait for a semaphore here. Just see if it is available */
    cpqw = (struct cellular_port_queue_wrapper *)queueHandle;
    tokens =  os_sem_get_count(&cpqw->send_sem);
    if (tokens == 0) {
        return CELLULAR_PORT_OUT_OF_MEMORY;
    }
    --cpqw->send_sem.sem_tokens;

    /* There has to be a free element! */
    msg = os_memblock_get(&cpqw->msgpool);
    assert(msg != NULL);

    /* Copy the data into the message buffer */
    memcpy(&msg->msg_databuf[0], pEventData, cpqw->item_length);

    /* Add to queue */
    STAILQ_INSERT_TAIL(&cpqw->msgq, msg, next);

    /* Release a semaphore to rx sem */
    os_sem_release(&cpqw->rx_sem);

    return CELLULAR_PORT_SUCCESS;
}

// Receive from the given queue, blocking.
int32_t cellularPortQueueReceive(const CellularPortQueueHandle_t queueHandle,
                                 void *pEventData)
{
    os_sr_t sr;
    os_error_t err;
    struct cellular_port_queue_msg *msg;
    struct cellular_port_queue_wrapper *cpqw;

    if ((queueHandle == NULL) || (pEventData == NULL)) {
        return CELLULAR_PORT_INVALID_PARAMETER;
    }

    /* Wait for semaphore to send */
    cpqw = (struct cellular_port_queue_wrapper *)queueHandle;
    err = os_sem_pend(&cpqw->rx_sem, OS_WAIT_FOREVER);
    if (err != OS_OK) {
        return CELLULAR_PORT_PLATFORM_ERROR;
    }

    /* There has to be an element on the queue */
    OS_ENTER_CRITICAL(sr);
    msg = STAILQ_FIRST(&cpqw->msgq);
    if (msg) {
        STAILQ_REMOVE_HEAD(&cpqw->msgq, next);
    }
    OS_EXIT_CRITICAL(sr);
    assert(msg != NULL);

    /* Copy the data out of the message buffer */
    memcpy(pEventData, &msg->msg_databuf[0], cpqw->item_length);

    /* Add message back to memory pool */
    os_memblock_put(&cpqw->msgpool, msg);

    /* Release a semaphore to rx sem */
    os_sem_release(&cpqw->send_sem);

    return CELLULAR_PORT_SUCCESS;
}

// Receive from the given queue, with a wait time.
int32_t cellularPortQueueTryReceive(const CellularPortQueueHandle_t queueHandle,
                                    int32_t waitMs, void *pEventData)
{
    os_sr_t sr;
    os_error_t err;
    struct cellular_port_queue_msg *msg;
    struct cellular_port_queue_wrapper *cpqw;

    if ((queueHandle == NULL) || (pEventData == NULL)) {
        return CELLULAR_PORT_INVALID_PARAMETER;
    }

    /* Wait for semaphore to send */
    cpqw = (struct cellular_port_queue_wrapper *)queueHandle;
    err = os_sem_pend(&cpqw->rx_sem, os_time_ms_to_ticks32(waitMs));
    if (err == OS_TIMEOUT) {
        return CELLULAR_PORT_TIMEOUT;
    } else if (err != OS_OK) {
        return CELLULAR_PORT_PLATFORM_ERROR;
    }

    /* There has to be an element on the queue */
    OS_ENTER_CRITICAL(sr);
    msg = STAILQ_FIRST(&cpqw->msgq);
    if (msg) {
        STAILQ_REMOVE_HEAD(&cpqw->msgq, next);
    }
    OS_EXIT_CRITICAL(sr);
    assert(msg != NULL);

    /* Copy the data out of the message buffer */
    memcpy(pEventData, &msg->msg_databuf[0], cpqw->item_length);

    /* Add message back to memory pool */
    os_memblock_put(&cpqw->msgpool, msg);

    /* Release a semaphore to rx sem */
    os_sem_release(&cpqw->send_sem);

    return CELLULAR_PORT_SUCCESS;
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: MUTEXES
 * -------------------------------------------------------------- */
/* Create a mutex. */
int32_t cellularPortMutexCreate(CellularPortMutexHandle_t *pMutexHandle)
{
    struct os_mutex *mu;
    CellularPortErrorCode_t errorCode;

    if (pMutexHandle == NULL) {
        return CELLULAR_PORT_INVALID_PARAMETER;
    }

    mu = pCellularPort_malloc(sizeof(struct os_mutex));
    if (!mu) {
        errorCode = CELLULAR_PORT_OUT_OF_MEMORY;
    } else {
        os_mutex_init(mu);
        *pMutexHandle = (CellularPortMutexHandle_t)mu;
        errorCode = CELLULAR_PORT_SUCCESS;
    }

    return (int32_t) errorCode;
}

/* Destroy a mutex. */
int32_t cellularPortMutexDelete(const CellularPortMutexHandle_t mutexHandle)
{
    /*
     * XXX: mynewt does not have a mutex "delete" API. If this is called
     * when the mutex is locked or still used by someone there will be
     * issues.
     */
    if (mutexHandle == NULL) {
        return CELLULAR_PORT_INVALID_PARAMETER;
    }

    cellularPort_free(mutexHandle);

    return CELLULAR_PORT_SUCCESS;
}

// Lock the given mutex.
int32_t cellularPortMutexLock(const CellularPortMutexHandle_t mutexHandle)
{
    os_error_t rc;
    CellularPortErrorCode_t errorCode;

    if (mutexHandle == NULL) {
        return CELLULAR_PORT_INVALID_PARAMETER;
    }

    rc = os_mutex_pend((struct os_mutex *)mutexHandle, OS_TIMEOUT_NEVER);
    if (rc == OS_OK) {
        errorCode = CELLULAR_PORT_SUCCESS;
    } else {
        errorCode = CELLULAR_PORT_PLATFORM_ERROR;
    }

    return (int32_t) errorCode;
}

// Try to lock the given mutex.
int32_t cellularPortMutexTryLock(const CellularPortMutexHandle_t mutexHandle,
                                 int32_t delayMs)
{
    os_error_t rc;
    os_time_t ticks;
    CellularPortErrorCode_t errorCode;

    if (mutexHandle == NULL) {
        return CELLULAR_PORT_INVALID_PARAMETER;
    }

    os_time_ms_to_ticks((uint32_t)delayMs, &ticks);
    rc = os_mutex_pend((struct os_mutex *)mutexHandle, ticks);
    if (rc == OS_OK) {
        errorCode = CELLULAR_PORT_SUCCESS;
    } else {
        errorCode = CELLULAR_PORT_TIMEOUT;
    }

    return (int32_t) errorCode;
}

// Unlock the given mutex.
int32_t cellularPortMutexUnlock(const CellularPortMutexHandle_t mutexHandle)
{
    os_error_t rc;
    CellularPortErrorCode_t errorCode;

    if (mutexHandle == NULL) {
        return CELLULAR_PORT_INVALID_PARAMETER;
    }

    rc = os_mutex_release((struct os_mutex *)mutexHandle);
    if (rc == OS_OK) {
        errorCode = CELLULAR_PORT_SUCCESS;
    } else {
        errorCode = CELLULAR_PORT_PLATFORM_ERROR;
    }

    return (int32_t) errorCode;
}
#else
#include "cmsis_os.h"

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
 * STATIC FUNCTIONS
 * -------------------------------------------------------------- */

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: TASKS
 * -------------------------------------------------------------- */

// Create a task.
int32_t cellularPortTaskCreate(void (*pFunction)(void *),
                               const char *pName,
                               size_t stackSizeBytes,
                               void *pParameter,
                               int32_t priority,
                               CellularPortTaskHandle_t *pTaskHandle)
{
    CellularPortErrorCode_t errorCode = CELLULAR_PORT_INVALID_PARAMETER;
    osThreadDef_t threadDef = {0};

    if ((pFunction != NULL) && (pTaskHandle != NULL)) {

        threadDef.name = (char *) pName;
        threadDef.pthread = (void (*) (void const *)) pFunction;
        threadDef.tpriority = priority;
        threadDef.instances = 0;
        threadDef.stacksize = stackSizeBytes;

        *pTaskHandle = (CellularPortTaskHandle_t *) osThreadCreate(&threadDef,
                                                                   pParameter);
        if (*pTaskHandle != NULL) {
            errorCode = CELLULAR_PORT_SUCCESS;
        }
    }

    return (int32_t) errorCode;
}

// Delete the given task.
int32_t cellularPortTaskDelete(const CellularPortTaskHandle_t taskHandle)
{
    CellularPortErrorCode_t errorCode = CELLULAR_PORT_PLATFORM_ERROR;

    if (osThreadTerminate((osThreadId) taskHandle) == osOK) {
        errorCode = CELLULAR_PORT_SUCCESS;
    }

    return (int32_t) errorCode;
}

// Check if the current task handle is equal to the given task handle.
bool cellularPortTaskIsThis(const CellularPortTaskHandle_t taskHandle)
{
    return osThreadGetId() == (osThreadId) taskHandle;
}

// Block the current task for a time.
void cellularPortTaskBlock(int32_t delayMs)
{
    // Make sure the scheduler has been started
    // or this may fly off into space
    cellularPort_assert(osKernelRunning());
    osDelay(delayMs);
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: QUEUES
 * -------------------------------------------------------------- */

// Create a queue.
// Note: CMSIS-OS has osMessage which, in the case
// of the STM32F4 platform, maps to FreeRTOS queues,
// however an osMessage is fixed at 32 bits in size.
// Could use osMail but that would result in lots
// of malloc()/free() operations which is undesirable
// hence we go straight to the underlying FreeRTOS
// xQueue interface here.
int32_t cellularPortQueueCreate(size_t queueLength,
                                size_t itemSizeBytes,
                                CellularPortQueueHandle_t *pQueueHandle)
{
    CellularPortErrorCode_t errorCode = CELLULAR_PORT_INVALID_PARAMETER;

    if (pQueueHandle != NULL) {
        errorCode = CELLULAR_PORT_PLATFORM_ERROR;
        // Actually create the queue
        *pQueueHandle = (CellularPortQueueHandle_t) xQueueCreate(queueLength,
                                                                 itemSizeBytes);
        if (*pQueueHandle != NULL) {
            errorCode = CELLULAR_PORT_SUCCESS;
        }
    }

    return (int32_t) errorCode;
}

// Delete the given queue.
int32_t cellularPortQueueDelete(const CellularPortQueueHandle_t queueHandle)
{
    CellularPortErrorCode_t errorCode = CELLULAR_PORT_INVALID_PARAMETER;

    if (queueHandle != NULL) {
        vQueueDelete((QueueHandle_t) queueHandle);
        errorCode = CELLULAR_PORT_SUCCESS;
    }

    return (int32_t) errorCode;
}

// Send to the given queue.
int32_t cellularPortQueueSend(const CellularPortQueueHandle_t queueHandle,
                              const void *pEventData)
{
    CellularPortErrorCode_t errorCode = CELLULAR_PORT_INVALID_PARAMETER;

    if ((queueHandle != NULL) && (pEventData != NULL)) {
        errorCode = CELLULAR_PORT_PLATFORM_ERROR;
        if (xQueueSend((QueueHandle_t) queueHandle,
                       pEventData,
                       (portTickType) portMAX_DELAY) == pdTRUE) {
            errorCode = CELLULAR_PORT_SUCCESS;
        }
    }

    return (int32_t) errorCode;
}

// Receive from the given queue, blocking.
int32_t cellularPortQueueReceive(const CellularPortQueueHandle_t queueHandle,
                                 void *pEventData)
{
    CellularPortErrorCode_t errorCode = CELLULAR_PORT_INVALID_PARAMETER;

    if ((queueHandle != NULL) && (pEventData != NULL)) {
        errorCode = CELLULAR_PORT_PLATFORM_ERROR;
        if (xQueueReceive((QueueHandle_t) queueHandle,
                          pEventData,
                          (portTickType) portMAX_DELAY) == pdTRUE) {
            errorCode = CELLULAR_PORT_SUCCESS;
       }
    }

    return (int32_t) errorCode;
}

// Receive from the given queue, with a wait time.
int32_t cellularPortQueueTryReceive(const CellularPortQueueHandle_t queueHandle,
                                    int32_t waitMs, void *pEventData)
{
    CellularPortErrorCode_t errorCode = CELLULAR_PORT_INVALID_PARAMETER;

    if ((queueHandle != NULL) && (pEventData != NULL)) {
        errorCode = CELLULAR_PORT_TIMEOUT;
        if (xQueueReceive((QueueHandle_t) queueHandle,
                          pEventData,
                          waitMs / portTICK_PERIOD_MS) == pdTRUE) {
            errorCode = CELLULAR_PORT_SUCCESS;
       }
    }

    return (int32_t) errorCode;
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: MUTEXES
 * -------------------------------------------------------------- */

// Create a mutex.
int32_t cellularPortMutexCreate(CellularPortMutexHandle_t *pMutexHandle)
{
    CellularPortErrorCode_t errorCode = CELLULAR_PORT_INVALID_PARAMETER;
    osMutexDef_t mutexDef = {0}; // Required but with no meaningful content
                                 // in this case

    if (pMutexHandle != NULL) {
        errorCode = CELLULAR_PORT_PLATFORM_ERROR;
        *pMutexHandle = (CellularPortMutexHandle_t) osMutexCreate(&mutexDef);
        if (*pMutexHandle != NULL) {
            errorCode = CELLULAR_PORT_SUCCESS;
        }
    }

    return (int32_t) errorCode;
}

// Destroy a mutex.
int32_t cellularPortMutexDelete(const CellularPortMutexHandle_t mutexHandle)
{
    CellularPortErrorCode_t errorCode = CELLULAR_PORT_INVALID_PARAMETER;

    if (mutexHandle != NULL) {
        errorCode = CELLULAR_PORT_PLATFORM_ERROR;
        if (osMutexDelete((osMutexId) mutexHandle) == osOK) {
            errorCode = CELLULAR_PORT_SUCCESS;
        }
    }

    return (int32_t) errorCode;
}

// Lock the given mutex.
int32_t cellularPortMutexLock(const CellularPortMutexHandle_t mutexHandle)
{
    CellularPortErrorCode_t errorCode = CELLULAR_PORT_INVALID_PARAMETER;

    if (mutexHandle != NULL) {
        errorCode = CELLULAR_PORT_PLATFORM_ERROR;
        if (osMutexWait((osMutexId) mutexHandle, osWaitForever) == osOK) {
            errorCode = CELLULAR_PORT_SUCCESS;
        }
    }

    return (int32_t) errorCode;
}

// Try to lock the given mutex.
int32_t cellularPortMutexTryLock(const CellularPortMutexHandle_t mutexHandle,
                                 int32_t delayMs)
{
    CellularPortErrorCode_t errorCode = CELLULAR_PORT_INVALID_PARAMETER;

    if (mutexHandle != NULL) {
        errorCode = CELLULAR_PORT_TIMEOUT;
        if (osMutexWait((osMutexId) mutexHandle, delayMs) == osOK) {
            errorCode = CELLULAR_PORT_SUCCESS;
        }
    }

    return (int32_t) errorCode;
}

// Unlock the given mutex.
int32_t cellularPortMutexUnlock(const CellularPortMutexHandle_t mutexHandle)
{
    CellularPortErrorCode_t errorCode = CELLULAR_PORT_INVALID_PARAMETER;

    if (mutexHandle != NULL) {
        errorCode = CELLULAR_PORT_PLATFORM_ERROR;
        if (osMutexRelease((osMutexId) mutexHandle) == osOK) {
            errorCode = CELLULAR_PORT_SUCCESS;
        }
    }

    return (int32_t) errorCode;
}

/* ----------------------------------------------------------------
 * PUBLIC FUNCTIONS: HOOKS
 * -------------------------------------------------------------- */

// Stack overflow hook, employed when configCHECK_FOR_STACK_OVERFLOW is
// set to 1 in FreeRTOSConfig.h.
void vApplicationStackOverflowHook(TaskHandle_t taskHandle, char *pTaskName)
{
    cellularPortLog("CELLULAR_PORT: task handle 0x%08x, \"%s\", overflowed its stack.\n",
                    (int32_t) taskHandle, pTaskName);
    cellularPort_assert(false);
}

// Malloc failed hook, employed when configUSE_MALLOC_FAILED_HOOK is
// set to 1 in FreeRTOSConfig.h.
void vApplicationMallocFailedHook()
{
    cellularPortLog("CELLULAR_PORT: freeRTOS doesn't have enough heap, increase configTOTAL_HEAP_SIZE in FreeRTOSConfig.h.\n");
    cellularPort_assert(false);
}
#endif
// End of file
