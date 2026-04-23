#include "parking.h"

// Initialize semaphore with number of available slots
void init_semaphore() {
    if(sem_init(&parking_sem, 0, MAX_SLOTS) != 0) {
        perror("Semaphore initialization failed");
        exit(1);
    }
    printf("[SYSTEM] Semaphore initialized with %d slots\n", MAX_SLOTS);
}

// Vehicle waits for a slot to become available
// Returns 1 if got slot, 0 if timed out
int wait_for_slot(Vehicle* v) {
    struct timespec timeout;
    clock_gettime(CLOCK_REALTIME, &timeout);
    timeout.tv_sec += TIMEOUT_SECONDS;

    printf("[VEHICLE %d] (%s) waiting for a slot...\n",
        v->vehicle_id,
        v->vehicle_type == VIP ? "VIP" : "Regular");

    // sem_timedwait blocks until slot available OR timeout
    if(sem_timedwait(&parking_sem, &timeout) == 0) {
        return 1; // got a slot
    } else {
        printf("[VEHICLE %d] Timed out! Leaving without parking.\n", v->vehicle_id);
        pthread_mutex_lock(&lot_mutex);
        total_timeout++;
        total_waiting--;
        pthread_mutex_unlock(&lot_mutex);
        log_event(v->vehicle_id, -1, "TIMEOUT");
        return 0; // timed out
    }
}

// Release slot when vehicle leaves
void release_slot() {
    sem_post(&parking_sem);
}
