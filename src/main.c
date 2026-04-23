#include "parking.h"

// Define shared state (declared extern in parking.h)
ParkingSlot parking_lot[MAX_SLOTS];
LogEntry    activity_log[100];
int         log_count    = 0;
int         total_parked = 0;
int         total_waiting = 0;
int         total_timeout = 0;

sem_t           parking_sem;
pthread_mutex_t lot_mutex = PTHREAD_MUTEX_INITIALIZER;

// Initialize parking slots
void init_parking_lot() {
    for(int i = 0; i < MAX_SLOTS; i++) {
        parking_lot[i].slot_id     = i;
        parking_lot[i].status      = SLOT_FREE;
        parking_lot[i].vehicle_id  = -1;
        parking_lot[i].vehicle_type = REGULAR;
    }
    printf("[SYSTEM] Parking lot initialized with %d slots.\n", MAX_SLOTS);
}

// Print final statistics
void print_stats() {
    printf("\n========== SIMULATION COMPLETE ==========\n");
    printf("Total vehicles simulated : %d\n", MAX_VEHICLES);
    printf("Total timeouts           : %d\n", total_timeout);
    printf("Activity log entries     : %d\n", log_count);
    printf("\n--- Activity Log ---\n");
    for(int i = 0; i < log_count; i++) {
        printf("Vehicle %2d | Slot %2d | %-10s\n",
            activity_log[i].vehicle_id,
            activity_log[i].slot_id,
            activity_log[i].action);
    }
    printf("=========================================\n");
}

int main() {
    srand(time(NULL));

    printf("========== SMART PARKING SYSTEM ==========\n");

    // Initialize
    init_parking_lot();
    init_semaphore();

    // Create vehicles
    Vehicle vehicles[MAX_VEHICLES];
    pthread_t threads[MAX_VEHICLES];

    for(int i = 0; i < MAX_VEHICLES; i++) {
        vehicles[i].vehicle_id    = i + 1;
        vehicles[i].vehicle_type  = (i % 4 == 0) ? VIP : REGULAR; // every 4th is VIP
        vehicles[i].arrival_delay = rand() % 3;                    // 0-2 seconds
        vehicles[i].park_duration = PARK_TIME_MIN + rand() % PARK_TIME_MAX;
    }

    // Launch all vehicle threads
    for(int i = 0; i < MAX_VEHICLES; i++) {
        pthread_create(&threads[i], NULL, vehicle_thread, &vehicles[i]);
    }

    // Wait for all threads to finish
    for(int i = 0; i < MAX_VEHICLES; i++) {
        pthread_join(threads[i], NULL);
    }

    // Print stats
    print_stats();

    // Cleanup
    sem_destroy(&parking_sem);
    pthread_mutex_destroy(&lot_mutex);
    pthread_mutex_destroy(&queue_mutex);
    pthread_cond_destroy(&slot_available);
    return 0;
}

