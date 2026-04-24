#include "parking.h"

// Find free slot
int find_free_slot() {
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (parking_lot[i].status == SLOT_FREE)
            return i;
    }
    return -1;
}

void log_event(int vehicle_id, int slot_id, const char* action) {
    pthread_mutex_lock(&lot_mutex);

    if (log_count < 100) {
        activity_log[log_count].vehicle_id = vehicle_id;
        activity_log[log_count].slot_id = slot_id;
        strncpy(activity_log[log_count].action, action, 19);
        activity_log[log_count].timestamp = time(NULL);
        log_count++;
    }

    pthread_mutex_unlock(&lot_mutex);
}

void arrive(Vehicle* v) {
    sleep(v->arrival_delay);

    printf("[VEHICLE %d] (%s) arrived.\n",
           v->vehicle_id,
           v->vehicle_type == VIP ? "VIP" : "Regular");

    pthread_mutex_lock(&lot_mutex);
    total_waiting++;
    pthread_mutex_unlock(&lot_mutex);
}

void park(Vehicle* v, int slot_id) {
    pthread_mutex_lock(&lot_mutex);

    parking_lot[slot_id].status = SLOT_OCCUPIED;
    parking_lot[slot_id].vehicle_id = v->vehicle_id;
    parking_lot[slot_id].vehicle_type = v->vehicle_type;

    total_parked++;
    total_waiting--;

    pthread_mutex_unlock(&lot_mutex);

    log_event(v->vehicle_id, slot_id, "PARKED");

printf("[VEHICLE %2d] (%-7s) parked in slot %d. Staying for %d seconds.\n",
    v->vehicle_id,
    v->vehicle_type == VIP ? "VIP" : "Regular",
    slot_id,
    v->park_duration);

    sleep(v->park_duration);
}

void leave(Vehicle* v, int slot_id) {
    pthread_mutex_lock(&lot_mutex);
    parking_lot[slot_id].status     = SLOT_FREE;
    parking_lot[slot_id].vehicle_id = -1;
    total_parked--;
    pthread_mutex_unlock(&lot_mutex);

    log_event(v->vehicle_id, slot_id, "LEFT");
    printf("[VEHICLE %2d] (%-7s) left slot %d.\n",
        v->vehicle_id,
        v->vehicle_type == VIP ? "VIP" : "Regular",
        slot_id);
}

void* vehicle_thread(void* arg) {
    Vehicle* v = (Vehicle*)arg;

    if (!simulation_running) return NULL;   // guard for clean shutdown
    arrive(v);
    if (!simulation_running) return NULL;

    if (wait_for_slot(v) == 0)
        return NULL;

    // Find and claim slot atomically under mutex
pthread_mutex_lock(&lot_mutex);
int slot_id = find_free_slot();
if (slot_id == -1) {
    total_waiting--;           // ← fix inflated waiting count
    pthread_mutex_unlock(&lot_mutex);
    release_slot();
    return NULL;
}

    // Reserve immediately while still locked
    parking_lot[slot_id].status       = SLOT_OCCUPIED;
    parking_lot[slot_id].vehicle_id   = v->vehicle_id;
    parking_lot[slot_id].vehicle_type = v->vehicle_type;
    total_parked++;
    total_waiting--;
    pthread_mutex_unlock(&lot_mutex);

    log_event(v->vehicle_id, slot_id, "PARKED");
    printf("[VEHICLE %2d] (%-7s) parked in slot %d. Staying for %d seconds.\n",
        v->vehicle_id,
        v->vehicle_type == VIP ? "VIP" : "Regular",
        slot_id,
        v->park_duration);

    sleep(v->park_duration);
    leave(v, slot_id);
    release_slot();
    return NULL;
}
