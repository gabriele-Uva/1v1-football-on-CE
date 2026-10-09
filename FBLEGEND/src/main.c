/*
 * Football Legends CE
 * A 2D side-view "head soccer" game for the TI-84 Plus CE.
 * Built with the CE C/C++ Toolchain (CEdev): graphx, keypadc, fileioc.
 *
 * Engine notes
 *  - Fixed 30 Hz timestep driven by clock() (32768 Hz hardware timer).
 *  - All physics in integer fixed point: 1 pixel = 64 units.
 *    int is 24 bits on the eZ80, so every product that can get big is
 *    done in long (32 bits).
 *  - Double buffered. The static stadium is drawn once into both buffers;
 *    each frame only the rectangles that moving objects covered two frames
 *    ago (in the same buffer) are restored, by redrawing the background
 *    inside them (rectangles, horizontal/vertical lines and pre-captured
 *    sprites only, so a clipped redraw is pixel-identical to the full one).
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
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
#define HUD_H 26
#define GROUND 214          /* feet of players / bottom of ball */
#define BAR_TOP 152         /* crossbar top */
#define BAR_BOT 158         /* crossbar bottom */
#define BAR_LEN 32          /* crossbar spans x 0..32 and 288..320 */
#define GOAL_LINE 30        /* ball wholly left of x=30 (or right of 290) = goal */
#define CROWD_Y 48
#define CROWD_W 32
#define CROWD_H 24
#define CROWD_TIERS 3
#define WALL_Y 120
#define AD_Y 126
#define AD_W 64
#define AD_H 14
#define GRASS_Y 140

/* ------------------------------------------------------------------ */
/* Physics constants (fixed point, 64 units = 1 px, per 1/30 s tick)   */
/* ------------------------------------------------------------------ */
#define FP 64
#define PX(v) ((v) >> 6)
#define TICK_HZ 30
#define HALF_TICKS (30 * TICK_HZ)

#define BALL_R 6
#define MEGA_R 12
#define HEAD_R 11
#define BODY_HW 7
#define BODY_H 23
#define HEAD_DY 33
#define LIE_HW 13
#define LIE_H 10
#define LIE_HEAD_DX 17

#define GRAV_B 36           /* 0.56 px/tick^2 */
#define GRAV_P 64           /* 1.00 px/tick^2 */
#define JUMP_V 672          /* 10.5 px/tick -> ~55 px jump */
#define RUN_V 320           /* 5 px/tick */
#define RUN_ACC 112
#define AIR_ACC 64
#define SLIDE_V 512
#define SLIDE_T 12
#define RECOVER_T 8
#define KNOCK_T 30
#define FREEZE_T 45         /* 1.5 s */
#define KICK_T 9
#define KICK_ACTIVE 5       /* kick connects while kick_t >= this */
#define CHARGE_T 90
#define POWER_MAX 100
#define MAXV 1152           /* 18 px/tick */
#define SHOT_V 576          /* 9 px/tick */
#define VOLLEY_V 544        /* 8.5 px/tick */
#define HEADER_V 576        /* 9 px/tick */
#define MAXFALL 1024
#define BOUNCE_MIN 77

#define MIN_X 12
#define MAX_X 308
#define GOAL_ZONE 42        /* centre x < 42 means the head is under the crossbar */

/* ------------------------------------------------------------------ */
/* Palette                                                             */
/* ------------------------------------------------------------------ */
enum {
    C_BLACK, C_WHITE, C_SKY1, C_SKY2, C_SKY3, C_LAMP, C_ROOF, C_STAND1,
    C_STAND2, C_WALL, C_GRASS1, C_GRASS2, C_SHADOW, C_LINE, C_NET, C_POST,
    C_HUD, C_HUD2, C_GOLD, C_RED, C_ORANGE, C_YELLOW, C_CYAN, C_ICE,
    C_PURPLE, C_PINK, C_GRAY, C_DKGRAY, C_LTGRAY, C_SKIN1, C_SKIN2, C_SKIN3,
    C_SKIN4, C_HAIRBK, C_HAIRBR, C_HAIRBL, C_HAIRGI, C_KYEL, C_KGRN, C_KBLUE,
    C_KSKY, C_KNAVY, C_KRED, C_KMAR, C_KAZU, C_KORA, C_KDGRN, C_PANEL,
    C_PANEL2, C_SEL, C_DKRED, C_FIRE2, C_TRANSP, C_NUM
};

#define RGB(r, g, b) gfx_RGBTo1555(r, g, b)
static const uint16_t palette[C_NUM] = {
    RGB(0, 0, 0), RGB(255, 255, 255), RGB(10, 16, 48), RGB(20, 32, 82),
    RGB(38, 56, 118), RGB(255, 250, 200), RGB(92, 98, 112), RGB(62, 66, 84),
    RGB(46, 50, 66), RGB(30, 34, 48), RGB(56, 156, 56), RGB(44, 134, 44),
    RGB(26, 92, 30), RGB(236, 246, 236), RGB(200, 206, 218), RGB(250, 250, 250),
    RGB(14, 20, 44), RGB(32, 44, 88), RGB(255, 200, 40), RGB(232, 40, 40),
    RGB(255, 140, 20), RGB(255, 236, 64), RGB(80, 220, 255), RGB(176, 232, 255),
    RGB(172, 72, 232), RGB(255, 112, 200), RGB(128, 128, 128), RGB(62, 62, 62),
    RGB(192, 192, 192), RGB(255, 214, 172), RGB(226, 170, 120), RGB(172, 112, 72),
    RGB(112, 68, 40), RGB(26, 20, 20), RGB(104, 62, 30), RGB(242, 210, 100),
    RGB(214, 100, 40), RGB(255, 220, 0), RGB(0, 152, 64), RGB(32, 72, 204),
    RGB(122, 192, 242), RGB(20, 30, 92), RGB(212, 22, 32), RGB(132, 20, 42),
    RGB(22, 112, 212), RGB(255, 122, 0), RGB(0, 92, 42), RGB(16, 24, 56),
    RGB(40, 56, 112), RGB(64, 92, 172), RGB(120, 10, 10), RGB(255, 90, 0),
    RGB(255, 0, 255)
};

/* ------------------------------------------------------------------ */
/* Teams and legends                                                   */
/* ------------------------------------------------------------------ */
enum { PAT_COLLAR, PAT_STRIPES, PAT_BAND, PAT_SASH };
enum { HS_BUZZ, HS_LONG, HS_SLICK, HS_CURLY, HS_BALD, HS_SPIKY, HS_MOHAWK, HS_AFRO };
enum { PW_FIRE, PW_TELE, PW_FREEZE, PW_ROCKET, PW_ZAP, PW_METEOR, PW_TORNADO, PW_MEGA };

typedef struct {
    const char *name;
    const char *code;
    const char *legend;
    uint8_t shirt, trim, shorts, socks, pattern;
    uint8_t alt_shirt, alt_trim, alt_shorts;
    uint8_t skin, hair, style, beard;
    uint8_t power;
} team_t;

#define NUM_TEAMS 8
static const team_t TEAMS[NUM_TEAMS] = {
    { "BRAZIL", "BRA", "FERA", C_KYEL, C_KGRN, C_KBLUE, C_WHITE, PAT_COLLAR,
      C_KBLUE, C_KYEL, C_WHITE, C_SKIN3, C_HAIRBK, HS_BUZZ, 0, PW_FIRE },
    { "ARGENTINA", "ARG", "EL PIBE", C_KSKY, C_WHITE, C_BLACK, C_WHITE, PAT_STRIPES,
      C_KNAVY, C_KSKY, C_KNAVY, C_SKIN2, C_HAIRBR, HS_LONG, 0, PW_TELE },
    { "GERMANY", "GER", "KAISER", C_WHITE, C_BLACK, C_BLACK, C_WHITE, PAT_BAND,
      C_KDGRN, C_WHITE, C_WHITE, C_SKIN1, C_HAIRBL, HS_SLICK, 0, PW_FREEZE },
    { "NETHERLANDS", "NED", "ROCKY", C_KORA, C_BLACK, C_BLACK, C_KORA, PAT_COLLAR,
      C_KNAVY, C_KORA, C_KNAVY, C_SKIN1, C_HAIRGI, HS_CURLY, 1, PW_ROCKET },
    { "FRANCE", "FRA", "LE ROI", C_KNAVY, C_KRED, C_WHITE, C_KRED, PAT_BAND,
      C_WHITE, C_KNAVY, C_KNAVY, C_SKIN4, C_HAIRBK, HS_BALD, 0, PW_ZAP },
    { "SPAIN", "ESP", "MATADOR", C_KRED, C_KYEL, C_KNAVY, C_KNAVY, PAT_COLLAR,
      C_WHITE, C_KRED, C_WHITE, C_SKIN2, C_HAIRBK, HS_SPIKY, 1, PW_METEOR },
    { "PORTUGAL", "POR", "CICLONE", C_KMAR, C_KGRN, C_KGRN, C_KMAR, PAT_SASH,
      C_WHITE, C_KMAR, C_WHITE, C_SKIN2, C_HAIRBK, HS_MOHAWK, 0, PW_TORNADO },
    { "ITALY", "ITA", "MURO", C_KAZU, C_WHITE, C_WHITE, C_KAZU, PAT_COLLAR,
      C_WHITE, C_KAZU, C_KAZU, C_SKIN1, C_HAIRBK, HS_AFRO, 1, PW_MEGA },
};

static const char *const POWER_NAME[8] = {
    "FIREBALL", "TELEPORT", "FREEZE", "ROCKET",
    "LIGHTNING", "METEOR", "TORNADO", "MEGA BALL"
};
static const char *const POWER_DESC[8] = {
    "A blazing shot, dead straight at goal",
    "Ball warps to the rival's goal mouth",
    "Rival frozen solid for 1.5 seconds",
    "Fast, high-arc rocket shot",
    "Zig-zag thunder strike at goal",
    "Ball soars up, then crashes down",
    "Wavy twister that floors defenders",
    "Giant ball that bowls over keepers"
};
static const uint8_t POWER_COL[8] = {
    C_ORANGE, C_PURPLE, C_CYAN, C_LTGRAY, C_YELLOW, C_RED, C_WHITE, C_GOLD
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
    int8_t lie_dir;         /* feet point this way while lying */
    bool on_ground;
    bool knocked;
    bool kick_hit;
    bool tackled;
    uint8_t kick_t, slide_t, down_t, frozen_t, charged_t, touch_cd;
    uint8_t power;
    uint8_t anim;
} player_t;

enum { BM_NONE, BM_FIRE, BM_TELE, BM_ROCKET, BM_ZAP, BM_METEOR_UP,
       BM_METEOR_DOWN, BM_TORNADO, BM_MEGA };

typedef struct {
    int x, y, vx, vy;
    uint8_t r;
    uint8_t mode;
    uint8_t mode_t;
    uint8_t owner;
    uint8_t immune;
    uint8_t spin;
    uint8_t phase;
    bool plowed;
    int base_y;             /* zig-zag / tornado centre line (px) */
    int anchor_x, anchor_y; /* stuck-ball watchdog */
    int idle_t;
} ball_t;

typedef struct {
    int x, y;               /* 1/16 px */
    int8_t vx, vy;          /* 1/16 px per tick */
    uint8_t life, col, size, grav;
} particle_t;

#define MAX_PARTS 56

enum { MODE_QUICK, MODE_TOUR, MODE_FRIENDLY };
enum { SCR_ID_TITLE, SCR_ID_TEAMSEL, SCR_ID_MATCH, SCR_ID_PAUSE, SCR_ID_RESULTS,
       SCR_ID_BRACKET, SCR_ID_MESSAGE, SCR_ID_STATS };
enum { PH_KICKOFF, PH_PLAY, PH_GOAL, PH_HALFTIME, PH_GGINTRO, PH_END };

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
    uint8_t ai_level[2];    /* reaction ticks */
    bool ai_hard[2];
    uint8_t ai_bonus;
    unsigned long total_ticks;
} match_t;

/* ------------------------------------------------------------------ */
/* Save data                                                           */
/* ------------------------------------------------------------------ */
#define SAVE_NAME "FBLGSAVE"
#define SAVE_MAGIC 0x4C46u
#define SAVE_VER 1

typedef struct {
    uint8_t active, team, round, diff;
    uint8_t slot[8];
    uint8_t qf[4][2];       /* scores */
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

/* sprites generated at start-up */
static uint8_t crowd_buf[4][2 + CROWD_W * CROWD_H];
static uint8_t ad_buf[5][2 + AD_W * AD_H];
#define CROWD_SPR(i) ((gfx_sprite_t *)(void *)crowd_buf[i])
#define AD_SPR(i) ((gfx_sprite_t *)(void *)ad_buf[i])
static uint8_t crowd_cheer;

/* centre circle outline */
#define ELL_MAX 224
static int8_t ell_x[ELL_MAX], ell_y[ELL_MAX];
static uint8_t ell_n;

/* dirty rectangles, one list per buffer */
#define MAX_DIRTY 80
typedef struct { int16_t x0, y0, x1, y1; } rect_t;
static rect_t dirty[2][MAX_DIRTY];
static uint8_t ndirty[2];
static bool dirty_full[2];
static uint8_t cur_buf;

typedef struct { uint8_t s0, s1, secs, half, flash, p0, p1, blink; } hud_state_t;
static hud_state_t hud_last[2];
static bool hud_valid[2];

/* timing */
static clock_t next_tick;
static uint8_t tick_frac;

/* AI */
#define PRED_N 24
static int pred_x[PRED_N + 1], pred_y[PRED_N + 1];
static int jump_h[PRED_N + 1];
typedef struct { int target; uint8_t timer; int8_t err; bool want_jump; } ai_t;
static ai_t ai[2];

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

/* unsigned to decimal string, returns end pointer */
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

/* ------------------------------------------------------------------ */
/* Input                                                               */
/* ------------------------------------------------------------------ */
static void read_keys(void)
{
    uint8_t i;
    memcpy(prev_keys, keys, sizeof keys);
    kb_Scan();
    for (i = 1; i < 8; i++) keys[i] = kb_Data[i];
    keys[0] = 0;
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

/* Player 1: arrows, 2nd, alpha. Player 2: , ( ) apps prgm vars.
 * The two key sets share no keypad matrix row or column, so they can
 * never ghost into each other. Jump wins over slide (see README). */
static void human_ctrl(uint8_t who, ctrl_t *c)
{
    if (who == 0) {
        c->left = KDOWN(7, kb_Left);
        c->right = KDOWN(7, kb_Right);
        c->jump = KDOWN(7, kb_Up);
        c->slide = KPRESS(7, kb_Down);
        c->shoot = KPRESS(1, kb_2nd);
        c->super = KPRESS(2, kb_Alpha);
    } else {
        c->left = KDOWN(3, kb_Comma);
        c->right = KDOWN(5, kb_RParen);
        c->jump = KDOWN(4, kb_Prgm);
        c->slide = KPRESS(4, kb_LParen);
        c->shoot = KPRESS(3, kb_Apps);
        c->super = KPRESS(5, kb_Vars);
    }
    if (c->left && c->right) c->left = c->right = false;
    if (c->jump) c->slide = false;
}

/* ------------------------------------------------------------------ */
/* Timing: fixed 30 Hz ticks from the 32768 Hz clock                   */
/* 32768 / 30 = 1092 remainder 8                                       */
/* ------------------------------------------------------------------ */
static void tick_reset(void)
{
    next_tick = clock();
    tick_frac = 0;
}

static void tick_advance(void)
{
    next_tick += 1092;
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
    if (tick_due()) tick_reset();   /* far behind: do not try to catch up */
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

static void txt_c(const char *s, int y, uint8_t c, uint8_t sc)
{
    txt_sh(s, (SCR_W - txt_w(s, sc)) / 2, y, c, sc);
}

static void panel(int x, int y, int w, int h)
{
    gfx_SetColor(C_PANEL);
    gfx_FillRectangle(x, y, w, h);
    gfx_SetColor(C_GOLD);
    gfx_Rectangle(x, y, w, h);
    gfx_SetColor(C_PANEL2);
    gfx_Rectangle(x + 2, y + 2, w - 4, h - 4);
}

/* ------------------------------------------------------------------ */
/* Background                                                          */
/* ------------------------------------------------------------------ */
static int rx0, ry0, rx1, ry1;     /* region being redrawn (exclusive) */

static void rfill(int x0, int y0, int x1, int y1, uint8_t c)
{
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
    return x0 < rx1 && x1 > rx0 && y0 < ry1 && y1 > ry0;
}

static void draw_goal_bg(bool right)
{
    int gx0 = right ? SCR_W - BAR_LEN : 0;
    int i;
    if (!rhit(gx0, BAR_TOP, gx0 + BAR_LEN, GROUND + 2)) return;
    /* net */
    for (i = 2; i < 28; i += 4) {
        int x = right ? SCR_W - 1 - i : i;
        rfill(x, BAR_BOT, x + 1, GROUND, C_NET);
    }
    for (i = BAR_BOT + 4; i < GROUND; i += 4) {
        if (right) rfill(SCR_W - 28, i, SCR_W, i + 1, C_NET);
        else rfill(0, i, 28, i + 1, C_NET);
    }
    /* back stanchion, crossbar, front post */
    if (right) {
        rfill(SCR_W - 2, BAR_BOT, SCR_W, GROUND, C_LTGRAY);
        rfill(SCR_W - BAR_LEN, BAR_TOP, SCR_W, BAR_BOT, C_POST);
        rfill(SCR_W - BAR_LEN, BAR_BOT - 1, SCR_W, BAR_BOT, C_LTGRAY);
        rfill(SCR_W - BAR_LEN, BAR_BOT, SCR_W - 28, GROUND + 1, C_POST);
        rfill(SCR_W - 29, BAR_BOT, SCR_W - 28, GROUND + 1, C_LTGRAY);
    } else {
        rfill(0, BAR_BOT, 2, GROUND, C_LTGRAY);
        rfill(0, BAR_TOP, BAR_LEN, BAR_BOT, C_POST);
        rfill(0, BAR_BOT - 1, BAR_LEN, BAR_BOT, C_LTGRAY);
        rfill(28, BAR_BOT, BAR_LEN, GROUND + 1, C_POST);
        rfill(28, BAR_BOT, 29, GROUND + 1, C_LTGRAY);
    }
}

/* Redraw the static stadium inside (x0,y0)-(x1,y1), exclusive ends. */
static void bg_draw(int x0, int y0, int x1, int y1)
{
    int i;
    rx0 = x0; ry0 = y0; rx1 = x1; ry1 = y1;
    gfx_SetClipRegion(x0, y0, x1, y1);

    if (y0 < CROWD_Y) {
        rfill(0, HUD_H, SCR_W, 32, C_SKY1);
        rfill(0, 32, SCR_W, 38, C_SKY2);
        rfill(0, 38, SCR_W, 44, C_SKY3);
        /* floodlights */
        for (i = 0; i < 4; i++) {
            int lx = 28 + i * 80;
            uint8_t k;
            if (!rhit(lx, 28, lx + 24, 34)) continue;
            rfill(lx, 28, lx + 24, 34, C_DKGRAY);
            for (k = 0; k < 5; k++) rfill(lx + 2 + k * 4, 29, lx + 5 + k * 4, 33, C_LAMP);
        }
        rfill(0, 44, SCR_W, 47, C_ROOF);
        rfill(0, 47, SCR_W, 48, C_DKGRAY);
    }
    if (y0 < WALL_Y && y1 > CROWD_Y) {
        uint8_t t, c;
        for (t = 0; t < CROWD_TIERS; t++) {
            int ty = CROWD_Y + t * CROWD_H;
            if (ty >= y1 || ty + CROWD_H <= y0) continue;
            for (c = 0; c < SCR_W / CROWD_W; c++) {
                int tx = c * CROWD_W;
                uint8_t v;
                if (tx >= x1 || tx + CROWD_W <= x0) continue;
                v = (uint8_t)(((c * 3 + t) & 1) << 1);
                if (crowd_cheer) v |= (uint8_t)((c + t + crowd_cheer) & 1);
                gfx_Sprite(CROWD_SPR(v), tx, ty);
            }
        }
    }
    if (y0 < GRASS_Y && y1 > WALL_Y) {
        rfill(0, WALL_Y, SCR_W, AD_Y, C_WALL);
        if (y1 > AD_Y && y0 < AD_Y + AD_H) {
            for (i = 0; i < 5; i++) {
                if (i * AD_W < x1 && i * AD_W + AD_W > x0) gfx_Sprite(AD_SPR(i), i * AD_W, AD_Y);
            }
        }
    }
    if (y1 > GRASS_Y) {
        for (i = 0; i < 8; i++) rfill(i * 40, GRASS_Y, i * 40 + 40, SCR_H, (i & 1) ? C_GRASS2 : C_GRASS1);
        /* touchlines, halfway line, boxes */
        rfill(0, 148, SCR_W, 149, C_LINE);
        rfill(0, 232, SCR_W, 233, C_LINE);
        rfill(160, 148, 161, 232, C_LINE);
        rfill(0, 160, 72, 161, C_LINE);
        rfill(0, 224, 72, 225, C_LINE);
        rfill(72, 160, 73, 225, C_LINE);
        rfill(248, 160, SCR_W, 161, C_LINE);
        rfill(248, 224, SCR_W, 225, C_LINE);
        rfill(247, 160, 248, 225, C_LINE);
        rfill(158, 189, 163, 192, C_LINE);
        /* centre circle (flattened ellipse) */
        if (rhit(118, 174, 203, 206)) {
            uint8_t k;
            gfx_SetColor(C_LINE);
            for (k = 0; k < ell_n; k++) {
                int px = 160 + ell_x[k], py = 190 + ell_y[k];
                if (px >= x0 && px < x1 && py >= y0 && py < y1) gfx_SetPixel((uint24_t)px, (uint8_t)py);
            }
        }
        draw_goal_bg(false);
        draw_goal_bg(true);
    }
}

static void bg_full(void)
{
    bg_draw(0, HUD_H, SCR_W, SCR_H);
    gfx_SetClipRegion(0, 0, SCR_W, SCR_H);
}

/* ------------------------------------------------------------------ */
/* Dirty rectangles                                                    */
/* ------------------------------------------------------------------ */
static void mark_to(uint8_t b, int x0, int y0, int x1, int y1)
{
    rect_t *r;
    if (x0 < 0) x0 = 0;
    if (y0 < HUD_H) y0 = HUD_H;
    if (x1 > SCR_W) x1 = SCR_W;
    if (y1 > SCR_H) y1 = SCR_H;
    if (x0 >= x1 || y0 >= y1 || dirty_full[b]) return;
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

static void restore_dirty(uint8_t b)
{
    uint8_t i;
    if (dirty_full[b]) {
        bg_draw(0, HUD_H, SCR_W, SCR_H);
    } else {
        for (i = 0; i < ndirty[b]; i++) {
            rect_t *r = &dirty[b][i];
            bg_draw(r->x0, r->y0, r->x1, r->y1);
        }
    }
    ndirty[b] = 0;
    dirty_full[b] = false;
}

/* ------------------------------------------------------------------ */
/* Sprite generation (drawn to the back buffer, then captured)          */
/* ------------------------------------------------------------------ */
static const uint8_t FAN_SHIRTS[10] = {
    C_KRED, C_KBLUE, C_KYEL, C_WHITE, C_KORA, C_KSKY, C_KGRN, C_PINK, C_KAZU, C_GOLD
};
static const uint8_t FAN_SKINS[4] = { C_SKIN1, C_SKIN2, C_SKIN3, C_SKIN4 };

static void gen_crowd(uint8_t variant, bool cheer)
{
    uint32_t saved = rng_s;
    uint8_t row, k;
    gfx_sprite_t *spr = CROWD_SPR(variant * 2 + (cheer ? 1 : 0));
    rng_s = 0x9E3779B9UL + variant * 7919UL;
    gfx_SetClipRegion(0, 0, CROWD_W, CROWD_H);
    gfx_SetColor(C_STAND1);
    gfx_FillRectangle_NoClip(0, 0, CROWD_W, CROWD_H);
    gfx_SetColor(C_STAND2);
    gfx_FillRectangle_NoClip(0, 10, CROWD_W, 2);
    gfx_FillRectangle_NoClip(0, 22, CROWD_W, 2);
    for (row = 0; row < 2; row++) {
        for (k = 0; k < 4; k++) {
            int fx = 4 + k * 8 + row * 4;
            int fy = row * 12 + 3;
            uint8_t shirt = FAN_SHIRTS[rnd_n(10)];
            uint8_t skin = FAN_SKINS[rnd_n(4)];
            bool up = cheer && ((k + row) & 1) == 0;
            int lift = cheer ? 1 : 0;
            int w;
            for (w = -CROWD_W; w <= CROWD_W; w += CROWD_W) {
                int x = fx + w;
                gfx_SetColor(shirt);
                gfx_FillRectangle(x - 3, fy + 3 - lift, 6, 6);
                gfx_SetColor(skin);
                gfx_FillCircle(x, fy + 1 - lift, 2);
                if (up) {
                    gfx_FillRectangle(x - 4, fy - 3, 1, 5);
                    gfx_FillRectangle(x + 3, fy - 3, 1, 5);
                } else if (cheer) {
                    gfx_FillRectangle(x - 4, fy + 2, 1, 3);
                    gfx_FillRectangle(x + 3, fy + 2, 1, 3);
                }
            }
        }
    }
    spr->width = CROWD_W;
    spr->height = CROWD_H;
    gfx_GetSprite(spr, 0, 0);
    gfx_SetClipRegion(0, 0, SCR_W, SCR_H);
    rng_s = saved;
}

static void gen_ad(uint8_t i, const char *s, uint8_t bg, uint8_t fg)
{
    gfx_sprite_t *spr = AD_SPR(i);
    gfx_SetColor(bg);
    gfx_FillRectangle_NoClip(0, 0, AD_W, AD_H);
    gfx_SetColor(C_BLACK);
    gfx_FillRectangle_NoClip(0, AD_H - 1, AD_W, 1);
    gfx_FillRectangle_NoClip(AD_W - 1, 0, 1, AD_H);
    gfx_SetColor(C_WHITE);
    gfx_FillRectangle_NoClip(0, 0, AD_W - 1, 1);
    txt(s, (AD_W - txt_w(s, 1)) / 2, 3, fg);
    spr->width = AD_W;
    spr->height = AD_H;
    gfx_GetSprite(spr, 0, 0);
}

static void ell_add(int dx, int dy)
{
    if (ell_n < ELL_MAX) {
        ell_x[ell_n] = (int8_t)dx;
        ell_y[ell_n] = (int8_t)dy;
        ell_n++;
    }
}

static void gen_assets(void)
{
    int d;
    uint8_t t;
    gen_crowd(0, false);
    gen_crowd(0, true);
    gen_crowd(1, false);
    gen_crowd(1, true);
    gen_ad(0, "TI-84 CE", C_KRED, C_WHITE);
    gen_ad(1, "GRAPHX", C_KBLUE, C_YELLOW);
    gen_ad(2, "LEGENDS", C_YELLOW, C_KNAVY);
    gen_ad(3, "EZ80", C_KDGRN, C_WHITE);
    gen_ad(4, "CE CUP", C_KNAVY, C_GOLD);
    /* centre ellipse rx 40, ry 14 */
    ell_n = 0;
    for (d = -40; d <= 40; d++) {
        int dy = (int)isqrt32((unsigned long)(1600 - d * d) * 196UL / 1600UL);
        ell_add(d, dy);
        ell_add(d, -dy);
    }
    for (d = -14; d <= 14; d++) {
        int dx = (int)isqrt32((unsigned long)(196 - d * d) * 1600UL / 196UL);
        ell_add(dx, d);
        ell_add(-dx, d);
    }
    /* jump height table for the AI */
    for (t = 0; t <= PRED_N; t++) {
        long h = (long)t * JUMP_V - (long)GRAV_P * t * (t + 1) / 2;
        jump_h[t] = (int)(h / FP);
    }
}

/* ------------------------------------------------------------------ */
/* Character drawing                                                   */
/* ------------------------------------------------------------------ */
static void fill(int x, int y, int w, int h, uint8_t c)
{
    gfx_SetColor(c);
    gfx_FillRectangle(x, y, w, h);
}

/* horizontal spans of a circle, limited to rows y0..y1 and columns xa..xb */
static void circ_span(int cx, int cy, int r, int y0, int y1, int xa, int xb, uint8_t c)
{
    int y;
    gfx_SetColor(c);
    for (y = y0; y <= y1; y++) {
        int dy = y - cy, hw, l, rr;
        if (dy < -r || dy > r) continue;
        hw = (int)isqrt32((unsigned long)(r * r - dy * dy));
        l = cx - hw;
        rr = cx + hw;
        if (l < xa) l = xa;
        if (rr > xb) rr = xb;
        if (l <= rr) gfx_HorizLine(l, y, rr - l + 1);
    }
}

static void draw_head(int hx, int hy, int r, uint8_t team, int dir)
{
    const team_t *T = &TEAMS[team];
    int s = r;                       /* scale reference: 11 in game */
    int back_l = dir > 0 ? hx - r - 4 : hx - s / 3;
    int back_r = dir > 0 ? hx + s / 3 : hx + r + 4;
    int ex, ey;

    /* hair behind the head */
    if (T->style == HS_AFRO) {
        gfx_SetColor(T->hair);
        gfx_FillCircle(hx - dir * 2, hy - 3, r + 4);
    } else if (T->style == HS_LONG) {
        circ_span(hx - dir, hy, r + 2, hy - r, hy + r, back_l, back_r, T->hair);
    }

    gfx_SetColor(T->skin);
    gfx_FillCircle(hx, hy, r);
    /* nose */
    fill(dir > 0 ? hx + r - 1 : hx - r - 1, hy - 1, 3, 3, T->skin);
    gfx_SetColor(C_BLACK);
    gfx_Circle(hx, hy, r);

    switch (T->style) {
    case HS_BUZZ:
        circ_span(hx, hy, r, hy - r, hy - r / 2, hx - r, hx + r, T->hair);
        circ_span(hx, hy, r, hy - r / 2, hy, back_l, dir > 0 ? hx - s / 2 : hx + r, T->hair);
        break;
    case HS_LONG:
        circ_span(hx, hy, r, hy - r, hy - r / 3, hx - r, hx + r, T->hair);
        circ_span(hx, hy, r, hy - r / 3, hy + r / 2, back_l, dir > 0 ? hx - s / 3 : hx + r, T->hair);
        circ_span(hx, hy, r, hy - r / 2, hy - r / 2 + 1, hx - r, hx + r, C_WHITE);
        break;
    case HS_SLICK:
        circ_span(hx, hy, r, hy - r, hy - r * 4 / 11, hx - r, hx + r, T->hair);
        circ_span(hx, hy, r, hy - r * 4 / 11, hy - r * 2 / 11, back_l, back_r, T->hair);
        break;
    case HS_CURLY: {
        int k;
        circ_span(hx, hy, r, hy - r, hy - r / 3, hx - r, hx + r, T->hair);
        gfx_SetColor(T->hair);
        for (k = -2; k <= 2; k++) gfx_FillCircle(hx + k * (r * 5 / 11), hy - r + 1, r * 3 / 11);
        circ_span(hx, hy, r, hy - r / 3, hy + 1, back_l, dir > 0 ? hx - s / 3 : hx + r, T->hair);
        break;
    }
    case HS_BALD:
        fill(hx - dir * (r / 3) - 1, hy - r + 2, 3, 2, C_WHITE);
        circ_span(hx, hy, r, hy - r / 2, hy - r / 2 + 1, hx - r, hx + r, C_KRED);
        break;
    case HS_SPIKY:
        circ_span(hx, hy, r, hy - r, hy - r / 2, hx - r, hx + r, T->hair);
        gfx_SetColor(T->hair);
        gfx_FillTriangle(hx - r * 8 / 11, hy - r / 2, hx - r * 7 / 11, hy - r - 6, hx - r * 2 / 11, hy - r + 2);
        gfx_FillTriangle(hx - r * 3 / 11, hy - r + 1, hx + r * 1 / 11, hy - r - 7, hx + r * 4 / 11, hy - r + 1);
        gfx_FillTriangle(hx + r * 2 / 11, hy - r + 2, hx + r * 7 / 11, hy - r - 5, hx + r * 9 / 11, hy - r / 2);
        break;
    case HS_MOHAWK:
        fill(hx - r * 2 / 11, hy - r - 4, (r * 4 / 11) + 1, r / 2 + 4, T->hair);
        break;
    default: /* HS_AFRO: fringe */
        circ_span(hx, hy, r, hy - r, hy - r * 6 / 11, hx - r, hx + r, T->hair);
        break;
    }

    if (T->beard) {
        circ_span(hx, hy, r, hy + r * 3 / 11, hy + r, dir > 0 ? hx - r * 2 / 11 : hx - r,
                  dir > 0 ? hx + r : hx + r * 2 / 11, T->hair);
    }

    /* eye, brow, mouth */
    ex = hx + dir * (r * 5 / 11);
    ey = hy - r * 2 / 11;
    fill(ex - r * 2 / 11, ey - r * 2 / 11, r * 5 / 11, r * 5 / 11, C_WHITE);
    fill(dir > 0 ? ex : ex - r * 2 / 11 + 1, ey - r / 11, r * 2 / 11 + 1, r * 3 / 11, C_BLACK);
    fill(ex - r * 3 / 11, ey - r * 4 / 11, r * 6 / 11, 1 + r / 16, C_BLACK);
    if (dir > 0) {
        fill(hx + r * 2 / 11, hy + r * 5 / 11, r * 5 / 11, 1 + r / 16, C_BLACK);
    } else {
        fill(hx - r * 7 / 11, hy + r * 5 / 11, r * 5 / 11, 1 + r / 16, C_BLACK);
    }
    if (T->power == PW_FIRE) {
        /* trademark grin */
        fill(dir > 0 ? hx + r * 3 / 11 : hx - r * 6 / 11, hy + r * 5 / 11 - 2, r * 3 / 11 + 1, 2, C_WHITE);
    }
}

enum { POSE_STAND, POSE_RUN, POSE_JUMP, POSE_KICK, POSE_LIE };

static void kit_of(uint8_t team, bool alt, uint8_t *shirt, uint8_t *trim, uint8_t *shorts)
{
    const team_t *T = &TEAMS[team];
    *shirt = alt ? T->alt_shirt : T->shirt;
    *trim = alt ? T->alt_trim : T->trim;
    *shorts = alt ? T->alt_shorts : T->shorts;
}

/* Draws a legend with feet at (x, gy). */
static void draw_char(int x, int gy, uint8_t team, int dir, bool alt, uint8_t pose,
                      uint8_t phase, int lie_dir)
{
    const team_t *T = &TEAMS[team];
    uint8_t shirt, trim, shorts;
    kit_of(team, alt, &shirt, &trim, &shorts);

    if (pose == POSE_LIE) {
        int ld = lie_dir;
        int bx0 = ld > 0 ? x - 12 : x;
        /* legs and boots */
        fill(ld > 0 ? x + 2 : x - 14, gy - 6, 12, 4, T->socks);
        fill(ld > 0 ? x + 14 : x - 17, gy - 7, 3, 6, C_BLACK);
        /* shorts and torso */
        fill(ld > 0 ? x - 3 : x - 2, gy - 9, 6, 8, shorts);
        fill(bx0, gy - 11, 12, 10, shirt);
        fill(ld > 0 ? x - 8 : x + 4, gy - 11, 4, 10, trim);
        draw_head(x - ld * LIE_HEAD_DX, gy - HEAD_R, HEAD_R, team, -ld);
        return;
    }

    {
        int lo = 0, lo2 = 0, tuck = 0;
        int armup = 0;
        if (pose == POSE_RUN) {
            static const int8_t step[4] = { 0, 2, 0, -2 };
            lo = step[phase & 3];
            lo2 = -lo;
        } else if (pose == POSE_JUMP) {
            tuck = 2;
            armup = 6;
        }
        /* back leg */
        fill(x - 5 + lo2, gy - 8 - tuck, 4, 6, T->socks);
        fill(x - 5 + lo2, gy - 10 - tuck, 4, 2, T->skin);
        fill(dir > 0 ? x - 5 + lo2 : x - 7 + lo2, gy - 2 - tuck, 6, 2, C_BLACK);
        /* front leg */
        if (pose == POSE_KICK) {
            int lx = dir > 0 ? x + 2 : x - 14;
            fill(lx, gy - 11, 12, 4, T->socks);
            fill(dir > 0 ? x + 14 : x - 17, gy - 12, 3, 6, C_BLACK);
        } else {
            fill(x + 1 + lo, gy - 8 - tuck, 4, 6, T->socks);
            fill(x + 1 + lo, gy - 10 - tuck, 4, 2, T->skin);
            fill(dir > 0 ? x + 1 + lo : x - 1 + lo, gy - 2 - tuck, 6, 2, C_BLACK);
        }
        /* shorts */
        fill(x - BODY_HW, gy - 14 - tuck, BODY_HW * 2, 5, shorts);
        /* shirt */
        fill(x - BODY_HW, gy - BODY_H - tuck, BODY_HW * 2, 10, shirt);
        switch (T->pattern) {
        case PAT_STRIPES:
            fill(x - 4, gy - BODY_H - tuck, 2, 10, trim);
            fill(x + 2, gy - BODY_H - tuck, 2, 10, trim);
            break;
        case PAT_BAND:
            fill(x - BODY_HW, gy - 19 - tuck, BODY_HW * 2, 3, trim);
            break;
        case PAT_SASH:
            fill(x - 6, gy - 22 - tuck, 4, 3, trim);
            fill(x - 2, gy - 19 - tuck, 4, 3, trim);
            fill(x + 2, gy - 16 - tuck, 4, 3, trim);
            break;
        default:
            fill(x - 3, gy - BODY_H - tuck, 6, 2, trim);
            break;
        }
        /* arm */
        fill(dir > 0 ? x + BODY_HW - 1 : x - BODY_HW - 2, gy - 21 - tuck - armup, 3, 7, T->skin);
        fill(dir > 0 ? x + BODY_HW - 1 : x - BODY_HW - 2, gy - 22 - tuck - armup, 3, 3, shirt);
        draw_head(x + dir, gy - HEAD_DY - tuck, HEAD_R, team, dir);
    }
}

/* ------------------------------------------------------------------ */
/* Particles                                                           */
/* ------------------------------------------------------------------ */
static void spawn(int x, int y, int vx, int vy, uint8_t col, uint8_t life, uint8_t size, uint8_t grav)
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
    p->grav = grav;
}

static void burst(int x, int y, uint8_t col, uint8_t col2, uint8_t n, int speed)
{
    uint8_t i;
    for (i = 0; i < n; i++) {
        uint8_t a = (uint8_t)rnd_n(32);
        int sp = speed / 2 + rnd_n(speed / 2 + 1);
        spawn(x, y, COS32(a) * sp / 64, SIN32[a] * sp / 64, (i & 1) ? col : col2,
              (uint8_t)(10 + rnd_n(12)), (uint8_t)(2 + (i & 1)), 0);
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
        if (p->grav && p->vy < 120) p->vy = (int8_t)(p->vy + p->grav);
        if (p->y < HUD_H * 16 || p->y > SCR_H * 16 || p->x < 0 || p->x > SCR_W * 16) p->life = 0;
    }
}

static void clear_particles(void)
{
    memset(parts, 0, sizeof parts);
}

/* ------------------------------------------------------------------ */
/* Physics: player shapes and collisions                               */
/* ------------------------------------------------------------------ */
static bool is_lying(const player_t *p)
{
    return p->slide_t || p->down_t;
}

static void head_pos(const player_t *p, int *hx, int *hy)
{
    if (is_lying(p)) {
        *hx = p->x - p->lie_dir * (LIE_HEAD_DX * FP);
        *hy = p->y - HEAD_R * FP;
    } else {
        *hx = p->x + p->dir * FP;
        *hy = p->y - HEAD_DY * FP;
    }
}

static void body_box(const player_t *p, int *x0, int *y0, int *x1, int *y1)
{
    if (is_lying(p)) {
        *x0 = p->x - LIE_HW * FP; *x1 = p->x + LIE_HW * FP;
        *y0 = p->y - LIE_H * FP;
    } else {
        *x0 = p->x - BODY_HW * FP; *x1 = p->x + BODY_HW * FP;
        *y0 = p->y - BODY_H * FP;
    }
    *y1 = p->y;
}

static void cap_ball_speed(void)
{
    ball.vx = clampi(ball.vx, -MAXV, MAXV);
    ball.vy = clampi(ball.vy, -MAXV, MAXV);
}

/* Resolve ball against a contact point (ax, ay) with radius R around it.
 * (dx, dy) = ball - anchor, d = |(dx,dy)| > 0. e10 = restitution * 10. */
static void resolve(int ax, int ay, long dx, long dy, long d, int R, int pvx, int pvy, int e10)
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
    }
    /* make sure the ball leaves the contact */
    rvx = ball.vx - pvx;
    rvy = ball.vy - pvy;
    vn = (rvx * nx + rvy * ny) / 64;
    if (vn < 96) {
        ball.vx += (int)((96 - vn) * nx / 64);
        ball.vy += (int)((96 - vn) * ny / 64);
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
    resolve(cx, cy, dx, dy, d, R, pvx, pvy, e10);
    return true;
}

static bool collide_box(int x0, int y0, int x1, int y1, int pvx, int pvy, int e10)
{
    int cx = clampi(ball.x, x0, x1), cy = clampi(ball.y, y0, y1);
    long dx = ball.x - cx, dy = ball.y - cy;
    int r = ball.r * FP;
    long d2 = dx * dx + dy * dy;
    long d;
    if (d2 >= (long)r * r) return false;
    if (d2 == 0) {
        /* centre inside the box: push out through the nearest side */
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
    resolve(cx, cy, dx, dy, d, r, pvx, pvy, e10);
    return true;
}

/* ------------------------------------------------------------------ */
/* Supershots                                                          */
/* ------------------------------------------------------------------ */
static int goal_cx(int dir)
{
    return dir > 0 ? SCR_W - 14 : 14;
}

static void touch(uint8_t i, uint8_t amount);

static void knock(uint8_t i, int dir)
{
    player_t *p = &pl[i];
    if (p->down_t && p->knocked) return;
    p->slide_t = 0;
    p->down_t = KNOCK_T;
    p->knocked = true;
    p->lie_dir = (int8_t)dir;
    p->vx = dir * 3 * FP;
    p->kick_t = 0;
    burst(PX(p->x), PX(p->y) - 20, C_YELLOW, C_WHITE, 8, 40);
}

static void fire_super(uint8_t i)
{
    player_t *p = &pl[i];
    player_t *o = &pl[i ^ 1];
    int dir = p->dir;
    int bxp = PX(ball.x), byp = PX(ball.y);
    int gx = goal_cx(dir);
    uint8_t pw = TEAMS[M.team[i]].power;

    p->charged_t = 0;
    if (M.supers[i] < 255) M.supers[i]++;
    ball.owner = i;
    ball.immune = 8;
    ball.plowed = false;
    ball.phase = 0;
    ball.r = BALL_R;
    TEST_HOOK(test_on_super(i, pw));

    switch (pw) {
    case PW_FIRE: {
        int dxp = iabs(gx - bxp), t = dxp / 15;
        if (t < 1) t = 1;
        ball.mode = BM_FIRE;
        ball.mode_t = 50;
        ball.vx = dir * 15 * FP;
        ball.vy = clampi((186 - byp) * FP / t, -8 * FP, 8 * FP);
        burst(bxp, byp, C_ORANGE, C_YELLOW, 10, 48);
        break;
    }
    case PW_TELE: {
        int nx = dir > 0 ? SCR_W - GOAL_LINE - 40 : GOAL_LINE + 40;
        burst(bxp, byp, C_PURPLE, C_PINK, 12, 40);
        ball.x = nx * FP;
        ball.y = 176 * FP;
        ball.vx = dir * 4 * FP;
        ball.vy = -3 * FP;
        ball.mode = BM_TELE;
        ball.mode_t = 20;
        burst(nx, 176, C_PURPLE, C_WHITE, 12, 40);
        break;
    }
    case PW_ROCKET: {
        /* doubled gravity while in rocket mode: 72/tick^2, vy0 = -16 px */
        int t = (2 * 16 * FP) / (2 * GRAV_B);
        int vxp = (gx - bxp) * FP / t;
        if (dir > 0) vxp = clampi(vxp, 5 * FP, 14 * FP);
        else vxp = clampi(vxp, -14 * FP, -5 * FP);
        ball.mode = BM_ROCKET;
        ball.mode_t = 70;
        ball.vx = vxp;
        ball.vy = -16 * FP;
        burst(bxp, byp, C_ORANGE, C_LTGRAY, 10, 40);
        break;
    }
    case PW_ZAP:
        ball.mode = BM_ZAP;
        ball.mode_t = 60;
        ball.vx = dir * 13 * FP;
        ball.vy = (byp > 186 ? -6 : 6) * FP;
        ball.base_y = 186;
        burst(bxp, byp, C_YELLOW, C_WHITE, 12, 48);
        break;
    case PW_METEOR:
        ball.mode = BM_METEOR_UP;
        ball.mode_t = 24;
        ball.vx = 0;
        ball.vy = -14 * FP;
        burst(bxp, byp, C_RED, C_ORANGE, 12, 40);
        break;
    case PW_TORNADO:
        ball.mode = BM_TORNADO;
        ball.mode_t = 70;
        ball.vx = dir * 9 * FP;
        ball.vy = 0;
        ball.base_y = 178;
        burst(bxp, byp, C_WHITE, C_LTGRAY, 12, 40);
        break;
    case PW_MEGA:
        ball.mode = BM_MEGA;
        ball.mode_t = 120;
        ball.r = MEGA_R;
        if (ball.y > (GROUND - MEGA_R) * FP) ball.y = (GROUND - MEGA_R) * FP;
        ball.vx = dir * 11 * FP;
        ball.vy = -5 * FP;
        burst(bxp, byp, C_GOLD, C_WHITE, 12, 48);
        break;
    default: /* PW_FREEZE */
        o->frozen_t = FREEZE_T;
        o->vx = 0;
        o->slide_t = 0;
        o->kick_t = 0;
        burst(PX(o->x), PX(o->y) - 24, C_ICE, C_WHITE, 16, 48);
        break;
    }
}

static void activate_super(uint8_t i)
{
    player_t *p = &pl[i];
    int dx, dy;
    p->power = 0;
    if (TEAMS[M.team[i]].power == PW_FREEZE) {
        fire_super(i);
        return;
    }
    dx = PX(ball.x) - PX(p->x);
    dy = PX(ball.y) - (PX(p->y) - 20);
    if (dx * dx + dy * dy <= 36 * 36) fire_super(i);
    else p->charged_t = CHARGE_T;
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
    int oldx = p->x;

    if (p->touch_cd) p->touch_cd--;
    if (p->kick_t) p->kick_t--;
    if (p->charged_t) {
        p->charged_t--;
        if ((p->charged_t & 3) == 0) {
            int hx, hy;
            head_pos(p, &hx, &hy);
            spawn(PX(hx) - 10 + rnd_n(20), PX(hy) + 8 - rnd_n(30), 0, -12,
                  POWER_COL[TEAMS[M.team[i]].power], 10, 2, 0);
        }
    }

    if (p->frozen_t) {
        p->frozen_t--;
        p->vx = 0;
    } else if (p->slide_t) {
        p->slide_t--;
        p->vx = p->slide_dir * (SLIDE_V * p->slide_t / SLIDE_T + FP);
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
        int target = mv * RUN_V;
        int acc = p->on_ground ? RUN_ACC : AIR_ACC;
        p->vx += clampi(target - p->vx, -acc, acc);
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

    /* integrate */
    p->vy += GRAV_P;
    if (p->vy > MAXFALL) p->vy = MAXFALL;
    p->y += p->vy;
    if (p->y >= GROUND * FP) {
        p->y = GROUND * FP;
        p->vy = 0;
        p->on_ground = true;
    } else {
        p->on_ground = false;
    }
    p->x += p->vx;
    p->x = clampi(p->x, MIN_X * FP, MAX_X * FP);

    /* goals: the head may only be under the crossbar if the body is low */
    {
        int top = p->y - (HEAD_DY + HEAD_R) * FP;
        bool in_l = p->x < GOAL_ZONE * FP, in_r = p->x > (SCR_W - GOAL_ZONE) * FP;
        if ((in_l || in_r) && top < BAR_BOT * FP) {
            bool was_in = in_l ? oldx < GOAL_ZONE * FP : oldx > (SCR_W - GOAL_ZONE) * FP;
            if (!was_in) {
                p->x = in_l ? GOAL_ZONE * FP : (SCR_W - GOAL_ZONE) * FP;
                p->vx = 0;
            } else {
                p->y = (BAR_BOT + HEAD_DY + HEAD_R) * FP;
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
    uint8_t i;

    /* stomp: landing on the other player's head */
    for (i = 0; i < 2; i++) {
        player_t *p = &pl[i], *o = &pl[i ^ 1];
        if (p->vy > 0 && !is_lying(o) && iabs(p->x - o->x) < 14 * FP &&
            p->y < o->y - 30 * FP && p->y > o->y - 46 * FP) {
            p->vy = -JUMP_V * 2 / 3;
        }
    }

    if (iabs(dx) < 18 * FP && ady < 30 * FP) {
        int push = (18 * FP - iabs(dx)) / 2 + 1;
        if (dx >= 0) {
            a->x -= push;
            b->x += push;
        } else {
            a->x += push;
            b->x -= push;
        }
        a->x = clampi(a->x, MIN_X * FP, MAX_X * FP);
        b->x = clampi(b->x, MIN_X * FP, MAX_X * FP);
    }

    /* slide tackles (checked in alternating order, so neither side is favoured) */
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
    int dir = p->dir;
    int dx, dy;
    bool hit = false;

    if (p->kick_t < KICK_ACTIVE || p->kick_hit || is_lying(p) || p->frozen_t) return;
    dx = (PX(ball.x) - PX(p->x)) * dir;
    dy = PX(ball.y) - PX(p->y);

    if (p->on_ground) {
        if (dx >= -4 && dx <= 26 && dy >= -32 && dy <= 4) {
            ball.vx = dir * SHOT_V;           /* grounded shot: low and straight */
            ball.vy = -4 * FP;
            hit = true;
        }
    } else {
        int hx, hy, hdx, hdy;
        head_pos(p, &hx, &hy);
        hdx = PX(ball.x) - PX(hx);
        hdy = PX(ball.y) - PX(hy);
        if (hdx * hdx + hdy * hdy <= 20 * 20) {
            ball.vx = dir * HEADER_V;         /* power header, driven down */
            ball.vy = FP + FP / 2;
            hit = true;
        } else if (dx >= -4 && dx <= 28 && dy >= -46 && dy <= 6) {
            ball.vx = dir * VOLLEY_V;         /* volley, lifted */
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
        if (M.shots[i] < 255) M.shots[i]++;
        touch(i, 6);
        if (p->charged_t) fire_super(i);
    }
}

/* ------------------------------------------------------------------ */
/* Ball                                                                */
/* ------------------------------------------------------------------ */
static bool knock_mode(void)
{
    return ball.mode == BM_FIRE || ball.mode == BM_ROCKET || ball.mode == BM_ZAP ||
           ball.mode == BM_METEOR_DOWN || ball.mode == BM_TORNADO || ball.mode == BM_MEGA;
}

static void end_mode(void)
{
    ball.mode = BM_NONE;
    ball.r = BALL_R;
    if (ball.y > (GROUND - BALL_R) * FP) ball.y = (GROUND - BALL_R) * FP;
}

static void drop_ball(int x)
{
    ball.x = x * FP;
    ball.y = 70 * FP;
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
    if (ball.x - r < 0) {
        ball.x = r;
        if (ball.vx < 0) ball.vx = -ball.vx * 7 / 10;
        hit = true;
    } else if (ball.x + r > SCR_W * FP) {
        ball.x = SCR_W * FP - r;
        if (ball.vx > 0) ball.vx = -ball.vx * 7 / 10;
        hit = true;
    }
    if (ball.y - r < (HUD_H + 1) * FP) {
        ball.y = (HUD_H + 1) * FP + r;
        if (ball.vy < 0) ball.vy = -ball.vy / 2;
        hit = true;
    }
    if (ball.y + r > GROUND * FP) {
        ball.y = GROUND * FP - r;
        if (ball.vy > 0) {
            ball.vy = -ball.vy * 5 / 8;
            if (ball.vy > -BOUNCE_MIN) ball.vy = 0;
        }
        if (ball.mode == BM_METEOR_DOWN || ball.mode == BM_ROCKET) {
            burst(PX(ball.x), GROUND - 4, C_ORANGE, C_LTGRAY, 8, 32);
            end_mode();
        }
    }
    if (collide_box(0, BAR_TOP * FP, BAR_LEN * FP, BAR_BOT * FP, 0, 0, 6)) hit = true;
    if (collide_box((SCR_W - BAR_LEN) * FP, BAR_TOP * FP, SCR_W * FP, BAR_BOT * FP, 0, 0, 6)) hit = true;
    return hit;
}

static void ball_hits_players(bool *squeeze)
{
    uint8_t n, i, hits = 0;
    for (n = 0; n < 2; n++) {
        player_t *p;
        i = (uint8_t)(n ^ (M.total_ticks & 1));   /* alternate who is checked first */
        p = &pl[i];
        int hx, hy, x0, y0, x1, y1;
        bool hh, hb;
        if (ball.immune && ball.owner == i) continue;
        /* mega ball plows through the first defender it meets */
        if (ball.mode == BM_MEGA && ball.owner != i && ball.plowed) continue;
        head_pos(p, &hx, &hy);
        body_box(p, &x0, &y0, &x1, &y1);
        {
            /* cheap reject */
            int ddx = ball.x - p->x, ddy = ball.y - (p->y - 24 * FP);
            if (iabs(ddx) > (34 + MEGA_R) * FP || iabs(ddy) > (40 + MEGA_R) * FP) continue;
        }
        hh = collide_circle(hx, hy, HEAD_R, p->vx, p->vy, 7);
        hb = collide_box(x0, y0, x1, y1, p->vx, p->vy, 4);
        if (!hh && !hb) continue;
        hits++;

        if (knock_mode() && ball.owner != i) {
            knock(i, -p->dir);
            if (ball.mode == BM_MEGA) {
                ball.plowed = true;
                ball.vx = -p->dir * 10 * FP;
                ball.vy = -4 * FP;
                continue;
            }
            ball.vx /= 2;
            ball.vy /= 2;
            end_mode();
            continue;
        }
        if (ball.mode != BM_NONE && ball.mode != BM_MEGA) end_mode();

        if (hh && !is_lying(p)) {
            /* headers always go towards the opponent's goal */
            ball.vx += p->dir * 96;
            if (ball.vx * p->dir < 2 * FP) ball.vx = p->dir * 2 * FP;
            if (!p->on_ground && p->vy < 0) ball.vy -= FP;
            cap_ball_speed();
        }
        if (p->slide_t) {
            ball.vx = p->slide_dir * 8 * FP;
            ball.vy = -5 * FP;
        }
        touch(i, hh ? 4 : 3);
        if (p->charged_t) fire_super(i);
    }
    if (hits >= 2) *squeeze = true;
}

static void ball_mode_tick(int *grav)
{
    int bxp = PX(ball.x), byp = PX(ball.y);
    switch (ball.mode) {
    case BM_FIRE:
        *grav = 0;
        spawn(bxp, byp - 2 + rnd_n(5), -ball.vx / 128, -4 + rnd_n(9), (rnd() & 1) ? C_ORANGE : C_YELLOW, 8, 3, 0);
        spawn(bxp, byp, -ball.vx / 96, 0, C_RED, 6, 2, 0);
        break;
    case BM_ROCKET:
        *grav = GRAV_B * 2;
        spawn(bxp, byp, -ball.vx / 128, -ball.vy / 128, C_LTGRAY, 14, 3, 0);
        if (ball.mode_t & 1) spawn(bxp, byp, 0, 0, C_ORANGE, 5, 2, 0);
        break;
    case BM_ZAP:
        *grav = 0;
        if (iabs(byp - ball.base_y) > 18) {
            ball.vy = byp > ball.base_y ? -6 * FP : 6 * FP;
            burst(bxp, byp, C_YELLOW, C_WHITE, 3, 24);
        }
        spawn(bxp, byp, 0, 0, C_YELLOW, 5, 2, 0);
        break;
    case BM_METEOR_UP:
        *grav = 0;
        spawn(bxp, byp + 4, -6 + rnd_n(13), 8, C_ORANGE, 8, 3, 0);
        if (byp < 56 || ball.mode_t <= 1) {
            int dir = pl[ball.owner].dir;
            long dx = (long)(goal_cx(dir) - bxp), dy = (long)(196 - byp);
            long len = (long)isqrt32((unsigned long)(dx * dx + dy * dy));
            if (len < 1) len = 1;
            ball.mode = BM_METEOR_DOWN;
            ball.mode_t = 60;
            ball.vx = (int)(dx * 15 * FP / len);
            ball.vy = (int)(dy * 15 * FP / len);
        }
        break;
    case BM_METEOR_DOWN:
        *grav = 0;
        spawn(bxp, byp, -ball.vx / 128, -ball.vy / 128, (rnd() & 1) ? C_RED : C_ORANGE, 10, 3, 0);
        break;
    case BM_TORNADO: {
        int ty;
        *grav = 0;
        ball.phase++;
        ty = ball.base_y + SIN32[ball.phase & 31] * 28 / 64;
        ball.vy = ty * FP - ball.y;
        spawn(bxp + COS32(ball.phase * 3) / 6, byp + SIN32[(ball.phase * 3) & 31] / 6, 0, -6,
              (ball.phase & 1) ? C_WHITE : C_LTGRAY, 8, 2, 0);
        break;
    }
    case BM_MEGA:
        if ((ball.mode_t & 3) == 0) spawn(bxp - 10 + rnd_n(21), byp - 10 + rnd_n(21), 0, -8, C_GOLD, 8, 2, 0);
        break;
    case BM_TELE:
        if (ball.mode_t & 1) spawn(bxp - 8 + rnd_n(17), byp - 8 + rnd_n(17), 0, -6, C_PURPLE, 8, 2, 0);
        break;
    default:
        break;
    }
    if (ball.mode != BM_NONE) {
        if (ball.mode_t) ball.mode_t--;
        if (ball.mode_t == 0) end_mode();
    }
}

static void check_goal(void);

static void update_ball(void)
{
    int grav = GRAV_B;
    uint8_t s;
    bool squeeze = false;

    if (ball.immune) ball.immune--;
    ball_mode_tick(&grav);
    ball.vy += grav;
    cap_ball_speed();

    for (s = 0; s < 2; s++) {
        bool hitw;
        ball.x += ball.vx / 2;
        ball.y += ball.vy / 2;
        hitw = world_collide();
        if (hitw && (ball.mode == BM_FIRE || ball.mode == BM_ZAP || ball.mode == BM_TORNADO ||
                     ball.mode == BM_METEOR_DOWN)) {
            burst(PX(ball.x), PX(ball.y), C_ORANGE, C_WHITE, 6, 32);
            end_mode();
        }
        ball_hits_players(&squeeze);
    }
    if (squeeze) {
        ball.vy = -9 * FP;
        ball.vx /= 2;
    }

    /* rolling friction */
    if (ball.y >= (GROUND - ball.r) * FP - 2 && ball.vy == 0 && ball.mode != BM_FIRE && ball.mode != BM_ZAP) {
        ball.vx -= ball.vx / 12;
        if (iabs(ball.vx) < 8) ball.vx = 0;
    }
    /* nothing can rest on a crossbar: it rolls back into play */
    if (ball.y + ball.r * FP >= BAR_TOP * FP - 2 * FP && ball.y < BAR_TOP * FP) {
        if (ball.x < (BAR_LEN + 2) * FP) ball.vx += 12;
        else if (ball.x > (SCR_W - BAR_LEN - 2) * FP) ball.vx -= 12;
    }
    ball.spin = (uint8_t)(ball.spin + ball.vx / 160);

    /* watchdog: if the ball has not really moved for 8 s, drop it again */
    if (iabs(ball.x - ball.anchor_x) > 24 * FP || iabs(ball.y - ball.anchor_y) > 24 * FP) {
        ball.anchor_x = ball.x;
        ball.anchor_y = ball.y;
        ball.idle_t = 0;
    } else if (++ball.idle_t > 8 * TICK_HZ) {
        burst(PX(ball.x), PX(ball.y), C_WHITE, C_LTGRAY, 10, 32);
        drop_ball(160);
    }
    CHK24(ball.x); CHK24(ball.y); CHK24(ball.vx); CHK24(ball.vy);
}

/* ------------------------------------------------------------------ */
/* AI                                                                  */
/* ------------------------------------------------------------------ */
static void predict_ball(void)
{
    int x = ball.x, y = ball.y, vx = ball.vx, vy = ball.vy;
    int r = ball.r * FP;
    bool straight = ball.mode == BM_FIRE || ball.mode == BM_ZAP || ball.mode == BM_TORNADO ||
                    ball.mode == BM_METEOR_UP || ball.mode == BM_METEOR_DOWN;
    uint8_t t;
    pred_x[0] = PX(x);
    pred_y[0] = PX(y);
    for (t = 1; t <= PRED_N; t++) {
        if (!straight) vy += GRAV_B;
        x += vx;
        y += vy;
        if (x < r) { x = r; vx = -vx * 7 / 10; }
        if (x > SCR_W * FP - r) { x = SCR_W * FP - r; vx = -vx * 7 / 10; }
        if (y > GROUND * FP - r) {
            y = GROUND * FP - r;
            if (vy > 0) vy = -vy * 5 / 8;
            vx -= vx / 12;
        }
        pred_x[t] = PX(x);
        pred_y[t] = PX(y);
    }
}

static void ai_control(uint8_t i, ctrl_t *c)
{
    player_t *me = &pl[i], *op = &pl[i ^ 1];
    ai_t *A = &ai[i];
    bool hard = M.ai_hard[i];
    int dir = me->dir;
    int mx = PX(me->x), my = PX(me->y);
    int bx = PX(ball.x), by = PX(ball.y);
    int ox = PX(op->x);
    int own_goal = dir > 0 ? 0 : SCR_W;
    int opp_goal = dir > 0 ? SCR_W : 0;
    int rel = (bx - mx) * dir;               /* > 0: ball is in front */
    int shoot_range = hard ? 170 : 110;
    int t;

    memset(c, 0, sizeof *c);
    if (me->frozen_t || is_lying(me) || M.phase != PH_PLAY) return;

    /* ---- strategic decision, refreshed every few ticks ---- */
    if (A->timer) {
        A->timer--;
    } else {
        int bvx = ball.vx * dir;              /* > 0: ball moving towards opponent */
        int op_ball = iabs(ox - bx);
        int me_ball = iabs(mx - bx);
        int from_goal = (bx - own_goal) * dir;  /* ball distance from our goal line */
        bool danger = bvx < -4 * FP && from_goal < 170;
        int engage = hard ? 160 : 120;
        A->timer = M.ai_level[i];
        A->err = (int8_t)(hard ? rnd_n(5) - 2 : rnd_n(33) - 16);

        if (rel < -4 && bvx < -FP && from_goal < 120) {
            /* beaten: get back on the goal line and block */
            A->target = own_goal + dir * 22;
        } else if (rel < -4) {
            /* ball is behind us: get back goal-side of where it is going */
            int px = pred_x[8];
            A->target = ((px - own_goal) * dir < (bx - own_goal) * dir ? px : bx) - dir * 24;
        } else if (from_goal < engage || me_ball + 24 < op_ball ||
                   (iabs(ball.vx) < 3 * FP && me_ball <= op_ball + 12)) {
            /* our half, we win the race, or a loose ball nobody is closer to */
            if (by < my - 50) {
                /* ball in the air: wait just behind where it comes down to head height */
                int k, lx = pred_x[PRED_N];
                for (k = 1; k <= PRED_N; k++) {
                    if (pred_y[k] > my - 44) { lx = pred_x[k]; break; }
                }
                A->target = lx - dir * 8;
            } else {
                /* attack: run through the ball towards the goal */
                A->target = bx - dir * 6;
                if (rel >= 0 && rel < 18) A->target = bx + dir * 20;
            }
        } else {
            /* hold a line between the ball and our goal */
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

        /* slide tackle: Hard picks its moment (opponent close enough to be hit) */
        if (me->on_ground && rel > 6 && rel < 40 && by > my - 22 && op_ball < 26 && (ox - mx) * dir > 0) {
            int od = (ox - mx) * dir;
            if (hard ? (od < 34 && rnd_n(100) < 45) : rnd_n(100) < 8) c->slide = true;
        }
    }

    /* ---- movement ---- */
    {
        int d = A->target - mx;
        if (d > 3) c->right = true;
        else if (d < -3) c->left = true;
    }

    /* jump over a low ball that is in the way when running back */
    if (me->on_ground && rel < 0 && rel > -24 && by > my - 26 &&
        ((dir > 0 && c->left) || (dir < 0 && c->right))) {
        c->jump = true;
    }

    /* ---- headers: will a jump started now meet the ball? ---- */
    if (me->on_ground && !c->jump && (hard || (rnd() & 3) == 0)) {
        int hy0 = my - HEAD_DY;
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

    /* ---- shooting ---- */
    if (me->kick_t == 0) {
        int dx = (bx - mx) * dir, dy = by - my;
        int dist_goal = iabs(opp_goal - bx);
        bool blocked = (ox - mx) * dir > 0 && (ox - mx) * dir < 50;
        if (me->on_ground) {
            if (dx >= 0 && dx <= 24 && dy >= -30 && dy <= 4) {
                if (dist_goal < shoot_range || (blocked && rnd_n(100) < 50) || rnd_n(100) < (hard ? 12 : 4)) {
                    c->shoot = true;
                }
            }
        } else {
            int hx, hy, hdx, hdy;
            bool clearing = (bx - own_goal) * dir < 90;
            head_pos(me, &hx, &hy);
            hdx = bx - PX(hx);
            hdy = by - PX(hy);
            if ((hdx * hdx + hdy * hdy <= 19 * 19 || (dx >= -2 && dx <= 26 && dy >= -44 && dy <= 4)) &&
                (dist_goal < shoot_range || clearing) && (hard || rnd_n(100) < 55)) {
                c->shoot = true;
            }
        }
    }

    /* ---- supershot ---- */
    if (me->power >= POWER_MAX && me->charged_t == 0) {
        if (TEAMS[M.team[i]].power == PW_FREEZE) {
            /* freeze when we are on the ball going forward, or to stop a shot at our goal */
            int me_b = iabs(mx - bx), op_b = iabs(ox - bx);
            if ((me_b < 36 && rel >= 0 && iabs(opp_goal - bx) < 200) ||
                (op_b < 30 && (bx - own_goal) * dir < 120)) c->super = true;
        } else {
            int dx = bx - mx, dy = by - (my - 20);
            if (dx * dx + dy * dy < 30 * 30 && rel >= -4) c->super = true;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Match flow                                                          */
/* ------------------------------------------------------------------ */
static void place_players(void)
{
    uint8_t i;
    for (i = 0; i < 2; i++) {
        player_t *p = &pl[i];
        uint8_t keep = p->power;
        memset(p, 0, sizeof *p);
        p->power = keep;
        p->dir = (int8_t)(i == 0 ? 1 : -1);
        p->x = (i == 0 ? 92 : 228) * FP;
        p->y = GROUND * FP;
        p->on_ground = true;
        p->lie_dir = p->dir;
        ai[i].timer = 0;
        ai[i].target = PX(p->x);
    }
}

/* side < 0: centre; otherwise the ball drops on that (conceding) side */
static void start_kickoff(int side)
{
    place_players();
    drop_ball(side < 0 ? 160 : (side == 0 ? 112 : 208));
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
    burst(PX(ball.x), PX(ball.y), C_GOLD, C_WHITE, 16, 56);
    TEST_HOOK(test_on_goal((uint8_t)scorer, M.half, M.half_ticks));
}

static void match_tick(const ctrl_t *c0, const ctrl_t *c1)
{
    M.total_ticks++;
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
                if (M.half == 0) {
                    set_phase(PH_HALFTIME, 60);
                } else if (M.score[0] != M.score[1]) {
                    set_phase(PH_END, 0);
                } else {
                    set_phase(PH_GGINTRO, 60);
                }
            }
        }
        break;
    case PH_GOAL: {
        ctrl_t cel;
        uint8_t s = M.last_scorer;
        memset(&cel, 0, sizeof cel);
        cel.jump = true;
        update_player(s, &cel);
        memset(&cel, 0, sizeof cel);
        update_player(s ^ 1, &cel);
        /* ball settles in the net */
        ball.vy += GRAV_B;
        ball.y += ball.vy;
        if (ball.y > (GROUND - ball.r) * FP) {
            ball.y = (GROUND - ball.r) * FP;
            ball.vy = -ball.vy / 3;
        }
        if ((M.phase_t & 1) == 0) {
            const team_t *T = &TEAMS[M.team[s]];
            spawn(8 + rnd_n(304), HUD_H + 2, -8 + rnd_n(17), 4 + rnd_n(8),
                  (rnd() & 1) ? (M.kit_alt[s] ? T->alt_shirt : T->shirt) : C_GOLD, 40, 3, 1);
        }
        if ((M.phase_t % 6) == 0) {
            crowd_cheer = (uint8_t)(crowd_cheer == 1 ? 2 : 1);
            invalidate(0, CROWD_Y, SCR_W, WALL_Y);
        }
        if (--M.phase_t <= 0) {
            crowd_cheer = 0;
            invalidate(0, CROWD_Y, SCR_W, WALL_Y);
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
/* Match rendering                                                     */
/* ------------------------------------------------------------------ */
static unsigned match_secs(void)
{
    return (unsigned)(M.half >= 2 ? 60 : M.half * 30) + (unsigned)(M.half_ticks / TICK_HZ);
}

static void hud_compute(hud_state_t *h)
{
    int remain = HALF_TICKS - M.half_ticks;
    memset(h, 0, sizeof *h);
    h->s0 = M.score[0];
    h->s1 = M.score[1];
    h->secs = (uint8_t)(match_secs() > 255 ? 255 : match_secs());
    h->half = M.half;
    if (M.half < 2 && remain <= 5 * TICK_HZ && M.phase == PH_PLAY) {
        h->flash = (uint8_t)(1 + ((M.half_ticks / 5) & 1));
        if ((M.half_ticks % TICK_HZ) < 4) h->flash = 3;   /* the "beep" pulse */
    }
    h->p0 = (uint8_t)(pl[0].power * 86 / POWER_MAX);
    h->p1 = (uint8_t)(pl[1].power * 86 / POWER_MAX);
    h->blink = (uint8_t)(((M.total_ticks >> 2) & 1) |
                         (pl[0].power >= POWER_MAX ? 2 : 0) | (pl[1].power >= POWER_MAX ? 4 : 0));
    if (!(h->blink & 6)) h->blink = 0;
}

static void hud_bar(int x, uint8_t fill_w, bool full, bool blink)
{
    gfx_SetColor(C_BLACK);
    gfx_FillRectangle_NoClip((uint24_t)x, 15, 88, 7);
    gfx_SetColor(C_GRAY);
    gfx_Rectangle_NoClip((uint24_t)x, 15, 88, 7);
    if (fill_w) {
        gfx_SetColor(full ? (blink ? C_WHITE : C_GOLD) : C_ORANGE);
        gfx_FillRectangle_NoClip((uint24_t)(x + 1), 16, fill_w, 5);
    }
}

static void hud_chip(int x, uint8_t side)
{
    uint8_t shirt, trim, shorts;
    kit_of(M.team[side], M.kit_alt[side], &shirt, &trim, &shorts);
    gfx_SetColor(C_WHITE);
    gfx_FillRectangle_NoClip((uint24_t)x, 3, 10, 10);
    gfx_SetColor(shirt);
    gfx_FillRectangle_NoClip((uint24_t)(x + 1), 4, 8, 5);
    gfx_SetColor(shorts);
    gfx_FillRectangle_NoClip((uint24_t)(x + 1), 9, 8, 3);
    gfx_SetColor(trim);
    gfx_FillRectangle_NoClip((uint24_t)(x + 3), 4, 4, 1);
}

static void hud_score(int x, uint8_t s)
{
    char buf[4];
    gfx_SetColor(C_HUD2);
    gfx_FillRectangle_NoClip((uint24_t)x, 2, 24, 21);
    u2s(buf, s);
    if (s < 10) txt_sh(buf, x + 4, 5, C_WHITE, 2);
    else txt_sh(buf, x + 4, 9, C_WHITE, 1);
}

static void draw_hud(const hud_state_t *h)
{
    char buf[12], *p;
    const char *lbl;
    unsigned secs = match_secs();
    uint8_t ccol = C_WHITE;
    gfx_SetColor(C_HUD);
    gfx_FillRectangle_NoClip(0, 0, SCR_W, HUD_H - 1);
    gfx_SetColor(C_GOLD);
    gfx_HorizLine_NoClip(0, HUD_H - 1, SCR_W);

    hud_chip(3, 0);
    hud_chip(SCR_W - 13, 1);
    txt(TEAMS[M.team[0]].name, 16, 4, C_WHITE);
    lbl = TEAMS[M.team[1]].name;
    txt(lbl, 304 - txt_w(lbl, 1), 4, C_WHITE);

    hud_score(108, h->s0);
    hud_score(188, h->s1);

    /* clock box */
    if (h->flash == 3) {
        gfx_SetColor(C_WHITE);
        gfx_FillRectangle_NoClip(136, 2, 48, 11);
        ccol = C_RED;
    } else if (h->flash) {
        ccol = h->flash == 1 ? C_RED : C_YELLOW;
    }
    p = u2s(buf, secs / 60);
    *p++ = ':';
    *p++ = (char)('0' + (secs % 60) / 10);
    *p++ = (char)('0' + secs % 10);
    *p = 0;
    txt(buf, 160 - txt_w(buf, 1) / 2, 4, ccol);
    lbl = M.half == 0 ? "1ST" : (M.half == 1 ? "2ND" : "G.GOAL");
    txt(lbl, 160 - txt_w(lbl, 1) / 2, 15, M.half == 2 ? C_GOLD : C_LTGRAY);

    hud_bar(16, h->p0, pl[0].power >= POWER_MAX, h->blink & 1);
    hud_bar(216, h->p1, pl[1].power >= POWER_MAX, h->blink & 1);
}

static void draw_player(uint8_t i)
{
    player_t *p = &pl[i];
    int x = PX(p->x), gy = PX(p->y);
    uint8_t pose = POSE_STAND;
    const char *lbl;

    if (is_lying(p)) pose = POSE_LIE;
    else if (p->kick_t >= 4) pose = POSE_KICK;
    else if (!p->on_ground) pose = POSE_JUMP;
    else if (p->anim) pose = POSE_RUN;

    /* shadow and mini power bar on the ground */
    gfx_SetColor(C_SHADOW);
    gfx_FillRectangle(x - 9, GROUND + 1, 18, 2);
    gfx_SetColor(C_BLACK);
    gfx_FillRectangle(x - 11, GROUND + 5, 22, 4);
    if (p->power) {
        gfx_SetColor(p->power >= POWER_MAX ? ((M.total_ticks & 4) ? C_WHITE : C_GOLD) : C_ORANGE);
        gfx_FillRectangle(x - 10, GROUND + 6, p->power / 5, 2);
    }
    mark(x - 12, GROUND + 1, x + 12, GROUND + 10);

    draw_char(x, gy, M.team[i], p->dir, M.kit_alt[i], pose, (uint8_t)(p->anim / 3), p->lie_dir);

    if (p->frozen_t) {
        gfx_SetColor(C_ICE);
        gfx_Rectangle(x - 15, gy - 47, 30, 48);
        gfx_Rectangle(x - 14, gy - 46, 28, 46);
        gfx_SetColor(C_WHITE);
        gfx_Line(x - 10, gy - 40, x - 4, gy - 46);
        gfx_Line(x + 4, gy - 8, x + 12, gy - 16);
        gfx_Line(x - 12, gy - 20, x - 8, gy - 24);
    }
    if (p->charged_t && (p->charged_t & 2)) {
        int hx, hy;
        head_pos(p, &hx, &hy);
        gfx_SetColor(POWER_COL[TEAMS[M.team[i]].power]);
        gfx_Circle(PX(hx), PX(hy), 15);
    }
    if (p->down_t && p->knocked) {
        uint8_t k, a = (uint8_t)(M.total_ticks & 31);
        int hx = x - p->lie_dir * LIE_HEAD_DX;
        gfx_SetColor(C_YELLOW);
        for (k = 0; k < 3; k++) {
            uint8_t ang = (uint8_t)((a + k * 11) & 31);
            gfx_FillRectangle(hx + COS32(ang) * 12 / 64 - 1, gy - 26 + SIN32[ang] * 4 / 64, 3, 3);
        }
    }
    /* player tag */
    lbl = M.human[i] ? (i == 0 ? "P1" : "P2") : "AI";
    if (gy - 66 >= HUD_H + 1 && !is_lying(p)) {
        int tx = clampi(x - 8, 0, SCR_W - 17);
        txt(lbl, tx, gy - 66, M.human[i] ? C_GOLD : C_LTGRAY);
    }
    mark(x - 32, gy - 67, x + 33, gy + 6);   /* incl. the charge ring round a lying head */
}

static void draw_ball(void)
{
    int x = PX(ball.x), y = PX(ball.y), r = ball.r;
    int h = GROUND - (y + r);
    int sw = r + 2 - h / 16;
    uint8_t fillc = C_WHITE, outc = C_BLACK, spot = C_BLACK;
    uint8_t k;

    if (sw < 2) sw = 2;
    gfx_SetColor(C_SHADOW);
    gfx_FillRectangle(x - sw, GROUND + 1, sw * 2, 2);
    mark(x - sw, GROUND + 1, x + sw, GROUND + 3);

    switch (ball.mode) {
    case BM_FIRE: fillc = C_ORANGE; outc = C_RED; spot = C_YELLOW; break;
    case BM_TELE: fillc = C_PINK; outc = C_PURPLE; spot = C_PURPLE; break;
    case BM_ROCKET: fillc = C_LTGRAY; outc = C_DKGRAY; spot = C_RED; break;
    case BM_ZAP: fillc = C_YELLOW; outc = C_ORANGE; spot = C_WHITE; break;
    case BM_METEOR_UP:
    case BM_METEOR_DOWN: fillc = C_RED; outc = C_DKRED; spot = C_ORANGE; break;
    case BM_TORNADO: fillc = C_ICE; outc = C_GRAY; spot = C_WHITE; break;
    case BM_MEGA: outc = C_GOLD; break;
    default: break;
    }
    gfx_SetColor(fillc);
    gfx_FillCircle(x, y, (uint24_t)r);
    gfx_SetColor(outc);
    gfx_Circle(x, y, (uint24_t)r);
    gfx_SetColor(spot);
    for (k = 0; k < 3; k++) {
        uint8_t a = (uint8_t)((ball.spin / 8 + k * 11) & 31);
        int sx = x + COS32(a) * r / 128, sy = y + SIN32[a] * r / 128;
        gfx_FillRectangle(sx - 1, sy - 1, r > 8 ? 4 : 2, r > 8 ? 4 : 2);
    }
    mark(x - r - 1, y - r - 1, x + r + 2, y + r + 2);
}

static void draw_particles(void)
{
    uint8_t i;
    for (i = 0; i < MAX_PARTS; i++) {
        particle_t *p = &parts[i];
        int x, y;
        if (!p->life) continue;
        x = p->x >> 4;
        y = p->y >> 4;
        gfx_SetColor(p->col);
        gfx_FillRectangle(x, y, p->size, p->size);
        mark(x, y, x + p->size, y + p->size);
    }
}

static void overlay_text(const char *s, int y, uint8_t c, uint8_t sc)
{
    int w = txt_w(s, sc);
    txt_c(s, y, c, sc);
    mark((SCR_W - w) / 2, y, (SCR_W + w) / 2 + sc, y + 8 * sc + sc);
}

static void draw_overlays(void)
{
    char buf[12], *p;
    switch (M.phase) {
    case PH_KICKOFF:
        if (M.phase_t > 8) overlay_text("READY", 64, C_WHITE, 3);
        else overlay_text("GO!", 64, C_GOLD, 3);
        break;
    case PH_GOAL:
        overlay_text("GOAL!", 52, (M.phase_t & 4) ? C_GOLD : C_WHITE, 4);
        overlay_text(TEAMS[M.team[M.last_scorer]].name, 92, C_WHITE, 2);
        break;
    case PH_HALFTIME:
        overlay_text("HALF TIME", 56, C_GOLD, 3);
        p = u2s(buf, M.score[0]);
        p = scat(p, " - ");
        u2s(p, M.score[1]);
        overlay_text(buf, 90, C_WHITE, 2);
        break;
    case PH_GGINTRO:
        overlay_text("GOLDEN GOAL", 56, C_GOLD, 2);
        overlay_text("NEXT GOAL WINS!", 80, C_WHITE, 1);
        break;
    default:
        break;
    }
}

static void render_match(void)
{
    hud_state_t h;
    uint8_t b = cur_buf;

    restore_dirty(b);
    TEST_HOOK(test_after_restore(crowd_cheer));
    gfx_SetClipRegion(0, 0, SCR_W, SCR_H);
    hud_compute(&h);
    if (!hud_valid[b] || memcmp(&h, &hud_last[b], sizeof h) != 0) {
        draw_hud(&h);
        hud_last[b] = h;
        hud_valid[b] = true;
    }
    gfx_SetClipRegion(0, HUD_H, SCR_W, SCR_H);
    draw_player(0);
    draw_player(1);
    if (M.phase != PH_KICKOFF || (M.phase_t & 2) || M.phase_t < 12) draw_ball();
    draw_particles();
    draw_overlays();
    gfx_SetClipRegion(0, 0, SCR_W, SCR_H);
    gfx_SwapDraw();
    cur_buf ^= 1;
}

static void full_redraw_both(void)
{
    uint8_t k;
    for (k = 0; k < 2; k++) {
        bg_full();
        ndirty[k] = 0;
        dirty_full[k] = false;
        hud_valid[k] = false;
        gfx_SwapDraw();
    }
    cur_buf = 0;
    TEST_HOOK(test_bg_ready(crowd_cheer));
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
    gfx_SetDrawScreen();
    gfx_SetClipRegion(0, 0, SCR_W, SCR_H);
    for (;;) {
        tick_wait();
        read_keys();
        TEST_HOOK(test_screen(SCR_ID_PAUSE));
        if (KPRESS(7, kb_Up)) { sel = (uint8_t)(sel ? sel - 1 : 2); redraw = true; }
        if (KPRESS(7, kb_Down)) { sel = (uint8_t)(sel == 2 ? 0 : sel + 1); redraw = true; }
        if (key_back()) { sel = PAUSE_RESUME; break; }
        if (key_ok()) break;
        if (redraw) {
            panel(80, 70, 160, 100);
            txt_c("PAUSED", 80, C_GOLD, 2);
            for (k = 0; k < 3; k++) {
                int y = 108 + k * 18;
                if (k == sel) {
                    gfx_SetColor(C_SEL);
                    gfx_FillRectangle(92, y - 3, 136, 14);
                }
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
    M.kit_alt[1] = t0 == t1;
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
    crowd_cheer = 0;
    start_kickoff(-1);
}

static uint8_t play_match(void)
{
    ctrl_t c[2];
    uint8_t i, steps;

    full_redraw_both();
    tick_reset();
    for (;;) {
        /* wait for the next fixed tick */
        while (!tick_due()) {
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
            }
            match_tick(&c[0], &c[1]);
            tick_advance();
            steps++;
            if (M.phase == PH_END) {
                render_match();
                TEST_HOOK(test_on_match_end(M.team[0], M.team[1], M.score[0], M.score[1], M.total_ticks));
                return RES_DONE;
            }
        } while (tick_due() && steps < 3);
        if (tick_due()) tick_reset();
        render_match();
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
    if (!h) return;
    if (ti_Read(&save, sizeof save, 1, h) != 1 || save.magic != SAVE_MAGIC || save.ver != SAVE_VER) {
        save_defaults();
    }
    ti_Close(h);
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
/* Menu screens                                                        */
/* ------------------------------------------------------------------ */
static void screen_bg(void)
{
    gfx_SetClipRegion(0, 0, SCR_W, SCR_H);
    gfx_SetColor(C_HUD);
    gfx_FillRectangle_NoClip(0, 0, SCR_W, HUD_H);
    bg_full();
}

static void show(void)
{
    gfx_SwapDraw();
}

static void footer(const char *s)
{
    gfx_SetColor(C_PANEL);
    gfx_FillRectangle_NoClip(0, 220, SCR_W, 20);
    gfx_SetColor(C_GOLD);
    gfx_HorizLine_NoClip(0, 220, SCR_W);
    txt_c(s, 226, C_LTGRAY, 1);
}

/* simple vertical menu; returns index or -1 on Clear */
static int run_menu(bool title, const char *const *items, uint8_t n, uint8_t sel,
                    uint8_t screen_id, const char *const *extra)
{
    bool redraw = true;
    uint8_t k;
    (void)screen_id;
    for (;;) {
        tick_wait();
        read_keys();
        TEST_HOOK(test_screen(screen_id));
        if (KPRESS(7, kb_Up)) { sel = (uint8_t)(sel ? sel - 1 : n - 1); redraw = true; }
        if (KPRESS(7, kb_Down)) { sel = (uint8_t)(sel + 1 >= n ? 0 : sel + 1); redraw = true; }
        if (key_back()) return -1;
        if (key_ok()) return sel;
        if (KPRESS(7, kb_Left) || KPRESS(7, kb_Right)) return 100 + sel;
        if (!redraw) continue;
        redraw = false;
        screen_bg();
        if (title) {
            panel(44, 28, 232, 58);
            txt_c("FOOTBALL", 34, C_GOLD, 3);
            txt_c("LEGENDS CE", 62, C_WHITE, 2);
            draw_char(40, 132, 0, 1, false, POSE_KICK, 0, 1);
            draw_char(280, 132, 7, -1, false, POSE_JUMP, 0, -1);
            gfx_SetColor(C_WHITE);
            gfx_FillCircle(70, 118, 6);
            gfx_SetColor(C_BLACK);
            gfx_Circle(70, 118, 6);
        }
        panel(72, 90, 176, (int)n * 18 + 16);
        for (k = 0; k < n; k++) {
            int y = 100 + k * 18;
            const char *s = items[k];
            if (extra && extra[k]) s = extra[k];
            if (k == sel) {
                gfx_SetColor(C_SEL);
                gfx_FillRectangle(78, y - 3, 164, 14);
            }
            txt_c(s, y, k == sel ? C_WHITE : C_LTGRAY, 1);
        }
        footer("ARROWS:MOVE  2ND/ENTER:OK");
        show();
    }
}

static void draw_bracket_line(int x, int y, uint8_t a, uint8_t b, const uint8_t *score, bool played, bool mine)
{
    char buf[16], *p;
    if (mine) {
        gfx_SetColor(C_SEL);
        gfx_FillRectangle(x - 2, y - 2, 92, 12);
    }
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
    panel(0, 26, SCR_W, 194);
    p = scat(buf, "CUP: ");
    scat(p, rname[T->round]);
    txt_c(buf, 34, C_GOLD, 1);
    txt("QUARTERS", 4, 50, C_LTGRAY);
    txt("SEMIS", 116, 50, C_LTGRAY);
    txt("FINAL", 228, 50, C_LTGRAY);
    for (k = 0; k < 4; k++) {
        draw_bracket_line(6, 66 + k * 32, T->slot[k * 2], T->slot[k * 2 + 1], T->qf[k], T->round > 0, k == 0 && T->round == 0);
    }
    for (k = 0; k < 2; k++) {
        if (T->round >= 1)
            draw_bracket_line(118, 82 + k * 64, T->w_qf[k * 2], T->w_qf[k * 2 + 1], T->sf[k], T->round > 1, k == 0 && T->round == 1);
        else
            txt("--- v ---", 118, 82 + k * 64, C_GRAY);
    }
    if (T->round >= 2) draw_bracket_line(230, 114, T->w_sf[0], T->w_sf[1], T->fn, false, true);
    else txt("--- v ---", 230, 114, C_GRAY);
    gfx_SetColor(C_GOLD);
    for (k = 0; k < 2; k++) {
        gfx_VertLine(102, 70 + k * 64, 33);
        gfx_HorizLine(102, 86 + k * 64, 12);
    }
    gfx_VertLine(214, 86, 65);
    gfx_HorizLine(214, 118, 12);
    p = scat(buf, "YOU: ");
    p = scat(p, TEAMS[T->team].name);
    p = scat(p, "  (");
    p = scat(p, POWER_NAME[TEAMS[T->team].power]);
    scat(p, ")");
    txt_c(buf, 196, C_WHITE, 1);
    footer("2ND:PLAY  CLEAR:MENU  DEL:QUIT CUP");
    show();
}

static void message_screen(const char *big, const char *line1, const char *line2, uint8_t col)
{
    screen_bg();
    panel(20, 60, 280, 120);
    txt_c(big, 80, col, 3);
    if (line1) txt_c(line1, 120, C_WHITE, 1);
    if (line2) txt_c(line2, 136, C_LTGRAY, 1);
    txt_c("PRESS 2ND OR ENTER", 160, C_GOLD, 1);
    show();
    for (;;) {
        tick_wait();
        read_keys();
        TEST_HOOK(test_screen(SCR_ID_MESSAGE));
        if (key_ok() || key_back()) return;
    }
}

/* Team select. Returns team index or -1. */
static int team_select(const char *who, int start)
{
    int sel = start;
    bool redraw = true;
    uint8_t k;
    for (;;) {
        tick_wait();
        read_keys();
        TEST_HOOK(test_screen(SCR_ID_TEAMSEL));
        if (KPRESS(7, kb_Left)) { sel = (sel + 7) & 7; redraw = true; }
        if (KPRESS(7, kb_Right)) { sel = (sel + 1) & 7; redraw = true; }
        if (KPRESS(7, kb_Up) || KPRESS(7, kb_Down)) { sel ^= 4; redraw = true; }
        if (key_back()) return -1;
        if (key_ok()) return sel;
        if (!redraw) continue;
        redraw = false;
        screen_bg();
        panel(0, 26, SCR_W, 214);
        {
            char buf[40], *p = scat(buf, who);
            scat(p, ": CHOOSE YOUR LEGEND");
            txt_c(buf, 32, C_GOLD, 1);
        }
        for (k = 0; k < NUM_TEAMS; k++) {
            int cx = 10 + (k & 3) * 76, cy = 44 + (k >> 2) * 60;
            uint8_t shirt, trim, shorts;
            kit_of(k, false, &shirt, &trim, &shorts);
            gfx_SetColor(k == sel ? C_SEL : C_PANEL2);
            gfx_FillRectangle(cx, cy, 72, 56);
            if (k == sel) {
                gfx_SetColor(C_GOLD);
                gfx_Rectangle(cx, cy, 72, 56);
                gfx_Rectangle(cx + 1, cy + 1, 70, 54);
            }
            gfx_SetColor(shirt);
            gfx_FillRectangle(cx + 26, cy + 34, 20, 8);
            gfx_SetColor(trim);
            gfx_FillRectangle(cx + 32, cy + 34, 8, 2);
            draw_head(cx + 36, cy + 20, HEAD_R, k, 1);
            txt(TEAMS[k].code, cx + 24, cy + 45, C_WHITE);
        }
        {
            const team_t *T = &TEAMS[sel];
            char buf[40], *p;
            draw_char(40, 210, (uint8_t)sel, 1, false, POSE_STAND, 0, 1);
            txt_sh(T->name, 76, 166, C_WHITE, 2);
            p = scat(buf, "LEGEND: ");
            scat(p, T->legend);
            txt(buf, 76, 186, C_LTGRAY);
            p = scat(buf, "POWER: ");
            scat(p, POWER_NAME[T->power]);
            txt(buf, 76, 198, POWER_COL[T->power] == C_WHITE ? C_ICE : POWER_COL[T->power]);
            txt(POWER_DESC[T->power], 8, 216, C_WHITE);
            txt_c("ARROWS:CHOOSE 2ND:OK CLEAR:BACK", 229, C_LTGRAY, 1);
        }
        show();
    }
}

static void results_screen(const char *headline, uint8_t hcol)
{
    char buf[40], *p;
    uint8_t k;
    screen_bg();
    panel(8, 30, 304, 200);
    txt_c("FULL TIME", 38, C_LTGRAY, 1);
    txt_c(headline, 54, hcol, 2);
    for (k = 0; k < 2; k++) {
        int x = k ? 250 : 70;
        draw_char(x, 150, M.team[k], k ? -1 : 1, M.kit_alt[k], M.score[k] > M.score[k ^ 1] ? POSE_JUMP : POSE_STAND, 0, 1);
        txt(TEAMS[M.team[k]].code, x - 12, 158, C_WHITE);
    }
    p = u2s(buf, M.score[0]);
    p = scat(p, "-");
    u2s(p, M.score[1]);
    txt_c(buf, 96, C_WHITE, 4);
    if (M.half == 2) txt_c("GOLDEN GOAL", 136, C_GOLD, 1);
    p = scat(buf, "TOUCHES ");
    p = u2s(p, M.touches[0]);
    p = scat(p, " - ");
    u2s(p, M.touches[1]);
    txt_c(buf, 176, C_LTGRAY, 1);
    p = scat(buf, "SHOTS ");
    p = u2s(p, M.shots[0]);
    p = scat(p, " - ");
    p = u2s(p, M.shots[1]);
    p = scat(p, "   SUPERS ");
    p = u2s(p, M.supers[0]);
    p = scat(p, " - ");
    u2s(p, M.supers[1]);
    txt_c(buf, 190, C_LTGRAY, 1);
    txt_c("PRESS 2ND OR ENTER", 212, C_GOLD, 1);
    show();
    for (;;) {
        tick_wait();
        read_keys();
        TEST_HOOK(test_screen(SCR_ID_RESULTS));
        if (key_ok() || key_back()) return;
    }
}

static void stats_screen(void)
{
    char buf[40], *p;
    screen_bg();
    panel(20, 30, 280, 196);
    txt_c("RECORDS", 40, C_GOLD, 2);
#define STAT_LINE(label, val, y) \
    p = scat(buf, label); u2s(p, (unsigned)(val)); txt(buf, 48, y, C_WHITE)
    STAT_LINE("MATCHES PLAYED  ", save.played, 70);
    STAT_LINE("WON             ", save.won, 84);
    STAT_LINE("LOST            ", save.lost, 98);
    STAT_LINE("GOALS FOR       ", save.gf, 112);
    STAT_LINE("GOALS AGAINST   ", save.ga, 126);
    STAT_LINE("BIGGEST WIN BY  ", save.best_margin, 140);
    STAT_LINE("BEST WIN STREAK ", save.best_streak, 154);
    STAT_LINE("CUPS ENTERED    ", save.cups_played, 168);
    STAT_LINE("CUPS WON        ", save.cups, 182);
#undef STAT_LINE
    txt_c("FRIENDLY MATCHES ARE NOT COUNTED", 198, C_LTGRAY, 1);
    txt_c("PRESS 2ND OR CLEAR", 212, C_GOLD, 1);
    show();
    for (;;) {
        tick_wait();
        read_keys();
        TEST_HOOK(test_screen(SCR_ID_STATS));
        if (key_ok() || key_back()) return;
    }
}

/* ------------------------------------------------------------------ */
/* Modes                                                               */
/* ------------------------------------------------------------------ */
static int last_team;

static void quick_match(void)
{
    int t = team_select("P1", last_team), o;
    uint8_t r;
    if (t < 0) return;
    last_team = t;
    do {
        o = rnd_n(NUM_TEAMS);
    } while (o == t);
    match_setup(MODE_QUICK, (uint8_t)t, (uint8_t)o, !TEST_P1_AI, false, save.diff, 0);
    r = play_match();
    if (r == RES_QUIT) return;
    record_result(M.score[0], M.score[1]);
    save_store(false);
    results_screen(M.score[0] > M.score[1] ? "YOU WIN!" : "YOU LOSE", M.score[0] > M.score[1] ? C_GOLD : C_RED);
}

static void friendly(void)
{
    int t0, t1;
    t0 = team_select("P1", last_team);
    if (t0 < 0) return;
    t1 = team_select("P2", (t0 + 1) & 7);
    if (t1 < 0) return;
    match_setup(MODE_FRIENDLY, (uint8_t)t0, (uint8_t)t1, !TEST_P1_AI, !TEST_P1_AI, save.diff, 0);
    if (play_match() == RES_QUIT) return;
    results_screen(M.score[0] > M.score[1] ? "P1 WINS!" : "P2 WINS!", C_GOLD);
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
    uint8_t k, n = 1;
    memset(T, 0, sizeof *T);
    T->active = 1;
    T->team = team;
    T->diff = save.diff;
    T->slot[0] = team;
    for (k = 0; k < NUM_TEAMS; k++) if (k != team) T->slot[n++] = k;
    for (k = 7; k > 1; k--) {   /* shuffle opponents */
        uint8_t j = (uint8_t)(1 + rnd_n(k)), tmp = T->slot[k];
        T->slot[k] = T->slot[j];
        T->slot[j] = tmp;
    }
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
        bool redraw = true;
        /* bracket screen */
        for (;;) {
            if (redraw) { tour_bracket_draw(); redraw = false; }
            tick_wait();
            read_keys();
            TEST_HOOK(test_screen(SCR_ID_BRACKET));
            if (key_back()) return;
            if (KPRESS(1, kb_Del)) {
                T->active = 0;
                save_store(false);
                return;
            }
            if (key_ok()) break;
        }
        opp = T->round == 0 ? T->slot[1] : (T->round == 1 ? T->w_qf[1] : T->w_sf[1]);
        match_setup(MODE_TOUR, T->team, opp, !TEST_P1_AI, false, T->diff, T->round);
        r = play_match();
        if (r == RES_QUIT) continue;
        record_result(M.score[0], M.score[1]);
        /* store the player's result and simulate the rest of the round */
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
    static const char *const items[6] = {
        "QUICK MATCH", "TOURNAMENT", "FRIENDLY 2P", "DIFFICULTY", "RECORDS", "QUIT"
    };
    const char *extra[6] = { 0, 0, 0, 0, 0, 0 };
    uint8_t sel = 0;

    rng_s ^= (uint32_t)clock() * 2654435761UL;
    if (!rng_s) rng_s = 1;

    gfx_Begin();
    gfx_SetPalette(palette, sizeof palette, 0);
    gfx_SetDrawBuffer();
    gfx_SetTransparentColor(C_TRANSP);
    gfx_SetTextTransparentColor(C_TRANSP);
    gfx_SetTextBGColor(C_TRANSP);
    gfx_SetMonospaceFont(8);
    gfx_SetClipRegion(0, 0, SCR_W, SCR_H);
    gen_assets();
    save_load();

    read_keys();
    read_keys();   /* ignore keys still held from launching */
    tick_reset();

    for (;;) {
        int r;
        extra[1] = save.tour.active ? "CONTINUE CUP" : 0;
        extra[3] = save.diff ? "DIFFICULTY: HARD" : "DIFFICULTY: NORMAL";
        r = run_menu(true, items, 6, sel, SCR_ID_TITLE, extra);
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
        else if (sel == 2) friendly();
        else if (sel == 3) { save.diff ^= 1; save_store(false); }
        else if (sel == 4) stats_screen();
        else break;
    }

    gfx_End();
    save_store(true);
    return 0;
}
