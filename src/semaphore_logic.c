#include "parking.h"
#include <errno.h>

// Shared queue objects — declared extern in parking.h
WaitQueue       wait_queue     = {NULL, NULL, 0};
pthread_mutex_t queue_mutex    = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  slot_available = PTHREAD_COND_INITIALIZER;

// ─────────────────────────────────────────────
void init_semaphore() {
    if (sem_init(&parking_sem, 0, MAX_SLOTS) != 0) {
        perror("Semaphore initialization failed");
        exit(1);
    }
    printf("[SYSTEM] Semaphore initialized with %d slots\n", MAX_SLOTS);
}

// ─────────────────────────────────────────────
// Enqueue — must be called WITH queue_mutex already locked
static void enqueue_locked(int vehicle_id, int vehicle_type) {
    QNode* node        = malloc(sizeof(QNode));
    node->vehicle_id   = vehicle_id;
    node->vehicle_type = vehicle_type;
    node->next         = NULL;

    if (vehicle_type == VIP && wait_queue.front != NULL) {
        // VIP jumps to front
        node->next       = wait_queue.front;
        wait_queue.front = node;
    } else {
        // Regular goes to back
        if (wait_queue.rear == NULL)
            wait_queue.front = node;
        else
            wait_queue.rear->next = node;
        wait_queue.rear = node;
    }
    wait_queue.size++;

    printf("[QUEUE ] Vehicle %2d (%-7s) joined queue. Queue size: %d\n",
        vehicle_id,
        vehicle_type == VIP ? "VIP" : "Regular",
        wait_queue.size);
}

// ─────────────────────────────────────────────
// Dequeue — must be called WITH queue_mutex already locked
int dequeue_vehicle() {
    if (wait_queue.front == NULL) return -1;

    QNode* node      = wait_queue.front;
    int    vid       = node->vehicle_id;
    wait_queue.front = node->next;
    if (wait_queue.front == NULL)
        wait_queue.rear = NULL;
    wait_queue.size--;
    free(node);
    return vid;
}

// ─────────────────────────────────────────────
// Check if this vehicle is at the front — called WITH queue_mutex locked
int is_my_turn(int vehicle_id) {
    return (wait_queue.front != NULL &&
            wait_queue.front->vehicle_id == vehicle_id);
}

// ─────────────────────────────────────────────
// Remove a specific vehicle from queue (timeout cleanup)
// Called WITH queue_mutex already locked
static void remove_from_queue(int vehicle_id) {
    QNode* prev = NULL;
    QNode* cur  = wait_queue.front;
    while (cur != NULL) {
        if (cur->vehicle_id == vehicle_id) {
            if (prev) prev->next    = cur->next;
            else      wait_queue.front = cur->next;
            if (cur == wait_queue.rear) wait_queue.rear = prev;
            free(cur);
            wait_queue.size--;
            return;
        }
        prev = cur;
        cur  = cur->next;
    }
}

// ─────────────────────────────────────────────
int wait_for_slot(Vehicle* v) {

    // ── Lock queue_mutex FIRST, then enqueue atomically ──────
    pthread_mutex_lock(&queue_mutex);
    enqueue_locked(v->vehicle_id, v->vehicle_type);

    // Build timeout deadline
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += TIMEOUT_SECONDS;

    // Wait until we are at the front of the queue
    while (!is_my_turn(v->vehicle_id)) {
        int rc = pthread_cond_timedwait(&slot_available, &queue_mutex, &deadline);
        if (rc == ETIMEDOUT) {
            remove_from_queue(v->vehicle_id);
            pthread_mutex_unlock(&queue_mutex);

            printf("[VEHICLE %2d] Timed out in queue! Leaving.\n", v->vehicle_id);
            pthread_mutex_lock(&lot_mutex);
            total_timeout++;
            total_waiting--;
            pthread_mutex_unlock(&lot_mutex);
            log_event(v->vehicle_id, -1, "TIMEOUT");
            return 0;
        }
    }

    // We are at the front — dequeue ourselves
    dequeue_vehicle();
    pthread_mutex_unlock(&queue_mutex);

    // Acquire semaphore (blocks if lot is full, but we are next in line)
    sem_wait(&parking_sem);
    return 1;
}

// ─────────────────────────────────────────────
void release_slot() {
    sem_post(&parking_sem);

    // Wake all waiters — the one at queue front will proceed
    pthread_mutex_lock(&queue_mutex);
    pthread_cond_broadcast(&slot_available);
    pthread_mutex_unlock(&queue_mutex);
}
