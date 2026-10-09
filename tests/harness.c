/* Host test harness for Football Legends CE (v2).
 * Builds the real src/main.c against stub headers with a fake framebuffer,
 * drives it with scripted / random key presses and checks invariants.
 * The keypad stub applies the matrix ghosting rule to every scan.
 *
 *   harness aivai    <matches> <seed>   AI vs AI quick matches
 *   harness tour     <cups>    <seed>   AI-played cups, incl. save/continue
 *   harness fuzz     <frames>  <seed>   random keys on every screen
 *   harness friendly <matches> <seed>   2 humans mashing keys (single + best of 3)
 *   harness ghost                       every realistic P1+P2 key combination
 *   harness panic                       panic exit from every screen
 *   harness balance  <per-pair> <seed>  all legend pairs, headless
 *   harness shots    <outdir>  <seed>   screenshots
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "stub/stub_internal.h"
#include "stub/test_hooks.h"
#include "stub/keypadc.h"

int game_main(void);

enum { SCR_ID_TITLE, SCR_ID_TEAMSEL, SCR_ID_MATCH, SCR_ID_PAUSE, SCR_ID_RESULTS,
       SCR_ID_BRACKET, SCR_ID_MESSAGE, SCR_ID_STATS, SCR_ID_CONTROLS, SCR_ID_TWOP,
       SCR_ID_SERIES };
enum { SC_AIVAI, SC_TOUR, SC_FUZZ, SC_FRIENDLY, SC_GHOST, SC_PANIC, SC_BALANCE, SC_SHOTS };
#define NTEAMS 10
#define TITLE_ITEMS 7

static int scenario, p1ai, target_n, headless;
static unsigned rs = 12345;
static unsigned rr(void) { rs = rs * 1103515245u + 12345u; return (rs >> 16) & 0x7fff; }

static long fclock;
clock_t fake_clock(void) { fclock += 137; return (clock_t)fclock; }

/* ---- results ---- */
long frames;
static long match_frames, matches, goals_side[2], wins_side[2], golden_goals, gg_ticks_max, max_ticks, total_ticks;
static long team_w[NTEAMS], team_l[NTEAMS], team_gf[NTEAMS], supers_by_power[NTEAMS], high_score;
static long tours, champs, out_round[4], chk24_fail, pix_mismatch, pix_checks, stuck_matches;
static long loads, load_active, watchdogs, platform_rests, max_rest, series_done;
static long pair_w[NTEAMS][NTEAMS], pair_n[NTEAMS][NTEAMS];
static long hist[20];
static double cyc_sum; static long cyc_n; static double cyc_max; static long over_budget;
static unsigned long cur_match_ticks;

int test_p1_ai(void) { return p1ai; }
int test_p1_diff(void) { return getenv("P1DIFF") ? atoi(getenv("P1DIFF")) : -1; }
int test_headless(void) { return headless; }
void test_chk24(long v, int line)
{
    if (v > 8388607L || v < -8388608L) {
        chk24_fail++;
        if (chk24_fail < 5) fprintf(stderr, "24-bit overflow %ld at line %d\n", v, line);
    }
}

/* ---- widget rects (excluded from the background check) ---- */
static int wr[8][4], nwr;
void test_set_widget(uint8_t w, int x0, int y0, int x1, int y1)
{
    if (w < 8) { wr[w][0] = x0; wr[w][1] = y0; wr[w][2] = x1; wr[w][3] = y1; if (w + 1 > nwr) nwr = w + 1; }
}
static int in_widget(int x, int y)
{
    int k;
    for (k = 0; k < nwr; k++) if (x >= wr[k][0] && x < wr[k][2] && y >= wr[k][1] && y < wr[k][3]) return 1;
    return 0;
}

/* ---- key scripting ---- */
typedef struct { uint8_t g, m; } tapk_t;
static tapk_t q[256];
static int qh, qt, tap_release, cur_screen = -1, stable, title_sel;
static void tap(uint8_t g, uint8_t m) { q[qt & 255].g = g; q[qt & 255].m = m; qt++; }
#define TAP_OK() tap(6, kb_Enter)
#define TAP_CLEAR() tap(6, kb_Clear)
static void title_goto(int t) { while (title_sel != t) { tap(7, kb_Down); title_sel = (title_sel + 1) % TITLE_ITEMS; } }
static void panic_keys(void)
{
    tap(1, kb_Yequ); tap(1, kb_Window); tap(1, kb_Zoom); tap(1, kb_Trace); tap(1, kb_Graph);
}

void test_screen(int id)
{
    if (id == cur_screen) stable++;
    else stable = 0;
    cur_screen = id;
}

/* ---- screenshots ---- */
static const char *shot_dir;
static int shot_pending; static char shot_name[64];
static int shots_taken[32];
static void write_ppm(const char *name, int buf)
{
    char path[256]; FILE *f; int x, y;
    if (!shot_dir) return;
    snprintf(path, sizeof path, "%s/%s.ppm", shot_dir, name);
    f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n320 240\n255\n");
    for (y = 0; y < 240; y++) for (x = 0; x < 320; x++) {
        uint16_t c = stub_pal[vram[buf][y][x]];
        unsigned char rgb[3] = { (unsigned char)(((c >> 10) & 31) * 255 / 31),
                                 (unsigned char)(((c >> 5) & 31) * 255 / 31),
                                 (unsigned char)((c & 31) * 255 / 31) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}
static void shot_later(const char *name, int delay)
{
    if (shot_pending) return;
    snprintf(shot_name, sizeof shot_name, "%s", name);
    shot_pending = delay;
}

/* ---- per-frame cost model (rough eZ80 @ 48 MHz estimate, NOT a measurement) ---- */
static double frame_cycles(void)
{
    return cost.calls * 350.0 + cost.rows * 60.0 + cost.fill_px * 3.0 + cost.sprite_px * 4.0 +
           cost.slow_px * 40.0 + cost.text_px * 30.0 + cost.chars * 800.0 + cost.direct * 2.0 + 150000.0;
}

static int match_frame_flag;
void test_match_frame(void) { match_frame_flag = 1; }
void stub_on_swap(void)
{
    frames++;
    if (frames > 6000000L) { fprintf(stderr, "ERROR: frame cap reached (script stuck?)\n"); exit(3); }
    if (match_frame_flag) {
        double c = frame_cycles();
        int b = (int)(c / 100000.0);
        match_frames++;
        cyc_sum += c; cyc_n++;
        if (c > cyc_max) cyc_max = c;
        if (c > 1600000.0) over_budget++;
        hist[b > 19 ? 19 : b]++;
        if (getenv("COSTDBG") && c > atof(getenv("COSTDBG")))
            fprintf(stderr, "heavy frame %ld: est %.0f calls=%ld rows=%ld fill=%ld spr=%ld slow=%ld textpx=%ld chars=%ld\n",
                    frames, c, cost.calls, cost.rows, cost.fill_px, cost.sprite_px, cost.slow_px, cost.text_px, cost.chars);
    }
    match_frame_flag = 0;
    memset(&cost, 0, sizeof cost);
    if (shot_pending && --shot_pending == 0) write_ppm(shot_name, scr_idx);
    if (getenv("MOVIE") && cur_screen == SCR_ID_MATCH) {
        static long mf;
        long a = atol(getenv("MOVIE_FROM")), e = atol(getenv("MOVIE_TO"));
        mf++;
        if (mf >= a && mf < e && (mf & 1) == 0) {
            char n[32];
            const char *save = shot_dir;
            shot_dir = getenv("MOVIE");
            snprintf(n, sizeof n, "m%06ld", mf);
            write_ppm(n, scr_idx);
            shot_dir = save;
        }
    }
}

/* ---- pixel-exact check of the dirty-rectangle restore ---- */
static uint8_t pristine[240][320];
static int have_pristine;
void test_bg_ready(uint8_t jump)
{
    if (!jump) { memcpy(pristine, stub_draw_buffer(), sizeof pristine); have_pristine = 1; }
}
void test_after_restore(uint8_t jump, int shaking)
{
    uint8_t (*b)[320] = stub_draw_buffer();
    int y, x;
    if (jump || shaking || !have_pristine) return;
    pix_checks++;
    for (y = 0; y < 240; y++) {
        if (memcmp(b[y], pristine[y], 320) == 0) continue;
        for (x = 0; x < 320; x++) {
            if (b[y][x] != pristine[y][x] && !in_widget(x, y)) {
                pix_mismatch++;
                if (pix_mismatch < 4) {
                    fprintf(stderr, "restore mismatch at (%d,%d) got %d want %d (check %ld frame %ld)\n",
                            x, y, b[y][x], pristine[y][x], pix_checks, frames);
                    if (shot_dir) write_ppm("restore_mismatch", scr_idx ^ 1);
                }
                return;
            }
        }
    }
}

/* ---- game events ---- */
static const char *PNAME[NTEAMS] = { "fireball", "teleport", "freeze", "rocketjump", "giant",
                                     "magnet", "clone", "wall", "speed", "heavyball" };
void test_on_goal(uint8_t scorer, uint8_t half, int half_ticks)
{
    goals_side[scorer]++;
    if (half == 2) { golden_goals++; if (half_ticks > gg_ticks_max) gg_ticks_max = half_ticks; }
    if (scenario == SC_SHOTS && !shots_taken[0]) { shots_taken[0] = 1; shot_later("09_goal", 10); }
}
void test_on_super(uint8_t who, uint8_t power)
{
    char n[48];
    (void)who;
    if (power < NTEAMS) supers_by_power[power]++;
    if (scenario == SC_SHOTS && power < NTEAMS && !shots_taken[2 + power]) {
        shots_taken[2 + power] = 1;
        snprintf(n, sizeof n, "08_ability_%s", PNAME[power]);
        shot_later(n, power == 2 || power == 4 || power == 6 || power == 7 ? 8 : 5);
    }
}
static int forced_t0 = -1, forced_t1 = -1;
void test_force_teams(int *t0, int *t1)
{
    if (forced_t0 >= 0 && t0) *t0 = forced_t0;
    if (forced_t1 >= 0 && t1) *t1 = forced_t1;
}
void test_on_match_end(uint8_t t0, uint8_t t1, uint8_t s0, uint8_t s1, unsigned long ticks)
{
    matches++;
    total_ticks += (long)ticks;
    if ((long)ticks > max_ticks) max_ticks = (long)ticks;
    if (s0 == s1) fprintf(stderr, "ERROR: match ended level %d-%d\n", s0, s1);
    if (s0 > s1) { wins_side[0]++; team_w[t0]++; team_l[t1]++; pair_w[t0][t1]++; }
    else { wins_side[1]++; team_w[t1]++; team_l[t0]++; pair_w[t1][t0]++; }
    pair_n[t0][t1]++;
    pair_n[t1][t0]++;
    team_gf[t0] += s0; team_gf[t1] += s1;
    if (s0 + s1 > high_score) high_score = s0 + s1;
    cur_match_ticks = 0;
}
void test_on_tour_end(uint8_t round, bool champion)
{
    tours++;
    if (champion) champs++;
    out_round[round > 3 ? 3 : round]++;
}
void test_on_load(uint8_t active, uint8_t round, unsigned played)
{
    loads++;
    if (active) load_active++;
    (void)round; (void)played;
}
void test_on_watchdog(int x, int y)
{
    watchdogs++;
    if (getenv("WDDBG")) fprintf(stderr, "watchdog re-drop, ball was at %d,%d\n", x, y);
}
void test_ball_tick(int x, int y, int vx, int vy)
{
    static long rest;
    int near_platform = x < 56 || x > 264;
    (void)y;
    if (near_platform && vx > -32 && vx < 32 && vy > -40 && vy < 40) {
        if (++rest == 60) { platform_rests++; if (getenv("RESTDBG")) fprintf(stderr, "rest at %d,%d v %d,%d\n", x, y, vx, vy); }
        if (rest > max_rest) max_rest = rest;
    } else {
        rest = 0;
    }
}
void test_on_series(uint8_t w0, uint8_t w1, uint8_t need)
{
    if (w0 >= need || w1 >= need) series_done++;
}
static int panic_seen;
void test_on_panic(void) { panic_seen = 1; }

/* ---- per-scenario key logic ---- */
static int tour_phase, hard_set, need_restart_check;

/* human-like holds for one player; ghosting is applied by the stub afterwards */
static void random_hold(uint8_t *kb, int p2)
{
    static int hold_dir[2], hold_t[2];
    int i = p2;
    if (hold_t[i]-- <= 0) { hold_dir[i] = (int)(rr() % 3) - 1; hold_t[i] = (int)(rr() % 30); }
    if (!p2) {
        if (hold_dir[i] < 0) kb[7] |= kb_Left;
        if (hold_dir[i] > 0) kb[7] |= kb_Right;
        if (rr() % 9 == 0) kb[7] |= kb_Up;
        if (rr() % 40 == 0) kb[7] |= kb_Down;
        if (rr() % 6 == 0) kb[1] |= kb_2nd;
        if (rr() % 30 == 0) kb[2] |= kb_Alpha;
    } else {
        if (hold_dir[i] < 0) kb[3] |= kb_Apps;
        if (hold_dir[i] > 0) kb[5] |= kb_Vars;
        if (rr() % 9 == 0) kb[4] |= kb_Stat;
        if (rr() % 40 == 0) kb[4] |= kb_Prgm;
        if (rr() % 6 == 0) kb[4] |= kb_LParen;
        if (rr() % 30 == 0) kb[5] |= kb_RParen;
    }
}

static void match_guard(void)
{
    if (++cur_match_ticks == 60000) {
        stuck_matches++;
        fprintf(stderr, "match exceeded 60000 ticks, quitting it\n");
        TAP_CLEAR(); tap(7, kb_Down); tap(7, kb_Down); TAP_OK();
        cur_match_ticks = 0;
    }
}

/* panic scenario: navigate to target screen, then type the sequence */
static int panic_target, panic_typed, panic_runs_ok;
static long panic_writes_before;
static const char *PANIC_NAMES[] = { "title", "team select", "match (1P)", "pause", "results",
                                     "cup bracket", "records", "controls", "2P menu",
                                     "2P match", "series screen", "message screen" };
#define PANIC_TARGETS 12
static void panic_script(void)
{
    int want = -1;
    if (panic_typed) return;
    switch (panic_target) {
    case 0: want = SCR_ID_TITLE; break;
    case 1: want = SCR_ID_TEAMSEL; break;
    case 2: case 9: want = SCR_ID_MATCH; break;
    case 3: want = SCR_ID_PAUSE; break;
    case 4: want = SCR_ID_RESULTS; break;
    case 5: want = SCR_ID_BRACKET; break;
    case 6: want = SCR_ID_STATS; break;
    case 7: want = SCR_ID_CONTROLS; break;
    case 8: want = SCR_ID_TWOP; break;
    default: want = SCR_ID_MESSAGE; break;
    }
    if (cur_screen == want && stable > 3 && (want != SCR_ID_MATCH || cur_match_ticks > 120)) {
        panic_writes_before = stub_writes;
        panic_keys();
        panic_typed = 1;
        if (getenv("PANICDBG")) fprintf(stderr, "  typing sequence on screen %d\n", cur_screen);
        return;
    }
    if (stable < 8 || cur_screen == want) return;    /* menus slide in for 6 frames */
    /* navigation */
    switch (cur_screen) {
    case SCR_ID_TITLE:
        if (panic_target == 0) return;
        if (panic_target == 5) { title_goto(1); TAP_OK(); }
        else if (panic_target == 6) { title_goto(5); TAP_OK(); }
        else if (panic_target == 7) { title_goto(4); TAP_OK(); }
        else if (panic_target >= 8) { title_goto(2); TAP_OK(); }
        else { title_goto(0); TAP_OK(); }
        break;
    case SCR_ID_TWOP:
        if (panic_target == 8) return;
        if (panic_target == 9) TAP_OK();
        else { tap(7, kb_Down); TAP_OK(); }          /* best of 3 */
        break;
    case SCR_ID_TEAMSEL: TAP_OK(); break;
    case SCR_ID_BRACKET: TAP_OK(); break;
    case SCR_ID_RESULTS: TAP_OK(); break;
    case SCR_ID_MATCH:
        if (panic_target == 3) TAP_CLEAR();
        break;
    default: break;
    }
}

static void script(uint8_t *kb)
{
    static int shots_stage, shot_match;
    switch (scenario) {
    case SC_AIVAI:
    case SC_BALANCE:
        if (cur_screen == SCR_ID_MATCH) { match_guard(); return; }
        if (stable < 8) return;
        if (cur_screen == SCR_ID_TITLE) {
            if (matches >= target_n) { TAP_CLEAR(); return; }
            if (getenv("HARD") && !hard_set) { hard_set = 1; title_goto(3); TAP_OK(); return; }
            if (scenario == SC_AIVAI && !getenv("NOSWITCH") && !getenv("HARD") &&
                matches == target_n / 2 && title_sel != 3) { title_goto(3); TAP_OK(); return; }
            if (scenario == SC_BALANCE) {
                /* schedule: every ordered pair, per-pair matches each */
                long per = atol(getenv("PER") ? getenv("PER") : "4");
                long k = matches / per;
                int a = (int)(k % (NTEAMS * (NTEAMS - 1)));
                forced_t0 = a / (NTEAMS - 1);
                forced_t1 = a % (NTEAMS - 1);
                if (forced_t1 >= forced_t0) forced_t1++;
            }
            title_goto(0); TAP_OK();
        } else if (cur_screen == SCR_ID_TEAMSEL) {
            int k, n = (int)(rr() % NTEAMS);
            for (k = 0; k < n; k++) tap(7, kb_Right);
            TAP_OK();
        } else {
            TAP_OK();
        }
        return;
    case SC_TOUR:
        if (cur_screen == SCR_ID_MATCH) { match_guard(); return; }
        if (stable < 8) return;
        if (cur_screen == SCR_ID_TITLE) {
            if (tours >= target_n) { TAP_CLEAR(); return; }
            if (tour_phase == 1) { TAP_CLEAR(); need_restart_check = 1; tour_phase = 2; return; }
            title_goto(1); TAP_OK();
        } else if (cur_screen == SCR_ID_TEAMSEL) {
            int k, n = (int)(rr() % NTEAMS);
            for (k = 0; k < n; k++) tap(7, kb_Right);
            TAP_OK();
        } else if (cur_screen == SCR_ID_BRACKET && tour_phase == 0 && matches == 1) {
            TAP_CLEAR(); tour_phase = 1;
        } else {
            TAP_OK();
        }
        return;
    case SC_FRIENDLY:
        if (cur_screen == SCR_ID_MATCH) { random_hold(kb, 0); random_hold(kb, 1); match_guard(); return; }
        if (stable < 8) return;
        if (cur_screen == SCR_ID_TITLE) {
            if (series_done >= 1 && matches >= target_n) { TAP_CLEAR(); return; }
            title_goto(2); TAP_OK();
        } else if (cur_screen == SCR_ID_TWOP) {
            if (matches < target_n - 1 || series_done) TAP_OK();   /* quick friendly */
            else { tap(7, kb_Down); TAP_OK(); }                   /* then a best of 3 */
        } else if (cur_screen == SCR_ID_TEAMSEL) {
            int k, n = (int)(rr() % NTEAMS);
            for (k = 0; k < n; k++) tap(7, kb_Right);
            TAP_OK();
        } else {
            TAP_OK();
        }
        return;
    case SC_PANIC:
        if (cur_screen == SCR_ID_MATCH) {
            cur_match_ticks++;
            if (panic_target == 9) { random_hold(kb, 0); random_hold(kb, 1); }
        }
        panic_script();
        return;
    case SC_FUZZ: {
        static const uint8_t g[] = { 7, 7, 7, 7, 1, 2, 6, 6, 3, 4, 4, 5, 4, 5, 1 };
        static const uint8_t m[] = { kb_Left, kb_Right, kb_Up, kb_Down, kb_2nd, kb_Alpha, kb_Enter,
                                     kb_Clear, kb_Apps, kb_Stat, kb_Prgm, kb_Vars, kb_LParen, kb_RParen, kb_Del };
        int n = (int)(rr() % 4), k;
        if (cur_screen == SCR_ID_MATCH) { random_hold(kb, 0); random_hold(kb, 1); }
        for (k = 0; k < n; k++) {
            int i = (int)(rr() % 15);
            if (m[i] == kb_Clear && rr() % 60) continue;
            kb[g[i]] |= m[i];
        }
        return;
    }
    case SC_SHOTS:
        if (stable < 8 && cur_screen != SCR_ID_MATCH) return;
        switch (shots_stage) {
        case 0: if (cur_screen == SCR_ID_TITLE) { write_ppm("01_title", scr_idx); title_goto(4); TAP_OK(); shots_stage++; } break;
        case 1: if (cur_screen == SCR_ID_CONTROLS) { write_ppm("02_controls", scr_idx); TAP_OK(); shots_stage++; } break;
        case 2: if (cur_screen == SCR_ID_TITLE) { title_goto(2); TAP_OK(); shots_stage++; } break;
        case 3: if (cur_screen == SCR_ID_TWOP) { write_ppm("03_two_players", scr_idx); TAP_CLEAR(); shots_stage++; } break;
        case 4: if (cur_screen == SCR_ID_TITLE) { title_goto(0); TAP_OK(); shots_stage++; } break;
        case 5: if (cur_screen == SCR_ID_TEAMSEL) { int k; for (k = 0; k < 3; k++) tap(7, kb_Right); shots_stage++; } break;
        case 6: if (cur_screen == SCR_ID_TEAMSEL) { write_ppm("04_team_select", scr_idx); TAP_OK(); shots_stage++; } break;
        case 7:
            if (cur_screen == SCR_ID_MATCH) {
                cur_match_ticks++;
                if (cur_match_ticks == 20) shot_later("05_kickoff_help", 1);
                if (cur_match_ticks == 260) shot_later("06_match", 1);
                if (cur_match_ticks == 700) { TAP_CLEAR(); shots_stage++; }
            }
            break;
        case 8: if (cur_screen == SCR_ID_PAUSE && stable > 2) { write_ppm("10_pause", scr_idx); TAP_OK(); shots_stage++; } break;
        case 9:
            if (cur_screen == SCR_ID_MATCH) match_guard();
            if (cur_screen == SCR_ID_RESULTS) { write_ppm("11_results", scr_idx); TAP_OK(); shots_stage++; }
            break;
        case 10:
            /* more AI matches with forced teams until every ability was captured */
            if (cur_screen == SCR_ID_MATCH) { match_guard(); return; }
            if (cur_screen == SCR_ID_TITLE) {
                int k, all = 1;
                for (k = 0; k < NTEAMS; k++) if (!shots_taken[2 + k]) all = 0;
                if (all || shot_match >= 30) { title_goto(1); TAP_OK(); shots_stage++; return; }
                forced_t0 = shot_match % NTEAMS;
                forced_t1 = (shot_match + 5) % NTEAMS;
                shot_match++;
                title_goto(0); TAP_OK();
            } else TAP_OK();
            break;
        case 11: if (cur_screen == SCR_ID_TEAMSEL) { TAP_OK(); shots_stage++; } break;
        case 12: if (cur_screen == SCR_ID_BRACKET) { write_ppm("12_bracket", scr_idx); TAP_CLEAR(); shots_stage++; } break;
        case 13: if (cur_screen == SCR_ID_TITLE) { p1ai = 0; title_goto(2); TAP_OK(); shots_stage++; } break;
        case 14: if (cur_screen == SCR_ID_TWOP) { tap(7, kb_Down); TAP_OK(); shots_stage++; } break;
        case 15:
            if (cur_screen == SCR_ID_TEAMSEL) TAP_OK();
            if (cur_screen == SCR_ID_MATCH) {
                random_hold(kb, 0); random_hold(kb, 1);
                if (++cur_match_ticks == 400) shot_later("13_two_player_match", 1);
            }
            if (cur_screen == SCR_ID_RESULTS) TAP_OK();
            if (cur_screen == SCR_ID_MESSAGE) { write_ppm("14_series", scr_idx); TAP_CLEAR(); shots_stage++; }
            break;
        default:
            if (cur_screen == SCR_ID_MATCH) { TAP_CLEAR(); tap(7, kb_Down); tap(7, kb_Down); TAP_OK(); }
            else if (cur_screen == SCR_ID_TITLE) TAP_CLEAR();
            else TAP_CLEAR();
            break;
        }
        return;
    default:
        return;
    }
}

void stub_feed_keys(uint8_t *kb)
{
    memset(kb, 0, 8);
    if (tap_release) { tap_release = 0; return; }
    if (qh != qt) {
        kb[q[qh & 255].g] |= q[qh & 255].m;
        qh++;
        tap_release = 1;
        return;
    }
    script(kb);
}

/* ---- exhaustive ghosting check through the game's own decoder ---- */
static int ghost_check(void)
{
    /* realistic per-player combos: direction x jump x one action */
    static const uint8_t P1G[6] = { 7, 7, 7, 7, 1, 2 }, P1M[6] = { kb_Left, kb_Right, kb_Up, kb_Down, kb_2nd, kb_Alpha };
    static const uint8_t P2G[6] = { 3, 5, 4, 4, 4, 5 }, P2M[6] = { kb_Apps, kb_Vars, kb_Stat, kb_Prgm, kb_LParen, kb_RParen };
    /* order in both tables: left, right, jump, slide, shoot, super */
    int c1, c2, bad = 0, total = 0;
    for (c1 = 0; c1 < 24; c1++) for (c2 = 0; c2 < 24; c2++) {
        uint8_t kb[8] = { 0 }, gk[8], none[8] = { 0 };
        uint8_t want[2][6], got[2][6];
        int who;
        for (who = 0; who < 2; who++) {
            int c = who ? c2 : c1, d = c / 8, j = (c / 4) & 1, a = c & 3;
            const uint8_t *G = who ? P2G : P1G, *Mk = who ? P2M : P1M;
            if (d == 1) kb[G[0]] |= Mk[0];
            if (d == 2) kb[G[1]] |= Mk[1];
            if (j) kb[G[2]] |= Mk[2];
            if (a) kb[G[2 + a]] |= Mk[2 + a];
        }
        memcpy(gk, kb, 8);
        stub_ghost(gk);
        for (who = 0; who < 2; who++) {
            fbl_test_decode(kb, none, (uint8_t)who, want[who]);
            fbl_test_decode(gk, none, (uint8_t)who, got[who]);
        }
        total++;
        if (memcmp(want, got, sizeof want) || (gk[6] & kb_Clear) || (gk[1] & 0x1F)) {
            bad++;
            if (bad < 5) fprintf(stderr, "ghost problem: P1 combo %d, P2 combo %d\n", c1, c2);
        }
    }
    printf("ghost check: %d of %d realistic two-player key combinations decode wrongly (incl. phantom CLEAR/panic keys)\n", bad, total);
    /* the number pad layout suggested in the brief, for comparison */
    {
        static const uint8_t NG[6] = { 3, 5, 4, 4, 3, 3 }, NM[6] = { kb_4, (1 << 2), (1 << 3), (1 << 2), kb_1, kb_0 };
        int nb = 0;
        for (c1 = 0; c1 < 24; c1++) for (c2 = 0; c2 < 24; c2++) {
            uint8_t kb[8] = { 0 }, gk[8];
            int who, k, diff = 0;
            for (who = 0; who < 2; who++) {
                int c = who ? c2 : c1, d = c / 8, j = (c / 4) & 1, a = c & 3;
                const uint8_t *G = who ? NG : P1G, *Mk = who ? NM : P1M;
                if (d == 1) kb[G[0]] |= Mk[0];
                if (d == 2) kb[G[1]] |= Mk[1];
                if (j) kb[G[2]] |= Mk[2];
                if (a) kb[G[2 + a]] |= Mk[2 + a];
            }
            memcpy(gk, kb, 8);
            stub_ghost(gk);
            for (k = 0; k < 6; k++) {
                if (((gk[P1G[k]] & P1M[k]) != 0) != ((kb[P1G[k]] & P1M[k]) != 0)) diff = 1;
                if (((gk[NG[k]] & NM[k]) != 0) != ((kb[NG[k]] & NM[k]) != 0)) diff = 1;
            }
            nb += diff;
        }
        printf("for comparison, a number-pad P2 layout (4 6 8 5 1 0) gives phantom presses in %d of 576\n", nb);
    }
    return bad;
}

int main(int argc, char **argv)
{
    const char *sc = argc > 1 ? argv[1] : "aivai";
    long budget = 0;
    int runs = 0, fail = 0;
    target_n = argc > 2 ? atoi(argv[2]) : 4;
    rs = argc > 3 ? (unsigned)atoi(argv[3]) : 1;
    if (!strcmp(sc, "aivai")) { scenario = SC_AIVAI; p1ai = 1; }
    else if (!strcmp(sc, "tour")) { scenario = SC_TOUR; p1ai = 1; }
    else if (!strcmp(sc, "friendly")) { scenario = SC_FRIENDLY; p1ai = 0; }
    else if (!strcmp(sc, "fuzz")) { scenario = SC_FUZZ; p1ai = 0; budget = target_n; }
    else if (!strcmp(sc, "ghost")) { scenario = SC_GHOST; return ghost_check() ? 1 : 0; }
    else if (!strcmp(sc, "panic")) { scenario = SC_PANIC; p1ai = 1; }
    else if (!strcmp(sc, "balance")) { scenario = SC_BALANCE; p1ai = 1; headless = 1; }
    else if (!strcmp(sc, "shots")) { scenario = SC_SHOTS; p1ai = 1; shot_dir = argv[2]; target_n = 1; }
    else { fprintf(stderr, "unknown scenario\n"); return 2; }
    if (getenv("SHOTDIR")) shot_dir = getenv("SHOTDIR");

    if (scenario == SC_PANIC) {
        for (panic_target = 0; panic_target < PANIC_TARGETS; panic_target++) {
            long f0;
            p1ai = panic_target == 9 ? 0 : 1;
            title_sel = 0; qh = qt = 0; tap_release = 0; cur_screen = -1; stable = 0;
            panic_typed = 0; panic_seen = 0; cur_match_ticks = 0;
            f0 = frames;
            fprintf(stderr, "panic target %d (%s)\n", panic_target, PANIC_NAMES[panic_target]);
            game_main();
            if (panic_seen && panic_typed && qh == qt && stub_gfx_ended && stub_writes == panic_writes_before) {
                panic_runs_ok++;
                printf("panic exit from %-15s OK  (returned in the same scan as the 5th key, no save written)\n",
                       PANIC_NAMES[panic_target]);
            } else {
                fail = 1;
                printf("panic exit from %-15s FAILED (seen=%d typed=%d pending=%d ended=%d writes %ld->%ld)\n",
                       PANIC_NAMES[panic_target], panic_seen, panic_typed, qt - qh, stub_gfx_ended,
                       panic_writes_before, stub_writes);
            }
            (void)f0;
        }
        printf("panic exits OK: %d of %d screens\n", panic_runs_ok, PANIC_TARGETS);
        return fail;
    }

    do {
        title_sel = 0; qh = qt = 0; tap_release = 0; cur_screen = -1;
        panic_seen = 0;
        game_main();
        runs++;
        if (panic_seen) { fprintf(stderr, "ERROR: unexpected panic exit\n"); fail = 1; break; }
        if (stub_files_open) { fprintf(stderr, "ERROR: %d file handles left open\n", stub_files_open); return 1; }
        if (scenario == SC_TOUR && need_restart_check) {
            need_restart_check = 0;
            title_sel = 0; qh = qt = 0; tap_release = 0; cur_screen = -1;
            game_main();
            runs++;
            if (load_active < 1) { fprintf(stderr, "ERROR: saved cup not found after restart\n"); return 1; }
        }
    } while ((scenario == SC_FUZZ && frames < budget) || (scenario == SC_SHOTS && runs < 2));

    printf("scenario=%s runs=%d frames=%ld match_frames=%ld\n", sc, runs, frames, match_frames);
    printf("matches=%ld left_wins=%ld right_wins=%ld goals L=%ld R=%ld golden-goal matches=%ld (longest golden goal %ld ticks)\n",
           matches, wins_side[0], wins_side[1], goals_side[0], goals_side[1], golden_goals, gg_ticks_max);
    if (matches) printf("avg match ticks=%ld (%.1f s)  max=%ld  max total goals in a match=%ld\n",
                        total_ticks / matches, total_ticks / matches / 30.0, max_ticks, high_score);
    printf("ball rests >2s near a platform: %ld (longest %ld ticks)  watchdog re-drops: %ld\n",
           platform_rests, max_rest, watchdogs);
    printf("team W/L/GF:");
    { int t; for (t = 0; t < NTEAMS; t++) printf(" %d:%ld/%ld/%ld", t, team_w[t], team_l[t], team_gf[t]); }
    printf("\nsupers by ability:");
    { int t; for (t = 0; t < NTEAMS; t++) printf(" %s=%ld", PNAME[t], supers_by_power[t]); }
    printf("\ntours=%ld champs=%ld out_by_round=%ld/%ld/%ld loads=%ld load_active=%ld series_done=%ld\n",
           tours, champs, out_round[0], out_round[1], out_round[2], loads, load_active, series_done);
    printf("restore pixel checks=%ld mismatches=%ld  24bit overflows=%ld  violations=%ld  text overflow=%ld  stuck=%ld\n",
           pix_checks, pix_mismatch, chk24_fail, violations, text_overflow, stuck_matches);
    if (cyc_n) {
        int b;
        printf("EST. cycles/frame avg=%.0f max=%.0f (budget 1.6M @30fps) frames over budget=%ld\n",
               cyc_sum / cyc_n, cyc_max, over_budget);
        printf("cost histogram (100k-cycle buckets):");
        for (b = 0; b < 20; b++) if (hist[b]) printf(" %d00k:%ld", b, hist[b]);
        printf("\n");
    }
    if (scenario == SC_BALANCE) {
        int a, b;
        printf("\nlegend win rates (all pairs, both sides):\n");
        for (a = 0; a < NTEAMS; a++) {
            long w = team_w[a], n = team_w[a] + team_l[a];
            printf("  %-11s %5.1f%%  (%ld/%ld)\n", PNAME[a], n ? 100.0 * w / n : 0.0, w, n);
        }
        printf("pair matrix (row beat column, %% of their meetings):\n            ");
        for (b = 0; b < NTEAMS; b++) printf("%5.5s ", PNAME[b]);
        printf("\n");
        for (a = 0; a < NTEAMS; a++) {
            printf("  %-9.9s ", PNAME[a]);
            for (b = 0; b < NTEAMS; b++) {
                if (a == b || !pair_n[a][b]) printf("   -  ");
                else printf("%4ld%% ", 100 * pair_w[a][b] / pair_n[a][b]);
            }
            printf("\n");
        }
    }
    printf("save var size=%zu archived=%d\n", stub_var_size("FBLGSAVE"), stub_var_archived("FBLGSAVE"));
    return (fail || pix_mismatch || chk24_fail || violations || text_overflow || stuck_matches) ? 1 : 0;
}
