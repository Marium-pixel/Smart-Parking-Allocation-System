#ifndef PARKING_H
#define PARKING_H

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <string.h>

// Configuration
#define MAX_SLOTS 6       // total parking slots
#define MAX_VEHICLES 15    // total vehicles to simulate
#define PARK_TIME_MIN 3    // min seconds a vehicle stays
#define PARK_TIME_MAX 6    // max seconds a vehicle stays
#define TIMEOUT_SECONDS 8  // max wait time before vehicle leaves

// Vehicle types
#define REGULAR 0
#define VIP 1

// Slot status
#define SLOT_FREE 0
#define SLOT_OCCUPIED 1

// Single parking slot
typedef struct {
    int slot_id;
    int status;        // SLOT_FREE or SLOT_OCCUPIED
    int vehicle_id;    // which vehicle is parked here
    int vehicle_type;  // REGULAR or VIP
} ParkingSlot;

// Single vehicle
typedef struct {
    int vehicle_id;
    int vehicle_type;  // REGULAR or VIP
    int arrival_delay; // seconds before this vehicle arrives
    int park_duration; // seconds this vehicle stays parked
} Vehicle;

// Activity log entry
typedef struct {
    int vehicle_id;
    int slot_id;
    char action[20];   // "PARKED" or "LEFT" or "TIMEOUT"
    time_t timestamp;
} LogEntry;

// Queue node
typedef struct QNode {
    int vehicle_id;
    int vehicle_type;
    struct QNode* next;
} QNode;

// Queue structure
typedef struct {
    QNode* front;
    QNode* rear;
    int    size;
} WaitQueue;


// Shared state — everyone accesses this
extern ParkingSlot parking_lot[MAX_SLOTS];
extern LogEntry activity_log[100];
extern int log_count;
extern int total_parked;
extern int total_waiting;
extern int total_timeout;
extern volatile int simulation_running;

// Sync primitives — Armeen initializes mutex, you initialize semaphore
extern sem_t parking_sem;
extern pthread_mutex_t lot_mutex;

extern WaitQueue wait_queue;
extern pthread_mutex_t queue_mutex;
extern pthread_cond_t  slot_available;  // signals when a slot is freed

// Function declarations
void* vehicle_thread(void* arg);
void  arrive(Vehicle* v);
void  park(Vehicle* v, int slot_id);
void  leave(Vehicle* v, int slot_id);
void  log_event(int vehicle_id, int slot_id, const char* action);
int   find_free_slot();
void  enqueue_vehicle(int vehicle_id, int vehicle_type);
int   dequeue_vehicle();   // returns vehicle_id of next in line, -1 if empty
int   is_my_turn(int vehicle_id);
void  init_semaphore();    // already exists, keep it
//int wait_for_slot(Vehicle *v);
//void release_slot(void);
#endif
