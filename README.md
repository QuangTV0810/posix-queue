# pthread-queue (FreeRTOS-like Queue API for POSIX)

A tiny **header-only**, **thread-safe** queue for Linux/POSIX using **`pthread_mutex_t`** and **`pthread_cond_t`**.
The API is intentionally modeled after a minimal subset of the **FreeRTOS Queue** functions.

This repo includes:
- `queue.h`: the queue implementation (ring buffer + pthread sync)
- `main.c`: a simple producer/consumer demo

---

## Features

- Fixed-size **ring buffer** (FIFO)
- **By-value** item storage (`item_size` bytes per item)
- Multiple producers / multiple consumers (protected by a single mutex)
- Blocking / timed blocking semantics similar to FreeRTOS:
  - `0` ticks: do not wait
  - `portMAX_DELAY`: wait forever
  - otherwise: wait up to `xTicksToWait` ticks
- Extra API: **send to front** (priority push)

---

## Build & Run (demo)

```bash
gcc -Wall -Wextra -O2 -pthread main.c -o queue_demo
./queue_demo
```

You should see the producer sending numbers and the consumer receiving them (consumer is slower so the queue can become full).

---

## Usage

Include the header:

```c
#include "queue.h"
```

Create a queue:

```c
QueueHandle_t q = xQueueCreate(QUEUE_LEN, sizeof(int));
```

Send / Receive:

```c
int v = 123;
xQueueSend(q, &v, 100);                 // wait up to 100 ticks if full

int out = 0;
xQueueReceive(q, &out, portMAX_DELAY);  // wait forever until an item arrives
```

Delete when done:

```c
vQueueDelete(q);
```

---

## API Reference

### `QueueHandle_t xQueueCreate(UBaseType_t uxQueueLength, UBaseType_t uxItemSize)`
Creates a queue able to store `uxQueueLength` items, each `uxItemSize` bytes.

Returns `NULL` on allocation/init failure.

### `BaseType_t xQueueSend(QueueHandle_t xQueue, const void* pvItemToQueue, TickType_t xTicksToWait)`
Pushes an item to the **back** of the queue (FIFO).

Returns:
- `pdTRUE` on success
- `pdFALSE` on failure/timeout

### `BaseType_t xQueueSendToFront(QueueHandle_t xQueue, const void* pvItemToQueue, TickType_t xTicksToWait)`
Pushes an item to the **front** of the queue (priority insert).  
The next `xQueueReceive()` will return this item before older items.

### `BaseType_t xQueueReceive(QueueHandle_t xQueue, void* pvBuffer, TickType_t xTicksToWait)`
Pops an item from the **front** of the queue into `pvBuffer` (must be at least `item_size` bytes).

Returns:
- `pdTRUE` on success
- `pdFALSE` on failure/timeout

### `void vQueueDelete(QueueHandle_t xQueue)`
Destroys mutex/condition variables and frees the queue buffer + object.

---

## Timeout / Tick configuration

Timeouts are expressed in **ticks**. The conversion is:

```
wait_ms = xTicksToWait * QUEUE_TICK_MS
```

You can override `QUEUE_TICK_MS` at compile time:

```bash
gcc -DQUEUE_TICK_MS=10 -pthread main.c -o queue_demo
```

Or in a config header before including `queue.h`.

---

## Notes & Caveats

- **Clock source:** `pthread_cond_timedwait()` uses `CLOCK_REALTIME` by default, and this implementation builds deadlines using `CLOCK_REALTIME`.
  If the system time jumps (NTP adjustment / manual clock change), the effective timeout may shift.
- This queue stores items **by value** (copies bytes). If you store pointers, you manage the pointed memory lifetime yourself.
- Condition variables use `pthread_cond_signal()` (wake one waiter). This is typically what you want for queue semantics.

---

## License

No license file is included in this snippet. If you plan to publish/distribute, consider adding a license (MIT/BSD/Apache-2.0, etc.).