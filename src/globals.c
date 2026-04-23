#include "parking.h"

// ───── Shared Parking State ─────
ParkingSlot parking_lot[MAX_SLOTS];

LogEntry activity_log[100];
int log_count = 0;

int total_parked = 0;
int total_waiting = 0;
int total_timeout = 0;

// ───── Sync Primitives ─────
sem_t parking_sem;
pthread_mutex_t lot_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t queue_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t slot_available = PTHREAD_COND_INITIALIZER;

// ───── Queue ─────
WaitQueue wait_queue = {NULL, NULL, 0};
