/**
 * @file queue.h
 * @brief A small thread-safe FIFO queue (FreeRTOS-like API) implemented with pthread mutex/cond.
 *
 * This queue is designed for Linux/POSIX environments where producer(s) and consumer(s)
 * may run on different threads. It provides a minimal subset of the FreeRTOS queue API:
 *   - xQueueCreate
 *   - xQueueSend
 *   - xQueueSendToFront
 *   - xQueueReceive
 *   - vQueueDelete
 *
 * Items are stored by value (fixed item size per queue).
 *
 * @note This implementation uses pthread mutex and condition variables.
 * @note Timeout is specified in "ticks". 1 tick equals QUEUE_TICK_MS milliseconds.
 */

#ifndef ICAM_QUEUE_H
#define ICAM_QUEUE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>

/* --------------------------------------------------------------------------
 * FreeRTOS-like minimal types/macros
 * -------------------------------------------------------------------------- */

/** @brief Base type for return values (pdTRUE/pdFALSE). */
typedef int BaseType_t;

/** @brief Unsigned base type for lengths/indexes. */
typedef unsigned int UBaseType_t;

/** @brief Tick type for timeouts. */
typedef unsigned int TickType_t;

/** @brief Boolean true. */
#define pdTRUE  ((BaseType_t)1)

/** @brief Boolean false. */
#define pdFALSE ((BaseType_t)0)

/** @brief Success. */
#define pdPASS  (pdTRUE)

/** @brief Failure. */
#define pdFAIL  (pdFALSE)

/** @brief Special value meaning "wait forever". */
#define portMAX_DELAY ((TickType_t)0xFFFFFFFFUL)

/** @brief Opaque queue handle type (compatible with FreeRTOS-like usage). */
typedef void* QueueHandle_t;

/**
 * @brief Define how many milliseconds one tick represents.
 *
 * Example:
 *   - QUEUE_TICK_MS = 1  -> 1 tick = 1 ms
 *   - QUEUE_TICK_MS = 10 -> 1 tick = 10 ms
 */
#ifndef QUEUE_TICK_MS
#define QUEUE_TICK_MS 1
#endif

/* --------------------------------------------------------------------------
 * Internal queue structure
 * -------------------------------------------------------------------------- */

/**
 * @brief Internal queue object.
 *
 * - buffer holds `length` items, each item has size `item_size`.
 * - head: index of the next item to read.
 * - tail: index of the next position to write.
 * - count: number of items currently in queue.
 */
typedef struct QueueDefinition {
    unsigned char *buffer;
    UBaseType_t length;
    UBaseType_t item_size;

    UBaseType_t count;
    UBaseType_t head;
    UBaseType_t tail;

    pthread_mutex_t mutex;
    pthread_cond_t  not_empty;
    pthread_cond_t  not_full;
} Queue_t;

/* --------------------------------------------------------------------------
 * Utility: build absolute time for pthread_cond_timedwait
 * -------------------------------------------------------------------------- */

/**
 * @brief Build an absolute timespec for a timeout "wait_ms" milliseconds from now.
 *
 * pthread_cond_timedwait uses CLOCK_REALTIME by default, so we use CLOCK_REALTIME
 * to build the correct absolute deadline.
 *
 * @param[out] ts      Output timespec.
 * @param[in]  wait_ms Timeout in milliseconds.
 */
static inline void queue_get_abstime(struct timespec *ts, unsigned long wait_ms)
{
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);

    unsigned long sec_add = wait_ms / 1000UL;
    unsigned long ms_rem  = wait_ms % 1000UL;

    ts->tv_sec  = now.tv_sec + (time_t)sec_add;
    ts->tv_nsec = now.tv_nsec + (long)(ms_rem * 1000000UL); // 1ms = 1.000.000ns

    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_sec += 1;
        ts->tv_nsec -= 1000000000L; // 1s
    }

    /**
     * @brief for example
     * now = { 
     *  tv_sec=100, 
     *  tv_nsec=900,000,000 
     * }
     * and wait_ms = 200
     * sec_add=0, ms_rem=200 → plus 200,000,000 ns
     * tv_nsec = 900,000,000 + 200,000,000 = 1,100,000,000 (overflow)
     * normalization → tv_sec=101, tv_nsec=100,000,000
     */
}

/* --------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------- */

/**
 * @brief Create a queue.
 *
 * @param[in] uxQueueLength Number of items the queue can store.
 * @param[in] uxItemSize    Size of each item in bytes.
 *
 * @return QueueHandle_t on success, NULL on failure.
 */
static inline QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize)
{
    Queue_t *q;

    if (uxQueueLength == 0 || uxItemSize == 0)
        return NULL;

    q = (Queue_t*)calloc(1, sizeof(Queue_t));
    if (!q)
        return NULL;

    q->buffer = (unsigned char*)malloc((size_t)uxQueueLength * (size_t)uxItemSize);
    if (!q->buffer) {
        free(q);
        return NULL;
    }

    q->length = uxQueueLength;
    q->item_size = uxItemSize;
    q->count = 0;
    q->head = 0;
    q->tail = 0;

    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);

    return (QueueHandle_t)q;
}

/**
 * @brief Send an item to the back of the queue (FIFO).
 *
 * @param[in] xQueue        Queue handle.
 * @param[in] pvItemToQueue Pointer to item data to copy into queue.
 * @param[in] xTicksToWait  Timeout in ticks:
 *                          - 0: do not wait
 *                          - portMAX_DELAY: wait forever
 *                          - otherwise: wait up to xTicksToWait ticks
 *
 * @return pdTRUE on success, pdFALSE on failure/timeout.
 */
static inline BaseType_t xQueueSend(QueueHandle_t xQueue,
                                    const void* const pvItemToQueue,
                                    TickType_t xTicksToWait)
{
    Queue_t *q;
    bool wait_forever;
    struct timespec deadline;

    if (!xQueue || !pvItemToQueue)
        return pdFALSE;

    q = (Queue_t*)xQueue;

    pthread_mutex_lock(&q->mutex);
    
    /* No-wait mode */
    if (xTicksToWait == 0) {
        if (q->count == q->length) { // if queue is full
            pthread_mutex_unlock(&q->mutex);
            return pdFALSE;
        } else {
            // Go to enqueue in step
        }
    } else { 
        // Wait until queue is not full
        if (xTicksToWait == portMAX_DELAY) {
            wait_forever = true;
        } else {
            wait_forever = false;
        }

        /* If we have a finite timeout, build absolute deadline */
        if (wait_forever == false) {
            unsigned long wait_ms = (unsigned long)xTicksToWait * (unsigned long)QUEUE_TICK_MS;
            queue_get_abstime(&deadline, wait_ms);
        }

        /* Wait until there is space in the queue */
        while (q->count == q->length) {
            int rc;
            if (wait_forever == true) {
                rc = pthread_cond_wait(&q->not_full, &q->mutex);
                if (rc != 0) { /* Handle error */
                    pthread_mutex_unlock(&q->mutex);
                    return pdFALSE;
                }
            } else {
                rc = pthread_cond_timedwait(&q->not_full, &q->mutex, &deadline);
                if (rc == ETIMEDOUT) {
                    pthread_mutex_unlock(&q->mutex);
                    return pdFALSE;
                }
                
                if (rc != 0) { /* Handle other errors */
                    pthread_mutex_unlock(&q->mutex);
                    return pdFALSE;
                }
            }
        }
    }

    /* Enqueue */
    /* Copy item into queue at tail position */
    /**
     * @brief for example
     * length = 4, item_size = 8 bytes
     * tail = 2
     * Slot 2 starts at offset 2*8 = 16 bytes in buffer.
     */
    memcpy(&q->buffer[q->tail * q->item_size], pvItemToQueue, (size_t)q->item_size);
    /**
     * @brief example
     * tail=0 → 1
     * tail=1 → 2
     * tail=2 → 3
     * tail=3 → (3+1)%4 = 0 (ringbuffer)
     */
    q->tail = (q->tail + 1) % q->length;
    q->count++;

    /* Wake up any waiting receiver */
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);

    return pdTRUE;
}

/**
 * @brief Send an item to the front of the queue.
 *
 * Useful for prioritizing some events.
 *
 * @param[in] xQueue        Queue handle.
 * @param[in] pvItemToQueue Pointer to item data to copy into queue.
 * @param[in] xTicksToWait  Timeout in ticks (same rules as xQueueSend).
 *
 * @return pdTRUE on success, pdFALSE on failure/timeout.
 */
static inline BaseType_t xQueueSendToFront(QueueHandle_t xQueue,
                                           const void* const pvItemToQueue,
                                           TickType_t xTicksToWait)
{
    Queue_t *q;
    bool wait_forever;
    struct timespec deadline;

    if (!xQueue || !pvItemToQueue)
        return pdFALSE;

    q = (Queue_t*)xQueue;

    pthread_mutex_lock(&q->mutex);

    /* No-wait mode */
    if (xTicksToWait == 0) {
        if (q->count == q->length) { // if queue is full
            pthread_mutex_unlock(&q->mutex);
            return pdFALSE;
        } else {
            // Go to enqueue in step
        }
    } else {
        // Wait until queue is not full
        if (xTicksToWait == portMAX_DELAY) {
            wait_forever = true;
        } else {
            wait_forever = false;
        }
           
        if (wait_forever == false) {
            unsigned long wait_ms = (unsigned long)xTicksToWait * (unsigned long)QUEUE_TICK_MS;
            queue_get_abstime(&deadline, wait_ms);
        }

        /* Wait until there is space in the queue */
        while (q->count == q->length) {
            int rc;
            if (wait_forever == true) {
                rc = pthread_cond_wait(&q->not_full, &q->mutex);
                if (rc != 0) { /* Handle error */
                    pthread_mutex_unlock(&q->mutex);
                    return pdFALSE;
                }
            } else {
                rc = pthread_cond_timedwait(&q->not_full, &q->mutex, &deadline);
                if (rc == ETIMEDOUT) {
                    pthread_mutex_unlock(&q->mutex);
                    return pdFALSE;
                }
                if (rc != 0) { /* Handle other errors */
                    pthread_mutex_unlock(&q->mutex);
                    return pdFALSE;
                }
            }
        }
    }

    /* Move head backward and copy item there */
    if (q->head == 0) {
        q->head = q->length - 1;
    }
    else {
        q->head = q->head - 1;
    }

    memcpy(&q->buffer[q->head * q->item_size], pvItemToQueue, (size_t)q->item_size);
    q->count++;

    /* Wake up any waiting receiver */
    pthread_cond_signal(&q->not_empty);
    pthread_mutex_unlock(&q->mutex);

    return pdTRUE;
}

/**
 * @brief Receive (pop) an item from the queue.
 *
 * @param[in]  xQueue       Queue handle.
 * @param[out] pvBuffer     Output buffer to copy the item to (must be item_size bytes).
 * @param[in]  xTicksToWait Timeout in ticks:
 *                          - 0: do not wait
 *                          - portMAX_DELAY: wait forever
 *                          - otherwise: wait up to xTicksToWait ticks
 *
 * @return pdTRUE on success, pdFALSE on failure/timeout.
 */
static inline BaseType_t xQueueReceive(QueueHandle_t xQueue,
                                       void* const pvBuffer,
                                       TickType_t xTicksToWait)
{
    Queue_t *q;
    bool wait_forever = false;
    struct timespec deadline;

    if (!xQueue || !pvBuffer)
        return pdFALSE;

    q = (Queue_t*)xQueue;

    pthread_mutex_lock(&q->mutex);

    /* Wait until queue is not empty (if needed) */
    if (xTicksToWait == 0) {
        if (q->count == 0) {
            pthread_mutex_unlock(&q->mutex);
            return pdFALSE;
        }
    } else {
        if (xTicksToWait == portMAX_DELAY) {
            wait_forever = true;
        } else {
            wait_forever = false;
        }
        if (wait_forever == false) {
            unsigned long wait_ms = (unsigned long)xTicksToWait * (unsigned long)QUEUE_TICK_MS;
            queue_get_abstime(&deadline, wait_ms);
        }

        while (q->count == 0) {
            int rc;
            if (wait_forever == true) {
                rc = pthread_cond_wait(&q->not_empty, &q->mutex);
                if (rc != 0) { /* Handle error */
                    pthread_mutex_unlock(&q->mutex);
                    return pdFALSE;
                }
            } else {
                rc = pthread_cond_timedwait(&q->not_empty, &q->mutex, &deadline);
                if (rc == ETIMEDOUT) {
                    pthread_mutex_unlock(&q->mutex);
                    return pdFALSE;
                }
                if (rc != 0) { /* Handle other errors */
                    pthread_mutex_unlock(&q->mutex);
                    return pdFALSE;
                }
            }
        }
    }

    /* Copy item from head and advance */
    memcpy(pvBuffer, &q->buffer[q->head * q->item_size], (size_t)q->item_size);
    q->head = (q->head + 1) % q->length;
    q->count--;

    /* Wake up any waiting sender */
    pthread_cond_signal(&q->not_full);
    pthread_mutex_unlock(&q->mutex);

    return pdTRUE;
}

/**
 * @brief Delete a queue and free all resources.
 *
 * @param[in] xQueue Queue handle.
 */
static inline void vQueueDelete(QueueHandle_t xQueue)
{
    Queue_t *q;

    if (!xQueue)
        return;

    q = (Queue_t*)xQueue;

    pthread_mutex_destroy(&q->mutex);
    pthread_cond_destroy(&q->not_empty);
    pthread_cond_destroy(&q->not_full);

    free(q->buffer);
    free(q);
}

#ifdef __cplusplus
}
#endif

#endif /* ICAM_QUEUE_H */