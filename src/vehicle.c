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

    printf("[VEHICLE %d] parked in slot %d\n",
           v->vehicle_id, slot_id);

    sleep(v->park_duration);
}

void leave(Vehicle* v, int slot_id) {
    pthread_mutex_lock(&lot_mutex);

    parking_lot[slot_id].status = SLOT_FREE;
    parking_lot[slot_id].vehicle_id = -1;

    total_parked--;

    pthread_mutex_unlock(&lot_mutex);

    log_event(v->vehicle_id, slot_id, "LEFT");

    printf("[VEHICLE %d] left slot %d\n",
           v->vehicle_id, slot_id);
}

void* vehicle_thread(void* arg) {
    Vehicle* v = (Vehicle*)arg;

    arrive(v);

    if (wait_for_slot(v) == 0)
        return NULL;

    int slot_id = find_free_slot();

    if (slot_id == -1) {
        release_slot();
        return NULL;
    }

    park(v, slot_id);
    leave(v, slot_id);

    release_slot();

    return NULL;
}
