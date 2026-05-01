
#include "parking.h"
#include "raylib.h"
#include <math.h>

// ─── Window ───────────────────────────────────────────────────────────────────
#define WIN_W      1440
#define WIN_H       780   // reduced height

// ─── Fixed layout ─────────────────────────────────────────────────────────────
#define HEADER_H     72
#define FOOTER_H     36
#define PANEL_W     400
#define PANEL_PAD    24
#define COLS          3
#define GRID_PAD_X   48   // gap: panel-right → first slot
#define GRID_PAD_Y   36   // gap: header-bottom → first slot row
#define SLOT_GAP_X   24
#define SLOT_GAP_Y   24

// ─── Slot size — computed once from available space ───────────────────────────

#define GRID_X  (PANEL_W + GRID_PAD_X)
#define GRID_RIGHT_PAD  32
#define GRID_BOTTOM_PAD 16

// ─── Simulation tuning ────────────────────────────────────────────────────────
#define ARRIVAL_DELAY_MIN    0
#define ARRIVAL_DELAY_MAX    2
#define PARK_DURATION_MIN    6
#define PARK_DURATION_MAX   12
#define WAVE_SPAWN_DELAY    600000
#define WAVE_INTERVAL_SEC   15.f

// ─── Palette ──────────────────────────────────────────────────────────────────
#define COL_BG        (Color){13,  17,  23,  255}
#define COL_PANEL     (Color){20,  28,  40,  255}
#define COL_PANEL2    (Color){26,  36,  50,  255}
#define COL_BORDER    (Color){42,  58,  80,  255}
#define COL_FREE      (Color){28,  42,  60,  255}
#define COL_REG       (Color){175, 48,  48,  255}
#define COL_VIP       (Color){210, 158, 28,  255}
#define COL_VIP_DIM   (Color){85,  62,  7,   255}
#define COL_ACCENT    (Color){0,   178, 128, 255}
#define COL_TEXT_HI   (Color){222, 236, 255, 255}
#define COL_TEXT_MID  (Color){138, 160, 188, 255}
#define COL_TEXT_DIM  (Color){68,  90,  118, 255}
#define COL_OK        (Color){38,  198, 118, 255}
#define COL_WARN      (Color){238, 158, 28,  255}
#define COL_ERR       (Color){218, 68,  68,  255}
// Departure flash colour — distinct teal/cyan so "just left" reads differently
#define COL_DEPART    (Color){0,   210, 200, 255}

// ─── Slot dimensions (computed at startup) ───────────────────────────────────
static int SLOT_W = 185;
static int SLOT_H = 150;

static void compute_slot_size(void) {
    int rows        = (MAX_SLOTS + COLS - 1) / COLS;
    int grid_w_avail = WIN_W - GRID_X - GRID_RIGHT_PAD;
    int grid_h_avail = WIN_H - HEADER_H - FOOTER_H - GRID_PAD_Y - GRID_BOTTOM_PAD;
    // slot + gap repeated COLS times, last slot has no trailing gap
    SLOT_W = (grid_w_avail - SLOT_GAP_X * (COLS - 1)) / COLS;
    SLOT_H = (grid_h_avail - SLOT_GAP_Y * (rows - 1)) / rows;
}

// ─── Continuous simulation state ─────────────────────────────────────────────
volatile int simulation_running = 1;
static int   next_vehicle_id    = 1;

#define MAX_ACTIVE_THREADS 128
static pthread_t active_threads[MAX_ACTIVE_THREADS];
static Vehicle   active_vehicles[MAX_ACTIVE_THREADS];
static int       threads_launched = 0;

// ─── Per-slot animation ───────────────────────────────────────────────────────
typedef struct {
    float pulse;        // continuous breathing (0..1)
    int   last_status;  // previous frame status — detect transitions
    float arrive_flash; // 0..1  white flash on arrival
    float depart_flash; // 0..1  teal  flash on departure
} SlotAnim;
static SlotAnim slot_anim[MAX_SLOTS];

// ─── Draw helpers ─────────────────────────────────────────────────────────────
static void DrawCard(int x, int y, int w, int h, Color fill, Color border) {
    DrawRectangleRounded((Rectangle){x, y, w, h}, 0.10f, 10, fill);
    DrawRectangleRoundedLines((Rectangle){x, y, w, h}, 0.10f, 10, border);
}

static void DrawSection(int x, int y, int w, const char *title) {
    DrawTextEx(GetFontDefault(), title, (Vector2){x, y}, 13, 2, COL_TEXT_DIM);
    DrawRectangle(x, y + 20, w, 1, COL_BORDER);
}

static void DrawStat(int x, int y, int cw, int ch,
                     const char *label, int value, Color vcol) {
    DrawCard(x, y, cw, ch, COL_PANEL2, COL_BORDER);
    DrawTextEx(GetFontDefault(), label,
               (Vector2){x + 14, y + 12}, 13, 1, COL_TEXT_DIM);
    DrawTextEx(GetFontDefault(), TextFormat("%d", value),
               (Vector2){x + 14, y + 32}, 26, 1, vcol);
}

// ─── Wave spawner ─────────────────────────────────────────────────────────────
typedef struct { int wave_num; } WaveArg;
static WaveArg   wave_arg;
static pthread_t wave_thread;

static void *wave_spawner(void *arg) {
    WaveArg *wa = (WaveArg *)arg;
    printf("[SYSTEM] Wave %d — spawning %d vehicles\n", wa->wave_num, MAX_VEHICLES);
    for (int i = 0; i < MAX_VEHICLES; i++) {
        if (!simulation_running) break;
        int slot = threads_launched % MAX_ACTIVE_THREADS;
        active_vehicles[slot].vehicle_id    = next_vehicle_id++;
        active_vehicles[slot].vehicle_type  = (active_vehicles[slot].vehicle_id % 4 == 0) ? VIP : REGULAR;
        active_vehicles[slot].arrival_delay = ARRIVAL_DELAY_MIN
                                            + rand() % (ARRIVAL_DELAY_MAX - ARRIVAL_DELAY_MIN + 1);
        active_vehicles[slot].park_duration = PARK_DURATION_MIN
                                            + rand() % (PARK_DURATION_MAX - PARK_DURATION_MIN + 1);
        if (pthread_create(&active_threads[slot], NULL, vehicle_thread, &active_vehicles[slot]) == 0)
            pthread_detach(active_threads[slot]);
        threads_launched++;
        usleep(WAVE_SPAWN_DELAY);
    }
    return NULL;
}

static void spawn_wave_async(int wave_num) {
    wave_arg.wave_num = wave_num;
    if (pthread_create(&wave_thread, NULL, wave_spawner, &wave_arg) == 0)
        pthread_detach(wave_thread);
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
        slot_anim[i].arrive_flash   = 0.f;
        slot_anim[i].depart_flash   = 0.f;
    }
    printf("[SYSTEM] Parking lot initialized — %d slots.\n", MAX_SLOTS);
}

// ─── Main ─────────────────────────────────────────────────────────────────────
int main(void) {
    srand((unsigned)time(NULL));
    printf("========== SMART PARKING SYSTEM ==========\n");
    printf("[SYSTEM] Close window or press ESC to stop.\n\n");

    init_parking_lot();
    init_semaphore();

    InitWindow(WIN_W, WIN_H, "Smart Parking — Dashboard");
    SetTargetFPS(60);

    compute_slot_size();   // auto-size slots to fill the grid area
    printf("[SYSTEM] Slot size computed: %d x %d px\n", SLOT_W, SLOT_H);

    const int PW     = PANEL_W - PANEL_PAD * 2;
    const int SC_GAP = 12;
    const int SC_W   = (PW - SC_GAP) / 2;
    const int SC_H   = 72;

    float time_acc   = 0.f;
    float wave_timer = 0.f;
    int   wave_count = 1;
    spawn_wave_async(wave_count);

    typedef struct { int vid; int vtype; } QEntry;

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        time_acc   += dt;
        wave_timer += dt;

        if (wave_timer >= WAVE_INTERVAL_SEC) {
            wave_timer = 0.f;
            wave_count++;
            spawn_wave_async(wave_count);
        }

        // ── Snapshot ──────────────────────────────────────────────────────────
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

        // ── Animate slots ─────────────────────────────────────────────────────
        for (int i = 0; i < MAX_SLOTS; i++) {
            slot_anim[i].pulse += dt * 0.75f;
            if (slot_anim[i].pulse > 1.f) slot_anim[i].pulse -= 1.f;

            int prev = slot_anim[i].last_status;
            int cur  = snap_status[i];

            if (cur != prev) {
                if (cur == SLOT_OCCUPIED) {
                    // Vehicle arrived → white flash
                    slot_anim[i].arrive_flash = 1.f;
                    slot_anim[i].depart_flash = 0.f;
                } else {
                    // Vehicle left → teal departure flash, slower fade (1.8s)
                    slot_anim[i].depart_flash = 1.f;
                    slot_anim[i].arrive_flash = 0.f;
                }
                slot_anim[i].last_status = cur;
            }

            // Arrival flash fades fast (0.5s)
            slot_anim[i].arrive_flash -= dt * 2.0f;
            if (slot_anim[i].arrive_flash < 0.f) slot_anim[i].arrive_flash = 0.f;

            // Departure flash fades slowly (2s) so "just left" is clearly visible
            slot_anim[i].depart_flash -= dt * 0.5f;
            if (slot_anim[i].depart_flash < 0.f) slot_anim[i].depart_flash = 0.f;
        }

        // ══════════════════════════════════════════════════════════════════════
        BeginDrawing();
        ClearBackground(COL_BG);

        // ── HEADER ────────────────────────────────────────────────────────────
        DrawRectangle(0, 0, WIN_W, HEADER_H, COL_PANEL);
        DrawRectangle(0, HEADER_H - 1, WIN_W, 1, COL_ACCENT);
        DrawRectangle(0, 0, 5, HEADER_H, COL_ACCENT);
        DrawTextEx(GetFontDefault(), "SMART PARKING",
                   (Vector2){22, 12}, 32, 3, COL_TEXT_HI);
        DrawTextEx(GetFontDefault(), "REAL-TIME DASHBOARD",
                   (Vector2){25, 46}, 13, 2, COL_ACCENT);

        float blink  = (sinf(time_acc * 3.14f) + 1.f) * 0.5f;
        Color dotCol = ColorLerp(COL_ERR, COL_OK, blink);
        DrawCircle(WIN_W - 28, HEADER_H / 2 - 8, 7, dotCol);
        DrawTextEx(GetFontDefault(), "LIVE",
                   (Vector2){WIN_W - 85, HEADER_H / 2 - 16}, 13, 1, COL_TEXT_DIM);
        int secs_left = (int)(WAVE_INTERVAL_SEC - wave_timer) + 1;
        DrawTextEx(GetFontDefault(),
                   TextFormat("Wave %d  |  next in %ds", wave_count, secs_left),
                   (Vector2){WIN_W - 235, HEADER_H / 2 + 4}, 12, 1, COL_TEXT_DIM);

        // ── LEFT PANEL ────────────────────────────────────────────────────────
        DrawRectangle(0, HEADER_H, PANEL_W,
                      WIN_H - HEADER_H - FOOTER_H, COL_PANEL);
        DrawRectangle(PANEL_W - 1, HEADER_H, 1,
                      WIN_H - HEADER_H - FOOTER_H, COL_BORDER);

        int lx = PANEL_PAD;
        int ly = HEADER_H + 24;

        // Stats
        DrawSection(lx, ly, PW, "STATISTICS");  ly += 28;
        DrawStat(lx,             ly, SC_W, SC_H, "PARKED",   snap_parked,  COL_OK);
        DrawStat(lx+SC_W+SC_GAP, ly, SC_W, SC_H, "WAITING",  snap_waiting, COL_WARN);
        ly += SC_H + SC_GAP;
        DrawStat(lx,             ly, SC_W, SC_H, "TIMEOUTS", snap_timeout, COL_ERR);
        DrawStat(lx+SC_W+SC_GAP, ly, SC_W, SC_H, "WAVE",     wave_count,   COL_ACCENT);
        ly += SC_H + 28;

        // Capacity bar
        DrawSection(lx, ly, PW, "CAPACITY");  ly += 28;
        float occ_frac = MAX_SLOTS > 0 ? (float)snap_parked / MAX_SLOTS : 0.f;
        Color barFill  = occ_frac > 0.8f ? COL_ERR :
                         occ_frac > 0.5f ? COL_WARN : COL_OK;
        DrawRectangleRounded((Rectangle){lx, ly, PW, 20}, 0.5f, 8, COL_BORDER);
        if (occ_frac > 0.f) {
            float fw = (PW - 4) * occ_frac;
            if (fw < 12) fw = 12;
            DrawRectangleRounded((Rectangle){lx+2, ly+2, fw, 16}, 0.5f, 8, barFill);
        }
        ly += 26;
        DrawTextEx(GetFontDefault(),
                   TextFormat("%.0f%%  —  %d of %d slots occupied",
                              occ_frac*100, snap_parked, MAX_SLOTS),
                   (Vector2){lx, ly}, 13, 1, COL_TEXT_MID);
        ly += 30;

        // Legend — now includes departure colour
        DrawSection(lx, ly, PW, "LEGEND");  ly += 28;
        const struct { Color c; const char *label; } legend[] = {
            { COL_FREE,   "Free slot"         },
            { COL_REG,    "Regular vehicle"   },
            { COL_VIP,    "VIP vehicle"       },
            { COL_DEPART, "Just departed"     },
        };
        for (int i = 0; i < 4; i++) {
            DrawRectangleRounded((Rectangle){lx, ly + i*28, 16, 16},
                                 0.3f, 4, legend[i].c);
            DrawTextEx(GetFontDefault(), legend[i].label,
                       (Vector2){lx + 26, ly + i*28 + 1}, 14, 1, COL_TEXT_MID);
        }
        ly += 4 * 28 + 20;

        // Queue
        DrawSection(lx, ly, PW, "WAITING QUEUE");  ly += 28;
        if (q_count == 0) {
            DrawTextEx(GetFontDefault(), "No vehicles waiting",
                       (Vector2){lx, ly}, 14, 1, COL_TEXT_DIM);
        } else {
            int row_h   = 34;
            int max_vis = (WIN_H - FOOTER_H - ly - 8) / row_h;
            if (max_vis < 1) max_vis = 1;
            for (int i = 0; i < q_count && i < max_vis; i++) {
                bool is_vip = (q_snap[i].vtype == VIP);
                int  qy     = ly + i * row_h;
                DrawCard(lx, qy, PW, row_h - 4,
                         is_vip ? (Color){52,38,4,255} : COL_FREE,
                         is_vip ? COL_VIP : COL_BORDER);
                DrawTextEx(GetFontDefault(), TextFormat("%d.", i+1),
                           (Vector2){lx+8,  qy+8}, 13, 1, COL_TEXT_DIM);
                DrawTextEx(GetFontDefault(),
                           TextFormat("Vehicle V%d", q_snap[i].vid),
                           (Vector2){lx+32, qy+8}, 14, 1, COL_TEXT_HI);
                DrawTextEx(GetFontDefault(), is_vip ? "VIP" : "REG",
                           (Vector2){lx+PW-40, qy+8}, 13, 1,
                           is_vip ? COL_VIP : COL_TEXT_DIM);
            }
            if (q_count > max_vis)
                DrawTextEx(GetFontDefault(),
                           TextFormat("+ %d more in queue", q_count - max_vis),
                           (Vector2){lx, ly + max_vis * row_h + 4},
                           12, 1, COL_TEXT_DIM);
        }

        // ── PARKING GRID ──────────────────────────────────────────────────────
        DrawSection(GRID_X, HEADER_H + 8,
                    WIN_W - GRID_X - GRID_RIGHT_PAD, "PARKING SLOTS");

        for (int i = 0; i < MAX_SLOTS; i++) {
            int row = i / COLS;
            int col = i % COLS;
            int sx  = GRID_X + col * (SLOT_W + SLOT_GAP_X);
            int sy  = HEADER_H + GRID_PAD_Y + 22 + row * (SLOT_H + SLOT_GAP_Y);

            bool occupied = (snap_status[i] == SLOT_OCCUPIED);
            bool is_vip   = (snap_vtype[i]  == VIP);

            // Base fill
            Color base = occupied ? (is_vip ? COL_VIP : COL_REG) : COL_FREE;
            Color fill = base;

            // Breathing pulse when occupied
            if (occupied) {
                float p = (sinf(slot_anim[i].pulse * 6.28f) + 1.f) * 0.5f;
                fill = ColorLerp(base, (Color){255,255,255,255}, p * 0.06f);
            }

            // Departure flash: blend toward teal (fades over ~2s)
            if (slot_anim[i].depart_flash > 0.f)
                fill = ColorLerp(fill, COL_DEPART, slot_anim[i].depart_flash * 0.75f);

            // Arrival flash: blend toward white (fades over ~0.5s)
            if (slot_anim[i].arrive_flash > 0.f)
                fill = ColorLerp(fill, (Color){255,255,255,255},
                                 slot_anim[i].arrive_flash * 0.45f);

            // Border
            Color border;
            if (slot_anim[i].depart_flash > 0.f)
                border = ColorLerp(COL_BORDER, COL_DEPART, slot_anim[i].depart_flash);
            else if (occupied)
                border = is_vip ? COL_VIP
                                : ColorLerp(COL_REG,(Color){255,255,255,255},0.2f);
            else
                border = COL_BORDER;

            DrawCard(sx, sy, SLOT_W, SLOT_H, fill, border);

            // Slot badge top-left
            DrawRectangleRounded((Rectangle){sx+8, sy+8, 50, 22},
                                 0.4f, 4, (Color){0,0,0,110});
            DrawTextEx(GetFontDefault(), TextFormat("S%02d", i),
                       (Vector2){sx+13, sy+10}, 13, 1,
                       occupied ? COL_TEXT_HI : COL_TEXT_DIM);

            if (occupied) {
                // Vehicle ID — centred
                const char *vid_str = TextFormat("V%d", snap_vid[i]);
                int tw = MeasureText(vid_str, 36);
                DrawTextEx(GetFontDefault(), vid_str,
                           (Vector2){sx + SLOT_W/2 - tw/2, sy + SLOT_H/2 - 28},
                           36, 1, COL_TEXT_HI);

                // Type badge — centred bottom
                Color bf  = is_vip ? COL_VIP_DIM   : (Color){85,14,14,255};
                Color bt  = is_vip ? COL_VIP        : (Color){228,118,118,255};
                const char *ts = is_vip ? "VIP" : "REGULAR";
                int bw = 70, bh = 24;
                DrawRectangleRounded(
                    (Rectangle){sx+SLOT_W/2-bw/2, sy+SLOT_H-bh-10, bw, bh},
                    0.4f, 6, bf);
                DrawTextEx(GetFontDefault(), ts,
                           (Vector2){sx+SLOT_W/2-bw/2+8, sy+SLOT_H-bh-7},
                           13, 1, bt);

            } else if (slot_anim[i].depart_flash > 0.f) {
                // "Just left" state — show checkmark-style text while flash active
                const char *msg = "DEPARTED";
                int tw = MeasureText(msg, 14);
                DrawTextEx(GetFontDefault(), msg,
                           (Vector2){sx + SLOT_W/2 - tw/2, sy + SLOT_H/2 - 9},
                           14, 1,
                           ColorLerp(COL_TEXT_DIM, COL_DEPART,
                                     slot_anim[i].depart_flash));
            } else {
                // Normal free state
                DrawCircleLines(sx+SLOT_W/2, sy+SLOT_H/2, 22, COL_BORDER);
                DrawTextEx(GetFontDefault(), "FREE",
                           (Vector2){sx+SLOT_W/2-18, sy+SLOT_H/2-9},
                           15, 1, COL_TEXT_DIM);
            }
        }

        // ── FOOTER ────────────────────────────────────────────────────────────
        DrawRectangle(0, WIN_H - FOOTER_H, WIN_W, FOOTER_H, COL_PANEL);
        DrawRectangle(0, WIN_H - FOOTER_H, WIN_W, 1, COL_BORDER);
        DrawTextEx(GetFontDefault(),
                   TextFormat("  Slots: %d   |   Vehicles served: %d"
                              "   |   Wave interval: %.0fs   |   ESC to exit",
                              MAX_SLOTS, next_vehicle_id - 1, WAVE_INTERVAL_SEC),
                   (Vector2){18, WIN_H - FOOTER_H + 10}, 13, 1, COL_TEXT_DIM);

        EndDrawing();
    }

    // ── Shutdown ──────────────────────────────────────────────────────────────
    printf("\n[SYSTEM] Shutting down...\n");
    simulation_running = 0;
    usleep(800000);
    printf("[SYSTEM] Done.\n");
    return 0;
}
