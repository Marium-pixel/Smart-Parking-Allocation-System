// main.c — Smart Parking System
// - Continuous simulation: waves of vehicles keep arriving until user closes window
// - 6 parking slots (2 rows × 3 cols) for better visual pressure
// - Professional dark dashboard GUI
// - Clean shutdown: sets simulation_running=0, detaches threads, then exits

#include "parking.h"
#include "raylib.h"
#include <math.h>

// ─── Layout ───────────────────────────────────────────────────────────────────
#define COLS          3
#define SLOT_W        140
#define SLOT_H        115
#define SLOT_GAP_X    20
#define SLOT_GAP_Y    20
#define GRID_X        390        // shifted right
#define GRID_Y        115

// ─── Simulation tuning ────────────────────────────────────────────────────────
#define ARRIVAL_DELAY_MIN   3    // min seconds before a vehicle tries to park
#define ARRIVAL_DELAY_MAX   6    // max seconds before a vehicle tries to park
#define PARK_DURATION_MIN   6    // min seconds a vehicle stays parked
#define PARK_DURATION_MAX   12   // max seconds a vehicle stays parked
#define WAVE_SPAWN_DELAY    600000 // µs between thread spawns within one wave
#define WAVE_INTERVAL_SEC   15.f   // seconds between new waves

// ─── Palette ──────────────────────────────────────────────────────────────────
#define COL_BG         (Color){13,  17,  23,  255}
#define COL_PANEL      (Color){22,  30,  41,  255}
#define COL_BORDER     (Color){40,  55,  75,  255}
#define COL_FREE       (Color){28,  42,  58,  255}
#define COL_REG        (Color){180, 50,  50,  255}
#define COL_VIP        (Color){215, 160, 30,  255}
#define COL_VIP_DIM    (Color){90,  65,  8,   255}
#define COL_ACCENT     (Color){0,   180, 130, 255}
#define COL_TEXT_HI    (Color){220, 235, 255, 255}
#define COL_TEXT_MID   (Color){140, 160, 185, 255}
#define COL_TEXT_DIM   (Color){70,  90,  115, 255}
#define COL_STAT_OK    (Color){40,  200, 120, 255}
#define COL_STAT_WAIT  (Color){240, 160, 30,  255}
#define COL_STAT_FAIL  (Color){220, 70,  70,  255}

// ─── Continuous simulation ────────────────────────────────────────────────────
// Declare as extern volatile int in parking.h so vehicle_thread() can read it
volatile int simulation_running = 1;

static int next_vehicle_id = 1;

#define MAX_ACTIVE_THREADS 128
static pthread_t active_threads[MAX_ACTIVE_THREADS];
static Vehicle   active_vehicles[MAX_ACTIVE_THREADS];
static int       threads_launched = 0;

// ─── Animation state per slot ─────────────────────────────────────────────────
typedef struct { float pulse; int last_status; float flash; } SlotAnim;
static SlotAnim slot_anim[MAX_SLOTS];

// ─── Helpers ──────────────────────────────────────────────────────────────────
static void DrawCard(int x, int y, int w, int h, Color fill, Color border) {
    DrawRectangleRounded((Rectangle){x, y, w, h}, 0.12f, 8, fill);
    DrawRectangleRoundedLines((Rectangle){x, y, w, h}, 0.12f, 8, border);
}

static void DrawStat(int x, int y, const char *label, int value, Color valColor) {
    DrawCard(x, y, 165, 62, COL_PANEL, COL_BORDER);
    DrawTextEx(GetFontDefault(), label,   (Vector2){x+14, y+9},  12, 1, COL_TEXT_DIM);
    DrawTextEx(GetFontDefault(), TextFormat("%d", value),
                                          (Vector2){x+14, y+28}, 22, 1, valColor);
}

static void DrawDivider(int x, int y, int w) {
    DrawRectangle(x, y, w, 1, COL_BORDER);
}

// ─── Spawn one wave ───────────────────────────────────────────────────────────
static void spawn_wave(int wave_num) {
    printf("[SYSTEM] Spawning wave %d (%d vehicles)...\n", wave_num, MAX_VEHICLES);
    for (int i = 0; i < MAX_VEHICLES; i++) {
        int slot = threads_launched % MAX_ACTIVE_THREADS;

        // Detach old thread occupying this slot (if any); it's done by now
        pthread_detach(active_threads[slot]);

        active_vehicles[slot].vehicle_id    = next_vehicle_id++;
        active_vehicles[slot].vehicle_type  = (next_vehicle_id % 4 == 0) ? VIP : REGULAR;
        active_vehicles[slot].arrival_delay = ARRIVAL_DELAY_MIN
                                            + rand() % (ARRIVAL_DELAY_MAX - ARRIVAL_DELAY_MIN + 1);
        active_vehicles[slot].park_duration = PARK_DURATION_MIN
                                            + rand() % (PARK_DURATION_MAX - PARK_DURATION_MIN + 1);

        pthread_create(&active_threads[slot], NULL,
                       vehicle_thread, &active_vehicles[slot]);
        threads_launched++;
        usleep(WAVE_SPAWN_DELAY);
    }
}

// ─── Init ─────────────────────────────────────────────────────────────────────
void init_parking_lot(void) {
    for (int i = 0; i < MAX_SLOTS; i++) {
        parking_lot[i].slot_id      = i;
        parking_lot[i].status       = SLOT_FREE;
        parking_lot[i].vehicle_id   = -1;
        parking_lot[i].vehicle_type = REGULAR;
        slot_anim[i].pulse          = (float)i / MAX_SLOTS;
        slot_anim[i].last_status    = SLOT_FREE;
        slot_anim[i].flash          = 0.f;
    }
    printf("[SYSTEM] Parking lot initialized — %d slots.\n", MAX_SLOTS);
}

// ─── Main ─────────────────────────────────────────────────────────────────────
int main(void) {
    srand((unsigned)time(NULL));
    printf("========== SMART PARKING SYSTEM ==========\n");
    printf("[SYSTEM] Close the window or press ESC to stop.\n\n");

    init_parking_lot();
    init_semaphore();

    const int WIN_W = 1020;
    const int WIN_H = 650;
    InitWindow(WIN_W, WIN_H, "Smart Parking — Dashboard");
    SetTargetFPS(60);

    float time_acc   = 0.f;
    float wave_timer = WAVE_INTERVAL_SEC; // fire immediately on first frame
    int   wave_count = 0;

    typedef struct { int vid; int vtype; } QEntry;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        time_acc   += dt;
        wave_timer += dt;

        // ── Spawn a new wave every WAVE_INTERVAL_SEC ─────────────────────────
        if (wave_timer >= WAVE_INTERVAL_SEC) {
            wave_timer = 0.f;
            wave_count++;
            spawn_wave(wave_count);
        }

        // ── Snapshot shared state (minimal lock time) ─────────────────────────
        pthread_mutex_lock(&lot_mutex);
        int snap_parked  = total_parked;
        int snap_waiting = total_waiting;
        int snap_timeout = total_timeout;
        int snap_status[MAX_SLOTS], snap_vid[MAX_SLOTS], snap_vtype[MAX_SLOTS];
        for (int i = 0; i < MAX_SLOTS; i++) {
            snap_status[i] = parking_lot[i].status;
            snap_vid[i]    = parking_lot[i].vehicle_id;
            snap_vtype[i]  = parking_lot[i].vehicle_type;
        }
        pthread_mutex_unlock(&lot_mutex);

        pthread_mutex_lock(&queue_mutex);
        QEntry q_snap[MAX_ACTIVE_THREADS];
        int    q_count = 0;
        for (QNode *n = wait_queue.front; n && q_count < MAX_ACTIVE_THREADS; n = n->next) {
            q_snap[q_count].vid   = n->vehicle_id;
            q_snap[q_count].vtype = n->vehicle_type;
            q_count++;
        }
        pthread_mutex_unlock(&queue_mutex);

        // ── Slot animations ───────────────────────────────────────────────────
        for (int i = 0; i < MAX_SLOTS; i++) {
            slot_anim[i].pulse += dt * 0.8f;
            if (slot_anim[i].pulse > 1.f) slot_anim[i].pulse -= 1.f;

            if (snap_status[i] != slot_anim[i].last_status) {
                slot_anim[i].flash       = 1.f;
                slot_anim[i].last_status = snap_status[i];
            }
            slot_anim[i].flash -= dt * 2.2f;
            if (slot_anim[i].flash < 0.f) slot_anim[i].flash = 0.f;
        }

        // ── Draw ──────────────────────────────────────────────────────────────
        BeginDrawing();
        ClearBackground(COL_BG);

        // Header
        DrawRectangle(0, 0, WIN_W, 62, COL_PANEL);
        DrawRectangle(0, 61, WIN_W, 1, COL_ACCENT);
        DrawRectangle(0, 0, 4, 62, COL_ACCENT);
        DrawTextEx(GetFontDefault(), "SMART PARKING", (Vector2){20, 10},  28, 3, COL_TEXT_HI);
        DrawTextEx(GetFontDefault(), "DASHBOARD",     (Vector2){23, 39},  12, 2, COL_ACCENT);

        // Live dot + next wave countdown
        float blink  = (sinf(time_acc * 3.14f) + 1.f) * 0.5f;
        Color dotCol = ColorLerp(COL_STAT_FAIL, COL_STAT_OK, blink);
        DrawCircle(WIN_W - 30, 31, 6, dotCol);
        DrawTextEx(GetFontDefault(), "LIVE", (Vector2){WIN_W - 72, 22}, 11, 1, COL_TEXT_DIM);
        int secs_left = (int)(WAVE_INTERVAL_SEC - wave_timer) + 1;
        DrawTextEx(GetFontDefault(),
                   TextFormat("Next wave: %ds", secs_left),
                   (Vector2){WIN_W - 135, 38}, 10, 1, COL_TEXT_DIM);

        // ── Left panel ────────────────────────────────────────────────────────
        int lx = 18;

        DrawTextEx(GetFontDefault(), "STATISTICS", (Vector2){lx, 78}, 11, 2, COL_TEXT_DIM);
        DrawDivider(lx, 94, 352);

        DrawStat(lx,       100, "PARKED",   snap_parked,  COL_STAT_OK);
        DrawStat(lx + 178, 100, "WAITING",  snap_waiting, COL_STAT_WAIT);
        DrawStat(lx,       172, "TIMEOUTS", snap_timeout, COL_STAT_FAIL);
        DrawStat(lx + 178, 172, "WAVE",     wave_count,   COL_ACCENT);

        // Capacity bar
        int   cap_y    = 252;
        float occ_frac = MAX_SLOTS > 0 ? (float)snap_parked / MAX_SLOTS : 0.f;
        int   bar_w    = 344;
        DrawTextEx(GetFontDefault(), "CAPACITY", (Vector2){lx, cap_y}, 11, 2, COL_TEXT_DIM);
        DrawDivider(lx, cap_y + 16, 352);
        DrawCard(lx, cap_y + 22, bar_w, 18, COL_BORDER, COL_BORDER);
        Color barFill = occ_frac > 0.8f ? COL_STAT_FAIL :
                        occ_frac > 0.5f ? COL_STAT_WAIT : COL_STAT_OK;
        if (occ_frac > 0.f)
            DrawRectangleRounded((Rectangle){lx+2, cap_y+24, (bar_w-4)*occ_frac, 14},
                                 0.4f, 6, barFill);
        DrawTextEx(GetFontDefault(),
                   TextFormat("%.0f%%   %d / %d slots occupied",
                              occ_frac * 100, snap_parked, MAX_SLOTS),
                   (Vector2){lx, cap_y + 46}, 11, 1, COL_TEXT_MID);

        // Legend
        int leg_y = 325;
        DrawTextEx(GetFontDefault(), "LEGEND", (Vector2){lx, leg_y}, 11, 2, COL_TEXT_DIM);
        DrawDivider(lx, leg_y + 16, 352);
        const struct { Color c; const char *label; } legend[] = {
            { COL_FREE, "Free slot"       },
            { COL_REG,  "Regular vehicle" },
            { COL_VIP,  "VIP vehicle"     },
        };
        for (int i = 0; i < 3; i++) {
            int ly = leg_y + 24 + i * 26;
            DrawRectangleRounded((Rectangle){lx, ly, 14, 14}, 0.3f, 4, legend[i].c);
            DrawTextEx(GetFontDefault(), legend[i].label,
                       (Vector2){lx + 22, ly + 1}, 12, 1, COL_TEXT_MID);
        }

        // Queue panel
        int qy_top = 425;
        DrawTextEx(GetFontDefault(), "WAITING QUEUE", (Vector2){lx, qy_top}, 11, 2, COL_TEXT_DIM);
        DrawDivider(lx, qy_top + 16, 352);
        if (q_count == 0) {
            DrawTextEx(GetFontDefault(), "No vehicles waiting",
                       (Vector2){lx, qy_top + 28}, 12, 1, COL_TEXT_DIM);
        } else {
            int max_vis = 6;
            for (int i = 0; i < q_count && i < max_vis; i++) {
                int qy     = qy_top + 24 + i * 30;
                bool is_vip = (q_snap[i].vtype == VIP);
                DrawCard(lx, qy, 344, 24,
                         is_vip ? (Color){55,40,4,255} : COL_FREE,
                         is_vip ? COL_VIP : COL_BORDER);
                DrawTextEx(GetFontDefault(), TextFormat("V%-3d", q_snap[i].vid),
                           (Vector2){lx + 8, qy + 5}, 12, 1, COL_TEXT_HI);
                DrawTextEx(GetFontDefault(), is_vip ? "VIP" : "REG",
                           (Vector2){lx + 305, qy + 5}, 11, 1,
                           is_vip ? COL_VIP : COL_TEXT_DIM);
            }
            if (q_count > max_vis)
                DrawTextEx(GetFontDefault(),
                           TextFormat("+ %d more in queue", q_count - max_vis),
                           (Vector2){lx, qy_top + 24 + max_vis * 30 + 4},
                           11, 1, COL_TEXT_DIM);
        }

        // ── Parking grid ──────────────────────────────────────────────────────
        DrawTextEx(GetFontDefault(), "PARKING SLOTS",
                   (Vector2){GRID_X, 78}, 11, 2, COL_TEXT_DIM);
        DrawDivider(GRID_X, 94, WIN_W - GRID_X - 18);

        for (int i = 0; i < MAX_SLOTS; i++) {
            int row = i / COLS;
            int col = i % COLS;
            int sx  = GRID_X + col * (SLOT_W + SLOT_GAP_X);
            int sy  = GRID_Y + row * (SLOT_H + SLOT_GAP_Y);

            bool occupied = (snap_status[i] == SLOT_OCCUPIED);
            bool is_vip   = (snap_vtype[i]  == VIP);

            Color base = occupied ? (is_vip ? COL_VIP : COL_REG) : COL_FREE;
            Color fill = base;
            if (occupied) {
                float p = (sinf(slot_anim[i].pulse * 6.28f) + 1.f) * 0.5f;
                fill = ColorLerp(base, (Color){255,255,255,255}, p * 0.07f);
            }
            if (slot_anim[i].flash > 0.f)
                fill = ColorLerp(fill, (Color){255,255,255,255},
                                 slot_anim[i].flash * 0.45f);

            Color border = occupied
                ? (is_vip ? COL_VIP : ColorLerp(COL_REG,(Color){255,255,255,255},0.18f))
                : COL_BORDER;

            DrawCard(sx, sy, SLOT_W, SLOT_H, fill, border);

            // Slot badge (top-left)
            DrawRectangleRounded((Rectangle){sx+6, sy+6, 40, 16}, 0.4f, 4, (Color){0,0,0,100});
            DrawTextEx(GetFontDefault(), TextFormat("S%02d", i),
                       (Vector2){sx+10, sy+8}, 10, 1,
                       occupied ? COL_TEXT_HI : COL_TEXT_DIM);

            if (occupied) {
                // Large vehicle ID
                DrawTextEx(GetFontDefault(), TextFormat("V%d", snap_vid[i]),
                           (Vector2){sx + SLOT_W/2 - 18, sy + 30}, 28, 1, COL_TEXT_HI);
                // Type badge (bottom-center)
                Color badge = is_vip ? COL_VIP_DIM : (Color){90,15,15,255};
                DrawRectangleRounded(
                    (Rectangle){sx + SLOT_W/2 - 20, sy + SLOT_H - 28, 40, 18},
                    0.4f, 4, badge);
                DrawTextEx(GetFontDefault(), is_vip ? "VIP" : "REG",
                           (Vector2){sx + SLOT_W/2 - 12, sy + SLOT_H - 25}, 11, 1,
                           is_vip ? COL_VIP : (Color){230,120,120,255});
            } else {
                DrawCircleLines(sx + SLOT_W/2, sy + SLOT_H/2, 18, COL_BORDER);
                DrawTextEx(GetFontDefault(), "FREE",
                           (Vector2){sx + SLOT_W/2 - 15, sy + SLOT_H/2 - 7},
                           13, 1, COL_TEXT_DIM);
            }
        }

        // Footer
        DrawRectangle(0, WIN_H - 30, WIN_W, 30, COL_PANEL);
        DrawRectangle(0, WIN_H - 31, WIN_W, 1, COL_BORDER);
        DrawTextEx(GetFontDefault(),
                   TextFormat("Slots: %d   |   Vehicles served: %d   "
                              "|   Wave interval: %.0fs   |   ESC to exit",
                              MAX_SLOTS, next_vehicle_id - 1, WAVE_INTERVAL_SEC),
                   (Vector2){18, WIN_H - 21}, 11, 1, COL_TEXT_DIM);

        EndDrawing();
    }

    // ── Graceful shutdown ─────────────────────────────────────────────────────
    printf("\n[SYSTEM] Window closed — stopping simulation...\n");
    simulation_running = 0;      // vehicle_thread() checks this flag

    usleep(800000);              // brief grace period for threads to notice
    for (int i = 0; i < MAX_ACTIVE_THREADS; i++)
        pthread_detach(active_threads[i]);   // don't block on sem_wait stragglers

    printf("[SYSTEM] Shutdown complete.\n");
    return 0;
}
