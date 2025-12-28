#include <stdio.h>
#include <pthread.h>
#include <unistd.h>
#include "queue.h"

#define QUEUE_LEN  5
#define ITEM_SIZE  sizeof(int)

void* producer_thread(void* arg) {
    QueueHandle_t xQueue = (QueueHandle_t)arg;
    for (int i = 1; i <= 10; i++) {
        printf("[Producer] Sending: %d\n", i);
        
        // Send data to the queue, wait up to 100 ticks if the queue is full.
        if (xQueueSend(xQueue, &i, 100) == pdPASS) {
            printf("[Producer] Successfully sent: %d\n", i);
        } else {
            printf("[Producer] Send failure (Timeout): %d\n", i);
        }
        usleep(500000); // Sleep 500ms
    }
    return NULL;
}

void* consumer_thread(void* arg) {
    QueueHandle_t xQueue = (QueueHandle_t)arg;
    int received_item;
    
    for (int i = 1; i <= 10; i++) {
        // Receive data from the queue, wait indefinitely until the data arrives.
        if (xQueueReceive(xQueue, &received_item, portMAX_DELAY) == pdPASS) {
            printf("[Consumer] Received: %d\n", received_item);
        }
        usleep(1000000); // Sleep for 1 second (slower than Producer to test for full queues)
    }
    return NULL;
}

int main() {
    QueueHandle_t myQueue = xQueueCreate(QUEUE_LEN, ITEM_SIZE);
    if (myQueue == NULL) {
        printf("Cannot create a queue!\n");
        return 1;
    }

    pthread_t prod, cons;

    pthread_create(&prod, NULL, producer_thread, myQueue);
    pthread_create(&cons, NULL, consumer_thread, myQueue);

    pthread_join(prod, NULL);
    pthread_join(cons, NULL);

    vQueueDelete(myQueue);
    printf("The program has ended.\n");

    return 0;
}