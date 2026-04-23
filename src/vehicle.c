#include "parking.h"

// Find a free slot in the parking lot
// Must be called with mutex locked
int find_free_slot() {
    for(int i = 0; i < MAX_SLOTS; i++) {
        if(parking_lot[i].status == SLOT_FREE) {
            return i;
        }
    }
    return -1; // no free slot found
}

// Log an event to the activity log
void log_event(int vehicle_id, int slot_id, const char* action) {
    pthread_mutex_lock(&lot_mutex);
    if(log_count < 100) {
        activity_log[log_count].vehicle_id = vehicle_id;
        activity_log[log_count].slot_id    = slot_id;
        strncpy(activity_log[log_count].action, action, 19);
        activity_log[log_count].timestamp  = time(NULL);
        log_count++;
    }
    pthread_mutex_unlock(&lot_mutex);
}

// Vehicle arrives
void arrive(Vehicle* v) {
    // Simulate arrival delay
    sleep(v->arrival_delay);
    printf("[VEHICLE %d] (%s) arrived.\n",
        v->vehicle_id,
        v->vehicle_type == VIP ? "VIP" : "Regular");

    pthread_mutex_lock(&lot_mutex);
    total_waiting++;
    pthread_mutex_unlock(&lot_mutex);
}

// Vehicle parks in a slot
void park(Vehicle* v, int slot_id) {
    pthread_mutex_lock(&lot_mutex);
    parking_lot[slot_id].status       = SLOT_OCCUPIED;
    parking_lot[slot_id].vehicle_id   = v->vehicle_id;
    parking_lot[slot_id].vehicle_type = v->vehicle_type;
    total_parked++;
    total_waiting--;
    pthread_mutex_unlock(&lot_mutex);

    log_event(v->vehicle_id, slot_id, "PARKED");
    printf("[VEHICLE %d] (%s) parked in slot %d. Staying for %d seconds.\n",
        v->vehicle_id,
        v->vehicle_type == VIP ? "VIP" : "Regular",
        slot_id,
        v->park_duration);

    // Stay parked for the duration
    sleep(v->park_duration);
}

// Vehicle leaves the slot
void leave(Vehicle* v, int slot_id) {
    pthread_mutex_lock(&lot_mutex);
    parking_lot[slot_id].status     = SLOT_FREE;
    parking_lot[slot_id].vehicle_id = -1;
    total_parked--;
    pthread_mutex_unlock(&lot_mutex);

    log_event(v->vehicle_id, slot_id, "LEFT");
    printf("[VEHICLE %d] left slot %d.\n", v->vehicle_id, slot_id);
}

// The main thread function — each vehicle runs this
void* vehicle_thread(void* arg) {
    Vehicle* v = (Vehicle*)arg;

    // Step 1: arrive
    arrive(v);

    // Step 2: wait for a slot (with timeout)
    if(wait_for_slot(v) == 0) {
        return NULL; // timed out, vehicle leaves
    }

    // Step 3: find which slot to use (mutex protected)
    pthread_mutex_lock(&lot_mutex);
    int slot_id = find_free_slot();
    if(slot_id == -1) {
        // Shouldn't happen if semaphore is correct
        pthread_mutex_unlock(&lot_mutex);
        release_slot();
        return NULL;
    }
    // Reserve the slot immediately while still locked
    parking_lot[slot_id].status = SLOT_OCCUPIED;
    pthread_mutex_unlock(&lot_mutex);

    // Step 4: park
    park(v, slot_id);

    // Step 5: leave
    leave(v, slot_id);

    // Step 6: release semaphore so waiting vehicle can enter
    release_slot();

    return NULL;
}
