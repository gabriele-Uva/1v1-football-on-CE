/* Host test harness for Football Legends CE.
 * Builds the real src/main.c against stub headers with a fake framebuffer,
 * drives it with scripted / random key presses and checks invariants.
 *
 *   harness aivai  <matches> <seed>   AI vs AI quick matches (Normal, then Hard)
 *   harness tour   <cups>    <seed>   AI-played tournaments, incl. save/continue
 *   harness fuzz   <frames>  <seed>   random keys on every screen
 *   harness friendly <matches> <seed> 2 players hammering random keys
 *   harness shots  <outdir>  <seed>   screenshots of every screen
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
       SCR_ID_BRACKET, SCR_ID_MESSAGE, SCR_ID_STATS };
enum { SC_AIVAI, SC_TOUR, SC_FUZZ, SC_FRIENDLY, SC_SHOTS };

static int scenario, p1ai, target_n;
static unsigned rs = 12345;
static unsigned rr(void) { rs = rs * 1103515245u + 12345u; return (rs >> 16) & 0x7fff; }

/* ---- fake clock: advances on every call, so busy waits terminate ---- */
static long fclock;
clock_t fake_clock(void) { fclock += 137; return (clock_t)fclock; }

/* ---- results ---- */
static long matches, goals_side[2], wins_side[2], golden, gg_ticks_max, max_ticks, total_ticks;
static long team_w[8], team_l[8], team_gf[8], supers_by_power[8], high_score;
static long tours, champs, out_round[4], chk24_fail, pix_mismatch, pix_checks, stuck_matches;
long frames;
static long match_frames, title_visits, loads, load_active;
static long hist[20];
static double cyc_sum; static long cyc_n; static double cyc_max; static long over_budget;
static unsigned long cur_match_ticks;
static long golden_goals;

int test_p1_ai(void) { return p1ai; }
int test_p1_diff(void) { return getenv("P1DIFF") ? atoi(getenv("P1DIFF")) : -1; }
void test_chk24(long v, int line)
{
    if (v > 8388607L || v < -8388608L) {
        chk24_fail++;
        if (chk24_fail < 5) fprintf(stderr, "24-bit overflow %ld at line %d\n", v, line);
    }
}

/* ---- key scripting ---- */
typedef struct { uint8_t g, m; } tapk_t;
static tapk_t q[256];
static int qh, qt, tap_release, cur_screen = -1, stable, title_sel;
static void tap(uint8_t g, uint8_t m) { q[qt & 255].g = g; q[qt & 255].m = m; qt++; }
#define TAP_OK() tap(6, kb_Enter)
#define TAP_CLEAR() tap(6, kb_Clear)
static void title_goto(int t) { while (title_sel != t) { tap(7, kb_Down); title_sel = (title_sel + 1) % 6; } }

void test_screen(int id)
{
    if (id == cur_screen) stable++;
    else stable = 0;
    if (id == SCR_ID_TITLE && cur_screen != SCR_ID_TITLE) title_visits++;
    cur_screen = id;
}

/* ---- screenshots ---- */
static const char *shot_dir;
static int shot_pending; static char shot_name[64];
static int shots_taken[16];
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
           cost.slow_px * 40.0 + cost.text_px * 30.0 + cost.chars * 800.0 + 150000.0 /* game logic */;
}

void stub_on_swap(void)
{
    frames++;
    if (frames > 3000000L) { fprintf(stderr, "ERROR: frame cap reached (script stuck?)\n"); exit(3); }
    if (cur_screen == SCR_ID_MATCH) {
        double c = frame_cycles();
        match_frames++;
        cyc_sum += c; cyc_n++;
        if (c > cyc_max) cyc_max = c;
        if (c > 1600000.0) over_budget++;
        { int bkt = (int)(c / 100000.0); if (bkt > 19) bkt = 19; hist[bkt]++; }
        if (getenv("COSTDBG") && c > 900000.0)
            fprintf(stderr, "heavy frame %ld: calls=%ld rows=%ld fill=%ld spr=%ld slow=%ld textpx=%ld chars=%ld\n",
                    frames, cost.calls, cost.rows, cost.fill_px, cost.sprite_px, cost.slow_px, cost.text_px, cost.chars);
    }
    memset(&cost, 0, sizeof cost);
    if (shot_pending && --shot_pending == 0) write_ppm(shot_name, scr_idx);
    if (getenv("MOVIE") && cur_screen == SCR_ID_MATCH) {
        static long mf;
        long a = atol(getenv("MOVIE_FROM")), b = atol(getenv("MOVIE_TO"));
        mf++;
        if (mf >= a && mf < b && (mf & 1) == 0) {
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
void test_bg_ready(uint8_t cheer)
{
    if (!cheer) { memcpy(pristine, stub_draw_buffer(), sizeof pristine); have_pristine = 1; }
}
void test_after_restore(uint8_t cheer)
{
    uint8_t (*b)[320] = stub_draw_buffer();
    int y, x;
    if (cheer || !have_pristine) return;
    pix_checks++;
    for (y = 26; y < 240; y++) {
        if (memcmp(b[y], pristine[y], 320) != 0) {
            for (x = 0; x < 320 && b[y][x] == pristine[y][x]; x++) {}
            pix_mismatch++;
            if (pix_mismatch < 4) {
                fprintf(stderr, "restore mismatch at (%d,%d) got %d want %d (check %ld frame %ld buf %d)\n",
                        x, y, b[y][x], pristine[y][x], pix_checks, frames, scr_idx ^ 1);
                if (shot_dir) {
                    write_ppm(pix_mismatch == 1 ? "restore_mismatch" : "restore_mismatch2", scr_idx ^ 1);
                    write_ppm(pix_mismatch == 1 ? "restore_mismatch_screen" : "restore_mismatch_screen2", scr_idx);
                }
            }
            return;
        }
    }
}

/* ---- game events ---- */
void test_on_goal(uint8_t scorer, uint8_t half, int half_ticks)
{
    static int last_ht = 0, last_half = -1;
    goals_side[scorer]++;
    if (half == 2) { golden_goals++; if (half_ticks > gg_ticks_max) gg_ticks_max = half_ticks; }
    if (getenv("GOALDBG")) fprintf(stderr, "goal side=%d half=%d t=%d since_prev=%d\n", scorer, half, half_ticks,
                                   half == last_half ? half_ticks - last_ht : half_ticks);
    last_ht = half_ticks; last_half = half;
    if (scenario == SC_SHOTS && !shots_taken[0]) { shots_taken[0] = 1; shot_later("06_goal", 12); }
    if (half == 2 && scenario == SC_SHOTS && !shots_taken[1]) { shots_taken[1] = 1; shot_later("09_golden_goal", 14); }
}
void test_on_super(uint8_t who, uint8_t power)
{
    char n[32];
    (void)who;
    supers_by_power[power]++;
    if (scenario == SC_SHOTS && power < 8 && !shots_taken[2 + power]) {
        shots_taken[2 + power] = 1;
        snprintf(n, sizeof n, "07_super_%d", power);
        shot_later(n, 5);
    }
}
void test_on_match_end(uint8_t t0, uint8_t t1, uint8_t s0, uint8_t s1, unsigned long ticks)
{
    matches++;
    total_ticks += (long)ticks;
    if ((long)ticks > max_ticks) max_ticks = (long)ticks;
    if (s0 == s1) { fprintf(stderr, "ERROR: match ended level %d-%d\n", s0, s1); }
    if (s0 > s1) { wins_side[0]++; team_w[t0]++; team_l[t1]++; }
    else { wins_side[1]++; team_w[t1]++; team_l[t0]++; }
    team_gf[t0] += s0; team_gf[t1] += s1;
    if (s0 + s1 > high_score) high_score = s0 + s1;
    golden = golden_goals;
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

/* ---- per-scenario key logic ---- */
static int tour_phase, hard_set, friendly_sel;   /* tournament scenario: 0 = first cup, quit after round 1 */
static int need_restart_check;

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
        if (hold_dir[i] < 0) kb[3] |= kb_Comma;
        if (hold_dir[i] > 0) kb[5] |= kb_RParen;
        if (rr() % 9 == 0) kb[4] |= kb_Prgm;
        if (rr() % 40 == 0) kb[4] |= kb_LParen;
        if (rr() % 6 == 0) kb[3] |= kb_Apps;
        if (rr() % 30 == 0) kb[5] |= kb_Vars;
    }
}

static void match_guard(void)
{
    /* a match that never ends would be a bug: bail out through the pause menu */
    if (++cur_match_ticks == 60000) {
        stuck_matches++;
        fprintf(stderr, "match exceeded 60000 ticks, quitting it\n");
        TAP_CLEAR(); tap(7, kb_Down); tap(7, kb_Down); TAP_OK();
        cur_match_ticks = 0;
    }
}

static void script(uint8_t *kb)
{
    static int shots_stage;
    switch (scenario) {
    case SC_AIVAI:
        if (cur_screen == SCR_ID_MATCH) { match_guard(); return; }
        if (stable < 1) return;
        if (cur_screen == SCR_ID_TITLE) {
            if (matches >= target_n) { TAP_CLEAR(); return; }
            if (getenv("HARD") && !hard_set) { hard_set = 1; title_goto(3); TAP_OK(); return; }
            if (!getenv("NOSWITCH") && !getenv("HARD") && matches == target_n / 2 && title_sel != 3) {
                /* switch to Hard for the second half of the run */
                title_goto(3); TAP_OK(); return;
            }
            title_goto(0); TAP_OK();
        } else if (cur_screen == SCR_ID_TEAMSEL) {
            int k, n = (int)(rr() % 8);
            for (k = 0; k < n; k++) tap(7, kb_Right);
            TAP_OK();
        } else {
            TAP_OK();
        }
        return;
    case SC_TOUR:
        if (cur_screen == SCR_ID_MATCH) { match_guard(); return; }
        if (stable < 1) return;
        if (cur_screen == SCR_ID_TITLE) {
            if (tours >= target_n) { TAP_CLEAR(); return; }
            if (tours == target_n / 2 && title_sel != 3 && tour_phase == 2) {
                title_goto(3); TAP_OK(); tour_phase = 3; return;
            }
            if (tour_phase == 1) { TAP_CLEAR(); need_restart_check = 1; tour_phase = 2; return; }
            title_goto(1); TAP_OK();
        } else if (cur_screen == SCR_ID_TEAMSEL) {
            int k, n = (int)(rr() % 8);
            for (k = 0; k < n; k++) tap(7, kb_Right);
            TAP_OK();
        } else if (cur_screen == SCR_ID_BRACKET && tour_phase == 0 && matches == 1) {
            /* leave mid-cup, quit the program, come back and continue */
            TAP_CLEAR(); tour_phase = 1;
        } else {
            TAP_OK();
        }
        return;
    case SC_FRIENDLY:
        if (cur_screen == SCR_ID_MATCH) { random_hold(kb, 0); random_hold(kb, 1); match_guard(); return; }
        if (stable < 1) return;
        if (cur_screen == SCR_ID_TITLE) {
            if (matches >= target_n) { TAP_CLEAR(); return; }
            title_goto(2); TAP_OK(); friendly_sel = 0;
        } else if (cur_screen == SCR_ID_TEAMSEL) {
            /* second pick: step back to P1's team to force the away kit */
            if (friendly_sel++ == 1) tap(7, kb_Left);
            TAP_OK();
        } else {
            TAP_OK();
        }
        return;
    case SC_FUZZ: {
        static const uint8_t g[] = { 7, 7, 7, 7, 1, 2, 6, 6, 3, 4, 4, 5, 3, 5, 1 };
        static const uint8_t m[] = { kb_Left, kb_Right, kb_Up, kb_Down, kb_2nd, kb_Alpha, kb_Enter,
                                     kb_Clear, kb_Comma, kb_LParen, kb_Prgm, kb_RParen, kb_Apps, kb_Vars, kb_Del };
        int n = (int)(rr() % 4), k;
        if (cur_screen == SCR_ID_MATCH) { random_hold(kb, 0); random_hold(kb, 1); }
        for (k = 0; k < n; k++) {
            int i = (int)(rr() % 15);
            if (m[i] == kb_Clear && rr() % 60) continue;   /* keep Clear rare */
            kb[g[i]] |= m[i];
        }
        return;
    }
    case SC_SHOTS:
        if (stable < 2 && cur_screen != SCR_ID_MATCH) return;
        switch (shots_stage) {
        case 0: if (cur_screen == SCR_ID_TITLE) { write_ppm("01_title", scr_idx); title_goto(4); TAP_OK(); shots_stage++; } break;
        case 1: if (cur_screen == SCR_ID_STATS) { write_ppm("02_records", scr_idx); TAP_OK(); shots_stage++; } break;
        case 2: if (cur_screen == SCR_ID_TITLE) { title_goto(0); TAP_OK(); shots_stage++; } break;
        case 3: if (cur_screen == SCR_ID_TEAMSEL) { int k; for (k = 0; k < 3; k++) tap(7, kb_Right); shots_stage++; } break;
        case 4: if (cur_screen == SCR_ID_TEAMSEL) { write_ppm("03_team_select", scr_idx); TAP_OK(); shots_stage++; } break;
        case 5:
            if (cur_screen == SCR_ID_MATCH) {
                if (cur_match_ticks == 30) shot_later("04_kickoff", 1);
                if (cur_match_ticks == 160) shot_later("05_match", 1);
                if (cur_match_ticks == 700) { TAP_CLEAR(); shots_stage++; }
                cur_match_ticks++;
            }
            break;
        case 6: if (cur_screen == SCR_ID_PAUSE && stable > 2) { write_ppm("08_pause", scr_idx); TAP_OK(); shots_stage++; } break;
        case 7:
            if (cur_screen == SCR_ID_MATCH) { match_guard(); if (cur_match_ticks == 1000) shot_later("05b_match", 1); }
            if (cur_screen == SCR_ID_RESULTS && stable > 2) { write_ppm("10_results", scr_idx); TAP_OK(); shots_stage++; }
            break;
        case 8: if (cur_screen == SCR_ID_TITLE) { title_goto(1); TAP_OK(); shots_stage++; } break;
        case 9: if (cur_screen == SCR_ID_TEAMSEL) { TAP_OK(); shots_stage++; } break;
        case 10: if (cur_screen == SCR_ID_BRACKET && stable > 2) { write_ppm("11_bracket", scr_idx); TAP_OK(); shots_stage++; } break;
        case 11:
            if (cur_screen == SCR_ID_MATCH) match_guard();
            if (cur_screen == SCR_ID_BRACKET && stable > 2) { write_ppm("12_bracket_r2", scr_idx); TAP_CLEAR(); shots_stage++; }
            if (cur_screen == SCR_ID_MESSAGE && stable > 2) { write_ppm("12_cup_over", scr_idx); TAP_OK(); }
            if (cur_screen == SCR_ID_RESULTS && stable > 2) TAP_OK();
            if (cur_screen == SCR_ID_TITLE && stable > 2) { TAP_CLEAR(); shots_stage++; }
            break;
        default:
            if (cur_screen == SCR_ID_TITLE) { write_ppm("13_title_continue", scr_idx); TAP_CLEAR(); }
            break;
        }
        return;
    }
}

void stub_feed_keys(uint8_t *kb)
{
    memset(kb, 0, 8);
    if (cur_screen == SCR_ID_MATCH && scenario != SC_SHOTS) cur_match_ticks += 0; /* counted in guard */
    if (tap_release) { tap_release = 0; return; }
    if (qh != qt) {
        kb[q[qh & 255].g] |= q[qh & 255].m;
        qh++;
        tap_release = 1;
        return;
    }
    script(kb);
}

int main(int argc, char **argv)
{
    const char *sc = argc > 1 ? argv[1] : "aivai";
    long budget;
    int runs = 0;
    target_n = argc > 2 ? atoi(argv[2]) : 4;
    rs = argc > 3 ? (unsigned)atoi(argv[3]) : 1;
    if (!strcmp(sc, "aivai")) { scenario = SC_AIVAI; p1ai = 1; }
    else if (!strcmp(sc, "tour")) { scenario = SC_TOUR; p1ai = 1; }
    else if (!strcmp(sc, "friendly")) { scenario = SC_FRIENDLY; p1ai = 0; }
    else if (!strcmp(sc, "fuzz")) { scenario = SC_FUZZ; p1ai = 0; }
    else if (!strcmp(sc, "shots")) { scenario = SC_SHOTS; p1ai = 1; shot_dir = argv[2]; target_n = 1; }
    else { fprintf(stderr, "unknown scenario\n"); return 2; }

    budget = scenario == SC_FUZZ ? target_n : 0;
    if (getenv("SHOTDIR")) shot_dir = getenv("SHOTDIR");
    do {
        title_sel = 0; qh = qt = 0; tap_release = 0; cur_screen = -1;
        game_main();
        runs++;
        if (stub_files_open) { fprintf(stderr, "ERROR: %d file handles left open\n", stub_files_open); return 1; }
        if (scenario == SC_TOUR && need_restart_check) {
            need_restart_check = 0;
            /* the next run must find the saved, still-active cup */
            title_sel = 0; qh = qt = 0; tap_release = 0; cur_screen = -1;
            game_main();
            runs++;
            if (load_active < 1) { fprintf(stderr, "ERROR: saved cup not found after restart\n"); return 1; }
        }
    } while ((scenario == SC_FUZZ && frames < budget) || (scenario == SC_SHOTS && runs < 2));

    printf("scenario=%s runs=%d frames=%ld match_frames=%ld\n", sc, runs, frames, match_frames);
    printf("matches=%ld left_wins=%ld right_wins=%ld goals L=%ld R=%ld golden-goal matches=%ld (longest golden goal %ld ticks)\n",
           matches, wins_side[0], wins_side[1], goals_side[0], goals_side[1], golden, gg_ticks_max);
    if (matches) printf("avg match ticks=%ld (%.1f s)  max=%ld  max total goals in a match=%ld\n",
                        total_ticks / matches, total_ticks / matches / 30.0, max_ticks, high_score);
    printf("team W/L/GF:");
    { int t; for (t = 0; t < 8; t++) printf(" %d:%ld/%ld/%ld", t, team_w[t], team_l[t], team_gf[t]); }
    printf("\nsupers by power:");
    { int t; for (t = 0; t < 8; t++) printf(" %ld", supers_by_power[t]); }
    printf("\ntours=%ld champs=%ld out_by_round=%ld/%ld/%ld loads=%ld load_active=%ld\n",
           tours, champs, out_round[0], out_round[1], out_round[2], loads, load_active);
    printf("restore pixel checks=%ld mismatches=%ld  24bit overflows=%ld  violations=%ld  text overflow=%ld  stuck=%ld\n",
           pix_checks, pix_mismatch, chk24_fail, violations, text_overflow, stuck_matches);
    if (cyc_n) printf("EST. cycles/frame avg=%.0f max=%.0f (budget 1.6M @30fps) frames over budget=%ld\n",
                      cyc_sum / cyc_n, cyc_max, over_budget);
    if (cyc_n) { int b; printf("cost histogram (100k-cycle buckets):"); for (b = 0; b < 20; b++) if (hist[b]) printf(" %d00k:%ld", b, hist[b]); printf("\n"); }
    printf("save var size=%zu archived=%d\n", stub_var_size("FBLGSAVE"), stub_var_archived("FBLGSAVE"));
    return (pix_mismatch || chk24_fail || violations || text_overflow || stuck_matches) ? 1 : 0;
}
