#include "parking.h"
#include <errno.h>
#include <stdlib.h>

void init_semaphore() {
    sem_init(&parking_sem, 0, MAX_SLOTS);
}

// enqueue helper
static void enqueue_locked(int id, int type) {
    QNode* node = malloc(sizeof(QNode));
    node->vehicle_id   = id;
    node->vehicle_type = type;
    node->next         = NULL;

    if (type == VIP && wait_queue.front != NULL) {
        node->next       = wait_queue.front;
        wait_queue.front = node;
    } else {
        if (!wait_queue.rear)
            wait_queue.front = node;
        else
            wait_queue.rear->next = node;
        wait_queue.rear = node;
    }
    wait_queue.size++;

    printf("[QUEUE ] Vehicle %2d (%-7s) joined queue. Queue size: %d\n",
        id,
        type == VIP ? "VIP" : "Regular",
        wait_queue.size);
}

int is_my_turn(int vehicle_id) {
    return wait_queue.front &&
           wait_queue.front->vehicle_id == vehicle_id;
}

static void remove_from_queue(int id) {
    QNode *prev = NULL, *cur = wait_queue.front;

    while (cur) {
        if (cur->vehicle_id == id) {
            if (prev) prev->next = cur->next;
            else wait_queue.front = cur->next;

            if (cur == wait_queue.rear)
                wait_queue.rear = prev;

            free(cur);
            wait_queue.size--;
            return;
        }
        prev = cur;
        cur = cur->next;
    }
}

int wait_for_slot(Vehicle* v) {
    pthread_mutex_lock(&queue_mutex);

    enqueue_locked(v->vehicle_id, v->vehicle_type);

    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += TIMEOUT_SECONDS;

    while (!is_my_turn(v->vehicle_id)) {
        int rc = pthread_cond_timedwait(
            &slot_available,
            &queue_mutex,
            &deadline
        );

        if (rc == ETIMEDOUT) {
    remove_from_queue(v->vehicle_id);
    pthread_mutex_unlock(&queue_mutex);
    pthread_mutex_lock(&lot_mutex);
    total_timeout++;
    total_waiting--;
    pthread_mutex_unlock(&lot_mutex);
    log_event(v->vehicle_id, -1, "TIMEOUT");   // ← add this
    printf("[VEHICLE %2d] Timed out! Leaving.\n", v->vehicle_id);
    return 0;
}
    }
remove_from_queue(v->vehicle_id);
pthread_mutex_unlock(&queue_mutex);

// Poll semaphore so shutdown signal is not missed
while (sem_trywait(&parking_sem) != 0) {
    if (!simulation_running) return 0;
    usleep(50000);   // 50ms between retries
}
return 1;
return 1;
    return 1;
}

void release_slot() {
    sem_post(&parking_sem);

    pthread_mutex_lock(&queue_mutex);
    pthread_cond_broadcast(&slot_available);
    pthread_mutex_unlock(&queue_mutex);
}
