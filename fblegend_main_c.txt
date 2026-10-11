/*
 * Football Legends CE  (v2)
 * A 2D side-view "head soccer" game for the TI-84 Plus CE.
 * Built with the CE C/C++ Toolchain (CEdev): graphx, keypadc, fileioc.
 *
 * Engine notes
 *  - Fixed 30 Hz timestep driven by clock() (32768 Hz hardware timer).
 *  - All physics in integer fixed point: 1 pixel = 64 units.
 *    int is 24 bits on the eZ80, so every product that can get big is
 *    done in long (32 bits).
 *  - Double buffered. The stadium is drawn procedurally (rectangles,
 *    horizontal/vertical spans, clipped sprites and pre-computed pixel
 *    lists only), once into both buffers. Each frame only the rectangles
 *    that moving things covered two frames ago (in the same buffer) are
 *    redrawn; a clipped redraw is pixel-identical to the full one.
 *  - The crowd, flags, fire and menu highlights are animated by rewriting
 *    palette entries, which costs no drawing at all.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <time.h>
#include <tice.h>
#include <graphx.h>
#include <keypadc.h>
#include <fileioc.h>

#ifdef FBL_HOST_TEST
#include "test_hooks.h"
#define TEST_HOOK(x) x
#define TEST_P1_AI (test_p1_ai())
#define CHK24(v) test_chk24((long)(v), __LINE__)
#else
#define TEST_HOOK(x)
#define TEST_P1_AI 0
#define CHK24(v) ((void)0)
#endif

/* ------------------------------------------------------------------ */
/* Screen and pitch geometry (pixels)                                  */
/* ------------------------------------------------------------------ */
#define SCR_W 320
#define SCR_H 240
#define G_MAIN 182          /* feet on the pitch */
#define G_PLAT 170          /* top of the goal platforms */
#define PLAT_X 36           /* platform top: x < 36 and x > 284 */
#define RAMP_X 50           /* ramp from x = 36..50 down to the pitch */
#define BAR_TOP 104         /* crossbar */
#define BAR_BOT 110
#define BAR_LEN 38          /* crossbar spans x 0..38 and 282..320 */
#define GOAL_LINE 34        /* ball wholly left of x=34 (or right of 286) = goal */
#define CEIL_Y 3
#define STAND_Y0 30
#define CROWD_W 64
#define CROWD_H 24
#define CROWD_TIERS 3
#define STAND_Y1 (STAND_Y0 + CROWD_TIERS * CROWD_H)   /* 102 */
#define AD_Y 102
#define AD_W 64
#define AD_H 10
#define TRACK_Y 112
#define PITCH_Y0 120
#define PITCH_Y1 196
#define SOIL_Y 200
#define ROOF_ROWS 60
#define NET_W 12
#define NET_H 8

/* ------------------------------------------------------------------ */
/* Physics constants (fixed point, 64 units = 1 px, per 1/30 s tick)   */
/* ------------------------------------------------------------------ */
#define FP 64
#define PX(v) (((v) + 32) >> 6)   /* rounded, so mirrored positions match */
#define TICK_HZ 30
#define TICK_CLK 1092     /* 32768 Hz clock units per tick (and 8/30 more) */
#define HALF_TICKS (30 * TICK_HZ)

#define BALL_R 6
#define HEAD_R 12
#define BODY_HW 6
#define BODY_H 20
#define HEAD_DY 30
#define LIE_HW 13
#define LIE_H 10
#define LIE_HEAD_DX 17

#define GRAV_B 36           /* 0.56 px/tick^2 */
#define GRAV_P 64           /* 1.00 px/tick^2 */
#define JUMP_V 672          /* 10.5 px/tick -> ~55 px jump */
#define ROCKET_V 906        /* rocket jump: ~100 px */
#define RUN_V 320           /* 5 px/tick */
#define RUN_ACC 112
#define AIR_ACC 64
#define SLIDE_V 512
#define SLIDE_T 12
#define RECOVER_T 8
#define KNOCK_T 30
#define FREEZE_T 45         /* 1.5 s */
#define KICK_T 9
#define KICK_ACTIVE 5
#define CHARGE_T 90
#define POWER_MAX 100
#define MAXV 1152
#define SHOT_V 576
#define VOLLEY_V 544
#define HEADER_V 576
#define MAXFALL 1024
#define BOUNCE_MIN 77

#define MIN_X 12
#define MAX_X 308
#define GOAL_ZONE 50        /* centre x < 50: head would be under the crossbar */

/* ability durations (ticks) */
#define GIANT_T 150
#define MAGNET_T 90
#define CLONE_T 120
#define WALL_T 120
#define SPEED_T 150
#define ROCKET_T 45
#define HEAVY_T 120

/* ------------------------------------------------------------------ */
/* Palette (256 entries, built at start-up from ramps)                 */
/* ------------------------------------------------------------------ */
enum {
    C_BLACK = 0, C_WHITE, C_TRANSP, C_DKGRAY, C_GRAY, C_LTGRAY, C_RED, C_ORANGE,
    C_YELLOW, C_GOLD, C_CYAN, C_PURPLE, C_PINK, C_GREEN, C_BLUE, C_NAVY,
    C_SKY = 16,         /* 6: top .. horizon */
    C_CLOUD = 22,       /* 2: white, shade */
    C_ROOF = 24,        /* 6: rim .. underside dark */
    C_WALL = 30,        /* 3 */
    C_SEAT = 33,        /* 6: light seat, mid, dark gap, rail, aisle, aisle dark */
    C_TUNNEL = 39,      /* 3 */
    C_AD = 42,          /* 4 */
    C_TRACK = 46,       /* 3 */
    C_GRASS = 49,       /* 8: A far/mid/near, B far/mid/near, line, shadow */
    C_LIP = 57,         /* 3 */
    C_PLAT = 60,        /* 4: top, face, face dark, edge */
    C_SOIL = 64,        /* 6 */
    C_NET = 70,         /* 4: net, net shade, post, post shade */
    C_SKIN = 74,        /* 4 tones x 3: hi, base, shadow */
    C_HAIR = 86,        /* 7 x 2: base, shade */
    C_KIT = 100,        /* 18 colours x 2: base, shade */
    C_FIRE = 136,       /* 4, animated */
    C_ICE = 140,        /* 4 */
    C_GHOST = 144,      /* 3 */
    C_PANEL = 147, C_PANEL2, C_BORDER, C_SEL, C_KEYCAP, C_KEYSH, C_OUTLINE, C_MOUTH,
    C_GLOW = 155,       /* animated: ready glow */
    C_MAGNET = 156,     /* animated */
    C_WALLFX = 157,     /* animated */
    C_ARM = 160,        /* 16, animated crowd arms */
    C_FLAG = 176,       /* 3, animated flags */
    C_NUM_USED = 179
};

/* kit colour pairs (base index; base+1 is the shade) */
enum {
    K_YEL = C_KIT, K_GRN = C_KIT + 2, K_BLUE = C_KIT + 4, K_SKY = C_KIT + 6,
    K_NAVY = C_KIT + 8, K_RED = C_KIT + 10, K_MAR = C_KIT + 12, K_AZU = C_KIT + 14,
    K_ORA = C_KIT + 16, K_DGRN = C_KIT + 18, K_WHITE = C_KIT + 20, K_BLACK = C_KIT + 22,
    K_GOLD = C_KIT + 24, K_PURP = C_KIT + 26, K_LIME = C_KIT + 28, K_PINK = C_KIT + 30,
    K_GRAY = C_KIT + 32, K_BROWN = C_KIT + 34
};

static uint16_t pal[256];

/* ------------------------------------------------------------------ */
/* Teams, legends and abilities                                        */
/* ------------------------------------------------------------------ */
enum { PAT_COLLAR, PAT_STRIPES, PAT_BAND, PAT_SASH };
enum { HS_BUZZ, HS_LONG, HS_SLICK, HS_CURLY, HS_BALD, HS_SPIKY, HS_MOHAWK, HS_AFRO,
       HS_BUN, HS_DREADS };
enum { PW_FIRE, PW_TELE, PW_FREEZE, PW_ROCKET, PW_GIANT, PW_MAGNET, PW_CLONE, PW_WALL,
       PW_SPEED, PW_HEAVY, PW_COUNT };
enum { HAIR_BLACK, HAIR_BROWN, HAIR_BLONDE, HAIR_GINGER, HAIR_DKBROWN, HAIR_GRAY, HAIR_RED };

typedef struct {
    const char *name;
    const char *code;
    const char *legend;
    uint8_t shirt, trim, shorts, socks, pattern;
    uint8_t alt_shirt, alt_trim, alt_shorts;
    uint8_t skin, hair, style, beard, boots;
    uint8_t power;
} team_t;

#define NUM_TEAMS 10
static const team_t TEAMS[NUM_TEAMS] = {
    { "BRAZIL", "BRA", "FERA", K_YEL, K_GRN, K_BLUE, K_WHITE, PAT_COLLAR,
      K_BLUE, K_YEL, K_WHITE, 2, HAIR_BLACK, HS_BUZZ, 0, K_WHITE, PW_FIRE },
    { "ARGENTINA", "ARG", "EL PIBE", K_SKY, K_WHITE, K_BLACK, K_WHITE, PAT_STRIPES,
      K_NAVY, K_SKY, K_NAVY, 1, HAIR_BROWN, HS_LONG, 0, K_PURP, PW_TELE },
    { "GERMANY", "GER", "KAISER", K_WHITE, K_BLACK, K_BLACK, K_WHITE, PAT_BAND,
      K_DGRN, K_WHITE, K_WHITE, 0, HAIR_BLONDE, HS_SLICK, 0, K_BLACK, PW_FREEZE },
    { "NETHERLANDS", "NED", "ROCKY", K_ORA, K_BLACK, K_BLACK, K_ORA, PAT_COLLAR,
      K_NAVY, K_ORA, K_NAVY, 0, HAIR_GINGER, HS_CURLY, 1, K_WHITE, PW_ROCKET },
    { "FRANCE", "FRA", "LE ROI", K_NAVY, K_RED, K_WHITE, K_RED, PAT_BAND,
      K_WHITE, K_NAVY, K_NAVY, 3, HAIR_BLACK, HS_BALD, 0, K_GOLD, PW_GIANT },
    { "SPAIN", "ESP", "MATADOR", K_RED, K_YEL, K_NAVY, K_NAVY, PAT_COLLAR,
      K_WHITE, K_RED, K_WHITE, 1, HAIR_BLACK, HS_SPIKY, 1, K_YEL, PW_MAGNET },
    { "PORTUGAL", "POR", "CICLONE", K_MAR, K_GRN, K_GRN, K_MAR, PAT_SASH,
      K_WHITE, K_MAR, K_WHITE, 1, HAIR_DKBROWN, HS_MOHAWK, 0, K_LIME, PW_CLONE },
    { "ITALY", "ITA", "MURO", K_AZU, K_WHITE, K_WHITE, K_AZU, PAT_COLLAR,
      K_WHITE, K_AZU, K_AZU, 0, HAIR_BLACK, HS_AFRO, 1, K_BLUE, PW_WALL },
    { "ENGLAND", "ENG", "LION", K_WHITE, K_RED, K_NAVY, K_WHITE, PAT_COLLAR,
      K_RED, K_WHITE, K_WHITE, 0, HAIR_BROWN, HS_BUN, 0, K_RED, PW_SPEED },
    { "MEXICO", "MEX", "CHICO", K_GRN, K_WHITE, K_WHITE, K_RED, PAT_BAND,
      K_WHITE, K_GRN, K_GRN, 2, HAIR_BLACK, HS_DREADS, 0, K_ORA, PW_HEAVY },
};

static const char *const POWER_NAME[PW_COUNT] = {
    "FIREBALL", "TELEPORT", "FREEZE", "ROCKET JUMP", "GIANT",
    "MAGNET", "CLONE", "WALL", "SPEED BOOST", "HEAVY BALL"
};
static const char *const POWER_SHORT[PW_COUNT] = {
    "FIRE", "WARP", "FREEZE", "ROCKET", "GIANT", "MAGNET", "CLONE", "WALL", "SPEED", "HEAVY"
};
static const char *const POWER_DESC[PW_COUNT] = {
    "A blazing, dead-straight shot at goal",
    "Ball warps to the rival's goal mouth",
    "Rival frozen solid for 1.5 seconds",
    "Huge jump, next header smashes down",
    "Grow for 5 s: bigger head and body",
    "Pulls the ball towards you for 3 s",
    "A shadow twin plays beside you, 4 s",
    "A barrier guards your goal for 4 s",
    "Run 60% faster for 5 seconds",
    "Iron ball: bowls over the keeper"
};
/* abilities that need the ball (charge, then the next touch fires them) */
#define PW_NEEDS_BALL(p) ((p) == PW_FIRE || (p) == PW_TELE || (p) == PW_HEAVY)
static const uint8_t POWER_COL[PW_COUNT] = {
    C_ORANGE, C_PURPLE, C_CYAN, C_RED, C_GOLD, C_MAGNET, C_GHOST + 1, C_WALLFX, C_YELLOW, C_GRAY
};

/* ------------------------------------------------------------------ */
/* Game objects                                                        */
/* ------------------------------------------------------------------ */
typedef struct {
    bool left, right, jump, slide, shoot, super;
} ctrl_t;

typedef struct {
    int x, y;               /* fixed point: x = centre, y = feet */
    int vx, vy;
    int8_t dir;             /* attack direction, also facing (+1 right) */
    int8_t slide_dir;
    int8_t lie_dir;
    bool on_ground;
    bool knocked;
    bool kick_hit;
    bool tackled;
    bool celebrate;
    uint8_t kick_t, slide_t, down_t, frozen_t, charged_t, touch_cd;
    uint8_t power;
    uint8_t anim;
    uint8_t ab_t;           /* remaining ticks of a timed ability */
    uint8_t land_t;         /* squash after landing */
    uint8_t popup_t;        /* ability name popup */
} player_t;

enum { BM_NONE, BM_FIRE, BM_TELE, BM_SMASH, BM_HEAVY };

typedef struct {
    int x, y, vx, vy;
    uint8_t r;
    uint8_t mode;
    uint8_t mode_t;
    uint8_t owner;
    uint8_t immune;
    uint8_t spin;
    uint8_t squash;
    bool plowed;
    int anchor_x, anchor_y;
    int idle_t;
} ball_t;

typedef struct {
    int x, y;               /* 1/16 px */
    int8_t vx, vy;
    uint8_t life, col, size, kind;
} particle_t;
enum { PK_DOT, PK_GRAV, PK_STAR };

#define MAX_PARTS 64

enum { MODE_QUICK, MODE_TOUR, MODE_FRIENDLY };
enum { PH_KICKOFF, PH_PLAY, PH_GOAL, PH_HALFTIME, PH_GGINTRO, PH_END };
enum { SCR_ID_TITLE, SCR_ID_TEAMSEL, SCR_ID_MATCH, SCR_ID_PAUSE, SCR_ID_RESULTS,
       SCR_ID_BRACKET, SCR_ID_MESSAGE, SCR_ID_STATS, SCR_ID_CONTROLS, SCR_ID_TWOP,
       SCR_ID_SERIES };

typedef struct {
    uint8_t mode, diff;
    uint8_t team[2];
    bool human[2];
    bool kit_alt[2];
    uint8_t score[2];
    uint8_t half;           /* 0, 1, 2 = golden goal */
    int half_ticks;
    uint8_t phase;
    int phase_t;
    uint8_t last_scorer;
    uint8_t touches[2], supers[2], shots[2];
    uint8_t ai_level[2];
    bool ai_hard[2];
    uint8_t ai_bonus;
    uint8_t hitstop;        /* frozen ticks after a big impact */
    uint8_t shake;          /* screen shake ticks */
    uint8_t help_t;         /* controls panels at the start */
    unsigned long total_ticks;
} match_t;

/* ------------------------------------------------------------------ */
/* Save data                                                           */
/* ------------------------------------------------------------------ */
#define SAVE_NAME "FBLGSAVE"
#define SAVE_MAGIC 0x4C46u
#define SAVE_VER 2

typedef struct {
    uint8_t active, team, round, diff;
    uint8_t slot[8];
    uint8_t qf[4][2];
    uint8_t sf[2][2];
    uint8_t fn[2];
    uint8_t w_qf[4];
    uint8_t w_sf[2];
} tour_t;

typedef struct {
    uint16_t magic;
    uint8_t ver;
    uint8_t diff;
    uint16_t played, won, lost, gf, ga;
    uint16_t cups, cups_played;
    uint8_t best_margin;
    uint8_t streak, best_streak;
    tour_t tour;
} save_t;

/* ------------------------------------------------------------------ */
/* Globals                                                             */
/* ------------------------------------------------------------------ */
static player_t pl[2];
static ball_t ball;
static particle_t parts[MAX_PARTS];
static uint8_t part_next;
static match_t M;
static save_t save;
static uint8_t keys[8], prev_keys[8];
static uint32_t rng_s = 0x2545F491UL;
static jmp_buf panic_env;

/* pre-rendered heads of the two legends in the match, both directions */
#define HEAD_SW 44
#define HEAD_SH 46
#define HEAD_CX 22
#define HEAD_CY 25
static uint8_t head_buf[2][2][2 + HEAD_SW * HEAD_SH];
#define HEAD_SPR(s, d) ((gfx_sprite_t *)(void *)head_buf[s][d])
static int8_t head_team[2] = { -1, -1 };
/* scratch head sprite (tinted copies) and a cached giant-size head */
static uint8_t head_tmp[2 + HEAD_SW * HEAD_SH];
static uint8_t giant_buf[2 + HEAD_SW * HEAD_SH];
static int8_t giant_key = -1;      /* team * 2 + direction, -1 = none */
/* pre-rendered ball, 8 spin frames */
#define BALL_SW 13
static uint8_t ballspr_buf[8][2 + BALL_SW * BALL_SW];
#define BALL_SPR(f) ((gfx_sprite_t *)(void *)ballspr_buf[f])
#define CROWD_SPR(i) ((gfx_sprite_t *)(void *)bgc.gen.crowd[i])
#define AD_SPR(i) ((gfx_sprite_t *)(void *)bgc.gen.ad[i])
#define NET_SPR ((gfx_sprite_t *)(void *)bgc.gen.net)

/* pre-computed background tables */
static int16_t roof_out[ROOF_ROWS], roof_in[ROOF_ROWS];   /* half-widths per row */
#define PITCH_ROWS (PITCH_Y1 - PITCH_Y0)
#define STRIPES 12
/* slanted lines, ellipses: pixels bucketed by row */
#define LINE_Y0 90
#define LINE_ROWS (PITCH_Y1 - LINE_Y0)
#define LINE_MAX 720
/* Background cache: the finished stadium in 16-pixel chunks. Each entry
 * of bgc_idx is a chunk number in the dictionary, or BGC_SOLID | colour
 * for a chunk of one colour. Restoring a rectangle is then a handful of
 * memcpy / memset calls per row. The tiles and tables used to draw the
 * stadium once at start-up share the dictionary's memory (they are not
 * needed afterwards). */
#define BGC_CH 16
#define BGC_COLS (SCR_W / BGC_CH)
#define BGC_MAX 1100
#define BGC_SOLID 0xF000u
#define BGC_HASH 2048
static uint16_t bgc_idx[SCR_H][BGC_COLS];
static union {
    uint8_t dict[BGC_MAX][BGC_CH];
    struct {
        uint8_t crowd[CROWD_TIERS][2 + CROWD_W * CROWD_H];
        uint8_t ad[5][2 + AD_W * AD_H];
        uint8_t net[2 + NET_W * NET_H];
        int16_t stripe_x[PITCH_ROWS][STRIPES + 1];   /* perspective stripes */
        int16_t line_px[LINE_MAX];                   /* line pixels by row */
        uint8_t line_col[LINE_MAX];
        uint16_t line_row[LINE_ROWS + 1];
        int16_t tmp_x[LINE_MAX];
        uint8_t tmp_y[LINE_MAX], tmp_c[LINE_MAX];
    } gen;
} bgc;
static uint16_t bgc_n;
static bool bgc_ready;
#define stripe_x (bgc.gen.stripe_x)
#define line_px (bgc.gen.line_px)
#define line_col (bgc.gen.line_col)
#define line_row (bgc.gen.line_row)
#define tmp_x (bgc.gen.tmp_x)
#define tmp_y (bgc.gen.tmp_y)
#define tmp_c (bgc.gen.tmp_c)

/* ability icon ring */
#define RING_N 64
static int8_t ring_x[RING_N], ring_y[RING_N];
/* colour remaps for frozen / clone tints */
static uint8_t map_ice[256], map_ghost[256];

/* dirty rectangles, one list per buffer */
#define MAX_DIRTY 96
typedef struct { int16_t x0, y0, x1, y1; } rect_t;
static rect_t dirty[2][MAX_DIRTY];
static uint8_t ndirty[2];
static bool dirty_full[2];
static uint8_t cur_buf;
static int shake_x, shake_y;     /* current background offset */
static uint8_t full_pending;     /* full redraws still needed after a shake */

/* HUD widgets, redrawn only when needed */
enum { W_CLOCK, W_SCORE, W_ICON0, W_ICON1, W_HELP0, W_HELP1, W_OVL, W_COUNT };
static rect_t wrect[W_COUNT];
static uint16_t wver[W_COUNT];
static uint16_t wdrawn[2][W_COUNT];

/* timing */
static clock_t next_tick;
static uint8_t tick_frac;

/* AI */
#define PRED_N 24
static int pred_x[PRED_N + 1], pred_y[PRED_N + 1];
static int jump_h[PRED_N + 1];
typedef struct { int target; uint8_t timer; int8_t err; } ai_t;
static ai_t ai[2];

/* Panic exit: the top row Y= WINDOW ZOOM TRACE GRAPH pressed in that order.
 * Keys are coded (group << 3) | bit. None of them is used anywhere else. */
#define PANIC_LEN 5
#define KEYCODE(g, b) ((uint8_t)(((g) << 3) | (b)))
static const uint8_t PANIC_SEQ[PANIC_LEN] = {
    KEYCODE(1, 4), KEYCODE(1, 3), KEYCODE(1, 2), KEYCODE(1, 1), KEYCODE(1, 0)
};
static uint8_t panic_ring[PANIC_LEN];
static uint8_t panic_pos;

/* ------------------------------------------------------------------ */
/* Small helpers                                                       */
/* ------------------------------------------------------------------ */
static const int8_t SIN32[32] = {
    0, 12, 24, 36, 45, 53, 59, 63, 64, 63, 59, 53, 45, 36, 24, 12,
    0, -12, -24, -36, -45, -53, -59, -63, -64, -63, -59, -53, -45, -36, -24, -12
};
#define COS32(a) SIN32[((a) + 8) & 31]

static uint16_t rnd(void)
{
    rng_s ^= rng_s << 13;
    rng_s ^= rng_s >> 17;
    rng_s ^= rng_s << 5;
    return (uint16_t)(rng_s >> 8);
}

static int rnd_n(int n)
{
    return (int)(rnd() % (unsigned)n);
}

static unsigned long isqrt32(unsigned long v)
{
    unsigned long r = 0, bit = 1UL << 30;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int iabs(int v)
{
    return v < 0 ? -v : v;
}

static char *u2s(char *p, unsigned v)
{
    char tmp[8];
    uint8_t n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < 7);
    while (n) *p++ = tmp[--n];
    *p = 0;
    return p;
}

static char *scat(char *p, const char *s)
{
    while (*s) *p++ = *s++;
    *p = 0;
    return p;
}

/* ground height (px) under x (px): platforms, ramps, pitch */
static int ground_px(int x)
{
    if (x > SCR_W / 2) x = SCR_W - x;
    if (x <= PLAT_X) return G_PLAT;
    if (x >= RAMP_X) return G_MAIN;
    return G_PLAT + (x - PLAT_X) * (G_MAIN - G_PLAT) / (RAMP_X - PLAT_X);
}

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */
/* Every new key press goes into a small ring buffer; if the last five
 * presses are the panic sequence the game quits at once (see main). */
static void panic_check(void)
{
    uint8_t g, b, k;
    for (g = 1; g < 8; g++) {
        uint8_t fresh = (uint8_t)(keys[g] & ~prev_keys[g]);
        if (!fresh) continue;
        for (b = 0; b < 8; b++) {
            if (!(fresh & (1 << b))) continue;
            panic_ring[panic_pos] = KEYCODE(g, b);
            panic_pos = (uint8_t)((panic_pos + 1) % PANIC_LEN);
            for (k = 0; k < PANIC_LEN; k++) {
                if (panic_ring[(panic_pos + k) % PANIC_LEN] != PANIC_SEQ[k]) break;
            }
            if (k == PANIC_LEN) longjmp(panic_env, 1);
        }
    }
}

static void read_keys(void)
{
    uint8_t i;
    memcpy(prev_keys, keys, sizeof keys);
    kb_Scan();
    for (i = 1; i < 8; i++) keys[i] = kb_Data[i];
    keys[0] = 0;
    panic_check();
}

#define KDOWN(g, m) ((keys[g] & (m)) != 0)
#define KPRESS(g, m) ((keys[g] & (m)) && !(prev_keys[g] & (m)))

static bool key_ok(void)
{
    return KPRESS(1, kb_2nd) || KPRESS(6, kb_Enter);
}

static bool key_back(void)
{
    return KPRESS(6, kb_Clear);
}

/* Player 1: arrows, 2nd shoot, MODE supershot.
 * Player 2: 1 / 3 run, 5 jump, 2 slide, + shoot, - supershot.
 * *dir remembers the direction last held alone (-1, 0, 1). Jump wins
 * over slide. */
static void decode_ctrl(const uint8_t *k, const uint8_t *pk, uint8_t who, ctrl_t *c, int8_t *dir)
{
#define KD(g, m) ((k[g] & (m)) != 0)
#define KP(g, m) ((k[g] & (m)) && !(pk[g] & (m)))
    if (who == 0) {
        c->left = KD(7, kb_Left);
        c->right = KD(7, kb_Right);
        c->jump = KD(7, kb_Up);
        c->slide = KP(7, kb_Down);
        c->shoot = KP(1, kb_2nd);
        c->super = KP(1, kb_Mode);
    } else {
        c->left = KD(3, kb_1);
        c->right = KD(5, kb_3);
        c->jump = KD(4, kb_5);
        c->slide = KP(4, kb_2);
        c->shoot = KP(6, kb_Add);
        c->super = KP(6, kb_Sub);
    }
#undef KD
#undef KP
    /* Both directions read at once: keep the one that was already held.
       This also hides the phantom opposite arrow that the keypad matrix
       produces when P2 holds 5 and 2 together (see README). */
    if (c->left && c->right) {
        c->left = *dir < 0;
        c->right = *dir > 0;
    } else {
        *dir = c->left ? -1 : c->right ? 1 : 0;
    }
    if (c->jump) c->slide = false;
}

static int8_t held_dir[2];

static void human_ctrl(uint8_t who, ctrl_t *c)
{
    decode_ctrl(keys, prev_keys, who, c, &held_dir[who]);
}

#ifdef FBL_HOST_TEST
/* lets the harness check the real decoder against simulated ghosting */
void fbl_test_decode(const uint8_t *k, const uint8_t *pk, uint8_t who, int8_t dir, uint8_t *out6)
{
    ctrl_t c;
    decode_ctrl(k, pk, who, &c, &dir);
    out6[0] = c.left; out6[1] = c.right; out6[2] = c.jump;
    out6[3] = c.slide; out6[4] = c.shoot; out6[5] = c.super;
}
#endif

/* ------------------------------------------------------------------ */
/* Timing: fixed 30 Hz ticks from the 32768 Hz clock (1092 r 8)        */
/* ------------------------------------------------------------------ */
static void tick_reset(void)
{
    next_tick = clock();
    tick_frac = 0;
}

static void tick_advance(void)
{
    next_tick += TICK_CLK;
    tick_frac += 8;
    if (tick_frac >= 30) {
        tick_frac -= 30;
        next_tick++;
    }
}

static bool tick_due(void)
{
    return (long)(clock() - next_tick) >= 0;
}

static void tick_wait(void)
{
    while (!tick_due()) {
    }
    tick_advance();
    if (tick_due()) tick_reset();
}

/* ------------------------------------------------------------------ */
/* Palette                                                             */
/* ------------------------------------------------------------------ */
#define RGB(r, g, b) gfx_RGBTo1555(r, g, b)

static uint16_t mix(uint8_t r0, uint8_t g0, uint8_t b0, uint8_t r1, uint8_t g1, uint8_t b1, int t, int n)
{
    int r = r0 + (r1 - r0) * t / n, g = g0 + (g1 - g0) * t / n, b = b0 + (b1 - b0) * t / n;
    return RGB(r, g, b);
}

static void ramp(uint8_t base, uint8_t n, uint8_t r0, uint8_t g0, uint8_t b0, uint8_t r1, uint8_t g1, uint8_t b1)
{
    uint8_t i;
    for (i = 0; i < n; i++) pal[base + i] = mix(r0, g0, b0, r1, g1, b1, i, n > 1 ? n - 1 : 1);
}

static void set2(uint8_t base, uint8_t r, uint8_t g, uint8_t b)
{
    pal[base] = RGB(r, g, b);
    pal[base + 1] = RGB(r * 3 / 4, g * 3 / 4, b * 3 / 4);
}

static const uint8_t SKIN_RGB[4][3] = {
    { 250, 206, 166 }, { 222, 164, 114 }, { 168, 108, 66 }, { 104, 64, 38 }
};
static const uint8_t HAIR_RGB[7][3] = {
    { 34, 26, 24 }, { 108, 66, 34 }, { 240, 206, 96 }, { 214, 104, 40 },
    { 70, 44, 28 }, { 150, 150, 150 }, { 170, 40, 30 }
};

static uint8_t lum(uint16_t c)
{
    return (uint8_t)((((c >> 10) & 31) * 3 + ((c >> 5) & 31) * 5 + (c & 31) * 2) / 10);
}

static void build_palette(void)
{
    int i;
    for (i = 0; i < 256; i++) pal[i] = RGB(255, 0, 255);
    pal[C_BLACK] = RGB(0, 0, 0);
    pal[C_WHITE] = RGB(255, 255, 255);
    pal[C_TRANSP] = RGB(255, 0, 254);
    pal[C_DKGRAY] = RGB(56, 60, 72);
    pal[C_GRAY] = RGB(128, 132, 140);
    pal[C_LTGRAY] = RGB(196, 200, 208);
    pal[C_RED] = RGB(236, 44, 44);
    pal[C_ORANGE] = RGB(255, 142, 24);
    pal[C_YELLOW] = RGB(255, 236, 70);
    pal[C_GOLD] = RGB(255, 202, 40);
    pal[C_CYAN] = RGB(70, 220, 255);
    pal[C_PURPLE] = RGB(176, 80, 236);
    pal[C_PINK] = RGB(255, 120, 200);
    pal[C_GREEN] = RGB(60, 200, 70);
    pal[C_BLUE] = RGB(40, 90, 220);
    pal[C_NAVY] = RGB(18, 30, 80);

    ramp(C_SKY, 6, 96, 176, 238, 196, 232, 255);
    pal[C_CLOUD] = RGB(255, 255, 255);
    pal[C_CLOUD + 1] = RGB(206, 226, 246);
    pal[C_ROOF] = RGB(236, 242, 250);              /* rim highlight */
    ramp(C_ROOF + 1, 5, 150, 170, 200, 40, 56, 96);
    ramp(C_WALL, 3, 34, 52, 104, 22, 34, 74);
    pal[C_SEAT] = RGB(64, 128, 214);                /* seat back */
    pal[C_SEAT + 1] = RGB(44, 100, 186);            /* seat */
    pal[C_SEAT + 2] = RGB(28, 66, 140);             /* gap / step */
    pal[C_SEAT + 3] = RGB(200, 224, 250);           /* rail */
    pal[C_SEAT + 4] = RGB(96, 152, 222);            /* aisle */
    pal[C_SEAT + 5] = RGB(70, 120, 196);            /* aisle step */
    ramp(C_TUNNEL, 3, 56, 150, 220, 10, 24, 60);
    pal[C_AD] = RGB(22, 66, 150);
    pal[C_AD + 1] = RGB(40, 104, 196);
    pal[C_AD + 2] = RGB(250, 250, 255);
    pal[C_AD + 3] = RGB(255, 210, 60);
    pal[C_TRACK] = RGB(178, 82, 64);
    pal[C_TRACK + 1] = RGB(226, 150, 128);
    pal[C_TRACK + 2] = RGB(136, 58, 46);
    /* grass: two stripe shades, each darker far away */
    ramp(C_GRASS, 3, 150, 206, 52, 178, 232, 68);
    ramp(C_GRASS + 3, 3, 136, 194, 44, 162, 220, 58);
    pal[C_GRASS + 6] = RGB(246, 252, 240);          /* lines */
    pal[C_GRASS + 7] = RGB(96, 150, 36);            /* shadows */
    pal[C_LIP] = RGB(182, 236, 74);
    pal[C_LIP + 1] = RGB(110, 170, 40);
    pal[C_LIP + 2] = RGB(80, 136, 30);
    pal[C_PLAT] = RGB(176, 234, 74);
    pal[C_PLAT + 1] = RGB(124, 196, 50);
    pal[C_PLAT + 2] = RGB(92, 160, 40);
    pal[C_PLAT + 3] = RGB(220, 252, 130);
    pal[C_SOIL] = RGB(150, 92, 54);
    pal[C_SOIL + 1] = RGB(118, 70, 40);
    pal[C_SOIL + 2] = RGB(176, 118, 74);
    pal[C_SOIL + 3] = RGB(96, 56, 32);
    pal[C_SOIL + 4] = RGB(196, 140, 92);
    pal[C_SOIL + 5] = RGB(134, 80, 46);
    pal[C_NET] = RGB(236, 244, 250);
    pal[C_NET + 1] = RGB(176, 196, 214);
    pal[C_NET + 2] = RGB(255, 255, 255);
    pal[C_NET + 3] = RGB(204, 160, 110);
    for (i = 0; i < 4; i++) {
        uint8_t r = SKIN_RGB[i][0], g = SKIN_RGB[i][1], b = SKIN_RGB[i][2];
        pal[C_SKIN + i * 3] = RGB(r + (255 - r) / 4, g + (255 - g) / 4, b + (255 - b) / 4);
        pal[C_SKIN + i * 3 + 1] = RGB(r, g, b);
        pal[C_SKIN + i * 3 + 2] = RGB(r * 4 / 5, g * 7 / 10, b * 7 / 10);
    }
    for (i = 0; i < 7; i++) set2((uint8_t)(C_HAIR + i * 2), HAIR_RGB[i][0], HAIR_RGB[i][1], HAIR_RGB[i][2]);
    set2(K_YEL, 255, 220, 20);
    set2(K_GRN, 20, 160, 70);
    set2(K_BLUE, 34, 76, 210);
    set2(K_SKY, 130, 196, 244);
    set2(K_NAVY, 26, 38, 104);
    set2(K_RED, 220, 30, 40);
    set2(K_MAR, 140, 24, 46);
    set2(K_AZU, 26, 116, 216);
    set2(K_ORA, 255, 124, 10);
    set2(K_DGRN, 0, 100, 50);
    pal[K_WHITE] = RGB(248, 248, 250);
    pal[K_WHITE + 1] = RGB(190, 196, 210);
    pal[K_BLACK] = RGB(34, 34, 40);
    pal[K_BLACK + 1] = RGB(16, 16, 20);
    set2(K_GOLD, 250, 200, 40);
    set2(K_PURP, 150, 70, 220);
    set2(K_LIME, 150, 240, 40);
    set2(K_PINK, 255, 110, 190);
    set2(K_GRAY, 150, 150, 160);
    set2(K_BROWN, 130, 80, 40);
    pal[C_FIRE] = RGB(255, 240, 120);
    pal[C_FIRE + 1] = RGB(255, 180, 40);
    pal[C_FIRE + 2] = RGB(250, 100, 20);
    pal[C_FIRE + 3] = RGB(210, 40, 20);
    ramp(C_ICE, 4, 236, 250, 255, 90, 170, 230);
    ramp(C_GHOST, 3, 236, 230, 255, 150, 140, 220);
    pal[C_PANEL] = RGB(16, 34, 78);
    pal[C_PANEL2] = RGB(30, 60, 124);
    pal[C_BORDER] = RGB(70, 190, 250);
    pal[C_SEL] = RGB(60, 120, 220);
    pal[C_KEYCAP] = RGB(250, 250, 252);
    pal[C_KEYSH] = RGB(150, 160, 180);
    pal[C_OUTLINE] = RGB(40, 24, 20);
    pal[C_MOUTH] = RGB(150, 40, 40);
    pal[C_GLOW] = RGB(255, 200, 40);
    pal[C_MAGNET] = RGB(200, 90, 255);
    pal[C_WALLFX] = RGB(120, 220, 255);
    /* crowd arms (animated in anim_palette) and flags */
    for (i = 0; i < 16; i++) pal[C_ARM + i] = pal[C_SEAT];
    pal[C_FLAG] = pal[K_RED];
    pal[C_FLAG + 1] = pal[K_YEL];
    pal[C_FLAG + 2] = pal[K_WHITE];

    /* tint maps: frozen = ice ramp by brightness, clone = ghost ramp */
    for (i = 0; i < 256; i++) {
        uint8_t l = lum(pal[i]);
        map_ice[i] = (uint8_t)(C_ICE + (l > 24 ? 0 : l > 17 ? 1 : l > 10 ? 2 : 3));
        map_ghost[i] = (uint8_t)(C_GHOST + (l > 22 ? 0 : l > 12 ? 1 : 2));
    }
    map_ice[C_TRANSP] = map_ghost[C_TRANSP] = C_TRANSP;
}

/* crowd arm palette entry: skin tone s (0..3), group g (0..1), up (0/1) */
#define ARM_IDX(s, g, up) ((uint8_t)(C_ARM + (((s) * 2 + (g)) * 2 + (up))))

static void set_pal_entry(uint8_t i, uint16_t c)
{
    gfx_palette[i] = c;
}

/* Animated colours: crowd arms waving, flags, fire, ready glow, menu
 * highlight. Called once per tick; writes only a handful of entries. */
static void anim_palette(unsigned long t, bool cheering)
{
    uint8_t s, g;
    uint8_t period = cheering ? 3 : 14;
    for (s = 0; s < 4; s++) {
        for (g = 0; g < 2; g++) {
            bool up = ((t / period) + g) & 1;
            set_pal_entry(ARM_IDX(s, g, 1), up ? pal[C_SKIN + s * 3 + 1] : pal[C_SEAT]);
            set_pal_entry(ARM_IDX(s, g, 0), up ? pal[C_SEAT] : pal[C_SKIN + s * 3 + 1]);
        }
    }
    {
        static const uint8_t fc[4] = { K_RED, K_YEL, K_WHITE, K_BLUE };
        uint8_t k = (uint8_t)((t / 6) & 3);
        set_pal_entry(C_FLAG, pal[fc[k]]);
        set_pal_entry(C_FLAG + 1, pal[fc[(k + 1) & 3]]);
        set_pal_entry(C_FLAG + 2, pal[fc[(k + 2) & 3]]);
    }
    {
        /* fire flickers: rotate the four flame colours */
        uint8_t k = (uint8_t)(t & 3), j;
        for (j = 0; j < 4; j++) set_pal_entry((uint8_t)(C_FIRE + j), pal[C_FIRE + ((j + k) & 3)]);
    }
    set_pal_entry(C_GLOW, (t & 4) ? RGB(255, 250, 200) : RGB(255, 160, 20));
    set_pal_entry(C_MAGNET, (t & 2) ? RGB(220, 120, 255) : RGB(140, 60, 230));
    set_pal_entry(C_WALLFX, (t & 4) ? RGB(160, 240, 255) : RGB(80, 180, 250));
    {
        int k = (int)(t & 31);
        if (k > 15) k = 31 - k;
        set_pal_entry(C_SEL, mix(50, 100, 210, 90, 160, 255, k, 15));
    }
}

/* ------------------------------------------------------------------ */
/* Text                                                                */
/* ------------------------------------------------------------------ */
static int txt_w(const char *s, uint8_t sc)
{
    return (int)strlen(s) * 8 * sc;
}

static void txt(const char *s, int x, int y, uint8_t c)
{
    gfx_SetTextFGColor(c);
    gfx_PrintStringXY(s, x, y);
}

static void txt_sh(const char *s, int x, int y, uint8_t c, uint8_t sc)
{
    gfx_SetTextScale(sc, sc);
    gfx_SetTextFGColor(C_BLACK);
    gfx_PrintStringXY(s, x + sc, y + sc);
    gfx_SetTextFGColor(c);
    gfx_PrintStringXY(s, x, y);
    gfx_SetTextScale(1, 1);
}

/* big outlined text, like the clock in the browser game */
static void txt_outline(const char *s, int x, int y, uint8_t c, uint8_t sc)
{
    /* drop shadow: two prints instead of a five-print outline (much faster) */
    gfx_SetTextScale(sc, sc);
    gfx_SetTextFGColor(C_PANEL);
    gfx_PrintStringXY(s, x + sc, y + sc);
    gfx_SetTextFGColor(c);
    gfx_PrintStringXY(s, x, y);
    gfx_SetTextScale(1, 1);
}

static void txt_c(const char *s, int y, uint8_t c, uint8_t sc)
{
    txt_sh(s, (SCR_W - txt_w(s, sc)) / 2, y, c, sc);
}

static void fill(int x, int y, int w, int h, uint8_t c)
{
    gfx_SetColor(c);
    gfx_FillRectangle(x, y, w, h);
}

/* bold outlined segment digits (much cheaper than scaled text) */
static const uint8_t SEG[10] = { 0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F };

static void seg_rects(int x, int y, int w, int h, int t, uint8_t m, int grow, uint8_t c)
{
    int hh = h / 2;
    gfx_SetColor(c);
    if (m & 0x01) gfx_FillRectangle(x - grow, y - grow, w + 2 * grow, t + 2 * grow);
    if (m & 0x02) gfx_FillRectangle(x + w - t - grow, y - grow, t + 2 * grow, hh + t / 2 + 2 * grow);
    if (m & 0x04) gfx_FillRectangle(x + w - t - grow, y + hh - t / 2 - grow, t + 2 * grow, h - hh + t / 2 + 2 * grow);
    if (m & 0x08) gfx_FillRectangle(x - grow, y + h - t - grow, w + 2 * grow, t + 2 * grow);
    if (m & 0x10) gfx_FillRectangle(x - grow, y + hh - t / 2 - grow, t + 2 * grow, h - hh + t / 2 + 2 * grow);
    if (m & 0x20) gfx_FillRectangle(x - grow, y - grow, t + 2 * grow, hh + t / 2 + 2 * grow);
    if (m & 0x40) gfx_FillRectangle(x - grow, y + hh - t / 2 - grow, w + 2 * grow, t + 2 * grow);
}

/* draws the decimal number n; returns its width */
static int seg_number(int x, int y, unsigned n, int w, int h, int t, uint8_t c, uint8_t outline, bool draw)
{
    char buf[6];
    int k, len, gap = t, total;
    u2s(buf, n);
    len = (int)strlen(buf);
    total = len * w + (len - 1) * gap;
    if (!draw) return total;
    for (k = 0; k < len; k++) seg_rects(x + k * (w + gap), y, w, h, t, SEG[buf[k] - '0'], 2, outline);
    for (k = 0; k < len; k++) seg_rects(x + k * (w + gap), y, w, h, t, SEG[buf[k] - '0'], 0, c);
    return total;
}

/* rounded panel like the browser game's HUD boxes */
static void rpanel(int x, int y, int w, int h, uint8_t bg, uint8_t border)
{
    fill(x + 2, y, w - 4, h, border);
    fill(x, y + 2, w, h - 4, border);
    fill(x + 1, y + 1, w - 2, h - 2, border);
    fill(x + 3, y + 2, w - 6, h - 4, bg);
    fill(x + 2, y + 3, w - 4, h - 6, bg);
}

static void panel(int x, int y, int w, int h)
{
    rpanel(x, y, w, h, C_PANEL, C_BORDER);
}

/* ------------------------------------------------------------------ */
/* Background                                                          */
/* All coordinates here are "world" pixels; the current shake offset   */
/* is added when drawing. rx0..ry1 is the screen region being redrawn. */
/* ------------------------------------------------------------------ */
static int rx0, ry0, rx1, ry1;

static void rfill(int x0, int y0, int x1, int y1, uint8_t c)
{
    x0 += shake_x; x1 += shake_x; y0 += shake_y; y1 += shake_y;
    if (x0 < rx0) x0 = rx0;
    if (y0 < ry0) y0 = ry0;
    if (x1 > rx1) x1 = rx1;
    if (y1 > ry1) y1 = ry1;
    if (x0 >= x1 || y0 >= y1) return;
    gfx_SetColor(c);
    gfx_FillRectangle_NoClip((uint24_t)x0, (uint8_t)y0, (uint24_t)(x1 - x0), (uint8_t)(y1 - y0));
}

static bool rhit(int x0, int y0, int x1, int y1)
{
    x0 += shake_x; x1 += shake_x; y0 += shake_y; y1 += shake_y;
    return x0 < rx1 && x1 > rx0 && y0 < ry1 && y1 > ry0;
}

static void rsprite(const gfx_sprite_t *s, int x, int y)
{
    if (rhit(x, y, x + s->width, y + s->height)) gfx_Sprite(s, x + shake_x, y + shake_y);
}

static void rtsprite(const gfx_sprite_t *s, int x, int y)
{
    if (rhit(x, y, x + s->width, y + s->height)) gfx_TransparentSprite(s, x + shake_x, y + shake_y);
}

static void rcircle(int x, int y, int r, uint8_t c)
{
    if (!rhit(x - r, y - r, x + r + 1, y + r + 1)) return;
    gfx_SetColor(c);
    gfx_FillCircle(x + shake_x, y + shake_y, (uint24_t)r);
}

/* world row range of the region, clamped to [a, b) */
static void rrows(int a, int b, int *y0, int *y1)
{
    *y0 = ry0 - shake_y;
    *y1 = ry1 - shake_y;
    if (*y0 < a) *y0 = a;
    if (*y1 > b) *y1 = b;
}

static void roof_draw(int from, int to);

/* --- tables built at start-up --- */
static int roof_outer_y(int dx) { return (int)((long)dx * dx / 900); }
static int roof_inner_y(int dx) { return 18 + (int)((long)dx * dx / 560); }

#define RIBS 8
static const int16_t RIB_X[RIBS] = { 24, 64, 104, 140, 180, 216, 256, 296 };
typedef struct { int16_t x, y, r; } circ_t;
static const circ_t CLOUDS[] = {
    { 12, 6, 6 }, { 22, 3, 6 }, { 31, 8, 5 }, { 5, 12, 4 }, { 42, 4, 4 },
    { 298, 6, 6 }, { 308, 10, 5 }, { 287, 4, 5 }, { 316, 2, 4 }
};
static const circ_t DIRT[] = {
    { 20, 214, 4 }, { 58, 226, 5 }, { 96, 210, 3 }, { 130, 232, 4 }, { 170, 218, 5 },
    { 206, 230, 3 }, { 240, 212, 4 }, { 276, 228, 5 }, { 310, 216, 3 }, { 40, 236, 3 },
    { 150, 206, 2 }, { 262, 238, 3 }, { 110, 222, 2 }, { 190, 236, 2 }, { 76, 238, 2 }
};
#define NCLOUDS ((int)(sizeof CLOUDS / sizeof CLOUDS[0]))
#define NDIRT ((int)(sizeof DIRT / sizeof DIRT[0]))

typedef struct { int16_t x0, x1, y; } hspan_t;
#define HSPANS 12
static hspan_t hspan[HSPANS];

/* pitch perspective: scale (1/256) of world x around the centre per row */
static int pscale(int row) { return 210 + 46 * row / (PITCH_ROWS - 1); }
static int pitch_x(int wx, int y) { return 160 + (int)((long)(wx - 160) * pscale(y - PITCH_Y0) / 256); }

static int ntmp;

static void lp_add(int x, int y, uint8_t c)
{
    if (ntmp < LINE_MAX && y >= LINE_Y0 && y < PITCH_Y1) {
        tmp_x[ntmp] = (int16_t)x;
        tmp_y[ntmp] = (uint8_t)(y - LINE_Y0);
        tmp_c[ntmp] = c;
        ntmp++;
    }
}

static void lp_vline_world(int wx, int ya, int yb)   /* perspective "depth" line */
{
    int y;
    for (y = ya; y <= yb; y++) {
        lp_add(pitch_x(wx, y), y, C_GRASS + 6);
        lp_add(pitch_x(SCR_W - wx, y), y, C_GRASS + 6);
    }
}

static void gen_tables(void)
{
    int y, r, k, i, d;
    for (y = 0; y < ROOF_ROWS; y++) {
        roof_out[y] = (int16_t)(y >= 28 ? 200 : (int)isqrt32(900UL * (unsigned long)y));
        roof_in[y] = (int16_t)(y < 18 ? -1 : (int)isqrt32(560UL * (unsigned long)(y - 18)));
    }
    for (r = 0; r < PITCH_ROWS; r++) {
        for (k = 0; k <= STRIPES; k++) {
            int wx = -80 + 40 * k;
            stripe_x[r][k] = (int16_t)clampi(160 + (int)((long)(wx - 160) * pscale(r) / 256), -1, SCR_W + 1);
        }
    }
    /* straight pitch lines (rows), mirrored for both halves */
    i = 0;
    hspan[i].y = 132; hspan[i].x0 = (int16_t)pitch_x(30, 132); hspan[i].x1 = (int16_t)pitch_x(96, 132); i++;
    hspan[i].y = 186; hspan[i].x0 = (int16_t)pitch_x(30, 186); hspan[i].x1 = (int16_t)pitch_x(96, 186); i++;
    hspan[i].y = 146; hspan[i].x0 = (int16_t)pitch_x(30, 146); hspan[i].x1 = (int16_t)pitch_x(62, 146); i++;
    hspan[i].y = 172; hspan[i].x0 = (int16_t)pitch_x(30, 172); hspan[i].x1 = (int16_t)pitch_x(62, 172); i++;
    for (k = 0; k < 4; k++, i++) {
        hspan[i].y = hspan[k].y;
        hspan[i].x0 = (int16_t)(SCR_W - hspan[k].x1);
        hspan[i].x1 = (int16_t)(SCR_W - hspan[k].x0);
    }
    hspan[i].y = 124; hspan[i].x0 = 0; hspan[i].x1 = SCR_W; i++;
    hspan[i].y = 192; hspan[i].x0 = 0; hspan[i].x1 = SCR_W; i++;
    while (i < HSPANS) { hspan[i].y = 0; hspan[i].x0 = hspan[i].x1 = 0; i++; }

    /* pixel lines: centre ellipse, box edges, goal side bars, spots */
    ntmp = 0;
    for (d = -38; d <= 38; d++) {
        int dy = (int)isqrt32((unsigned long)(1444 - d * d) * 196UL / 1444UL);
        lp_add(160 + d, 158 + dy, C_GRASS + 6);
        lp_add(160 + d, 158 - dy, C_GRASS + 6);
    }
    for (d = -14; d <= 14; d++) {
        int dx = (int)isqrt32((unsigned long)(196 - d * d) * 1444UL / 196UL);
        lp_add(160 + dx, 158 + d, C_GRASS + 6);
        lp_add(160 - dx, 158 + d, C_GRASS + 6);
    }
    lp_vline_world(96, 132, 186);
    lp_vline_world(62, 146, 172);
    for (y = 157; y <= 159; y++) {
        lp_add(pitch_x(80, 158) + (y - 158), 158, C_GRASS + 6);
        lp_add(SCR_W - pitch_x(80, 158) - (y - 158), 158, C_GRASS + 6);
        lp_add(159 + (y - 157), 158, C_GRASS + 6);
    }
    for (y = 96; y <= 104; y++) {           /* goal roof bars, back to front */
        int x = 30 + (y - 96) * 6 / 8;
        lp_add(x, y, C_NET + 2);
        lp_add(x + 1, y, C_NET + 1);
        lp_add(SCR_W - 1 - x, y, C_NET + 2);
        lp_add(SCR_W - 2 - x, y, C_NET + 1);
    }
    /* bucket by row */
    memset(line_row, 0, sizeof line_row);
    for (i = 0; i < ntmp; i++) line_row[tmp_y[i] + 1]++;
    for (r = 0; r < LINE_ROWS; r++) line_row[r + 1] = (uint16_t)(line_row[r + 1] + line_row[r]);
    {
        static uint16_t fillp[LINE_ROWS];
        for (r = 0; r < LINE_ROWS; r++) fillp[r] = line_row[r];
        for (i = 0; i < ntmp; i++) {
            uint16_t p = fillp[tmp_y[i]]++;
            line_px[p] = tmp_x[i];
            line_col[p] = tmp_c[i];
        }
    }
    /* ability icon ring, clockwise from the top */
    for (i = 0; i < RING_N; i++) {
        int a = i / 2, b = ((i + 1) / 2) & 31;
        int s = (SIN32[a] + SIN32[b]) / 2, c = (COS32(a) + COS32(b)) / 2;
        ring_x[i] = (int8_t)(s * 12 / 64);
        ring_y[i] = (int8_t)(-c * 12 / 64);
    }
    /* AI jump table */
    for (k = 0; k <= PRED_N; k++) {
        long h = (long)k * JUMP_V - (long)GRAV_P * k * (k + 1) / 2;
        jump_h[k] = (int)(h / FP);
    }
}

/* --- sprites generated at start-up (drawn into the back buffer, then captured) --- */
static const uint8_t FAN_SHIRTS[12] = {
    K_RED, K_BLUE, K_YEL, K_WHITE, K_ORA, K_SKY, K_GRN, K_PINK, K_AZU, K_GOLD, K_PURP, K_LIME
};

static void gen_crowd(uint8_t tier)
{
    const bool jump = false;
    uint32_t saved = rng_s;
    uint8_t band, k;
    gfx_sprite_t *spr = CROWD_SPR(tier);
    rng_s = 0x9E3779B9UL + tier * 7919UL;
    gfx_SetClipRegion(0, 0, CROWD_W, CROWD_H);
    for (band = 0; band < 2; band++) {
        int r0 = band * 12;
        fill(0, r0, CROWD_W, 12, C_SEAT + 2);
        fill(0, r0 + 2, CROWD_W, 6, C_SEAT);
        fill(0, r0 + 8, CROWD_W, 2, C_SEAT + 1);
        for (k = 0; k < 8; k++) fill(k * 8, r0 + 2, 1, 6, C_SEAT + 1);
    }
    fill(58, 0, 6, CROWD_H, C_SEAT + 4);
    for (k = 0; k < 6; k++) fill(58, k * 4 + 3, 6, 1, C_SEAT + 5);
    for (band = 0; band < 2; band++) {
        int r0 = band * 12 - (jump ? 2 : 0);
        for (k = 0; k < 7; k++) {
            int x = 4 + k * 8;
            uint8_t s = (uint8_t)rnd_n(4), g = (uint8_t)rnd_n(2);
            uint8_t shirt = FAN_SHIRTS[rnd_n(12)];
            uint8_t extra = (uint8_t)rnd_n(10);
            bool here = rnd_n(100) < 58, anim = rnd_n(100) < 60;
            if (!here) continue;
            fill(x - 3, r0 + 5, 6, 5, shirt);
            fill(x + 1, r0 + 5, 2, 5, (uint8_t)(shirt + 1));
            gfx_SetColor((uint8_t)(C_SKIN + s * 3 + 1));
            gfx_FillCircle(x, r0 + 3, 2);
            fill(x - 1, r0 + 1, 3, 1, (uint8_t)(C_HAIR + rnd_n(4) * 2));
            if (jump) {
                fill(x - 4, r0 + 1, 1, 4, (uint8_t)(C_SKIN + s * 3 + 1));
                fill(x + 3, r0 + 1, 1, 4, (uint8_t)(C_SKIN + s * 3 + 1));
            } else if (anim) {
                fill(x - 4, r0 + 2, 1, 3, ARM_IDX(s, g, 1));
                fill(x + 3, r0 + 2, 1, 3, ARM_IDX(s, g, 1));
                fill(x - 4, r0 + 6, 1, 2, ARM_IDX(s, g, 0));
                fill(x + 3, r0 + 6, 1, 2, ARM_IDX(s, g, 0));
            } else {
                fill(x - 4, r0 + 6, 1, 2, (uint8_t)(C_SKIN + s * 3 + 1));
                fill(x + 3, r0 + 6, 1, 2, (uint8_t)(C_SKIN + s * 3 + 1));
            }
            if (extra == 0) {                       /* flag */
                fill(x + 4, r0, 1, 6, C_DKGRAY);
                fill(x + 5, r0, 4, 1, C_FLAG);
                fill(x + 5, r0 + 1, 4, 1, C_FLAG + 1);
                fill(x + 5, r0 + 2, 4, 1, C_FLAG + 2);
            } else if (extra == 1) {                /* little sign */
                fill(x - 3, r0 + 1, 7, 3, C_WHITE);
                fill(x - 1, r0 + 2, 3, 1, C_RED);
            }
        }
    }
    spr->width = CROWD_W;
    spr->height = CROWD_H;
    gfx_GetSprite(spr, 0, 0);
    gfx_SetClipRegion(0, 0, SCR_W, SCR_H);
    rng_s = saved;
}

static void gen_ad(uint8_t i, const char *s, uint8_t fg)
{
    gfx_sprite_t *spr = AD_SPR(i);
    fill(0, 0, AD_W, AD_H, C_AD);
    fill(0, 0, AD_W, 1, C_AD + 1);
    fill(AD_W - 1, 0, 1, AD_H, C_PANEL);
    txt(s, (AD_W - txt_w(s, 1)) / 2, 1, fg);
    spr->width = AD_W;
    spr->height = AD_H;
    gfx_GetSprite(spr, 0, 0);
}

/* hexagonal goal-net tile: edges of the Voronoi cells of a hex lattice */
static void gen_net(void)
{
    static const int8_t CX[5] = { 0, 12, 6, 0, 12 }, CY[5] = { 0, 0, 4, 8, 8 };
    int x, y, k;
    bgc.gen.net[0] = NET_W;
    bgc.gen.net[1] = NET_H;
    for (y = 0; y < NET_H; y++) {
        for (x = 0; x < NET_W; x++) {
            int d1 = 9999, d2 = 9999;
            for (k = 0; k < 5; k++) {
                int dx = x - CX[k], dy = (y - CY[k]) * 3 / 2;
                int d = dx * dx + dy * dy;
                if (d < d1) { d2 = d1; d1 = d; } else if (d < d2) d2 = d;
            }
            bgc.gen.net[2 + y * NET_W + x] = (uint8_t)(d2 - d1 <= 9 ? ((x + y) & 3 ? C_NET : C_NET + 1) : C_TRANSP);
        }
    }
}

static void bgc_build(void);

static void gen_assets(void)
{
    uint8_t t;
    for (t = 0; t < CROWD_TIERS; t++) gen_crowd(t);
    gen_ad(0, "CE CUP", C_AD + 3);
    gen_ad(1, "GRAPHX", C_AD + 2);
    gen_ad(2, "LEGENDS", C_AD + 3);
    gen_ad(3, "TI-84 CE", C_AD + 2);
    gen_ad(4, "EZ80", C_AD + 3);
    gen_net();
    gen_tables();
    bgc_build();
}

/* --- stadium pieces --- */
static void draw_goal_bg(bool right)
{
    int i, j;
#define GX(x, w) (right ? SCR_W - (x) - (w) : (x))
    if (!rhit(GX(0, 40), 94, GX(0, 40) + 40, G_PLAT + 1)) return;
    for (j = 0; j < 9; j++)
        for (i = 0; i < 2; i++) rtsprite(NET_SPR, GX(6 + i * NET_W, NET_W), 98 + j * NET_H);
    rfill(GX(2, 2), 96, GX(2, 2) + 2, G_PLAT, C_NET + 1);          /* back post */
    rfill(GX(2, 28), 96, GX(2, 28) + 28, 98, C_NET + 1);           /* back bar */
    rfill(GX(0, BAR_LEN), BAR_TOP, GX(0, BAR_LEN) + BAR_LEN, BAR_BOT - 1, C_NET + 2);
    rfill(GX(0, BAR_LEN), BAR_BOT - 1, GX(0, BAR_LEN) + BAR_LEN, BAR_BOT, C_NET + 1);
    rfill(GX(30, 4), BAR_BOT, GX(30, 4) + 4, G_PLAT, C_NET + 2);   /* front post */
    rfill(GX(right ? 30 : 33, 1), BAR_BOT, GX(right ? 30 : 33, 1) + 1, G_PLAT, C_NET + 1);
#undef GX
}

static void draw_platform_bg(bool right)
{
    int x, run0 = PLAT_X, run_g = G_PLAT;
    int bx = right ? SCR_W - PLAT_X : 0;
    if (!rhit(right ? SCR_W - RAMP_X : 0, G_PLAT - 6, right ? SCR_W : RAMP_X, SOIL_Y)) return;
    rfill(bx, G_PLAT - 4, bx + PLAT_X, G_PLAT, C_PLAT);
    rfill(bx, G_PLAT - 4, bx + PLAT_X, G_PLAT - 3, C_PLAT + 3);
    rfill(right ? SCR_W - 34 : 4, G_PLAT - 2, right ? SCR_W - 4 : 34, G_PLAT, C_PLAT + 2);  /* goal shadow */
    rfill(bx, G_PLAT, bx + PLAT_X, SOIL_Y, C_PLAT + 1);
    rfill(bx, SOIL_Y - 4, bx + PLAT_X, SOIL_Y, C_PLAT + 2);
    /* ramp, in runs of columns with the same surface height */
    for (x = PLAT_X; x <= RAMP_X; x++) {
        int g = x < RAMP_X ? ground_px(x) : -1;
        if (g != run_g) {
            int xa = right ? SCR_W - x : run0, xb = right ? SCR_W - run0 : x;
            rfill(xa, run_g - 4, xb, run_g - 3, C_PLAT + 3);
            rfill(xa, run_g - 3, xb, run_g, C_PLAT);
            rfill(xa, run_g, xb, SOIL_Y - 4, C_PLAT + 1);
            rfill(xa, SOIL_Y - 4, xb, SOIL_Y, C_PLAT + 2);
            run0 = x;
            run_g = g;
        }
    }
}

/* sky, clouds, roof arch and back wall for world rows [from, to) */
static void roof_draw(int from, int to)
{
    int i, y, ya, yb;
    rrows(from, to, &ya, &yb);
    for (y = ya; y < yb; y++) {
        int o = roof_out[y];
        if (o < 160) {
            uint8_t sc = (uint8_t)(C_SKY + (y < 28 ? y / 5 : 5));
            rfill(0, y, 160 - o, y + 1, sc);
            rfill(160 + o, y, SCR_W, y + 1, sc);
        }
    }
    if (ya < 30 && ya < yb) {
        for (i = 0; i < NCLOUDS; i++) {
            rcircle(CLOUDS[i].x, CLOUDS[i].y + 1, CLOUDS[i].r, C_CLOUD + 1);
            rcircle(CLOUDS[i].x, CLOUDS[i].y, CLOUDS[i].r, C_CLOUD);
        }
    }
    for (y = ya; y < yb; y++) {
        int o = roof_out[y] > 160 ? 160 : roof_out[y], n = roof_in[y];
        uint8_t rc = (uint8_t)(C_ROOF + 2 + (y > 40 ? 2 : y > 20 ? 1 : 0));
        if (n < 0) {
            rfill(160 - o, y, 160 + o, y + 1, rc);
        } else {
            if (n < o) {
                rfill(160 - o, y, 160 - n, y + 1, rc);
                rfill(160 + n, y, 160 + o, y + 1, rc);
                rfill(160 - n - 1, y, 160 - n, y + 1, C_ROOF + 1);
                rfill(160 + n, y, 160 + n + 1, y + 1, C_ROOF + 1);
            }
            if (y < STAND_Y0) rfill(160 - n, y, 160 + n, y + 1, (uint8_t)(C_WALL + (y - 18) / 4));
        }
        if (o < 160) {
            rfill(160 - o, y, 160 - o + 2, y + 1, C_ROOF);
            rfill(160 + o - 2, y, 160 + o, y + 1, C_ROOF);
        } else if (y == 0) {
            rfill(0, 0, SCR_W, 1, C_ROOF);
        }
    }
    if (ya < yb) {
        for (i = 0; i < RIBS; i++) {
            int dx = iabs(RIB_X[i] - 160);
            int yt = roof_outer_y(dx) + 2, yb2 = roof_inner_y(dx);
            if (yb2 > to) yb2 = to;
            if (yt < from) yt = from;
            rfill(RIB_X[i], yt, RIB_X[i] + 1, yb2, C_ROOF + 1);
        }
    }
}

/* Redraw the static stadium inside screen rect (x0,y0)-(x1,y1), exclusive. */
static void bg_draw(int x0, int y0, int x1, int y1)
{
    int i, y, ya, yb;
    rx0 = x0; ry0 = y0; rx1 = x1; ry1 = y1;
    gfx_SetClipRegion(x0, y0, x1, y1);

    /* stands (behind the roof) */
    if (rhit(0, STAND_Y0, SCR_W, STAND_Y1)) {
        static const int8_t OFF[CROWD_TIERS] = { 0, 21, 42 };
        uint8_t t, k;
        for (t = 0; t < CROWD_TIERS; t++) {
            int ty = STAND_Y0 + t * CROWD_H;
            if (!rhit(0, ty, SCR_W, ty + CROWD_H)) continue;
            for (k = 0; k < 6; k++) rsprite(CROWD_SPR(t), -OFF[t] + k * CROWD_W, ty);
        }
        rfill(0, STAND_Y0 + CROWD_H - 1, SCR_W, STAND_Y0 + CROWD_H, C_SEAT + 3);
        rfill(0, STAND_Y0 + 2 * CROWD_H - 1, SCR_W, STAND_Y0 + 2 * CROWD_H, C_SEAT + 3);
        /* players' tunnel */
        rfill(136, 78, 184, 102, C_TUNNEL);
        rfill(136, 78, 184, 80, C_SEAT + 3);
        rfill(141, 84, 179, 102, C_TUNNEL + 1);
        rfill(146, 89, 174, 102, C_TUNNEL + 2);
        rfill(148, 87, 172, 89, C_TUNNEL + 2);
    }
    /* sky, clouds, roof arch, back wall */
    roof_draw(0, ROOF_ROWS);
    /* ad boards and running track */
    if (rhit(0, AD_Y, SCR_W, PITCH_Y0)) {
        for (i = 0; i < 5; i++) rsprite(AD_SPR(i), i * AD_W, AD_Y);
        rfill(0, TRACK_Y, SCR_W, PITCH_Y0, C_TRACK);
        rfill(0, TRACK_Y + 2, SCR_W, TRACK_Y + 3, C_TRACK + 1);
        rfill(0, TRACK_Y + 5, SCR_W, TRACK_Y + 6, C_TRACK + 1);
        rfill(0, PITCH_Y0 - 1, SCR_W, PITCH_Y0, C_TRACK + 2);
    }
    /* pitch: perspective stripes, merged into rectangles */
    rrows(PITCH_Y0, PITCH_Y1, &ya, &yb);
    if (ya < yb) {
        int k;
        for (k = 0; k < STRIPES; k++) {
            int sy = ya, sxa = 0, sxb = 0, sband = -1;
            for (y = ya; y <= yb; y++) {
                int xa = 0, xb = 0, band = -2;
                if (y < yb) {
                    int r = y - PITCH_Y0;
                    xa = stripe_x[r][k];
                    xb = stripe_x[r][k + 1];
                    band = r < 25 ? 0 : r < 50 ? 1 : 2;
                }
                if (y == yb || xa != sxa || xb != sxb || band != sband) {
                    if (sband >= 0 && y > sy)
                        rfill(sxa, sy, sxb, y, (uint8_t)(((k & 1) ? C_GRASS + 3 : C_GRASS) + sband));
                    sy = y; sxa = xa; sxb = xb; sband = band;
                }
            }
        }
        for (i = 0; i < HSPANS; i++)
            if (hspan[i].x1 > hspan[i].x0) rfill(hspan[i].x0, hspan[i].y, hspan[i].x1, hspan[i].y + 1, C_GRASS + 6);
        rfill(160, 124, 161, 193, C_GRASS + 6);
    }
    /* pixel lines (ellipse, box edges, goal roof bars) */
    rrows(LINE_Y0, PITCH_Y1, &ya, &yb);
    for (y = ya; y < yb; y++) {
        uint16_t p, e = line_row[y - LINE_Y0 + 1];
        int sy = y + shake_y;
        for (p = line_row[y - LINE_Y0]; p < e; p++) {
            int sx = line_px[p] + shake_x;
            if (sx >= rx0 && sx < rx1) gfx_vbuffer[sy][sx] = line_col[p];
        }
    }
    /* grass lip and soil cut-away */
    if (rhit(0, PITCH_Y1, SCR_W, SCR_H)) {
        rfill(0, PITCH_Y1, SCR_W, PITCH_Y1 + 2, C_LIP);
        rfill(0, PITCH_Y1 + 2, SCR_W, SOIL_Y, C_LIP + 1);
        rfill(0, SOIL_Y, SCR_W, SCR_H, C_SOIL);
        rfill(0, SOIL_Y, SCR_W, SOIL_Y + 3, C_SOIL + 1);
        rfill(0, SOIL_Y + 3, SCR_W, SOIL_Y + 4, C_SOIL + 5);
        for (i = 0; i < NDIRT; i++) {
            rcircle(DIRT[i].x, DIRT[i].y, DIRT[i].r, C_SOIL + 2);
            rfill(DIRT[i].x - 1, DIRT[i].y - DIRT[i].r + 1, DIRT[i].x + 1, DIRT[i].y - DIRT[i].r + 2, C_SOIL + 4);
            rfill(DIRT[i].x + DIRT[i].r + 3, DIRT[i].y + 2, DIRT[i].x + DIRT[i].r + 5, DIRT[i].y + 3, C_SOIL + 3);
        }
    }
    draw_platform_bg(false);
    draw_platform_bg(true);
    draw_goal_bg(false);
    draw_goal_bg(true);
    /* uncovered borders while the picture is shaken */
    if (shake_x > 0) rfill(-shake_x, 0, 0, SCR_H, C_NAVY);
    if (shake_x < 0) rfill(SCR_W, 0, SCR_W - shake_x, SCR_H, C_NAVY);
    if (shake_y > 0) rfill(0, -shake_y, SCR_W, 0, C_NAVY);
    if (shake_y < 0) rfill(0, SCR_H, SCR_W, SCR_H - shake_y, C_NAVY);
}

/* Copy screen rect (x0,y0)-(x1,y1) of the stadium from the cache into the
 * back buffer. Neighbouring chunks that are stored next to each other (or
 * share a colour) go in one memcpy / memset. */
static void bgc_restore(int x0, int y0, int x1, int y1)
{
    uint8_t *row = &gfx_vbuffer[y0][0];
    int y;
    TEST_HOOK(test_rows(y1 - y0));
    for (y = y0; y < y1; y++, row += SCR_W) {
        const uint16_t *ix = bgc_idx[y];
        int x = x0, xe;
        uint8_t c = (uint8_t)(x0 / BGC_CH);
        while (x < x1) {
            uint16_t e = ix[c];
            if (e >= BGC_SOLID) {
                do { c++; xe = c * BGC_CH; } while (xe < x1 && ix[c] == e);
                if (xe > x1) xe = x1;
                memset(row + x, (uint8_t)e, (size_t)(xe - x));
            } else {
                const uint8_t *src = &bgc.dict[e][x % BGC_CH];
                do { c++; e++; xe = c * BGC_CH; } while (xe < x1 && ix[c] == e);
                if (xe > x1) xe = x1;
                memcpy(row + x, src, (size_t)(xe - x));
            }
            TEST_HOOK(test_memop(xe - x));
            x = xe;
        }
    }
}

/* Draw the stadium once (procedurally) and store it in the cache. */
static void bgc_build(void)
{
    uint16_t *ht = (uint16_t *)(void *)head_buf;   /* free until the first match */
    int y, c;
    shake_x = shake_y = 0;
    gfx_Wait();
    bg_draw(0, 0, SCR_W, SCR_H);
    gfx_SetClipRegion(0, 0, SCR_W, SCR_H);
    TEST_HOOK(test_bg_proc());
    memset(ht, 0xFF, BGC_HASH * sizeof(uint16_t));
    bgc_n = 0;
    for (y = 0; y < SCR_H; y++) {
        const uint8_t *row = &gfx_vbuffer[y][0];
        for (c = 0; c < BGC_COLS; c++) {
            const uint8_t *p = row + c * BGC_CH;
            uint16_t h = 0, e;
            uint8_t k;
            bool solid = true;
            for (k = 0; k < BGC_CH; k++) {
                h = (uint16_t)(h * 31u + p[k]);
                if (p[k] != p[0]) solid = false;
            }
            if (solid) {
                bgc_idx[y][c] = (uint16_t)(BGC_SOLID | p[0]);
                continue;
            }
            h &= BGC_HASH - 1;
            for (;;) {
                e = ht[h];
                if (e == 0xFFFFu) {
                    if (bgc_n >= BGC_MAX) {         /* never happens with this stadium */
                        TEST_HOOK(test_bgc_full());
                        e = (uint16_t)(BGC_SOLID | p[0]);
                        break;
                    }
                    memcpy(bgc.dict[bgc_n], p, BGC_CH);
                    e = ht[h] = bgc_n++;
                    break;
                }
                if (!memcmp(bgc.dict[e], p, BGC_CH)) break;
                h = (uint16_t)((h + 1) & (BGC_HASH - 1));
            }
            bgc_idx[y][c] = e;
        }
    }
    head_team[0] = head_team[1] = -1;   /* head_buf held the hash table */
    giant_key = -1;
    bgc_ready = true;
    bgc_restore(0, 0, SCR_W, SCR_H);
    TEST_HOOK(test_bg_decoded(bgc_n));
}

static void bg_full(void)
{
    gfx_Wait();
    bgc_restore(0, 0, SCR_W, SCR_H);
    gfx_SetClipRegion(0, 0, SCR_W, SCR_H);
}

/* ------------------------------------------------------------------ */
/* Dirty rectangles                                                    */
/* ------------------------------------------------------------------ */
static void mark_to(uint8_t b, int x0, int y0, int x1, int y1)
{
    rect_t *r;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > SCR_W) x1 = SCR_W;
    if (y1 > SCR_H) y1 = SCR_H;
    if (x0 >= x1 || y0 >= y1 || dirty_full[b]) return;
    /* already covered by a rect of this frame? (shadows inside players ...) */
    for (r = &dirty[b][0]; r < &dirty[b][ndirty[b]]; r++) {
        if (x0 >= r->x0 && y0 >= r->y0 && x1 <= r->x1 && y1 <= r->y1) return;
        if (x0 <= r->x0 && y0 <= r->y0 && x1 >= r->x1 && y1 >= r->y1) {
            r->x0 = (int16_t)x0; r->y0 = (int16_t)y0; r->x1 = (int16_t)x1; r->y1 = (int16_t)y1;
            return;
        }
    }
    if (ndirty[b] >= MAX_DIRTY) {
        dirty_full[b] = true;
        return;
    }
    r = &dirty[b][ndirty[b]++];
    r->x0 = (int16_t)x0; r->y0 = (int16_t)y0; r->x1 = (int16_t)x1; r->y1 = (int16_t)y1;
}

static void mark(int x0, int y0, int x1, int y1)
{
    mark_to(cur_buf, x0, y0, x1, y1);
}

static void invalidate(int x0, int y0, int x1, int y1)
{
    mark_to(0, x0, y0, x1, y1);
    mark_to(1, x0, y0, x1, y1);
}

static bool rect_hit(const rect_t *a, const rect_t *b)
{
    return a->x0 < b->x1 && b->x0 < a->x1 && a->y0 < b->y1 && b->y0 < a->y1;
}

/* restore buffer b; returns a bit mask of HUD widgets that were touched */
static uint8_t restore_dirty(uint8_t b)
{
    uint8_t i, w, touched = 0;
    if (dirty_full[b]) {
        bgc_restore(0, 0, SCR_W, SCR_H);
        touched = (uint8_t)((1 << W_COUNT) - 1);
    } else {
        for (i = 0; i < ndirty[b]; i++) {
            rect_t *r = &dirty[b][i];
            bgc_restore(r->x0, r->y0, r->x1, r->y1);
            for (w = 0; w < W_COUNT; w++)
                if (rect_hit(r, &wrect[w])) touched |= (uint8_t)(1 << w);
        }
    }
    ndirty[b] = 0;
    dirty_full[b] = false;
    return touched;
}

/* ------------------------------------------------------------------ */
/* Character drawing                                                   */
/* ------------------------------------------------------------------ */
static const uint8_t *cmap;      /* colour remap for tints (NULL = none) */
#define CM(c) ((uint8_t)(cmap ? cmap[(c)] : (c)))

static void cfill(int x, int y, int w, int h, uint8_t c)
{
    if (w <= 0 || h <= 0) return;
    gfx_SetColor(CM(c));
    gfx_FillRectangle(x, y, w, h);
}

/* rows y0..y1 of an ellipse, limited to columns xa..xb */
static void ell_span(int cx, int cy, int rx, int ry, int y0, int y1, int xa, int xb, uint8_t c)
{
    int y;
    gfx_SetColor(CM(c));
    if (ry < 1) ry = 1;
    for (y = y0; y <= y1; y++) {
        int dy = y - cy, hw, l, r;
        if (dy < -ry || dy > ry) continue;
        hw = (int)(isqrt32((unsigned long)(ry * ry - dy * dy) * (unsigned long)(rx * rx)) / (unsigned)ry);
        l = cx - hw;
        r = cx + hw;
        if (l < xa) l = xa;
        if (r > xb) r = xb;
        if (l <= r) gfx_HorizLine(l, y, r - l + 1);
    }
}

static void ellipse(int cx, int cy, int rx, int ry, uint8_t c)
{
    ell_span(cx, cy, rx, ry, cy - ry, cy + ry, cx - rx, cx + rx, c);
}

static void draw_head(int hx, int hy, int r, int ry, uint8_t team, int dir)
{
    const team_t *T = &TEAMS[team];
    uint8_t skin = (uint8_t)(C_SKIN + T->skin * 3);
    uint8_t hair = (uint8_t)(C_HAIR + T->hair * 2);
#define R(v) ((v) * r / 12)
    int back_l = dir > 0 ? hx - r - 6 : hx + R(4);
    int back_r = dir > 0 ? hx - R(4) : hx + r + 6;
    int ex, ey;

    /* hair behind the head */
    switch (T->style) {
    case HS_AFRO:
        ellipse(hx - dir * R(2), hy - R(3), r + R(5), ry + R(4), C_OUTLINE);
        ellipse(hx - dir * R(2), hy - R(3), r + R(4), ry + R(3), hair);
        ell_span(hx - dir * R(2), hy - R(3), r + R(4), ry + R(3), hy - R(3), hy + ry, back_l, back_r, (uint8_t)(hair + 1));
        break;
    case HS_LONG:
        cfill(dir > 0 ? hx - r - 1 : hx + R(3), hy - R(4), r - R(2), ry + R(8), C_OUTLINE);
        cfill(dir > 0 ? hx - r : hx + R(4), hy - R(4), r - R(4), ry + R(7), hair);
        break;
    case HS_DREADS: {
        int k;
        for (k = 0; k < 4; k++) {
            int sx = dir > 0 ? hx - r + R(1) + k * R(3) : hx + r - R(3) - k * R(3);
            cfill(sx - 1, hy - R(2), R(3) + 1, ry + R(8) - k * R(2), C_OUTLINE);
            cfill(sx, hy - R(2), R(2), ry + R(7) - k * R(2), (uint8_t)(hair + (k & 1)));
        }
        break;
    }
    case HS_BUN:
        ellipse(hx - dir * R(5), hy - ry - R(1), R(5), R(4), C_OUTLINE);
        ellipse(hx - dir * R(5), hy - ry - R(1), R(4), R(3), hair);
        break;
    default:
        break;
    }

    /* face */
    ellipse(hx, hy, r, ry, C_OUTLINE);
    ellipse(hx, hy, r - 1, ry - 1, (uint8_t)(skin + 1));
    ell_span(hx, hy, r - 1, ry - 1, hy - ry, hy + ry, back_l, back_r, (uint8_t)(skin + 2));
    ell_span(hx, hy, r - 1, ry - 1, hy + ry - R(3), hy + ry, hx - r, hx + r, (uint8_t)(skin + 2));
    cfill(hx + dir * R(3) - 1, hy - R(7), R(3), R(2), skin);               /* shine */
    /* nose */
    cfill(dir > 0 ? hx + r - 1 : hx - r - R(2), hy - R(1), R(3), R(3), C_OUTLINE);
    cfill(dir > 0 ? hx + r - 1 : hx - r - R(2) + 1, hy - R(1) + 1, R(3) - 1, R(3) - 2, (uint8_t)(skin + 1));
    /* ear */
    ellipse(hx - dir * R(4), hy + R(1), R(2) + 1, R(3), C_OUTLINE);
    ellipse(hx - dir * R(4), hy + R(1), R(2), R(3) - 1, (uint8_t)(skin + 1));
    cfill(hx - dir * R(4), hy + R(1), 1, R(2), (uint8_t)(skin + 2));

    /* hair on top */
    switch (T->style) {
    case HS_BUZZ:
        ell_span(hx, hy, r, ry, hy - ry, hy - R(6), hx - r, hx + r, hair);
        ell_span(hx, hy, r, ry, hy - R(6), hy - R(1), back_l, dir > 0 ? hx - R(5) : hx + r, hair);
        break;
    case HS_LONG:
        ell_span(hx, hy, r, ry, hy - ry, hy - R(5), hx - r, hx + r, hair);
        ell_span(hx, hy, r, ry, hy - R(5), hy + R(3), back_l, dir > 0 ? hx - R(5) : hx + r, hair);
        ell_span(hx, hy, r, ry, hy - R(6), hy - R(5), hx - r, hx + r, C_WHITE);   /* headband */
        break;
    case HS_SLICK:
        ell_span(hx, hy, r, ry, hy - ry, hy - R(5), hx - r, hx + r, hair);
        ell_span(hx, hy, r, ry, hy - R(5), hy - R(2), back_l, back_r, hair);
        cfill(dir > 0 ? hx + R(2) : hx - R(10), hy - ry - R(1), R(8), R(3), hair);   /* quiff */
        cfill(dir > 0 ? hx - R(4) : hx + R(1), hy - ry, R(3), 1, (uint8_t)(hair + 1));
        break;
    case HS_CURLY: {
        int k;
        ell_span(hx, hy, r, ry, hy - ry, hy - R(4), hx - r, hx + r, hair);
        for (k = -2; k <= 2; k++) {
            ellipse(hx + k * R(5), hy - ry + R(1), R(3) + 1, R(3) + 1, C_OUTLINE);
            ellipse(hx + k * R(5), hy - ry + R(1), R(3), R(3), k & 1 ? (uint8_t)(hair + 1) : hair);
        }
        ell_span(hx, hy, r, ry, hy - R(4), hy + R(1), back_l, dir > 0 ? hx - R(5) : hx + r, hair);
        break;
    }
    case HS_BALD:
        cfill(hx + dir * R(1) - R(2), hy - ry + R(2), R(4), R(2), C_WHITE);
        ell_span(hx, hy, r, ry, hy - R(6), hy - R(4), hx - r, hx + r, K_RED);       /* sweatband */
        break;
    case HS_SPIKY: {
        int k;
        ell_span(hx, hy, r, ry, hy - ry, hy - R(5), hx - r, hx + r, hair);
        gfx_SetColor(CM(hair));
        for (k = -1; k <= 2; k++) {
            int bx = hx + k * R(5) - dir * R(2);
            gfx_FillTriangle(bx - R(4), hy - ry + R(2), bx - dir * R(3), hy - ry - R(7), bx + R(3), hy - ry + R(2));
        }
        break;
    }
    case HS_MOHAWK:
        cfill(hx - R(2) - 1, hy - ry - R(5), R(5) + 2, ry - R(2) + R(5), C_OUTLINE);
        cfill(hx - R(2), hy - ry - R(4), R(5), ry - R(3) + R(4), hair);
        cfill(hx - R(2), hy - ry - R(4), 1, ry - R(3) + R(4), (uint8_t)(hair + 1));
        break;
    case HS_AFRO:
        ell_span(hx, hy, r, ry, hy - ry, hy - R(6), hx - r, hx + r, hair);
        break;
    case HS_BUN:
        ell_span(hx, hy, r, ry, hy - ry, hy - R(5), hx - r, hx + r, hair);
        ell_span(hx, hy, r, ry, hy - R(5), hy - R(1), back_l, dir > 0 ? hx - R(5) : hx + r, hair);
        break;
    default: /* HS_DREADS */
        ell_span(hx, hy, r, ry, hy - ry, hy - R(5), hx - r, hx + r, hair);
        cfill(hx - r + R(2), hy - R(6), 2 * r - R(4), R(1) + 1, K_YEL);              /* headband */
        break;
    }
    if (T->beard) {
        ell_span(hx, hy, r - 1, ry - 1, hy + R(3), hy + ry, dir > 0 ? hx - R(3) : hx - r, dir > 0 ? hx + r : hx + R(3), hair);
        ell_span(hx, hy, r - 1, ry - 1, hy + R(3), hy + R(4), dir > 0 ? hx - R(3) : hx - r, dir > 0 ? hx + r : hx + R(3), (uint8_t)(hair + 1));
    }

    /* eye, brow, mouth */
    ex = hx + dir * R(5);
    ey = hy - R(2);
    cfill(ex - R(2), ey - R(2), R(4), R(5), C_WHITE);
    cfill(dir > 0 ? ex : ex - R(2), ey - R(1), R(2), R(3), C_BLACK);
    cfill(ex - R(3), ey - R(4), R(6), R(1) + 1, (uint8_t)(hair + 1));
    if (T->power == PW_FIRE || T->power == PW_SPEED) {
        cfill(dir > 0 ? hx + R(2) : hx - R(8), hy + R(5), R(6), R(2), C_OUTLINE);
        cfill(dir > 0 ? hx + R(3) : hx - R(7), hy + R(5), R(4), R(1), C_WHITE);      /* grin */
    } else {
        cfill(dir > 0 ? hx + R(3) : hx - R(7), hy + R(6), R(4), R(1) + 1, C_MOUTH);
    }
#undef R
}

/* Pre-rendered heads, cropped to the pixels actually used: the two
   legends of the match (both directions) and, while it is in use, one
   giant-size head. Frozen / clone tints are made from these on the fly. */
static int8_t head_ox[2][2], head_oy[2][2], giant_ox, giant_oy;

/* draw a head at the top-left of the back buffer and capture it */
static void grab_head(gfx_sprite_t *spr, int8_t *ox, int8_t *oy, uint8_t team, int r, int dir)
{
    int x, y, x0 = HEAD_SW, y0 = HEAD_SH, x1 = 0, y1 = 0;
    gfx_SetClipRegion(0, 0, HEAD_SW, HEAD_SH);
    fill(0, 0, HEAD_SW, HEAD_SH, C_TRANSP);
    draw_head(HEAD_CX, HEAD_CY, r, r, team, dir);
    for (y = 0; y < HEAD_SH; y++) {
        const uint8_t *row = &gfx_vbuffer[y][0];
        for (x = 0; x < HEAD_SW; x++) {
            if (row[x] == C_TRANSP) continue;
            if (x < x0) x0 = x;
            if (x >= x1) x1 = x + 1;
            if (y < y0) y0 = y;
            y1 = y + 1;
        }
    }
    if (x0 >= x1) { x0 = y0 = 0; x1 = y1 = 1; }
    spr->width = (uint8_t)(x1 - x0);
    spr->height = (uint8_t)(y1 - y0);
    gfx_GetSprite(spr, x0, y0);
    *ox = (int8_t)(x0 - HEAD_CX);
    *oy = (int8_t)(y0 - HEAD_CY);
    gfx_SetClipRegion(0, 0, SCR_W, SCR_H);
}

static void cache_heads(uint8_t t0, uint8_t t1)
{
    uint8_t s, d;
    for (s = 0; s < 2; s++) {
        uint8_t team = s ? t1 : t0;
        for (d = 0; d < 2; d++)
            grab_head(HEAD_SPR(s, d), &head_ox[s][d], &head_oy[s][d], team, HEAD_R, d ? -1 : 1);
        head_team[s] = (int8_t)team;
    }
    giant_key = -1;
}

/* make sure the giant head for (team, dir) is cached; draws in the back
   buffer's top-left corner, so call it before that buffer is restored */
static void cache_giant(uint8_t team, int dir)
{
    int8_t key = (int8_t)(team * 2 + (dir > 0 ? 0 : 1));
    if (key == giant_key) return;
    grab_head((gfx_sprite_t *)(void *)giant_buf, &giant_ox, &giant_oy, team, HEAD_R * 3 / 2, dir);
    giant_key = key;
}

static void put_head(int hx, int hy, int r, int ry, uint8_t team, int dir)
{
    uint8_t s, d = dir > 0 ? 0 : 1;
    gfx_sprite_t *spr = NULL;
    int ox = 0, oy = 0;
    (void)ry;   /* cached heads are not squashed (the body still is) */
    if (r == HEAD_R) {
        for (s = 0; s < 2; s++) {
            if (head_team[s] == (int8_t)team) {
                spr = HEAD_SPR(s, d);
                ox = head_ox[s][d];
                oy = head_oy[s][d];
                break;
            }
        }
    } else if (r == HEAD_R * 3 / 2 && giant_key == (int8_t)(team * 2 + d)) {
        spr = (gfx_sprite_t *)(void *)giant_buf;
        ox = giant_ox;
        oy = giant_oy;
    }
    if (!spr) {
        draw_head(hx, hy, r, ry, team, dir);
        return;
    }
    if (cmap) {
        /* frozen / clone: tinted copy of the cached head */
        gfx_sprite_t *t = (gfx_sprite_t *)(void *)head_tmp;
        unsigned k, n = (unsigned)spr->width * spr->height;
        t->width = spr->width;
        t->height = spr->height;
        for (k = 0; k < n; k++) {
            uint8_t c = spr->data[k];
            t->data[k] = c == C_TRANSP ? c : cmap[c];
        }
        spr = t;
    }
    gfx_TransparentSprite(spr, hx + ox, hy + oy);
}

enum { POSE_STAND, POSE_RUN, POSE_JUMP, POSE_KICK, POSE_LIE, POSE_CELEB };

static void kit_of(uint8_t team, bool alt, uint8_t *shirt, uint8_t *trim, uint8_t *shorts)
{
    const team_t *T = &TEAMS[team];
    *shirt = alt ? T->alt_shirt : T->shirt;
    *trim = alt ? T->alt_trim : T->trim;
    *shorts = alt ? T->alt_shorts : T->shorts;
}

/* Draws a legend with feet at (x, gy). sc = 2 normal, 3 giant.
 * squash: >0 landing squash, <0 take-off stretch. */
static void draw_char(int x, int gy, uint8_t team, int dir, bool alt, uint8_t pose,
                      uint8_t phase, int lie_dir, int sc, int squash)
{
    const team_t *T = &TEAMS[team];
    uint8_t shirt, trim, shorts;
    uint8_t skin = (uint8_t)(C_SKIN + T->skin * 3 + 1);
    int r = HEAD_R * sc / 2;
#define S(v) ((v) * sc / 2)
    kit_of(team, alt, &shirt, &trim, &shorts);

    if (pose == POSE_LIE) {
        int ld = lie_dir;
        cfill(ld > 0 ? x + S(12) : x - S(16), gy - S(6), S(4), S(5), T->boots);
        cfill(ld > 0 ? x + S(2) : x - S(12), gy - S(5), S(10), S(3), T->socks);
        cfill(ld > 0 ? x - S(3) : x - S(2), gy - S(8), S(6), S(6), shorts);
        cfill(ld > 0 ? x - S(13) : x + S(3), gy - S(10), S(10), S(8), shirt);
        cfill(ld > 0 ? x - S(13) : x + S(3), gy - S(4), S(10), S(2), (uint8_t)(shirt + 1));
        cfill(ld > 0 ? x - S(9) : x + S(6), gy - S(10), S(3), S(8), trim);
        put_head(x - ld * S(LIE_HEAD_DX), gy - r, r, r, team, -ld);
        return;
    }
    {
        int fl = 0, bl = 0, lift = 0, tuck = 0, armup = 0, swing = 0;
        int hy, ry = r;
        if (pose == POSE_RUN) {
            static const int8_t step[4] = { 2, 0, -2, 0 };
            fl = step[phase & 3];
            bl = -fl;
            lift = (phase & 1) ? 1 : 0;
            swing = fl;
        } else if (pose == POSE_JUMP) {
            tuck = 2;
            armup = 1;
        } else if (pose == POSE_CELEB) {
            tuck = 1;
            armup = 2;
        }
        if (squash > 0) { ry = r - S(2); gy += 0; }
        else if (squash < 0) ry = r + 1;
        /* back leg + boot */
        cfill(x - dir * S(3) + dir * bl - S(1), gy - S(7) - tuck, S(3), S(4), T->socks);
        cfill(x - dir * S(3) + dir * bl - S(2) + (dir > 0 ? 0 : -S(1)), gy - S(3) - tuck, S(5), S(3), T->boots);
        /* front leg + boot */
        if (pose == POSE_KICK) {
            int lx = dir > 0 ? x + S(1) : x - S(11);
            cfill(lx, gy - S(10), S(10), S(3), T->socks);
            cfill(dir > 0 ? x + S(10) : x - S(14), gy - S(11), S(4), S(5), T->boots);
            cfill(dir > 0 ? x + S(12) : x - S(14), gy - S(11), S(2), S(1), C_WHITE);
        } else {
            cfill(x + dir * S(2) + dir * fl - S(1), gy - S(7) - tuck - lift, S(3), S(4), T->socks);
            cfill(x + dir * S(2) + dir * fl - S(1) + (dir > 0 ? 0 : -S(1)), gy - S(3) - tuck - lift, S(5), S(3), T->boots);
        }
        /* shorts */
        cfill(x - S(6), gy - S(11) - tuck, S(12), S(4), shorts);
        cfill(dir > 0 ? x - S(6) : x + S(3), gy - S(11) - tuck, S(3), S(4), (uint8_t)(shorts + 1));
        /* shirt */
        cfill(x - S(6), gy - S(20) - tuck, S(12), S(9), shirt);
        cfill(dir > 0 ? x - S(6) : x + S(3), gy - S(20) - tuck, S(3), S(9), (uint8_t)(shirt + 1));
        switch (T->pattern) {
        case PAT_STRIPES:
            cfill(x - S(3), gy - S(20) - tuck, S(2), S(9), trim);
            cfill(x + S(2), gy - S(20) - tuck, S(2), S(9), trim);
            break;
        case PAT_BAND:
            cfill(x - S(6), gy - S(16) - tuck, S(12), S(3), trim);
            break;
        case PAT_SASH:
            cfill(x - S(5), gy - S(19) - tuck, S(4), S(3), trim);
            cfill(x - S(2), gy - S(16) - tuck, S(4), S(3), trim);
            cfill(x + S(1), gy - S(13) - tuck, S(4), S(2), trim);
            break;
        default:
            cfill(x - S(3), gy - S(20) - tuck, S(6), S(2), trim);
            break;
        }
        /* hands (fists) */
        if (armup) {
            cfill(x - S(10), gy - S(26) - tuck - (armup == 2 ? S(3) : 0), S(4), S(4), skin);
            cfill(x + S(6), gy - S(26) - tuck - (armup == 2 ? S(3) : 0), S(4), S(4), skin);
            cfill(x - S(8), gy - S(22) - tuck, S(3), S(4), shirt);
            cfill(x + S(5), gy - S(22) - tuck, S(3), S(4), shirt);
        } else {
            cfill(x + dir * S(7) - S(2), gy - S(14) - tuck + swing, S(4), S(4), skin);
            cfill(x - dir * S(7) - S(2), gy - S(14) - tuck - swing, S(4), S(4), (uint8_t)(skin + 1));
            cfill(x + dir * S(6) - S(1), gy - S(19) - tuck, S(3), S(4), shirt);
        }
        hy = gy - S(HEAD_DY) - tuck + (squash > 0 ? S(2) : 0);
        put_head(x + dir * S(1), hy, r, ry, team, dir);
    }
#undef S
}

/* ------------------------------------------------------------------ */
/* Particles                                                           */
/* ------------------------------------------------------------------ */
static void spawn(int x, int y, int vx, int vy, uint8_t col, uint8_t life, uint8_t size, uint8_t kind)
{
    particle_t *p = &parts[part_next];
    part_next = (uint8_t)((part_next + 1) % MAX_PARTS);
    p->x = x * 16;
    p->y = y * 16;
    p->vx = (int8_t)clampi(vx, -127, 127);
    p->vy = (int8_t)clampi(vy, -127, 127);
    p->col = col;
    p->life = life;
    p->size = size;
    p->kind = kind;
}

static void burst(int x, int y, uint8_t col, uint8_t col2, uint8_t n, int speed)
{
    uint8_t i;
    for (i = 0; i < n; i++) {
        uint8_t a = (uint8_t)rnd_n(32);
        int sp = speed / 2 + rnd_n(speed / 2 + 1);
        spawn(x, y, COS32(a) * sp / 64, SIN32[a] * sp / 64, (i & 1) ? col : col2,
              (uint8_t)(10 + rnd_n(12)), (uint8_t)(2 + (i & 1)), (i % 3 == 0) ? PK_STAR : PK_DOT);
    }
}

static void update_particles(void)
{
    uint8_t i;
    for (i = 0; i < MAX_PARTS; i++) {
        particle_t *p = &parts[i];
        if (!p->life) continue;
        p->life--;
        p->x += p->vx;
        p->y += p->vy;
        if (p->kind == PK_GRAV && p->vy < 120) p->vy = (int8_t)(p->vy + 1);
        if (p->y < 0 || p->y > SCR_H * 16 || p->x < 0 || p->x > SCR_W * 16) p->life = 0;
    }
}

static void clear_particles(void)
{
    memset(parts, 0, sizeof parts);
}

static void draw_particles(void)
{
    uint8_t i;
    for (i = 0; i < MAX_PARTS; i++) {
        particle_t *p = &parts[i];
        int x, y;
        if (!p->life) continue;
        x = (p->x >> 4) + shake_x;
        y = (p->y >> 4) + shake_y;
        /* not over the HUD: a dot there would force a whole widget redraw */
        if (y < 44 || y > 204) continue;
        gfx_SetColor(p->col);
        if (p->kind == PK_STAR) {
            gfx_FillRectangle(x - 1, y, 3, 1);
            gfx_FillRectangle(x, y - 1, 1, 3);
            mark(x - 1, y - 1, x + 2, y + 2);
        } else {
            gfx_FillRectangle(x, y, p->size, p->size);
            mark(x, y, x + p->size, y + p->size);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Physics: shapes and collisions                                      */
/* ------------------------------------------------------------------ */
typedef struct { int hx, hy, hr, x0, y0, x1, y1; } shape_t;

static uint8_t power_of(uint8_t i)
{
    return TEAMS[M.team[i]].power;
}

static bool ab_on(uint8_t i, uint8_t pw)
{
    return pl[i].ab_t && power_of(i) == pw;
}

static int scale_of(uint8_t i)
{
    return ab_on(i, PW_GIANT) ? 3 : 2;
}

static bool is_lying(const player_t *p)
{
    return p->slide_t || p->down_t;
}

/* clone stands 30 px behind its owner (towards its own goal) and copies jumps */
static void clone_pos(uint8_t i, int *cx, int *cy)
{
    player_t *p = &pl[i];
    int x = clampi(p->x - p->dir * 30 * FP, MIN_X * FP, MAX_X * FP);
    int lift = ground_px(PX(p->x)) * FP - p->y;
    *cx = x;
    *cy = ground_px(PX(x)) * FP - (lift > 0 ? lift : 0);
}

static void shape_at(const player_t *p, int x, int y, int sc, shape_t *s)
{
    if (is_lying(p)) {
        s->hx = x - p->lie_dir * (LIE_HEAD_DX * sc / 2) * FP;
        s->hy = y - (HEAD_R * sc / 2) * FP;
        s->x0 = x - (LIE_HW * sc / 2) * FP;
        s->x1 = x + (LIE_HW * sc / 2) * FP;
        s->y0 = y - (LIE_H * sc / 2) * FP;
    } else {
        s->hx = x + p->dir * FP;
        s->hy = y - (HEAD_DY * sc / 2) * FP;
        s->x0 = x - (BODY_HW * sc / 2) * FP;
        s->x1 = x + (BODY_HW * sc / 2) * FP;
        s->y0 = y - (BODY_H * sc / 2) * FP;
    }
    s->hr = HEAD_R * sc / 2;
    s->y1 = y;
}

static void get_shape(uint8_t i, bool clone, shape_t *s)
{
    if (clone) {
        int cx, cy;
        clone_pos(i, &cx, &cy);
        shape_at(&pl[i], cx, cy, 2, s);
    } else {
        shape_at(&pl[i], pl[i].x, pl[i].y, scale_of(i), s);
    }
}

static void head_pos(uint8_t i, int *hx, int *hy)
{
    shape_t s;
    get_shape(i, false, &s);
    *hx = s.hx;
    *hy = s.hy;
}

static void cap_ball_speed(void)
{
    ball.vx = clampi(ball.vx, -MAXV, MAXV);
    ball.vy = clampi(ball.vy, -MAXV, MAXV);
}

/* Resolve ball against anchor (ax, ay) at distance R. min_sep pushes the
 * ball clear of moving bodies; static world uses 0 so it can rest. */
static void resolve(int ax, int ay, long dx, long dy, long d, int R, int pvx, int pvy, int e10, int min_sep)
{
    long nx = dx * 64 / d, ny = dy * 64 / d;
    long rvx, rvy, vn;
    ball.x = ax + (int)(nx * R / 64);
    ball.y = ay + (int)(ny * R / 64);
    rvx = ball.vx - pvx;
    rvy = ball.vy - pvy;
    vn = (rvx * nx + rvy * ny) / 64;
    if (vn < 0) {
        long j = -vn * (10 + e10) / 10;
        ball.vx += (int)(j * nx / 64);
        ball.vy += (int)(j * ny / 64);
        if (j > 6 * FP) ball.squash = 3;
    }
    if (min_sep) {
        rvx = ball.vx - pvx;
        rvy = ball.vy - pvy;
        vn = (rvx * nx + rvy * ny) / 64;
        if (vn < min_sep) {
            ball.vx += (int)((min_sep - vn) * nx / 64);
            ball.vy += (int)((min_sep - vn) * ny / 64);
        }
    }
    cap_ball_speed();
    CHK24(ball.x); CHK24(ball.y);
}

static bool collide_circle(int cx, int cy, int cr, int pvx, int pvy, int e10)
{
    long dx = ball.x - cx, dy = ball.y - cy;
    int R = (cr + ball.r) * FP;
    long d2 = dx * dx + dy * dy;
    long d;
    if (d2 >= (long)R * R) return false;
    d = (long)isqrt32((unsigned long)d2);
    if (d == 0) { dx = 0; dy = -FP; d = FP; }
    resolve(cx, cy, dx, dy, d, R, pvx, pvy, e10, 96);
    return true;
}

static bool collide_box(int x0, int y0, int x1, int y1, int pvx, int pvy, int e10, int min_sep)
{
    int cx = clampi(ball.x, x0, x1), cy = clampi(ball.y, y0, y1);
    long dx = ball.x - cx, dy = ball.y - cy;
    int r = ball.r * FP;
    long d2 = dx * dx + dy * dy;
    long d;
    if (d2 >= (long)r * r) return false;
    if (d2 == 0) {
        int dl = ball.x - x0, dr = x1 - ball.x, dt = ball.y - y0, db = y1 - ball.y;
        int m = dl;
        cx = x0; cy = ball.y; dx = -FP; dy = 0;
        if (dr < m) { m = dr; cx = x1; dx = FP; }
        if (dt < m) { m = dt; cx = ball.x; cy = y0; dx = 0; dy = -FP; }
        if (db < m) { cx = ball.x; cy = y1; dx = 0; dy = FP; }
        d = FP;
    } else {
        d = (long)isqrt32((unsigned long)d2);
        if (d == 0) d = 1;
    }
    resolve(cx, cy, dx, dy, d, r, pvx, pvy, e10, min_sep);
    return true;
}

/* ball against a static line segment (the ramps) */
static bool collide_segment(int ax, int ay, int bx, int by, int e10)
{
    long sx = bx - ax, sy = by - ay;
    long px = ball.x - ax, py = ball.y - ay;
    long len2 = sx * sx + sy * sy;
    long t = (px * sx + py * sy) * 256 / len2;
    int cx, cy;
    long dx, dy, d2, d;
    int r = ball.r * FP;
    if (t < 0) t = 0;
    if (t > 256) t = 256;
    cx = ax + (int)(sx * t / 256);
    cy = ay + (int)(sy * t / 256);
    dx = ball.x - cx;
    dy = ball.y - cy;
    d2 = dx * dx + dy * dy;
    if (d2 >= (long)r * r) return false;
    d = (long)isqrt32((unsigned long)d2);
    if (d == 0) { dx = 0; dy = -FP; d = FP; }
    resolve(cx, cy, dx, dy, d, r, 0, 0, e10, 0);
    return true;
}

/* ------------------------------------------------------------------ */
/* Supershots / abilities                                              */
/* ------------------------------------------------------------------ */
static int goal_cx(int dir)
{
    return dir > 0 ? SCR_W - 15 : 15;
}

static void touch(uint8_t i, uint8_t amount);

static void knock(uint8_t i, int dir)
{
    player_t *p = &pl[i];
    if (p->down_t && p->knocked) return;
    if (ab_on(i, PW_GIANT)) {           /* giants cannot be knocked over */
        burst(PX(p->x), PX(p->y) - 30, C_GOLD, C_WHITE, 6, 32);
        return;
    }
    p->slide_t = 0;
    p->down_t = KNOCK_T;
    p->knocked = true;
    p->lie_dir = (int8_t)dir;
    p->vx = dir * 3 * FP;
    p->kick_t = 0;
    if (M.hitstop < 3) M.hitstop = 3;
    burst(PX(p->x), PX(p->y) - 20, C_YELLOW, C_WHITE, 8, 40);
}

static void shake(uint8_t t)
{
    if (M.shake < t) M.shake = t;
}

/* ball abilities, fired on a touch */
static void fire_super(uint8_t i)
{
    player_t *p = &pl[i];
    int dir = p->dir;
    int bxp = PX(ball.x), byp = PX(ball.y);
    int gx = goal_cx(dir);
    uint8_t pw = power_of(i);

    p->charged_t = 0;
    ball.owner = i;
    ball.immune = 8;
    ball.plowed = false;
    ball.r = BALL_R;
    M.hitstop = 4;
    shake(4);
    switch (pw) {
    case PW_FIRE: {
        int dxp = iabs(gx - bxp), t = dxp / 15;
        if (t < 1) t = 1;
        ball.mode = BM_FIRE;
        ball.mode_t = 50;
        ball.vx = dir * 15 * FP;
        ball.vy = clampi((150 - byp) * FP / t, -8 * FP, 8 * FP);
        burst(bxp, byp, C_FIRE, C_YELLOW, 12, 48);
        break;
    }
    case PW_TELE: {
        int nx = dir > 0 ? SCR_W - GOAL_LINE - 58 : GOAL_LINE + 58;
        burst(bxp, byp, C_PURPLE, C_PINK, 12, 40);
        ball.x = nx * FP;
        ball.y = 124 * FP;
        ball.vx = dir * 3 * FP;
        ball.vy = -2 * FP;
        ball.mode = BM_TELE;
        ball.mode_t = 20;
        burst(nx, 124, C_PURPLE, C_WHITE, 12, 40);
        break;
    }
    default: /* PW_HEAVY */
        ball.mode = BM_HEAVY;
        ball.mode_t = HEAVY_T;
        ball.vx = dir * 9 * FP;
        ball.vy = -4 * FP;
        burst(bxp, byp, C_DKGRAY, C_GRAY, 12, 40);
        break;
    }
}

static void activate_super(uint8_t i)
{
    player_t *p = &pl[i];
    player_t *o = &pl[i ^ 1];
    uint8_t pw = power_of(i);
    int x = PX(p->x), y = PX(p->y);
    p->power = 0;
    p->popup_t = 36;
    if (M.supers[i] < 255) M.supers[i]++;
    TEST_HOOK(test_on_super(i, pw));
    if (PW_NEEDS_BALL(pw)) {
        int dx = PX(ball.x) - x, dy = PX(ball.y) - (y - 20);
        if (dx * dx + dy * dy <= 36 * 36) fire_super(i);
        else p->charged_t = CHARGE_T;
        return;
    }
    switch (pw) {
    case PW_FREEZE:
        o->frozen_t = FREEZE_T;
        o->vx = 0;
        o->slide_t = 0;
        o->kick_t = 0;
        burst(PX(o->x), PX(o->y) - 24, C_ICE, C_WHITE, 16, 48);
        break;
    case PW_ROCKET:
        p->vy = -ROCKET_V;
        p->on_ground = false;
        p->ab_t = ROCKET_T;
        p->slide_t = p->down_t = 0;
        burst(x, y, C_FIRE, C_FIRE + 2, 14, 40);
        shake(3);
        break;
    case PW_GIANT:
        p->ab_t = GIANT_T;
        burst(x, y - 30, C_GOLD, C_WHITE, 16, 56);
        break;
    case PW_MAGNET:
        p->ab_t = MAGNET_T;
        burst(x, y - 20, C_MAGNET, C_PURPLE, 12, 40);
        break;
    case PW_CLONE:
        p->ab_t = CLONE_T;
        burst(x - p->dir * 30, y - 20, C_GHOST, C_WHITE, 16, 40);
        break;
    case PW_WALL:
        p->ab_t = WALL_T;
        burst(p->dir > 0 ? 41 : SCR_W - 41, 140, C_WALLFX, C_WHITE, 16, 48);
        break;
    default: /* PW_SPEED */
        p->ab_t = SPEED_T;
        burst(x, y - 10, C_YELLOW, C_WHITE, 12, 48);
        break;
    }
}

static void touch(uint8_t i, uint8_t amount)
{
    player_t *p = &pl[i];
    ball.idle_t = 0;
    if (p->touch_cd) return;
    p->touch_cd = 8;
    if (M.touches[i] < 255) M.touches[i]++;
    if (p->power < POWER_MAX) {
        p->power = (uint8_t)(p->power + amount > POWER_MAX ? POWER_MAX : p->power + amount);
    }
}

/* ------------------------------------------------------------------ */
/* Player update                                                       */
/* ------------------------------------------------------------------ */
static void update_player(uint8_t i, const ctrl_t *c)
{
    player_t *p = &pl[i];
    bool play = M.phase == PH_PLAY || M.phase == PH_GOAL;
    int oldx = p->x, gnd;
    bool was_ground = p->on_ground;
    uint8_t pw = power_of(i);

    if (p->touch_cd) p->touch_cd--;
    if (p->kick_t) p->kick_t--;
    if (p->land_t) p->land_t--;
    if (p->popup_t) p->popup_t--;
    if (p->ab_t) {
        p->ab_t--;
        if (pw == PW_SPEED && (p->ab_t & 1) && p->vx)
            spawn(PX(p->x) - p->dir * 6, PX(p->y) - 4 - rnd_n(30), -p->vx / 32, 0,
                  TEAMS[M.team[i]].shirt, 6, 2, PK_DOT);
        if (pw == PW_ROCKET && !p->on_ground)
            spawn(PX(p->x) - 2 + rnd_n(5), PX(p->y), -4 + rnd_n(9), 16, C_FIRE + (p->ab_t & 3), 8, 3, PK_DOT);
        if (pw == PW_MAGNET && (p->ab_t & 1)) {
            int bx = PX(ball.x), by = PX(ball.y);
            spawn(bx, by, (PX(p->x) - bx) / 6, (PX(p->y) - 16 - by) / 6, C_MAGNET, 6, 2, PK_STAR);
        }
        if (pw == PW_ROCKET && p->on_ground && p->ab_t < ROCKET_T - 4) p->ab_t = 0;
    }
    if (p->charged_t) {
        p->charged_t--;
        if ((p->charged_t & 3) == 0) {
            int hx, hy;
            head_pos(i, &hx, &hy);
            spawn(PX(hx) - 10 + rnd_n(20), PX(hy) + 8 - rnd_n(30), 0, -12, POWER_COL[pw], 10, 2, PK_STAR);
        }
    }

    if (p->frozen_t) {
        p->frozen_t--;
        p->vx = 0;
    } else if (p->slide_t) {
        p->slide_t--;
        p->vx = p->slide_dir * (SLIDE_V * p->slide_t / SLIDE_T + FP);
        if ((p->slide_t & 1) && p->on_ground)
            spawn(PX(p->x) - p->slide_dir * 8, PX(p->y) - 2, -p->slide_dir * 8, -6, C_LIP + 1, 8, 2, PK_DOT);
        if (p->slide_t == 0) {
            p->down_t = RECOVER_T;
            p->knocked = false;
            p->vx = 0;
        }
    } else if (p->down_t) {
        p->down_t--;
        p->vx = p->vx * 3 / 4;
        if (!p->down_t) p->knocked = false;
    } else if (play) {
        int mv = (c->right ? 1 : 0) - (c->left ? 1 : 0);
        int vmax = RUN_V, acc = p->on_ground ? RUN_ACC : AIR_ACC;
        if (ab_on(i, PW_SPEED)) { vmax = RUN_V * 8 / 5; acc = acc * 3 / 2; }
        else if (ab_on(i, PW_GIANT)) vmax = RUN_V * 9 / 10;
        p->vx += clampi(mv * vmax - p->vx, -acc, acc);
        if (c->jump && p->on_ground) {
            p->vy = -JUMP_V;
            p->on_ground = false;
        } else if (c->slide && p->on_ground) {
            p->slide_t = SLIDE_T;
            p->slide_dir = (int8_t)(mv ? mv : p->dir);
            p->lie_dir = p->slide_dir;
            p->vx = p->slide_dir * SLIDE_V;
            p->tackled = false;
            p->kick_t = 0;
        }
        if (c->shoot && p->kick_t == 0 && !p->slide_t) {
            p->kick_t = KICK_T;
            p->kick_hit = false;
        }
        if (c->super && p->power >= POWER_MAX) activate_super(i);
        if (mv && p->on_ground) p->anim++;
        else p->anim = 0;
    } else {
        p->vx = 0;
    }

    /* integrate: x first, then fall onto the ground under the new x */
    p->x += p->vx;
    p->x = clampi(p->x, MIN_X * FP, MAX_X * FP);
    gnd = ground_px(PX(p->x)) * FP;
    p->vy += GRAV_P;
    if (p->vy > MAXFALL) p->vy = MAXFALL;
    p->y += p->vy;
    if (p->y >= gnd) {
        if (!was_ground && p->vy > 4 * FP) p->land_t = 4;
        p->y = gnd;
        p->vy = 0;
        p->on_ground = true;
    } else if (was_ground && p->vy >= 0 && p->y > gnd - 6 * FP) {
        p->y = gnd;                      /* walking down a ramp */
        p->vy = 0;
        p->on_ground = true;
    } else {
        p->on_ground = false;
    }
    if (p->y < (CEIL_Y + 50) * FP) { p->y = (CEIL_Y + 50) * FP; if (p->vy < 0) p->vy = 0; }

    /* goals: the head may only be under the crossbar if the body is low */
    {
        int top = p->y - (HEAD_DY + HEAD_R) * scale_of(i) / 2 * FP;
        bool in_l = p->x < GOAL_ZONE * FP, in_r = p->x > (SCR_W - GOAL_ZONE) * FP;
        if ((in_l || in_r) && top < BAR_BOT * FP) {
            bool was_in = in_l ? oldx < GOAL_ZONE * FP : oldx > (SCR_W - GOAL_ZONE) * FP;
            if (!was_in) {
                p->x = in_l ? GOAL_ZONE * FP : (SCR_W - GOAL_ZONE) * FP;
                p->vx = 0;
            } else {
                p->y += BAR_BOT * FP - top;
                if (p->vy < 0) p->vy = 0;
            }
        }
    }
    CHK24(p->x); CHK24(p->y);
}

static void players_interact(void)
{
    player_t *a = &pl[0], *b = &pl[1];
    int dx = b->x - a->x;
    int ady = iabs(a->y - b->y);
    int w = (BODY_HW * scale_of(0) + BODY_HW * scale_of(1)) / 2 + 6;
    uint8_t i;

    for (i = 0; i < 2; i++) {
        player_t *p = &pl[i], *o = &pl[i ^ 1];
        int top = HEAD_DY * scale_of(i ^ 1) / 2;
        if (p->vy > 0 && !is_lying(o) && iabs(p->x - o->x) < 14 * FP &&
            p->y < o->y - top * FP && p->y > o->y - (top + 16) * FP) {
            p->vy = -JUMP_V * 2 / 3;
        }
    }
    if (iabs(dx) < w * FP && ady < 30 * FP) {
        int push = (w * FP - iabs(dx)) / 2 + 1;
        if (dx >= 0) { a->x -= push; b->x += push; }
        else { a->x += push; b->x -= push; }
        a->x = clampi(a->x, MIN_X * FP, MAX_X * FP);
        b->x = clampi(b->x, MIN_X * FP, MAX_X * FP);
    }
    for (i = 0; i < 2; i++) {
        uint8_t k = (uint8_t)(i ^ (M.total_ticks & 1));
        player_t *p = &pl[k], *o = &pl[k ^ 1];
        if (p->slide_t && !p->tackled && o->on_ground && !(o->down_t && o->knocked)) {
            int rel = (o->x - p->x) * p->slide_dir;
            if (rel > -6 * FP && rel < 22 * FP) {
                p->tackled = true;
                o->frozen_t = 0;
                knock(k ^ 1, p->slide_dir);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Kicks                                                               */
/* ------------------------------------------------------------------ */
static void do_kick(uint8_t i)
{
    player_t *p = &pl[i];
    int dir = p->dir, sc = scale_of(i);
    int dx, dy;
    bool hit = false;

    if (p->kick_t < KICK_ACTIVE || p->kick_hit || is_lying(p) || p->frozen_t) return;
    dx = (PX(ball.x) - PX(p->x)) * dir;
    dy = PX(ball.y) - PX(p->y);
    if (p->on_ground) {
        if (dx >= -4 && dx <= 26 * sc / 2 && dy >= -32 * sc / 2 && dy <= 4) {
            ball.vx = dir * SHOT_V * (sc + 8) / 10;
            ball.vy = -4 * FP;
            hit = true;
        }
    } else {
        int hx, hy, hdx, hdy, hr = 20 * sc / 2;
        head_pos(i, &hx, &hy);
        hdx = PX(ball.x) - PX(hx);
        hdy = PX(ball.y) - PX(hy);
        if (hdx * hdx + hdy * hdy <= hr * hr) {
            ball.vx = dir * HEADER_V;
            ball.vy = FP + FP / 2;
            hit = true;
        } else if (dx >= -4 && dx <= 28 * sc / 2 && dy >= -46 * sc / 2 && dy <= 6) {
            ball.vx = dir * VOLLEY_V;
            ball.vy = -7 * FP;
            hit = true;
        }
    }
    if (hit) {
        p->kick_hit = true;
        ball.mode = BM_NONE;
        ball.r = BALL_R;
        ball.owner = i;
        ball.immune = 4;
        ball.squash = 3;
        if (M.hitstop < 2) M.hitstop = 2;
        if (M.shots[i] < 255) M.shots[i]++;
        touch(i, 6);
        burst(PX(ball.x), PX(ball.y), C_WHITE, C_YELLOW, 5, 32);
        if (p->charged_t) fire_super(i);
    }
}

/* ------------------------------------------------------------------ */
/* Ball                                                                */
/* ------------------------------------------------------------------ */
static bool knock_mode(void)
{
    return ball.mode == BM_FIRE || ball.mode == BM_SMASH || ball.mode == BM_HEAVY;
}

static void end_mode(void)
{
    ball.mode = BM_NONE;
    ball.r = BALL_R;
}

static void drop_ball(int x)
{
    ball.x = x * FP;
    ball.y = 50 * FP;
    ball.vx = 0;
    ball.vy = 0;
    ball.r = BALL_R;
    ball.mode = BM_NONE;
    ball.immune = 0;
    ball.idle_t = 0;
    ball.anchor_x = ball.x;
    ball.anchor_y = ball.y;
}

static bool world_collide(void)
{
    int r = ball.r * FP;
    bool hit = false;
    int bxp = PX(ball.x);
    uint8_t i;
    if (ball.x - r < 0) {
        ball.x = r;
        if (ball.vx < 0) ball.vx = -ball.vx * 7 / 10;
        hit = true;
    } else if (ball.x + r > SCR_W * FP) {
        ball.x = SCR_W * FP - r;
        if (ball.vx > 0) ball.vx = -ball.vx * 7 / 10;
        hit = true;
    }
    if (ball.y - r < CEIL_Y * FP) {
        ball.y = CEIL_Y * FP + r;
        if (ball.vy < 0) ball.vy = -ball.vy / 2;
        hit = true;
    }
    /* flat ground: platforms and pitch */
    if (bxp <= PLAT_X || bxp >= SCR_W - PLAT_X || (bxp >= RAMP_X && bxp <= SCR_W - RAMP_X)) {
        int g = ground_px(bxp) * FP;
        if (ball.y + r > g) {
            ball.y = g - r;
            if (ball.vy > 0) {
                if (ball.vy > 5 * FP) ball.squash = 3;
                if (ball.mode == BM_HEAVY && ball.vy > 4 * FP) shake(2);
                ball.vy = -ball.vy * 5 / 8;
                if (ball.vy > -BOUNCE_MIN) ball.vy = 0;
            }
            if (ball.mode == BM_SMASH) {
                burst(bxp, PX(g) - 4, C_FIRE, C_LTGRAY, 8, 32);
                end_mode();
            }
        }
    }
    /* ramps (only tested near them, which also keeps the maths in range) */
    if (bxp > PLAT_X - 16 && bxp < RAMP_X + 16)
        collide_segment(PLAT_X * FP, G_PLAT * FP, RAMP_X * FP, G_MAIN * FP, 4);
    if (bxp > SCR_W - RAMP_X - 16 && bxp < SCR_W - PLAT_X + 16)
        collide_segment((SCR_W - RAMP_X) * FP, G_MAIN * FP, (SCR_W - PLAT_X) * FP, G_PLAT * FP, 4);
    if (collide_box(0, BAR_TOP * FP, BAR_LEN * FP, BAR_BOT * FP, 0, 0, 6, 0)) hit = true;
    if (collide_box((SCR_W - BAR_LEN) * FP, BAR_TOP * FP, SCR_W * FP, BAR_BOT * FP, 0, 0, 6, 0)) hit = true;
    /* WALL ability */
    for (i = 0; i < 2; i++) {
        if (ab_on(i, PW_WALL)) {
            int wx = pl[i].dir > 0 ? 37 : SCR_W - 45;
            if (collide_box(wx * FP, BAR_BOT * FP, (wx + 8) * FP, (G_PLAT + 8) * FP, 0, 0, 8, 64)) {
                hit = true;
                if (knock_mode()) end_mode();
            }
        }
    }
    return hit;
}

static void ball_hits_players(bool *squeeze)
{
    uint8_t n, hits = 0;
    for (n = 0; n < 4; n++) {
        uint8_t i = (uint8_t)((n & 1) ^ (M.total_ticks & 1));
        bool clone = n >= 2;
        player_t *p = &pl[i];
        shape_t s;
        bool hh, hb;
        int pvx = p->vx, pvy = p->vy;
        if (clone && !ab_on(i, PW_CLONE)) continue;
        if (ball.immune && ball.owner == i) continue;
        if (ball.mode == BM_HEAVY && ball.owner != i && ball.plowed) continue;
        get_shape(i, clone, &s);
        {
            int ddx = ball.x - (s.x0 + s.x1) / 2, ddy = ball.y - (s.y1 - 24 * FP);
            if (iabs(ddx) > 60 * FP || iabs(ddy) > 64 * FP) continue;
        }
        {
            int keep_vx = ball.vx, keep_vy = ball.vy;
            hh = collide_circle(s.hx, s.hy, s.hr, pvx, pvy, 7);
            hb = collide_box(s.x0, s.y0, s.x1, s.y1, pvx, pvy, 4, 96);
            if (!hh && !hb) continue;
            hits++;
            if (knock_mode() && ball.owner != i && !clone) {
                knock(i, -p->dir);
                if (ball.mode == BM_HEAVY) {
                    ball.plowed = true;          /* the iron ball barely slows down */
                    ball.vx = keep_vx * 3 / 5;
                    ball.vy = keep_vy * 3 / 5 - FP;
                    continue;
                }
                ball.vx /= 2;
                ball.vy /= 2;
                end_mode();
                continue;
            }
        }
        if (ball.mode != BM_NONE && ball.mode != BM_HEAVY) end_mode();

        if (hh && !is_lying(p)) {
            ball.vx += p->dir * 96;
            if (ball.vx * p->dir < 2 * FP) ball.vx = p->dir * 2 * FP;
            if (!p->on_ground && p->vy < 0) ball.vy -= FP;
            if (!clone && ab_on(i, PW_ROCKET) && !p->on_ground) {
                /* rocket jump smash header, aimed at the goal mouth */
                long dx = (long)(goal_cx(p->dir) - PX(ball.x)), dy = (long)(150 - PX(ball.y));
                long len = (long)isqrt32((unsigned long)(dx * dx + dy * dy));
                if (len < 1) len = 1;
                ball.vx = (int)(dx * 12 * FP / len);
                ball.vy = (int)(dy * 12 * FP / len);
                ball.mode = BM_SMASH;
                ball.mode_t = 40;
                ball.owner = i;
                ball.immune = 6;
                p->ab_t = 0;
                M.hitstop = 4;
                shake(4);
                burst(PX(ball.x), PX(ball.y), C_FIRE, C_WHITE, 12, 48);
            }
            cap_ball_speed();
        }
        if (p->slide_t && !clone) {
            ball.vx = p->slide_dir * 8 * FP;
            ball.vy = -5 * FP;
        }
        touch(i, hh ? 4 : 3);
        if (p->charged_t && !clone) fire_super(i);
    }
    if (hits >= 2) *squeeze = true;
}

static void ball_mode_tick(int *grav)
{
    int bxp = PX(ball.x), byp = PX(ball.y);
    switch (ball.mode) {
    case BM_FIRE:
        *grav = 0;
        spawn(bxp - ball.vx / 160, byp - 3 + rnd_n(7), -ball.vx / 128, -4 + rnd_n(9), (uint8_t)(C_FIRE + rnd_n(4)), 10, 4, PK_DOT);
        spawn(bxp, byp, -ball.vx / 96, 0, C_FIRE + 3, 7, 3, PK_DOT);
        break;
    case BM_SMASH:
        spawn(bxp, byp, -ball.vx / 128, -ball.vy / 128, (uint8_t)(C_FIRE + rnd_n(4)), 9, 3, PK_DOT);
        break;
    case BM_HEAVY:
        *grav = GRAV_B * 3 / 2;
        if (ball.mode_t & 1) spawn(bxp, byp, -ball.vx / 128, -4, C_DKGRAY, 10, 3, PK_DOT);
        break;
    case BM_TELE:
        if (ball.mode_t & 1) spawn(bxp - 8 + rnd_n(17), byp - 8 + rnd_n(17), 0, -6, C_PURPLE, 8, 2, PK_STAR);
        break;
    default:
        break;
    }
    if (ball.mode != BM_NONE) {
        if (ball.mode_t) ball.mode_t--;
        if (ball.mode_t == 0) end_mode();
    }
}

static void update_ball(void)
{
    int grav = GRAV_B;
    uint8_t s, i;
    bool squeeze = false;

    if (ball.immune) ball.immune--;
    if (ball.squash) ball.squash--;
    ball_mode_tick(&grav);
    ball.vy += grav;
    /* MAGNET ability: pull the ball towards the owner's feet */
    for (i = 0; i < 2; i++) {
        if (ab_on(i, PW_MAGNET)) {
            int tx = pl[i].x + pl[i].dir * 14 * FP, ty = pl[i].y - 12 * FP;
            ball.vx += clampi((tx - ball.x) / 20, -56, 56);
            ball.vy += clampi((ty - ball.y) / 20, -56, 56) - GRAV_B / 2;
            if (ball.mode == BM_FIRE || ball.mode == BM_HEAVY) end_mode();
        }
    }
    cap_ball_speed();

    for (s = 0; s < 2; s++) {
        bool hitw;
        ball.x += ball.vx / 2;
        ball.y += ball.vy / 2;
        hitw = world_collide();
        if (hitw && ball.mode == BM_FIRE) {
            burst(PX(ball.x), PX(ball.y), C_FIRE, C_WHITE, 6, 32);
            end_mode();
        }
        ball_hits_players(&squeeze);
    }
    if (squeeze) {
        ball.vy = -9 * FP;
        ball.vx /= 2;
    }
    /* rolling friction on the flat ground */
    {
        int bxp = PX(ball.x);
        int g = ground_px(bxp) * FP;
        if (ball.y >= g - ball.r * FP - 2 && ball.vy == 0 && ball.mode != BM_FIRE) {
            ball.vx -= ball.vx / 12;
            if (iabs(ball.vx) < 8) ball.vx = 0;
        }
    }
    /* nothing may rest on the platform lip in front of a goal line */
    /* ... nor in the corner at the foot of a ramp */
    if (iabs(ball.vy) < 2 * FP && iabs(ball.vx) < 16) {
        int bxp = PX(ball.x);
        if (bxp <= RAMP_X + 8 && bxp > GOAL_LINE - BALL_R) ball.vx += 10;
        else if (bxp >= SCR_W - RAMP_X - 8 && bxp < SCR_W - GOAL_LINE + BALL_R) ball.vx -= 10;
    }
    /* nothing can rest on a crossbar */
    if (ball.y + ball.r * FP >= (BAR_TOP - 2) * FP && ball.y < BAR_TOP * FP) {
        if (ball.x < (BAR_LEN + 2) * FP) ball.vx += 12;
        else if (ball.x > (SCR_W - BAR_LEN - 2) * FP) ball.vx -= 12;
    }
    /* sparkle trail when the ball is struck hard */
    if (ball.mode == BM_NONE && (iabs(ball.vx) > 9 * FP || iabs(ball.vy) > 11 * FP) && (M.total_ticks & 1))
        spawn(PX(ball.x), PX(ball.y), -ball.vx / 160, -ball.vy / 160, (M.total_ticks & 2) ? C_WHITE : C_YELLOW, 9, 3, PK_STAR);
    ball.spin = (uint8_t)(ball.spin + ball.vx / 160);

    /* watchdog: if the ball has not really moved for 8 s, drop it again */
    if (iabs(ball.x - ball.anchor_x) > 24 * FP || iabs(ball.y - ball.anchor_y) > 24 * FP) {
        ball.anchor_x = ball.x;
        ball.anchor_y = ball.y;
        ball.idle_t = 0;
    } else if (++ball.idle_t > 8 * TICK_HZ) {
        TEST_HOOK(test_on_watchdog(PX(ball.x), PX(ball.y)));
        burst(PX(ball.x), PX(ball.y), C_WHITE, C_LTGRAY, 10, 32);
        drop_ball(160);
    }
    TEST_HOOK(test_ball_tick(PX(ball.x), PX(ball.y), ball.vx, ball.vy));
    CHK24(ball.x); CHK24(ball.y); CHK24(ball.vx); CHK24(ball.vy);
}

/* ------------------------------------------------------------------ */
/* AI                                                                  */
/* ------------------------------------------------------------------ */
static void predict_ball(void)
{
    int x = ball.x, y = ball.y, vx = ball.vx, vy = ball.vy;
    int r = ball.r * FP;
    bool straight = ball.mode == BM_FIRE;
    uint8_t t;
    pred_x[0] = PX(x);
    pred_y[0] = PX(y);
    for (t = 1; t <= PRED_N; t++) {
        int g;
        if (!straight) vy += ball.mode == BM_HEAVY ? GRAV_B * 3 / 2 : GRAV_B;
        x += vx;
        y += vy;
        if (x < r) { x = r; vx = -vx * 7 / 10; }
        if (x > SCR_W * FP - r) { x = SCR_W * FP - r; vx = -vx * 7 / 10; }
        g = ground_px(clampi(PX(x), 0, SCR_W)) * FP - r;
        if (y > g) {
            y = g;
            if (vy > 0) vy = -vy * 5 / 8;
            vx -= vx / 12;
        }
        pred_x[t] = PX(x);
        pred_y[t] = PX(y);
    }
}

/* should the AI fire its ability now? */
static bool ai_wants_super(uint8_t i, int rel, int from_goal, int me_ball, int op_ball, bool danger)
{
    player_t *me = &pl[i];
    int mx = PX(me->x), my = PX(me->y), bx = PX(ball.x), by = PX(ball.y);
    int dist_goal = iabs((me->dir > 0 ? SCR_W : 0) - bx);
    switch (power_of(i)) {
    case PW_FIRE:
    case PW_TELE:
    case PW_HEAVY: {
        int dx = bx - mx, dy = by - (my - 20);
        return dx * dx + dy * dy < 30 * 30 && rel >= -4;
    }
    case PW_FREEZE:
        return (me_ball < 36 && rel >= 0 && dist_goal < 200) || (op_ball < 30 && from_goal < 120);
    case PW_ROCKET:
        /* ball high above and near: launch and smash it down */
        return me->on_ground && iabs(bx - mx) < 30 && by < my - 70 && by > 30 && dist_goal < 230 && ball.vy > -2 * FP;
    case PW_GIANT:
        return (danger && from_goal < 150) || (me_ball < 40 && rel >= 0 && dist_goal < 170);
    case PW_MAGNET:
        return me_ball > 40 && me_ball < 170 && (op_ball < me_ball || rel < 0);
    case PW_CLONE:
        return (danger && from_goal < 170) || (me_ball < 40 && rel >= 0 && dist_goal < 160);
    case PW_WALL:
        return (danger && from_goal < 160) || (op_ball < 30 && from_goal < 110);
    default: /* PW_SPEED */
        return (me_ball > 70 && op_ball < me_ball) || (me_ball < 30 && rel >= 0 && dist_goal < 200);
    }
}

static void ai_control(uint8_t i, ctrl_t *c)
{
    player_t *me = &pl[i], *op = &pl[i ^ 1];
    ai_t *A = &ai[i];
    bool hard = M.ai_hard[i];
    int dir = me->dir, sc = scale_of(i);
    int mx = PX(me->x), my = PX(me->y);
    int bx = PX(ball.x), by = PX(ball.y);
    int ox = PX(op->x);
    int own_goal = dir > 0 ? 0 : SCR_W;
    int opp_goal = dir > 0 ? SCR_W : 0;
    int rel = (bx - mx) * dir;
    int shoot_range = hard ? 170 : 110;
    int bvx = ball.vx * dir;
    int op_ball = iabs(ox - bx);
    int me_ball = iabs(mx - bx);
    int from_goal = (bx - own_goal) * dir;
    bool danger = bvx < -4 * FP && from_goal < 170;
    int t;

    memset(c, 0, sizeof *c);
    if (me->frozen_t || is_lying(me) || M.phase != PH_PLAY) return;

    if (A->timer) {
        A->timer--;
    } else {
        int engage = hard ? 160 : 120;
        A->timer = M.ai_level[i];
        A->err = (int8_t)(hard ? rnd_n(5) - 2 : rnd_n(33) - 16);
        if (rel < -4 && bvx < -FP && from_goal < 120) {
            A->target = own_goal + dir * 22;                 /* beaten: back on the line */
        } else if (rel < -4) {
            int px = pred_x[8];
            A->target = ((px - own_goal) * dir < (bx - own_goal) * dir ? px : bx) - dir * 24;
        } else if (from_goal < engage || me_ball + 24 < op_ball ||
                   (iabs(ball.vx) < 3 * FP && me_ball <= op_ball + 12)) {
            if (by < my - 50) {
                int k, lx = pred_x[PRED_N];
                for (k = 1; k <= PRED_N; k++) {
                    if (pred_y[k] > my - 44) { lx = pred_x[k]; break; }
                }
                A->target = lx - dir * 8;
            } else {
                A->target = bx - dir * 6;
                if (rel >= 0 && rel < 18) A->target = bx + dir * 20;
            }
        } else {
            int dd = from_goal / 3;
            if (dd > 64) dd = 64;
            if (dd > from_goal - 22) dd = from_goal - 22;
            if (dd < 30) dd = 30;
            A->target = own_goal + dir * dd;
            if (danger) {
                int e1 = own_goal + dir * 30, e2 = own_goal + dir * dd;
                A->target = dir > 0 ? clampi(pred_x[6], e1, e2) : clampi(pred_x[6], e2, e1);
            }
        }
        A->target += A->err;
        A->target = clampi(A->target, MIN_X + 2, MAX_X - 2);
        if (me->on_ground && rel > 6 && rel < 40 && by > my - 22 && op_ball < 26 && (ox - mx) * dir > 0) {
            int od = (ox - mx) * dir;
            if (hard ? (od < 34 && rnd_n(100) < 45) : rnd_n(100) < 8) c->slide = true;
        }
    }
    {
        int d = A->target - mx;
        if (d > 3) c->right = true;
        else if (d < -3) c->left = true;
    }
    if (me->on_ground && rel < 0 && rel > -24 && by > my - 26 &&
        ((dir > 0 && c->left) || (dir < 0 && c->right))) {
        c->jump = true;
    }
    /* headers: will a jump started now meet the ball? */
    if (me->on_ground && !c->jump && (hard || (rnd() & 3) == 0)) {
        int hy0 = my - HEAD_DY * sc / 2;
        for (t = 3; t <= 14; t++) {
            int reach = 5 * t;
            int px = mx + clampi(pred_x[t] - mx, -reach, reach);
            int hy = hy0 - jump_h[t];
            int dxh = pred_x[t] - px, dyh = pred_y[t] - hy;
            if (iabs(dxh) <= 10 && dyh > -12 && dyh < 12 && pred_y[t] < my - 40) {
                c->jump = true;
                A->target = pred_x[t] - dir * 4;
                break;
            }
        }
    }
    if (me->kick_t == 0) {
        int dx = (bx - mx) * dir, dy = by - my;
        int dist_goal = iabs(opp_goal - bx);
        bool blocked = (ox - mx) * dir > 0 && (ox - mx) * dir < 50;
        if (me->on_ground) {
            if (dx >= 0 && dx <= 24 * sc / 2 && dy >= -30 * sc / 2 && dy <= 4) {
                if (dist_goal < shoot_range || (blocked && rnd_n(100) < 50) || rnd_n(100) < (hard ? 12 : 4))
                    c->shoot = true;
            }
        } else {
            int hx, hy, hdx, hdy;
            bool clearing = from_goal < 90;
            head_pos(i, &hx, &hy);
            hdx = bx - PX(hx);
            hdy = by - PX(hy);
            if ((hdx * hdx + hdy * hdy <= 19 * 19 || (dx >= -2 && dx <= 26 && dy >= -44 && dy <= 4)) &&
                (dist_goal < shoot_range || clearing) && (hard || rnd_n(100) < 55)) {
                c->shoot = true;
            }
        }
    }
    if (me->power >= POWER_MAX && me->charged_t == 0 && me->ab_t == 0 &&
        ai_wants_super(i, rel, from_goal, me_ball, op_ball, danger)) {
        c->super = true;
    }
}

/* ------------------------------------------------------------------ */
/* Match flow                                                          */
/* ------------------------------------------------------------------ */
static void wbump(uint8_t w);

static void place_players(void)
{
    uint8_t i;
    for (i = 0; i < 2; i++) {
        player_t *p = &pl[i];
        uint8_t keep = p->power;
        memset(p, 0, sizeof *p);
        p->power = keep;
        p->dir = (int8_t)(i == 0 ? 1 : -1);
        p->x = (i == 0 ? 96 : 224) * FP;
        p->y = G_MAIN * FP;
        p->on_ground = true;
        p->lie_dir = p->dir;
        ai[i].timer = 0;
        ai[i].target = PX(p->x);
    }
}

static void start_kickoff(int side)
{
    place_players();
    drop_ball(side < 0 ? 160 : (side == 0 ? 116 : 204));
    M.phase = PH_KICKOFF;
    M.phase_t = 24;
}

static void set_phase(uint8_t ph, int t)
{
    M.phase = ph;
    M.phase_t = t;
}

static void check_goal(void)
{
    int r = ball.r * FP;
    int8_t scorer = -1;
    if (ball.y - r > BAR_BOT * FP) {
        if (ball.x + r < GOAL_LINE * FP) scorer = 1;
        else if (ball.x - r > (SCR_W - GOAL_LINE) * FP) scorer = 0;
    }
    if (scorer < 0) return;
    if (M.score[scorer] < 99) M.score[scorer]++;
    M.last_scorer = (uint8_t)scorer;
    set_phase(PH_GOAL, 75);
    ball.vx = 0;
    ball.mode = BM_NONE;
    M.shake = 8;
    M.hitstop = 0;
    if (M.help_t) {                   /* a goal dismisses the start-of-match panels */
        M.help_t = 0;
        wbump(W_HELP0);
        wbump(W_HELP1);
    }
    pl[scorer].celebrate = true;
    burst(PX(ball.x), PX(ball.y), C_GOLD, C_WHITE, 16, 56);
    wver[W_SCORE]++;
    TEST_HOOK(test_on_goal((uint8_t)scorer, M.half, M.half_ticks));
}

static void match_tick(const ctrl_t *c0, const ctrl_t *c1)
{
    M.total_ticks++;
    if (M.shake) M.shake--;
    if (M.help_t) M.help_t--;
    if (M.hitstop) {
        M.hitstop--;
        update_particles();
        return;
    }
    switch (M.phase) {
    case PH_KICKOFF:
        if (--M.phase_t <= 0) {
            M.phase = PH_PLAY;
            ball.vx = (rnd_n(3) - 1) * 24;
        }
        update_player(0, c0);
        update_player(1, c1);
        break;
    case PH_PLAY:
        if (M.total_ticks & 1) {
            update_player(1, c1);
            update_player(0, c0);
        } else {
            update_player(0, c0);
            update_player(1, c1);
        }
        players_interact();
        do_kick((uint8_t)(M.total_ticks & 1));
        do_kick((uint8_t)((M.total_ticks & 1) ^ 1));
        update_ball();
        check_goal();
        if (M.phase == PH_PLAY) {
            M.half_ticks++;
            if (M.half < 2 && M.half_ticks >= HALF_TICKS) {
                if (M.half == 0) set_phase(PH_HALFTIME, 60);
                else if (M.score[0] != M.score[1]) set_phase(PH_END, 0);
                else set_phase(PH_GGINTRO, 60);
            }
        }
        break;
    case PH_GOAL: {
        ctrl_t cel;
        uint8_t s = M.last_scorer;
        memset(&cel, 0, sizeof cel);
        update_player(s, &cel);    /* arms up, feet down: stays under the banner */
        memset(&cel, 0, sizeof cel);
        update_player(s ^ 1, &cel);
        ball.vy += GRAV_B;
        ball.y += ball.vy;
        {
            int g = ground_px(PX(ball.x)) * FP - ball.r * FP;
            if (ball.y > g) { ball.y = g; ball.vy = -ball.vy / 3; }
        }
        if ((M.phase_t & 1) == 0) {
            const team_t *T = &TEAMS[M.team[s]];
            spawn(8 + rnd_n(304), 106, -8 + rnd_n(17), 4 + rnd_n(8),
                  (rnd() & 1) ? (M.kit_alt[s] ? T->alt_shirt : T->shirt) : C_GOLD, 50, 3, PK_GRAV);
        }
        if (--M.phase_t <= 0) {
            pl[s].celebrate = false;
            if (M.half == 2) set_phase(PH_END, 0);
            else start_kickoff(s ^ 1);
        }
        break;
    }
    case PH_HALFTIME:
        if (--M.phase_t <= 0) {
            M.half = 1;
            M.half_ticks = 0;
            start_kickoff(-1);
        }
        break;
    case PH_GGINTRO:
        if (--M.phase_t <= 0) {
            M.half = 2;
            M.half_ticks = 0;
            start_kickoff(-1);
        }
        break;
    default:
        break;
    }
    update_particles();
}

/* ------------------------------------------------------------------ */
/* HUD widgets                                                         */
/* ------------------------------------------------------------------ */
static void set_wrect(uint8_t w, int x0, int y0, int x1, int y1)
{
    wrect[w].x0 = (int16_t)x0; wrect[w].y0 = (int16_t)y0;
    wrect[w].x1 = (int16_t)x1; wrect[w].y1 = (int16_t)y1;
    TEST_HOOK(test_set_widget(w, x0, y0, x1, y1));
}

static void hud_layout(void)
{
    set_wrect(W_CLOCK, 120, 0, 200, 40);
    set_wrect(W_SCORE, 98, 212, 222, 240);
    set_wrect(W_ICON0, 1, 208, 94, 240);
    set_wrect(W_ICON1, 226, 208, 319, 240);
    set_wrect(W_HELP0, 1, 0, 125, 42);
    set_wrect(W_HELP1, 195, 0, 319, 42);
    set_wrect(W_OVL, 0, 0, 0, 0);
}

static void wbump(uint8_t w)
{
    wver[w]++;
    invalidate(wrect[w].x0, wrect[w].y0, wrect[w].x1, wrect[w].y1);
}

static uint8_t clock_secs(void)
{
    if (M.half >= 2) return 0;
    return (uint8_t)((HALF_TICKS - M.half_ticks + TICK_HZ - 1) / TICK_HZ);
}

static void draw_crest(int cx, int cy, uint8_t side)
{
    uint8_t shirt, trim, shorts;
    char s[2];
    kit_of(M.team[side], M.kit_alt[side], &shirt, &trim, &shorts);
    gfx_SetColor(C_GOLD);
    gfx_FillCircle(cx, cy, 9);
    gfx_SetColor(shirt);
    gfx_FillCircle(cx, cy, 7);
    fill(cx - 7, cy - 1, 15, 3, trim);
    gfx_SetColor((uint8_t)(shirt + 1));
    gfx_Circle(cx, cy, 7);
    s[0] = TEAMS[M.team[side]].code[0];
    s[1] = 0;
    txt(s, cx - 3, cy - 3, shorts == shirt ? C_BLACK : shorts);
}

static void draw_keycap(int x, int y, const char *label, int w)
{
    if (y < 0) return;
    fill(x, y + 1, w, 9, C_KEYSH);
    fill(x, y, w, 9, C_KEYCAP);
    if (label[0] == '^' || label[0] == 'v' || label[0] == '<' || label[0] == '>') {
        int cx = x + w / 2, cy = y + 4;
        gfx_SetColor(C_BLACK);
        if (label[0] == '^') gfx_FillTriangle(cx - 3, cy + 2, cx + 3, cy + 2, cx, cy - 3);
        if (label[0] == 'v') gfx_FillTriangle(cx - 3, cy - 2, cx + 3, cy - 2, cx, cy + 3);
        if (label[0] == '<') gfx_FillTriangle(cx + 2, cy - 3, cx + 2, cy + 3, cx - 3, cy);
        if (label[0] == '>') gfx_FillTriangle(cx - 2, cy - 3, cx - 2, cy + 3, cx + 3, cy);
    } else {
        txt(label, x + (w - txt_w(label, 1)) / 2, y + 1, C_BLACK);
    }
}

static void draw_help(uint8_t side, int x, int y)
{
    rpanel(x, y, 124, 40, C_PANEL, C_BORDER);
    if (!M.human[side]) {
        if (y + 8 >= 0) txt(side ? "P2: COMPUTER" : "P1: COMPUTER", x + 8, y + 8, C_WHITE);
        if (y + 22 >= 0) txt(M.ai_hard[side] ? "LEVEL: HARD" : "LEVEL: NORMAL", x + 8, y + 22, C_LTGRAY);
        return;
    }
    if (side == 0) {
        draw_keycap(x + 14, y + 4, "^", 10);
        if (y + 5 >= 0) txt("JUMP", x + 27, y + 5, C_LTGRAY);
        draw_keycap(x + 58, y + 4, "2ND", 26);
        if (y + 5 >= 0) txt("SHOT", x + 87, y + 5, C_WHITE);
        draw_keycap(x + 3, y + 16, "<", 10);
        draw_keycap(x + 14, y + 16, "v", 10);
        draw_keycap(x + 25, y + 16, ">", 10);
        draw_keycap(x + 50, y + 16, "MODE", 34);
        if (y + 17 >= 0) txt("SUP", x + 88, y + 17, C_GOLD);
        if (y + 29 >= 0) txt("v SLIDE", x + 8, y + 29, C_LTGRAY);
    } else {
        draw_keycap(x + 14, y + 4, "5", 10);
        if (y + 5 >= 0) txt("JUMP", x + 27, y + 5, C_LTGRAY);
        draw_keycap(x + 74, y + 4, "+", 10);
        if (y + 5 >= 0) txt("SHOT", x + 87, y + 5, C_WHITE);
        draw_keycap(x + 3, y + 16, "1", 10);
        draw_keycap(x + 14, y + 16, "2", 10);
        draw_keycap(x + 25, y + 16, "3", 10);
        draw_keycap(x + 74, y + 16, "-", 10);
        if (y + 17 >= 0) txt("SUP", x + 88, y + 17, C_GOLD);
        if (y + 29 >= 0) txt("2 SLIDE", x + 8, y + 29, C_LTGRAY);
    }
}

static void draw_icon(uint8_t side)
{
    player_t *p = &pl[side];
    uint8_t pw = power_of(side);
    int cx = side ? SCR_W - 17 : 16, cy = 225, k, n;
    bool ready = p->power >= POWER_MAX;
    uint8_t ringc = C_ORANGE, namec = C_LTGRAY;
    gfx_SetColor(C_BORDER);
    gfx_FillCircle(cx, cy, 14);
    gfx_SetColor(C_PANEL);
    gfx_FillCircle(cx, cy, 12);
    if (p->ab_t) {
        n = p->ab_t * RING_N / (pw == PW_GIANT ? GIANT_T : pw == PW_MAGNET ? MAGNET_T : pw == PW_ROCKET ? ROCKET_T :
                                pw == PW_SPEED ? SPEED_T : 120);
        ringc = C_CYAN;
        namec = C_CYAN;
    } else if (p->charged_t) {
        n = RING_N;
        ringc = C_GLOW;
        namec = C_GLOW;
    } else {
        n = ready ? RING_N : p->power * RING_N / POWER_MAX;
        if (ready) { ringc = C_GLOW; namec = C_GLOW; }
    }
    for (k = 0; k < RING_N; k++) {
        uint8_t c = k < n ? ringc : C_DKGRAY;
        gfx_vbuffer[cy + ring_y[k]][cx + ring_x[k]] = c;
        gfx_vbuffer[cy + ring_y[k] * 10 / 12][cx + ring_x[k] * 10 / 12] = c;
        gfx_vbuffer[cy + ring_y[k] * 11 / 12][cx + ring_x[k] * 11 / 12] = c;
    }
    /* ball icon, flaming when the ability is ready */
    if (ready || p->charged_t) {
        gfx_SetColor(C_FIRE + 2);
        gfx_FillTriangle(cx - 6, cy, cx + 6, cy, cx, cy - 11);
        gfx_SetColor(C_FIRE);
        gfx_FillTriangle(cx - 4, cy, cx + 4, cy, cx + 1, cy - 8);
    }
    gfx_SetColor(C_OUTLINE);
    gfx_FillCircle(cx, cy + 1, 6);
    gfx_SetColor(ready ? C_FIRE + 1 : C_WHITE);
    gfx_FillCircle(cx, cy + 1, 5);
    fill(cx - 1, cy, 3, 3, C_BLACK);
    fill(cx - 4, cy - 2, 2, 2, C_BLACK);
    fill(cx + 3, cy + 3, 2, 2, C_BLACK);
    if (M.human[side]) {
        int lx = cx + 6, ly = cy + 6;
        fill(lx, ly + 1, 10, 9, C_KEYSH);
        fill(lx, ly, 10, 9, C_KEYCAP);
        txt(side ? "-" : "M", lx + 1, ly + 1, C_BLACK);
    }
    /* ability name tag */
    {
        int bx = side ? SCR_W - 92 : 32;
        rpanel(bx, 216, 60, 16, C_PANEL, ready ? C_GOLD : C_BORDER);
        txt(POWER_SHORT[pw], bx + (60 - txt_w(POWER_SHORT[pw], 1)) / 2, 220, namec);
    }
}

static void draw_overlays(void);
static uint8_t ovl_key(void);
static uint8_t ovl_last = 0xFF;

static void draw_widget(uint8_t w)
{
    gfx_SetClipRegion(0, 0, SCR_W, SCR_H);
    switch (w) {
    case W_CLOCK: {
        char buf[4];
        uint8_t s = clock_secs();
        int remain = HALF_TICKS - M.half_ticks;
        uint8_t col = C_WHITE;
        const char *lbl = M.half == 0 ? "1ST HALF" : M.half == 1 ? "2ND HALF" : "GOLDEN";
        if (M.half < 2 && remain <= 5 * TICK_HZ && M.phase == PH_PLAY && ((M.half_ticks / 5) & 1)) col = C_RED;
        if (M.half >= 2) {
            buf[0] = 'G'; buf[1] = 'G'; buf[2] = 0;
            txt_outline(buf, 160 - txt_w(buf, 3) / 2, 4, C_GOLD, 3);
        } else {
            int tw = seg_number(0, 0, s, 14, 22, 4, col, C_PANEL, false);
            seg_number(160 - tw / 2, 4, s, 14, 22, 4, col, C_PANEL, true);
        }
        txt(lbl, 160 - txt_w(lbl, 1) / 2, 30, M.half == 2 ? C_GOLD : C_WHITE);
        break;
    }
    case W_SCORE: {
        char buf[8], *p;
        rpanel(98, 216, 28, 22, C_PANEL, C_BORDER);
        rpanel(194, 216, 28, 22, C_PANEL, C_BORDER);
        rpanel(122, 212, 76, 28, C_PANEL, C_BORDER);
        draw_crest(112, 227, 0);
        draw_crest(208, 227, 1);
        {
            int w0 = seg_number(0, 0, M.score[0], 9, 16, 3, C_WHITE, C_PANEL, false);
            seg_number(152 - w0, 218, M.score[0], 9, 16, 3, C_WHITE, C_PANEL, true);
            seg_number(168, 218, M.score[1], 9, 16, 3, C_WHITE, C_PANEL, true);
            fill(158, 221, 4, 3, C_WHITE);
            fill(158, 229, 4, 3, C_WHITE);
        }
        (void)buf; (void)p;
        break;
    }
    case W_ICON0:
    case W_ICON1:
        draw_icon((uint8_t)(w - W_ICON0));
        break;
    default: {
        if (w == W_OVL) {
            if (ovl_last == ovl_key()) draw_overlays();   /* only inside its own rect */
        }
        else if (M.help_t) draw_help((uint8_t)(w - W_HELP0), w == W_HELP0 ? 1 : 195, 1);
        break;
    }
    }
}


/* what the big message currently shows; a change redraws it */
static uint8_t ovl_key(void)
{
    switch (M.phase) {
    case PH_KICKOFF: return M.phase_t > 8 ? 1 : 2;
    case PH_GOAL: {
        int t = 75 - M.phase_t;
        return (uint8_t)(t < 8 ? 10 + t : 20);
    }
    case PH_HALFTIME: return 30;
    case PH_GGINTRO: return 31;
    default: return 0;
    }
}

/* big messages (READY, GOAL!, HALF TIME ...) are a widget too */
static void ovl_update(void)
{
    uint8_t k = ovl_key();
    if (k != ovl_last) {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        ovl_last = k;
        invalidate(wrect[W_OVL].x0, wrect[W_OVL].y0, wrect[W_OVL].x1, wrect[W_OVL].y1);
        switch (M.phase) {
        case PH_KICKOFF:  x0 = 98;  y0 = 64; x1 = 225; y1 = 95;  break;
        case PH_GOAL:     x0 = 0;   y0 = 50; x1 = SCR_W; y1 = 99;  break;
        case PH_HALFTIME: x0 = 50;  y0 = 56; x1 = 275; y1 = 110; break;
        case PH_GGINTRO:  x0 = 70;  y0 = 56; x1 = 254; y1 = 94;  break;
        default: break;
        }
        set_wrect(W_OVL, x0, y0, x1, y1);
        wbump(W_OVL);
    }
}

/* version numbers that change when a widget's content changes */
static void hud_update_versions(void)
{
    static uint8_t last_secs = 255, last_flash, last_icon[2], last_help;
    uint8_t s = clock_secs(), fl = 0, i;
    int remain = HALF_TICKS - M.half_ticks;
    if (M.half < 2 && remain <= 5 * TICK_HZ && M.phase == PH_PLAY) fl = (uint8_t)(1 + ((M.half_ticks / 5) & 1));
    if (s != last_secs || fl != last_flash) {
        last_secs = s;
        last_flash = fl;
        wbump(W_CLOCK);
    }
    for (i = 0; i < 2; i++) {
        player_t *p = &pl[i];
        uint8_t v;
        if (p->ab_t) v = (uint8_t)(0x80 | (p->ab_t / 8));
        else if (p->charged_t || p->power >= POWER_MAX) v = (uint8_t)(0x40 | (p->charged_t ? 2 : 0));
        else v = (uint8_t)(p->power * RING_N / POWER_MAX / 2);
        if (v != last_icon[i]) {
            last_icon[i] = v;
            wbump((uint8_t)(W_ICON0 + i));
        }
    }
    if ((M.help_t == 0) != (last_help == 0)) {
        wbump(W_HELP0);
        wbump(W_HELP1);
    }
    last_help = M.help_t;
    ovl_update();
}

static void hud_reset(void)
{
    uint8_t w;
    hud_layout();
    ovl_last = 0xFF;
    ovl_update();
    for (w = 0; w < W_COUNT; w++) {
        wver[w]++;
        wdrawn[0][w] = wdrawn[1][w] = (uint16_t)(wver[w] - 1);
    }
}

/* ------------------------------------------------------------------ */
/* Match rendering                                                     */
/* ------------------------------------------------------------------ */
static void draw_shadow(int x, int height, int w)
{
    int g = ground_px(clampi(x, 0, SCR_W)) + 1 + shake_y;
    int sw = w - height / 10;
    if (sw < 3) sw = 3;
    x += shake_x;
    ellipse(x, g, sw, 2, C_GRASS + 7);
    mark(x - sw - 1, g - 3, x + sw + 2, g + 3);
}

static void draw_player(uint8_t i, bool clone)
{
    player_t *p = &pl[i];
    int x, gy, sc = clone ? 2 : scale_of(i);
    uint8_t pose = POSE_STAND;
    int squash = 0, top, hw;
    if (clone) {
        int cx, cy;
        clone_pos(i, &cx, &cy);
        x = PX(cx);
        gy = PX(cy);
        cmap = map_ghost;
    } else {
        x = PX(p->x);
        gy = PX(p->y);
        cmap = p->frozen_t ? map_ice : NULL;
    }
    if (is_lying(p)) pose = POSE_LIE;
    else if (p->kick_t >= 4) pose = POSE_KICK;
    else if (p->celebrate) pose = POSE_CELEB;
    else if (!p->on_ground) pose = POSE_JUMP;
    else if (p->anim) pose = POSE_RUN;
    if (p->land_t) squash = 1;
    else if (!p->on_ground && p->vy < -6 * FP) squash = -1;
    x += shake_x;
    gy += shake_y;
    draw_char(x, gy, M.team[i], p->dir, M.kit_alt[i], pose, (uint8_t)(p->anim / 3), p->lie_dir, sc, squash);
    cmap = NULL;
    top = gy - (HEAD_DY + HEAD_R + 8) * sc / 2;
    hw = (pose == POSE_LIE || p->down_t) ? 34 : 19;    /* half width of what was drawn */
    if (clone) {
        mark(x - hw, top - 4, x + hw + 1, gy + 6);
        return;
    }
    if (p->frozen_t) {
        gfx_SetColor(C_ICE);
        gfx_Rectangle(x - 16, gy - 48, 32, 49);
        gfx_SetColor(C_WHITE);
        gfx_Rectangle(x - 15, gy - 47, 30, 47);
        gfx_Line(x - 10, gy - 40, x - 4, gy - 46);
        gfx_Line(x + 4, gy - 8, x + 12, gy - 16);
    }
    if (p->charged_t && (p->charged_t & 2)) {
        int hx, hy;
        head_pos(i, &hx, &hy);
        gfx_SetColor(POWER_COL[power_of(i)]);
        gfx_Circle(PX(hx) + shake_x, PX(hy) + shake_y, 16 * sc / 2);
    }
    if (ab_on(i, PW_GIANT) && (p->ab_t & 4)) {
        gfx_SetColor(C_GOLD);
        gfx_FillRectangle(x - 20 + (p->ab_t * 7) % 40, top + (p->ab_t * 3) % 30, 2, 2);
    }
    if (p->down_t && p->knocked) {
        uint8_t k, a = (uint8_t)(M.total_ticks & 31);
        int hx = x - p->lie_dir * LIE_HEAD_DX;
        gfx_SetColor(C_YELLOW);
        for (k = 0; k < 3; k++) {
            uint8_t ang = (uint8_t)((a + k * 11) & 31);
            gfx_FillRectangle(hx + COS32(ang) * 12 / 64 - 1, gy - 28 + SIN32[ang] * 4 / 64, 3, 3);
        }
    }
    {
        const char *lbl = M.human[i] ? (i == 0 ? "P1" : "P2") : "CPU";
        int ty = top - 4;
        if (p->popup_t) {
            const char *nm = POWER_SHORT[power_of(i)];
            int w = txt_w(nm, 1);
            int py = top - 14 - (36 - p->popup_t) / 3;
            if (py >= 1) {
                int tx = clampi(x - w / 2, 1, SCR_W - w - 2);
                txt_outline(nm, tx, py, POWER_COL[power_of(i)] == C_GHOST + 1 ? C_WHITE : POWER_COL[power_of(i)], 1);
                mark(tx - 2, py - 2, tx + w + 2, py + 10);
            }
        } else if (ty >= 1 && !is_lying(p) && (M.mode == MODE_FRIENDLY || M.help_t)) {
            int tx = clampi(x - txt_w(lbl, 1) / 2, 0, SCR_W - 25);
            txt(lbl, tx, ty, M.human[i] ? C_GOLD : C_LTGRAY);
            mark(tx, ty, tx + 25, ty + 8);
        }
    }
    mark(x - hw * sc / 2, top - 6, x + hw * sc / 2 + 1, gy + 6);
}

/* glowing barrier in front of the goal (drawn over the players) */
static void draw_wall(uint8_t i)
{
    int wx = (pl[i].dir > 0 ? 37 : SCR_W - 45) + shake_x;
    int y, top = BAR_BOT + shake_y, bot = G_PLAT + 6 + shake_y;
    fill(wx - 1, top - 1, 10, bot - top + 2, C_WHITE);
    for (y = top; y < bot; y += 6) {
        int h = y + 6 > bot ? bot - y : 5;
        fill(wx, y, 8, h, C_WALLFX);
        fill(wx + 1, y + 1, 3, 1, C_WHITE);
        fill(wx + 6, y, 2, h, C_ICE + 2);
    }
    mark(wx - 1, top - 1, wx + 9, bot + 1);
}

static void gen_ball_sprites(void)
{
    uint8_t f, k;
    for (f = 0; f < 8; f++) {
        gfx_sprite_t *spr = BALL_SPR(f);
        gfx_SetClipRegion(0, 0, BALL_SW, BALL_SW);
        fill(0, 0, BALL_SW, BALL_SW, C_TRANSP);
        ellipse(6, 6, BALL_R, BALL_R, C_OUTLINE);
        ellipse(6, 6, BALL_R - 1, BALL_R - 1, C_WHITE);
        for (k = 0; k < 3; k++) {
            uint8_t a = (uint8_t)((f * 4 + k * 11) & 31);
            fill(6 + COS32(a) * 4 / 64 - 1, 6 + SIN32[a] * 4 / 64 - 1, 2, 2, C_BLACK);
        }
        fill(4, 5, 3, 3, C_BLACK);
        fill(3, 2, 2, 1, C_WHITE);
        spr->width = BALL_SW;
        spr->height = BALL_SW;
        gfx_GetSprite(spr, 0, 0);
    }
    gfx_SetClipRegion(0, 0, SCR_W, SCR_H);
}

/* own filled ellipse is used for the ball so it can squash and stretch */
static void draw_ball(void)
{
    int x = PX(ball.x) + shake_x, y = PX(ball.y) + shake_y, r = ball.r;
    int rx = r, ry = r;
    uint8_t fillc = C_WHITE, spot = C_BLACK, outc = C_OUTLINE;
    uint8_t k;
    if (ball.squash) { rx = r + 1; ry = r - 2; y += 1; }
    else if (iabs(ball.vx) > 10 * FP) { rx = r + 1; ry = r - 1; }
    else if (iabs(ball.vy) > 11 * FP) { rx = r - 1; ry = r + 1; }
    switch (ball.mode) {
    case BM_FIRE: case BM_SMASH: fillc = C_FIRE + 1; spot = C_FIRE + 3; break;
    case BM_TELE: fillc = C_PINK; spot = C_PURPLE; break;
    case BM_HEAVY: fillc = C_GRAY; spot = C_DKGRAY; outc = C_BLACK; break;
    default: break;
    }
    if (ball.mode == BM_NONE && rx == r && ry == r) {
        gfx_TransparentSprite(BALL_SPR((ball.spin >> 5) & 7), x - 6, y - 6);
        mark(x - r - 12, y - r - 2, x + r + 13, y + r + 3);
        return;
    }
    if (ball.mode == BM_FIRE || ball.mode == BM_SMASH) {
        int d = ball.vx > 0 ? -1 : 1;
        gfx_SetColor(C_FIRE + 2);
        gfx_FillTriangle(x, y - r - 1, x, y + r + 1, x + d * (r + 10), y);
        gfx_SetColor(C_FIRE);
        gfx_FillTriangle(x, y - r + 2, x, y + r - 2, x + d * (r + 6), y);
    }
    ellipse(x, y, rx, ry, outc);
    ellipse(x, y, rx - 1, ry - 1, fillc);
    gfx_SetColor(spot);
    for (k = 0; k < 3; k++) {
        uint8_t a = (uint8_t)((ball.spin / 8 + k * 11) & 31);
        int sx = x + COS32(a) * (rx - 2) / 64, sy = y + SIN32[a] * (ry - 2) / 64;
        gfx_FillRectangle(sx - 1, sy - 1, 2, 2);
    }
    fill(x - 2, y - 1, 3, 3, spot);
    fill(x - rx / 2, y - ry / 2 - 1, 2, 1, C_WHITE);
    mark(x - r - 12, y - r - 2, x + r + 13, y + r + 3);
}

static void overlay_text(const char *s, int y, uint8_t c, uint8_t sc)
{
    int w = txt_w(s, sc);
    txt_outline(s, (SCR_W - w) / 2, y, c, sc);
}

static void draw_overlays(void)
{
    char buf[12], *p;
    switch (M.phase) {
    case PH_KICKOFF:
        if (M.phase_t > 8) overlay_text("READY", 66, C_WHITE, 3);
        else overlay_text("GO!", 66, C_GOLD, 3);
        break;
    case PH_GOAL: {
        const team_t *T = &TEAMS[M.team[M.last_scorer]];
        uint8_t col = M.kit_alt[M.last_scorer] ? T->alt_shirt : T->shirt;
        int t = 75 - M.phase_t;
        int bw = t < 8 ? t * 40 : SCR_W;            /* banner sweeps in */
        int bx = (SCR_W - bw) / 2;
        fill(bx, 50, bw, 3, C_GOLD);
        fill(bx, 53, bw, 3, (uint8_t)(col + 1));
        fill(bx, 56, bw, 39, col);
        fill(bx, 95, bw, 3, C_GOLD);
        if (t >= 6) {
            /* chunky segment letters: G O A L ! */
            static const uint8_t GOAL_SEG[4] = { 0x3D, 0x3F, 0x77, 0x38 };
            uint8_t lc = C_WHITE, k;
            int lx = 160 - (4 * 26 + 8) / 2;
            for (k = 0; k < 4; k++) seg_rects(lx + k * 26, 57, 20, 28, 6, GOAL_SEG[k], 2, C_PANEL);
            for (k = 0; k < 4; k++) seg_rects(lx + k * 26, 57, 20, 28, 6, GOAL_SEG[k], 0, lc);
            fill(lx + 104 - 2, 55, 10, 22, C_PANEL);
            fill(lx + 104 - 2, 79, 10, 10, C_PANEL);
            fill(lx + 104, 57, 6, 18, lc);
            fill(lx + 104, 81, 6, 6, lc);
            txt_outline(T->name, (SCR_W - txt_w(T->name, 1)) / 2, 89, C_WHITE, 1);
        }
        break;
    }
    case PH_HALFTIME:
        overlay_text("HALF TIME", 58, C_GOLD, 3);
        p = u2s(buf, M.score[0]);
        p = scat(p, " - ");
        u2s(p, M.score[1]);
        overlay_text(buf, 90, C_WHITE, 2);
        break;
    case PH_GGINTRO:
        overlay_text("GOLDEN GOAL", 58, C_GOLD, 2);
        overlay_text("NEXT GOAL WINS!", 82, C_WHITE, 1);
        break;
    default:
        break;
    }
}

static void render_match(void)
{
    uint8_t b = cur_buf, w, touched, i, n0;
    gfx_Wait();     /* we write pixels straight into the buffer below */
    shake_x = shake_y = 0;
    if (full_pending) {
        dirty_full[b] = true;
        full_pending--;
    }
    for (i = 0; i < 2; i++) {
        if (scale_of(i) == 3) {
            int8_t old = giant_key;
            cache_giant(M.team[i], pl[i].dir);
            if (giant_key != old) mark_to(b, 0, 0, HEAD_SW, HEAD_SH);   /* restored just below */
        }
    }
    TEST_HOOK(test_phase(0));
    touched = restore_dirty(b);
    TEST_HOOK(test_phase(1));
    TEST_HOOK(test_after_restore(0, full_pending));
    gfx_SetClipRegion(0, 0, SCR_W, SCR_H);
    n0 = ndirty[b];
    /* shadows first, then bodies, ball, effects */
    for (i = 0; i < 2; i++) {
        draw_shadow(PX(pl[i].x), ground_px(PX(pl[i].x)) - PX(pl[i].y), 9 * scale_of(i) / 2);
        if (ab_on(i, PW_CLONE)) {
            int cx, cy;
            clone_pos(i, &cx, &cy);
            draw_shadow(PX(cx), ground_px(PX(cx)) - PX(cy), 9);
        }
    }
    draw_shadow(PX(ball.x), ground_px(clampi(PX(ball.x), 0, SCR_W)) - PX(ball.y), 7);
    TEST_HOOK(test_phase(2));
    for (i = 0; i < 2; i++) if (ab_on(i, PW_CLONE)) draw_player(i, true);
    draw_player(0, false);
    draw_player(1, false);
    TEST_HOOK(test_phase(3));
    for (i = 0; i < 2; i++) if (ab_on(i, PW_WALL)) draw_wall(i);
    if (M.phase != PH_KICKOFF || (M.phase_t & 2) || M.phase_t < 12) draw_ball();
    TEST_HOOK(test_phase(4));
    draw_particles();
    TEST_HOOK(test_phase(5));
    TEST_HOOK(test_phase(6));
    /* HUD widgets: redraw when changed, restored over, or drawn over */
    for (w = 0; w < W_COUNT; w++) {
        bool need = (touched & (1 << w)) || wdrawn[b][w] != wver[w];
        if (!need) {
            uint8_t k;
            for (k = n0; k < ndirty[b]; k++) if (rect_hit(&dirty[b][k], &wrect[w])) { need = true; break; }
            if (dirty_full[b]) need = true;
        }
        if (need) {
            TEST_HOOK(test_wcost(0xFF));
            draw_widget(w);
            TEST_HOOK(test_wcost(w));
            wdrawn[b][w] = wver[w];
        }
    }
    TEST_HOOK(test_phase(7));
    TEST_HOOK(test_match_frame());
    gfx_SwapDraw();
    cur_buf ^= 1;
}

static void full_redraw_both(void)
{
    uint8_t k;
    shake_x = shake_y = 0;
    full_pending = 0;
    for (k = 0; k < 2; k++) {
        bg_full();
        ndirty[k] = 0;
        dirty_full[k] = false;
        gfx_SwapDraw();
    }
    cur_buf = 0;
    hud_reset();
    TEST_HOOK(test_bg_ready(0));
}

/* ------------------------------------------------------------------ */
/* Pause menu (drawn straight onto the visible screen)                 */
/* ------------------------------------------------------------------ */
enum { PAUSE_RESUME, PAUSE_RESTART, PAUSE_QUIT };

static uint8_t pause_menu(void)
{
    static const char *const items[3] = { "RESUME", "RESTART", "QUIT TO MENU" };
    uint8_t sel = 0, k;
    bool redraw = true;
    unsigned long t = 0;
    gfx_SetDrawScreen();
    gfx_SetClipRegion(0, 0, SCR_W, SCR_H);
    for (;;) {
        tick_wait();
        read_keys();
        anim_palette(t++, false);
        TEST_HOOK(test_screen(SCR_ID_PAUSE));
        if (KPRESS(7, kb_Up)) { sel = (uint8_t)(sel ? sel - 1 : 2); redraw = true; }
        if (KPRESS(7, kb_Down)) { sel = (uint8_t)(sel == 2 ? 0 : sel + 1); redraw = true; }
        if (key_back()) { sel = PAUSE_RESUME; break; }
        if (key_ok()) break;
        if (redraw) {
            panel(80, 66, 160, 108);
            txt_c("PAUSED", 76, C_GOLD, 2);
            for (k = 0; k < 3; k++) {
                int y = 106 + k * 20;
                fill(90, y - 4, 140, 16, k == sel ? C_SEL : C_PANEL);
                txt_c(items[k], y, k == sel ? C_WHITE : C_LTGRAY, 1);
            }
            redraw = false;
        }
    }
    gfx_SetDrawBuffer();
    return sel;
}

/* ------------------------------------------------------------------ */
/* Playing a match                                                     */
/* ------------------------------------------------------------------ */
enum { RES_DONE, RES_QUIT };

static void match_setup(uint8_t mode, uint8_t t0, uint8_t t1, bool h0, bool h1,
                        uint8_t diff, uint8_t ai_bonus)
{
    uint8_t i;
    memset(&M, 0, sizeof M);
    M.mode = mode;
    M.diff = diff;
    M.ai_bonus = ai_bonus;
    M.team[0] = t0;
    M.team[1] = t1;
    M.human[0] = h0;
    M.human[1] = h1;
    M.kit_alt[1] = TEAMS[t0].shirt == TEAMS[t1].shirt;
    M.help_t = 100;
    for (i = 0; i < 2; i++) {
        int lv;
        M.ai_hard[i] = diff != 0;
        TEST_HOOK(if (i == 0 && test_p1_diff() >= 0) M.ai_hard[0] = test_p1_diff() != 0);
        lv = M.ai_hard[i] ? 2 : 8;
        lv -= ai_bonus;
        M.ai_level[i] = (uint8_t)(lv < 1 ? 1 : lv);
        pl[i].power = 0;
    }
    clear_particles();
    cache_heads(t0, t1);
    start_kickoff(-1);
}

/* positions before the latest tick, for the in-between frame */
static int ip_x[3], ip_y[3];

static void ip_save(void)
{
    ip_x[0] = pl[0].x; ip_y[0] = pl[0].y;
    ip_x[1] = pl[1].x; ip_y[1] = pl[1].y;
    ip_x[2] = ball.x;  ip_y[2] = ball.y;
}

static void ip_mid(int *v, int prev)
{
    int d = *v - prev;
    if (d > -24 * FP && d < 24 * FP) *v = prev + d / 2;   /* jumps (resets, teleport) snap */
}

static void render_between(void)
{
    int sx0 = pl[0].x, sy0 = pl[0].y, sx1 = pl[1].x, sy1 = pl[1].y, bx = ball.x, by = ball.y;
    ip_mid(&pl[0].x, ip_x[0]); ip_mid(&pl[0].y, ip_y[0]);
    ip_mid(&pl[1].x, ip_x[1]); ip_mid(&pl[1].y, ip_y[1]);
    ip_mid(&ball.x, ip_x[2]);  ip_mid(&ball.y, ip_y[2]);
    TEST_HOOK(test_logic_frame());
    render_match();
    pl[0].x = sx0; pl[0].y = sy0; pl[1].x = sx1; pl[1].y = sy1; ball.x = bx; ball.y = by;
}

static uint8_t play_match(void)
{
    ctrl_t c[2], latch[2];
    uint8_t i, steps;
    bool mid_drawn = false;

    memset(latch, 0, sizeof latch);
    full_redraw_both();
    tick_reset();
    for (;;) {
        /* 60 FPS: half-way through each 30 Hz tick, draw the newest state;
           right after a tick, draw half-way between the last two states */
        while (!tick_due()) {
            if (!mid_drawn && (long)(clock() - (next_tick - TICK_CLK / 2)) >= 0) {
                mid_drawn = true;
#ifdef FBL_HOST_TEST
                if (!test_headless())
#endif
                render_match();
            }
        }
        steps = 0;
        do {
            read_keys();
            TEST_HOOK(test_screen(SCR_ID_MATCH));
            if (key_back()) {
                uint8_t r = pause_menu();
                if (r == PAUSE_QUIT) return RES_QUIT;
                if (r == PAUSE_RESTART) {
                    match_setup(M.mode, M.team[0], M.team[1], M.human[0], M.human[1], M.diff, M.ai_bonus);
                }
                full_redraw_both();
                tick_reset();
                break;
            }
            if (!M.human[0] || !M.human[1]) predict_ball();
            for (i = 0; i < 2; i++) {
                if (M.human[i]) human_ctrl(i, &c[i]);
                else ai_control(i, &c[i]);
                /* presses made during a hit-pause are kept until it ends */
                if (M.hitstop) {
                    latch[i].slide |= c[i].slide;
                    latch[i].shoot |= c[i].shoot;
                    latch[i].super |= c[i].super;
                } else {
                    c[i].slide |= latch[i].slide && !c[i].jump;
                    c[i].shoot |= latch[i].shoot;
                    c[i].super |= latch[i].super;
                    memset(&latch[i], 0, sizeof latch[i]);
                }
            }
            ip_save();
            match_tick(&c[0], &c[1]);
            anim_palette(M.total_ticks, M.phase == PH_GOAL);
            hud_update_versions();
            tick_advance();
            steps++;
            if (M.phase == PH_END) {
                render_match();
                TEST_HOOK(test_on_match_end(M.team[0], M.team[1], M.score[0], M.score[1], M.total_ticks));
                return RES_DONE;
            }
        } while (tick_due() && steps < 3);
        if (tick_due()) tick_reset();
        mid_drawn = false;
#ifdef FBL_HOST_TEST
        if (test_headless()) continue;
#endif
        render_between();
    }
}

/* ------------------------------------------------------------------ */
/* Save / load                                                         */
/* ------------------------------------------------------------------ */
static void save_defaults(void)
{
    memset(&save, 0, sizeof save);
    save.magic = SAVE_MAGIC;
    save.ver = SAVE_VER;
}

static void save_load(void)
{
    uint8_t h = ti_Open(SAVE_NAME, "r");
    save_defaults();
    if (h) {
        if (ti_Read(&save, sizeof save, 1, h) != 1 || save.magic != SAVE_MAGIC || save.ver != SAVE_VER)
            save_defaults();
        ti_Close(h);
    }
    if (save.diff > 1) save.diff = 0;
    if (save.tour.active && (save.tour.round > 2 || save.tour.team >= NUM_TEAMS)) save.tour.active = 0;
    TEST_HOOK(test_on_load(save.tour.active, save.tour.round, save.played));
}

static void save_store(bool archive)
{
    uint8_t h = ti_Open(SAVE_NAME, "w");
    if (!h) return;
    ti_Write(&save, sizeof save, 1, h);
    if (archive) ti_SetArchiveStatus(true, h);
    ti_Close(h);
}

static void record_result(uint8_t my, uint8_t their)
{
    save.played++;
    save.gf = (uint16_t)(save.gf + my);
    save.ga = (uint16_t)(save.ga + their);
    if (my > their) {
        save.won++;
        if (my - their > save.best_margin) save.best_margin = (uint8_t)(my - their);
        if (save.streak < 255) save.streak++;
        if (save.streak > save.best_streak) save.best_streak = save.streak;
    } else {
        save.lost++;
        save.streak = 0;
    }
}

/* ------------------------------------------------------------------ */
/* Menu helpers                                                        */
/* ------------------------------------------------------------------ */
static unsigned long menu_t;

static void menu_frame(uint8_t screen_id)
{
    tick_wait();
    read_keys();
    anim_palette(menu_t++, false);
    TEST_HOOK(test_screen(screen_id));
    (void)screen_id;
}

static void screen_bg(void)
{
    shake_x = shake_y = 0;
    gfx_SetClipRegion(0, 0, SCR_W, SCR_H);
    bg_full();
}

static void show(void)
{
    gfx_SwapDraw();
}

static void footer(const char *s)
{
    fill(0, 221, SCR_W, 19, C_PANEL);
    fill(0, 220, SCR_W, 1, C_BORDER);
    txt_c(s, 226, C_LTGRAY, 1);
}

/* closing "shutter" wipe on the visible screen between screens */
static void transition(void)
{
    uint8_t f;
    gfx_SetDrawScreen();
    gfx_SetClipRegion(0, 0, SCR_W, SCR_H);
    for (f = 1; f <= 6; f++) {
        int h = f * 20;
        tick_wait();
        fill(0, 0, SCR_W, h, C_PANEL);
        fill(0, SCR_H - h, SCR_W, h, C_PANEL);
        fill(0, h - 2, SCR_W, 2, C_BORDER);
        fill(0, SCR_H - h, SCR_W, 2, C_BORDER);
    }
    gfx_SetDrawBuffer();
}

/* simple vertical menu with a panel that slides in; -1 = Clear */
static int run_menu(bool title, const char *head, const char *const *items, uint8_t n, uint8_t sel,
                    uint8_t screen_id, const char *const *extra)
{
    bool redraw = true;
    uint8_t k, slide = 6;
    int ph = (int)n * 16 + 14, py = title ? 86 : 120 - ph / 2;
    for (;;) {
        menu_frame(slide ? 99 : screen_id);     /* 99: still sliding in (for the test harness) */
        if (!slide) {
            if (KPRESS(7, kb_Up)) { sel = (uint8_t)(sel ? sel - 1 : n - 1); redraw = true; }
            if (KPRESS(7, kb_Down)) { sel = (uint8_t)(sel + 1 >= n ? 0 : sel + 1); redraw = true; }
            if (key_back()) return -1;
            if (key_ok()) return sel;
            if (KPRESS(7, kb_Left) || KPRESS(7, kb_Right)) return 100 + sel;
        }
        if (!redraw && !slide) continue;
        redraw = false;
        screen_bg();
        if (title) {
            panel(42, 22, 236, 58);
            txt_outline("FOOTBALL", 160 - txt_w("FOOTBALL", 3) / 2, 28, C_GOLD, 3);
            txt_outline("LEGENDS CE", 160 - txt_w("LEGENDS CE", 2) / 2, 58, C_WHITE, 2);
            draw_char(40, G_MAIN, 0, 1, false, POSE_KICK, 0, 1, 2, 0);
            draw_char(280, G_MAIN, 7, -1, false, POSE_JUMP, 0, -1, 2, 0);
            ellipse(68, G_MAIN - 8, 6, 6, C_OUTLINE);
            ellipse(68, G_MAIN - 8, 5, 5, C_WHITE);
            fill(67, G_MAIN - 9, 3, 3, C_BLACK);
        } else if (head) {
            panel(40, py - 30, 240, 24);
            txt_c(head, py - 22, C_GOLD, 1);
        }
        {
            int oy = slide * 30;
            panel(64, py + oy, 192, ph);
            for (k = 0; k < n; k++) {
                int y = py + 9 + k * 16 + oy;
                const char *s = (extra && extra[k]) ? extra[k] : items[k];
                if (y > SCR_H - 10) continue;
                if (k == sel) fill(70, y - 3, 180, 14, C_SEL);
                txt_c(s, y, k == sel ? C_WHITE : C_LTGRAY, 1);
            }
        }
        footer("ARROWS:MOVE  2ND/ENTER:OK  CLEAR:BACK");
        show();
        if (slide) slide--;
    }
}

static void wait_ok(uint8_t screen_id)
{
    for (;;) {
        menu_frame(screen_id);
        if (key_ok() || key_back()) return;
    }
}

static void message_screen(const char *big, const char *line1, const char *line2, uint8_t col)
{
    screen_bg();
    panel(20, 60, 280, 120);
    txt_outline(big, 160 - txt_w(big, 3) / 2, 78, col, 3);
    if (line1) txt_c(line1, 118, C_WHITE, 1);
    if (line2) txt_c(line2, 134, C_LTGRAY, 1);
    txt_c("PRESS 2ND OR ENTER", 158, C_GOLD, 1);
    show();
    wait_ok(SCR_ID_MESSAGE);
}

/* ------------------------------------------------------------------ */
/* Team select                                                         */
/* ------------------------------------------------------------------ */
static int team_select(const char *who, int start)
{
    int sel = start;
    bool redraw = true;
    uint8_t k;
    for (;;) {
        menu_frame(SCR_ID_TEAMSEL);
        if (KPRESS(7, kb_Left)) { sel = (sel + NUM_TEAMS - 1) % NUM_TEAMS; redraw = true; }
        if (KPRESS(7, kb_Right)) { sel = (sel + 1) % NUM_TEAMS; redraw = true; }
        if (KPRESS(7, kb_Up) || KPRESS(7, kb_Down)) { sel = (sel + 5) % NUM_TEAMS; redraw = true; }
        if (key_back()) return -1;
        if (key_ok()) return sel;
        if (!redraw) continue;
        redraw = false;
        screen_bg();
        rpanel(0, 0, SCR_W, 221, C_PANEL, C_BORDER);
        {
            char buf[40], *p = scat(buf, who);
            scat(p, ": CHOOSE YOUR LEGEND");
            txt_c(buf, 6, C_GOLD, 1);
        }
        for (k = 0; k < NUM_TEAMS; k++) {
            int cx = 8 + (k % 5) * 61, cy = 18 + (k / 5) * 62;
            uint8_t shirt, trim, shorts;
            kit_of(k, false, &shirt, &trim, &shorts);
            rpanel(cx, cy, 59, 60, k == sel ? C_SEL : C_PANEL2, k == sel ? C_GOLD : C_BORDER);
            fill(cx + 18, cy + 36, 24, 10, shirt);
            fill(cx + 26, cy + 36, 8, 2, trim);
            draw_head(cx + 30, cy + 22, HEAD_R, HEAD_R, k, 1);
            txt(TEAMS[k].code, cx + 18, cy + 49, C_WHITE);
        }
        {
            const team_t *T = &TEAMS[sel];
            char buf[40], *p;
            uint8_t pc = POWER_COL[T->power];
            if (pc == C_GHOST + 1 || pc == C_GRAY) pc = C_ICE;
            draw_char(34, 196, (uint8_t)sel, 1, false, POSE_STAND, 0, 1, 2, 0);
            txt_outline(T->name, 70, 148, C_WHITE, 2);
            p = scat(buf, "LEGEND: ");
            scat(p, T->legend);
            txt(buf, 70, 168, C_LTGRAY);
            p = scat(buf, "ABILITY: ");
            scat(p, POWER_NAME[T->power]);
            txt(buf, 70, 182, pc);
            txt(POWER_DESC[T->power], 8, 204, C_WHITE);
        }
        footer("ARROWS:CHOOSE  2ND:OK  CLEAR:BACK");
        show();
    }
}

/* ------------------------------------------------------------------ */
/* Controls screen                                                     */
/* ------------------------------------------------------------------ */
static void controls_screen(void)
{
    screen_bg();
    rpanel(0, 0, SCR_W, 221, C_PANEL, C_BORDER);
    txt_c("CONTROLS", 6, C_GOLD, 2);
    M.human[0] = M.human[1] = true;
    M.help_t = 100;
    draw_help(0, 30, 30);
    draw_help(1, 166, 30);
    txt("PLAYER 1", 60, 74, C_WHITE);
    txt("PLAYER 2", 196, 74, C_WHITE);
    txt("JUMP while near the ball: header", 8, 100, C_LTGRAY);
    txt("SHOT on the ground: low drive", 8, 112, C_LTGRAY);
    txt("SHOT in the air: volley / header", 8, 124, C_LTGRAY);
    txt("Touch the ball to fill the ring.", 8, 140, C_LTGRAY);
    txt("Ring full: SUPER fires your", 8, 152, C_LTGRAY);
    txt("legend's ability.", 8, 164, C_LTGRAY);
    txt("CLEAR: pause.", 8, 180, C_LTGRAY);
    txt("P2 plays on the number keys:", 8, 192, C_LTGRAY);
    txt("1 3 run, 5 jump, 2 slide.", 8, 204, C_LTGRAY);
    footer("PRESS 2ND OR CLEAR");
    show();
    wait_ok(SCR_ID_CONTROLS);
}

/* ------------------------------------------------------------------ */
/* Bracket, results, records                                           */
/* ------------------------------------------------------------------ */
static void draw_bracket_line(int x, int y, uint8_t a, uint8_t b, const uint8_t *score, bool played, bool mine)
{
    char buf[16], *p;
    if (mine) fill(x - 2, y - 2, 92, 12, C_SEL);
    p = scat(buf, TEAMS[a].code);
    if (played) {
        *p++ = ' ';
        p = u2s(p, score[0]);
        *p++ = '-';
        p = u2s(p, score[1]);
        *p++ = ' ';
        *p = 0;
    } else {
        p = scat(p, " v ");
    }
    scat(p, TEAMS[b].code);
    txt(buf, x, y, C_WHITE);
}

static void tour_bracket_draw(void)
{
    tour_t *T = &save.tour;
    static const char *const rname[3] = { "QUARTER-FINAL", "SEMI-FINAL", "FINAL" };
    uint8_t k;
    char buf[40], *p;
    screen_bg();
    rpanel(0, 0, SCR_W, 221, C_PANEL, C_BORDER);
    p = scat(buf, "CUP: ");
    scat(p, rname[T->round]);
    txt_c(buf, 8, C_GOLD, 1);
    txt("QUARTERS", 6, 26, C_LTGRAY);
    txt("SEMIS", 118, 26, C_LTGRAY);
    txt("FINAL", 230, 26, C_LTGRAY);
    for (k = 0; k < 4; k++)
        draw_bracket_line(6, 46 + k * 34, T->slot[k * 2], T->slot[k * 2 + 1], T->qf[k], T->round > 0, k == 0 && T->round == 0);
    for (k = 0; k < 2; k++) {
        if (T->round >= 1)
            draw_bracket_line(118, 63 + k * 68, T->w_qf[k * 2], T->w_qf[k * 2 + 1], T->sf[k], T->round > 1, k == 0 && T->round == 1);
        else
            txt("--- v ---", 118, 63 + k * 68, C_GRAY);
    }
    if (T->round >= 2) draw_bracket_line(230, 97, T->w_sf[0], T->w_sf[1], T->fn, false, true);
    else txt("--- v ---", 230, 97, C_GRAY);
    gfx_SetColor(C_GOLD);
    for (k = 0; k < 2; k++) {
        gfx_VertLine(102, 50 + k * 68, 35);
        gfx_HorizLine(102, 67 + k * 68, 12);
    }
    gfx_VertLine(214, 67, 69);
    gfx_HorizLine(214, 101, 12);
    p = scat(buf, "YOU: ");
    p = scat(p, TEAMS[T->team].name);
    p = scat(p, " - ");
    scat(p, POWER_NAME[TEAMS[T->team].power]);
    txt_c(buf, 192, C_WHITE, 1);
    footer("2ND:PLAY  CLEAR:MENU  DEL:QUIT CUP");
    show();
}

static void results_screen(const char *headline, uint8_t hcol)
{
    char buf[40], *p;
    uint8_t k;
    screen_bg();
    panel(8, 22, 304, 196);
    txt_c("FULL TIME", 30, C_LTGRAY, 1);
    txt_outline(headline, 160 - txt_w(headline, 2) / 2, 44, hcol, 2);
    for (k = 0; k < 2; k++) {
        int x = k ? 254 : 66;
        bool win = M.score[k] > M.score[k ^ 1];
        draw_char(x, 140, M.team[k], k ? -1 : 1, M.kit_alt[k], win ? POSE_CELEB : POSE_STAND, 0, 1, 2, 0);
        txt(TEAMS[M.team[k]].code, x - 12, 146, C_WHITE);
    }
    p = u2s(buf, M.score[0]);
    p = scat(p, ":");
    u2s(p, M.score[1]);
    txt_outline(buf, 160 - txt_w(buf, 4) / 2, 86, C_WHITE, 4);
    if (M.half == 2) txt_c("GOLDEN GOAL", 126, C_GOLD, 1);
    p = scat(buf, "TOUCHES ");
    p = u2s(p, M.touches[0]);
    p = scat(p, " - ");
    u2s(p, M.touches[1]);
    txt_c(buf, 166, C_LTGRAY, 1);
    p = scat(buf, "SHOTS ");
    p = u2s(p, M.shots[0]);
    p = scat(p, " - ");
    p = u2s(p, M.shots[1]);
    p = scat(p, "   SUPERS ");
    p = u2s(p, M.supers[0]);
    p = scat(p, " - ");
    u2s(p, M.supers[1]);
    txt_c(buf, 180, C_LTGRAY, 1);
    txt_c("PRESS 2ND OR ENTER", 200, C_GOLD, 1);
    show();
    wait_ok(SCR_ID_RESULTS);
}

static void stats_screen(void)
{
    char buf[40], *p;
    screen_bg();
    panel(20, 18, 280, 200);
    txt_c("RECORDS", 28, C_GOLD, 2);
#define STAT_LINE(label, val, y) \
    p = scat(buf, label); u2s(p, (unsigned)(val)); txt(buf, 48, y, C_WHITE)
    STAT_LINE("MATCHES PLAYED  ", save.played, 56);
    STAT_LINE("WON             ", save.won, 70);
    STAT_LINE("LOST            ", save.lost, 84);
    STAT_LINE("GOALS FOR       ", save.gf, 98);
    STAT_LINE("GOALS AGAINST   ", save.ga, 112);
    STAT_LINE("BIGGEST WIN BY  ", save.best_margin, 126);
    STAT_LINE("BEST WIN STREAK ", save.best_streak, 140);
    STAT_LINE("CUPS ENTERED    ", save.cups_played, 154);
    STAT_LINE("CUPS WON        ", save.cups, 168);
#undef STAT_LINE
    txt_c("2-PLAYER MATCHES ARE NOT COUNTED", 186, C_LTGRAY, 1);
    txt_c("PRESS 2ND OR CLEAR", 202, C_GOLD, 1);
    show();
    wait_ok(SCR_ID_STATS);
}

/* ------------------------------------------------------------------ */
/* Modes                                                               */
/* ------------------------------------------------------------------ */
static int last_team, last_team2 = 1;

static void quick_match(void)
{
    int t = team_select("P1", last_team), o;
    if (t < 0) return;
    last_team = t;
    do {
        o = rnd_n(NUM_TEAMS);
    } while (o == t);
    TEST_HOOK(test_force_teams(&t, &o));
    transition();
    match_setup(MODE_QUICK, (uint8_t)t, (uint8_t)o, !TEST_P1_AI, false, save.diff, 0);
    if (play_match() == RES_QUIT) return;
    record_result(M.score[0], M.score[1]);
    save_store(false);
    results_screen(M.score[0] > M.score[1] ? "YOU WIN!" : "YOU LOSE", M.score[0] > M.score[1] ? C_GOLD : C_RED);
}

/* best_of = 1 (single friendly), 3 or 5 */
static void friendly(uint8_t best_of)
{
    int t0, t1;
    uint8_t wins[2] = { 0, 0 }, need = (uint8_t)(best_of / 2 + 1);
    t0 = team_select("P1", last_team);
    if (t0 < 0) return;
    last_team = t0;
    t1 = team_select("P2", last_team2);
    if (t1 < 0) return;
    last_team2 = t1;
    for (;;) {
        char buf[40], *p;
        transition();
        match_setup(MODE_FRIENDLY, (uint8_t)t0, (uint8_t)t1, !TEST_P1_AI, !TEST_P1_AI, save.diff, 0);
        if (play_match() == RES_QUIT) return;
        wins[M.score[0] > M.score[1] ? 0 : 1]++;
        TEST_HOOK(test_on_series(wins[0], wins[1], need));
        if (best_of == 1) {
            results_screen(M.score[0] > M.score[1] ? "P1 WINS!" : "P2 WINS!", C_GOLD);
            return;
        }
        results_screen(M.score[0] > M.score[1] ? "P1 TAKES IT" : "P2 TAKES IT", C_GOLD);
        p = scat(buf, "SERIES  P1 ");
        p = u2s(p, wins[0]);
        p = scat(p, " - ");
        p = u2s(p, wins[1]);
        scat(p, " P2");
        if (wins[0] >= need || wins[1] >= need) {
            message_screen(wins[0] > wins[1] ? "P1 WINS" : "P2 WINS", buf, "THE SERIES IS OVER", C_GOLD);
            return;
        }
        message_screen("NEXT MATCH", buf, best_of == 3 ? "FIRST TO 2 WINS" : "FIRST TO 3 WINS", C_WHITE);
    }
}

static void two_player_menu(void)
{
    static const char *const items[5] = {
        "QUICK FRIENDLY", "BEST OF 3", "BEST OF 5", "P1 VS CPU", "BACK"
    };
    int r = run_menu(false, "2 PLAYERS ON ONE CALCULATOR", items, 5, 0, SCR_ID_TWOP, 0);
    if (r == 0) friendly(1);
    else if (r == 1) friendly(3);
    else if (r == 2) friendly(5);
    else if (r == 3) quick_match();
}

static void sim_score(uint8_t *s)
{
    s[0] = (uint8_t)rnd_n(5);
    s[1] = (uint8_t)rnd_n(5);
    if (s[0] == s[1]) s[rnd() & 1]++;
}

static uint8_t winner_of(uint8_t a, uint8_t b, const uint8_t *s)
{
    return s[0] > s[1] ? a : b;
}

static void tour_new(uint8_t team)
{
    tour_t *T = &save.tour;
    uint8_t pool[NUM_TEAMS], n = 0, k;
    memset(T, 0, sizeof *T);
    T->active = 1;
    T->team = team;
    T->diff = save.diff;
    T->slot[0] = team;
    for (k = 0; k < NUM_TEAMS; k++) if (k != team) pool[n++] = k;
    for (k = n - 1; k > 0; k--) {           /* shuffle, take 7 */
        uint8_t j = (uint8_t)rnd_n(k + 1), tmp = pool[k];
        pool[k] = pool[j];
        pool[j] = tmp;
    }
    for (k = 1; k < 8; k++) T->slot[k] = pool[k - 1];
    if (save.cups_played < 65535u) save.cups_played++;
}

static void tournament(void)
{
    tour_t *T = &save.tour;
    if (!T->active) {
        int t = team_select("P1", last_team);
        if (t < 0) return;
        last_team = t;
        tour_new((uint8_t)t);
        save_store(false);
    }
    for (;;) {
        uint8_t opp, k, r;
        tour_bracket_draw();
        for (;;) {
            menu_frame(SCR_ID_BRACKET);
            if (key_back()) return;
            if (KPRESS(1, kb_Del)) {
                T->active = 0;
                save_store(false);
                return;
            }
            if (key_ok()) break;
        }
        opp = T->round == 0 ? T->slot[1] : (T->round == 1 ? T->w_qf[1] : T->w_sf[1]);
        transition();
        match_setup(MODE_TOUR, T->team, opp, !TEST_P1_AI, false, T->diff, T->round);
        r = play_match();
        if (r == RES_QUIT) continue;
        record_result(M.score[0], M.score[1]);
        if (T->round == 0) {
            T->qf[0][0] = M.score[0];
            T->qf[0][1] = M.score[1];
            for (k = 1; k < 4; k++) sim_score(T->qf[k]);
            for (k = 0; k < 4; k++) T->w_qf[k] = winner_of(T->slot[k * 2], T->slot[k * 2 + 1], T->qf[k]);
        } else if (T->round == 1) {
            T->sf[0][0] = M.score[0];
            T->sf[0][1] = M.score[1];
            sim_score(T->sf[1]);
            for (k = 0; k < 2; k++) T->w_sf[k] = winner_of(T->w_qf[k * 2], T->w_qf[k * 2 + 1], T->sf[k]);
        } else {
            T->fn[0] = M.score[0];
            T->fn[1] = M.score[1];
        }
        if (M.score[0] < M.score[1]) {
            T->active = 0;
            save_store(false);
            TEST_HOOK(test_on_tour_end(T->round, false));
            results_screen("KNOCKED OUT", C_RED);
            message_screen("GAME OVER", "Your cup run is over.", "Try again from the menu!", C_RED);
            return;
        }
        if (T->round == 2) {
            T->active = 0;
            if (save.cups < 65535u) save.cups++;
            save_store(false);
            TEST_HOOK(test_on_tour_end(3, true));
            results_screen("CHAMPIONS!", C_GOLD);
            message_screen("CHAMPIONS!", TEAMS[T->team].name, "You won the CE Cup!", C_GOLD);
            return;
        }
        T->round++;
        save_store(false);
        results_screen("YOU WIN!", C_GOLD);
    }
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */
int main(void)
{
    static const char *const items[7] = {
        "QUICK MATCH", "TOURNAMENT", "2 PLAYERS", "DIFFICULTY", "CONTROLS", "RECORDS", "QUIT"
    };
    static const char *extra[7];
    static uint8_t sel;

    rng_s ^= (uint32_t)clock() * 2654435761UL;
    if (!rng_s) rng_s = 1;
    sel = 0;
    memset(panic_ring, 0, sizeof panic_ring);
    panic_pos = 0;

    gfx_Begin();
    build_palette();
    gfx_SetPalette(pal, sizeof pal, 0);
    gfx_SetDrawBuffer();
    gfx_SetTransparentColor(C_TRANSP);
    gfx_SetTextTransparentColor(C_TRANSP);
    gfx_SetTextBGColor(C_TRANSP);
    gfx_SetMonospaceFont(8);
    gfx_SetClipRegion(0, 0, SCR_W, SCR_H);

    /* panic exit lands here: leave at once, save nothing */
    if (setjmp(panic_env)) {
        gfx_End();
        TEST_HOOK(test_on_panic());
        return 0;
    }

    gen_assets();
    gen_ball_sprites();
    hud_layout();
    save_load();
    read_keys();
    read_keys();   /* ignore keys still held from launching */
    tick_reset();

    for (;;) {
        int r;
        extra[1] = save.tour.active ? "CONTINUE CUP" : 0;
        extra[3] = save.diff ? "DIFFICULTY: HARD" : "DIFFICULTY: NORMAL";
        r = run_menu(true, 0, items, 7, sel, SCR_ID_TITLE, extra);
        if (r < 0) break;
        if (r >= 100) {
            sel = (uint8_t)(r - 100);
            if (sel == 3) {
                save.diff ^= 1;
                save_store(false);
            }
            continue;
        }
        sel = (uint8_t)r;
        if (sel == 0) quick_match();
        else if (sel == 1) tournament();
        else if (sel == 2) two_player_menu();
        else if (sel == 3) { save.diff ^= 1; save_store(false); }
        else if (sel == 4) controls_screen();
        else if (sel == 5) stats_screen();
        else break;
    }

    gfx_End();
    save_store(true);
    return 0;
}
