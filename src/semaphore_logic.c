#include "parking.h"
#include <errno.h>

// Define the new shared objects here (declare extern in parking.h)
WaitQueue       wait_queue   = {NULL, NULL, 0};
pthread_mutex_t queue_mutex  = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  slot_available = PTHREAD_COND_INITIALIZER;

void init_semaphore() {
    if (sem_init(&parking_sem, 0, MAX_SLOTS) != 0) {
        perror("Semaphore initialization failed");
        exit(1);
    }
    printf("[SYSTEM] Semaphore initialized with %d slots\n", MAX_SLOTS);
}

// Add vehicle to queue — VIP goes to front, REGULAR goes to back
void enqueue_vehicle(int vehicle_id, int vehicle_type) {
    QNode* node = malloc(sizeof(QNode));
    node->vehicle_id   = vehicle_id;
    node->vehicle_type = vehicle_type;
    node->next         = NULL;

    pthread_mutex_lock(&queue_mutex);

    if (vehicle_type == VIP && wait_queue.front != NULL) {
        // VIP: insert at front
        node->next       = wait_queue.front;
        wait_queue.front = node;
    } else {
        // REGULAR (or empty queue): insert at back
        if (wait_queue.rear == NULL) {
            wait_queue.front = node;
        } else {
            wait_queue.rear->next = node;
        }
        wait_queue.rear = node;
    }
    wait_queue.size++;
    printf("[QUEUE ] Vehicle %d (%s) joined queue. Queue size: %d\n",
        vehicle_id,
        vehicle_type == VIP ? "VIP" : "Regular",
        wait_queue.size);

    pthread_mutex_unlock(&queue_mutex);
}

// Remove and return the front vehicle_id
int dequeue_vehicle() {
    // Caller must hold queue_mutex
    if (wait_queue.front == NULL) return -1;

    QNode* node = wait_queue.front;
    int vid     = node->vehicle_id;

    wait_queue.front = node->next;
    if (wait_queue.front == NULL)
        wait_queue.rear = NULL;
    wait_queue.size--;

    free(node);
    return vid;
}

// Check if this vehicle is at the front of the queue
int is_my_turn(int vehicle_id) {
    // Caller must hold queue_mutex
    return (wait_queue.front != NULL &&
            wait_queue.front->vehicle_id == vehicle_id);
}

// Wait for a slot — returns 1 if parked, 0 if timed out
int wait_for_slot(Vehicle* v) {
    // 1. Join the queue
    enqueue_vehicle(v->vehicle_id, v->vehicle_type);

    // 2. Set up timeout deadline
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += TIMEOUT_SECONDS;

    pthread_mutex_lock(&queue_mutex);

    // 3. Wait until it's this vehicle's turn AND a semaphore slot is free
    while (!is_my_turn(v->vehicle_id)) {
        int rc = pthread_cond_timedwait(&slot_available, &queue_mutex, &deadline);
        if (rc == ETIMEDOUT) {
            // Remove self from queue
            QNode* prev = NULL;
            QNode* cur  = wait_queue.front;
            while (cur != NULL) {
                if (cur->vehicle_id == v->vehicle_id) {
                    if (prev) prev->next = cur->next;
                    else      wait_queue.front = cur->next;
                    if (cur == wait_queue.rear) wait_queue.rear = prev;
                    free(cur);
                    wait_queue.size--;
                    break;
                }
                prev = cur;
                cur  = cur->next;
            }
            pthread_mutex_unlock(&queue_mutex);

            printf("[VEHICLE %d] Timed out in queue! Leaving.\n", v->vehicle_id);
            pthread_mutex_lock(&lot_mutex);
            total_timeout++;
            total_waiting--;
            pthread_mutex_unlock(&lot_mutex);
            log_event(v->vehicle_id, -1, "TIMEOUT");
            return 0;
        }
    }

    // 4. It's our turn — dequeue ourselves and grab semaphore
    dequeue_vehicle();
    pthread_mutex_unlock(&queue_mutex);

    // Now actually acquire the semaphore (slot count)
    sem_wait(&parking_sem);
    return 1;
}

// Called after a vehicle leaves — signal queue that a slot opened
void release_slot() {
    sem_post(&parking_sem);

    // Wake up all waiting vehicles so the front one can check
    pthread_mutex_lock(&queue_mutex);
    pthread_cond_broadcast(&slot_available);
    pthread_mutex_unlock(&queue_mutex);
}
