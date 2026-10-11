/* Host implementation of the graphx / keypadc / fileioc subset used by the
 * game. Test harness only: NOT part of the calculator build. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "graphx.h"
#include "keypadc.h"
#include "fileioc.h"
#include "font8.h"
#include "stub_internal.h"
#include <execinfo.h>
static int watch_x = -1, watch_y = -1;
static int draw_to_screen;
static void watch_check(int x0, int y0, int x1, int y1, uint8_t c)
{
    if (watch_x < 0) {
        const char *e = getenv("WATCH");
        if (!e) { watch_x = -2; return; }
        sscanf(e, "%d,%d", &watch_x, &watch_y);
    }
    if (watch_x < 0) return;
    if (watch_x >= x0 && watch_x < x1 && watch_y >= y0 && watch_y < y1) {
        extern long frames;
        fprintf(stderr, "WATCH f=%ld buf=%d c=%d rect %d,%d-%d,%d\n", frames, draw_to_screen ? scr_idx : scr_idx ^ 1, c, x0, y0, x1, y1);
        if (getenv("WATCHBT")) { void *bt[12]; int n = backtrace(bt, 12); backtrace_symbols_fd(bt, n, 2); }
    }
}

uint8_t vram[2][240][320];
uint16_t stub_pal[256];
int scr_idx = 0;            /* displayed buffer */
static int cx0 = 0, cy0 = 0, cx1 = 320, cy1 = 240;
static uint8_t color = 0, text_fg = 0, text_bg = 255, text_tr = 255, tscale_w = 1, tscale_h = 1;
uint8_t stub_kb_data[8];
long violations = 0;
long text_overflow = 0;
cost_t cost;

static uint8_t (*target(void))[320]
{
    return vram[draw_to_screen ? scr_idx : scr_idx ^ 1];
}

static void violation(const char *what, int a, int b, int c, int d)
{
    violations++;
    if (violations < 20) fprintf(stderr, "VIOLATION %s (%d,%d,%d,%d)\n", what, a, b, c, d);
}

static void watch_check(int x0, int y0, int x1, int y1, uint8_t c);
static inline void pset_clip(int x, int y, uint8_t c)
{
    if (x >= cx0 && x < cx1 && y >= cy0 && y < cy1) watch_check(x, y, x + 1, y + 1, c);
    if (x >= cx0 && x < cx1 && y >= cy0 && y < cy1) target()[y][x] = c;
}

static int in_shape;   /* rows of a circle / triangle: graphx draws them internally */
static void fill_clip(int x, int y, int w, int h, uint8_t c)
{
    int x1 = x + w, y1 = y + h, yy;
    if (!in_shape) cost.calls++;
    if (x < cx0) x = cx0;
    if (y < cy0) y = cy0;
    if (x1 > cx1) x1 = cx1;
    if (y1 > cy1) y1 = cy1;
    if (x >= x1 || y >= y1) return;
    watch_check(x, y, x1, y1, c);
    for (yy = y; yy < y1; yy++) memset(&target()[yy][x], c, (size_t)(x1 - x));
    cost.fill_px += (long)(x1 - x) * (y1 - y);
    cost.rows += y1 - y;
}

void gfx_Begin(void) { memset(vram, 0, sizeof vram); stub_gfx_ended = 0; }
int stub_gfx_ended = 0;
long stub_writes = 0;
void gfx_End(void) { stub_gfx_ended = 1; }
uint8_t gfx_SetColor(uint8_t index) { uint8_t o = color; color = index; return o; }
uint8_t stub_transp = 0;
uint8_t gfx_SetTransparentColor(uint8_t index) { uint8_t o = stub_transp; stub_transp = index; return o; }
void gfx_SetPalette(const void *palette, uint24_t size, uint8_t offset)
{
    if (offset + size / 2 > 256) violation("palette", (int)size, offset, 0, 0);
    memcpy(&stub_pal[offset], palette, size);
}
void gfx_FillScreen(uint8_t index) { cost.calls++; memset(target(), index, 320 * 240); cost.fill_px += 76800; }
void gfx_SetPixel(uint24_t x, uint8_t y)
{
    cost.calls++;
    if (x < 320 && y < 240) target()[y][x] = color;
}
void gfx_Line(int x0, int y0, int x1, int y1)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, err = dx + dy;
    cost.calls++;
    for (;;) {
        pset_clip(x0, y0, color);
        cost.slow_px++;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}
void gfx_HorizLine(int x, int y, int length) { fill_clip(x, y, length, 1, color); }
void gfx_HorizLine_NoClip(uint24_t x, uint8_t y, uint24_t length)
{
    if (x + length > 320 || y >= 240) violation("HorizLine_NoClip", (int)x, y, (int)length, 0);
    else fill_clip((int)x, y, (int)length, 1, color);
}
void gfx_VertLine(int x, int y, int length) { fill_clip(x, y, 1, length, color); }
void gfx_Rectangle(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0) return;
    fill_clip(x, y, w, 1, color);
    fill_clip(x, y + h - 1, w, 1, color);
    fill_clip(x, y, 1, h, color);
    fill_clip(x + w - 1, y, 1, h, color);
}
void gfx_Rectangle_NoClip(uint24_t x, uint8_t y, uint24_t w, uint8_t h)
{
    if (x + w > 320 || y + h > 240) violation("Rectangle_NoClip", (int)x, y, (int)w, h);
    else gfx_Rectangle((int)x, y, (int)w, h);
}
void gfx_FillRectangle(int x, int y, int w, int h) { if (w > 0 && h > 0) fill_clip(x, y, w, h, color); }
void gfx_FillRectangle_NoClip(uint24_t x, uint8_t y, uint24_t w, uint8_t h)
{
    if (x + w > 320 || (int)y + h > 240) violation("FillRectangle_NoClip", (int)x, y, (int)w, h);
    else fill_clip((int)x, y, (int)w, h, color);
}
void gfx_Circle(int xc, int yc, uint24_t radius)
{
    int r = (int)radius, x = 0, y = r, d = 3 - 2 * r;
    cost.calls++;
    while (y >= x) {
        pset_clip(xc + x, yc + y, color); pset_clip(xc - x, yc + y, color);
        pset_clip(xc + x, yc - y, color); pset_clip(xc - x, yc - y, color);
        pset_clip(xc + y, yc + x, color); pset_clip(xc - y, yc + x, color);
        pset_clip(xc + y, yc - x, color); pset_clip(xc - y, yc - x, color);
        cost.slow_px += 8;
        x++;
        if (d > 0) { y--; d += 4 * (x - y) + 10; } else d += 4 * x + 6;
    }
}
void gfx_FillCircle(int xc, int yc, uint24_t radius)
{
    int r = (int)radius, y;
    cost.calls++;
    in_shape = 1;
    for (y = -r; y <= r; y++) {
        int w = 0;
        while ((w + 1) * (w + 1) + y * y <= r * r + r) w++;
        fill_clip(xc - w, yc + y, 2 * w + 1, 1, color);
    }
    in_shape = 0;
}
void gfx_FillTriangle(int x0, int y0, int x1, int y1, int x2, int y2)
{
    int miny = y0, maxy = y0, y;
    cost.calls++;
    in_shape = 1;
    if (y1 < miny) miny = y1;
    if (y2 < miny) miny = y2;
    if (y1 > maxy) maxy = y1;
    if (y2 > maxy) maxy = y2;
    for (y = miny; y <= maxy; y++) {
        int xs[3], n = 0, i;
        int px[3] = { x0, x1, x2 }, py[3] = { y0, y1, y2 };
        for (i = 0; i < 3; i++) {
            int j = (i + 1) % 3;
            int ya = py[i], yb = py[j];
            if (ya == yb) continue;
            if ((y >= ya && y <= yb) || (y >= yb && y <= ya)) {
                if (n < 3) xs[n++] = px[i] + (px[j] - px[i]) * (y - ya) / (yb - ya);
            }
        }
        if (n >= 2) {
            int a = xs[0], b = xs[1];
            if (n == 3) { if (xs[2] < a) a = xs[2]; if (xs[2] > b) b = xs[2]; }
            if (a > b) { int t = a; a = b; b = t; }
            fill_clip(a, y, b - a + 1, 1, color);
        }
    }
    in_shape = 0;
}
void gfx_SetDraw(uint8_t location) { draw_to_screen = location == gfx_screen; }
void gfx_SwapDraw(void)
{
    scr_idx ^= 1;
    stub_on_swap();
}
void gfx_SetTextScale(uint8_t w, uint8_t h) { tscale_w = w; tscale_h = h; }
uint8_t gfx_SetTextFGColor(uint8_t c) { uint8_t o = text_fg; text_fg = c; return o; }
uint8_t gfx_SetTextBGColor(uint8_t c) { uint8_t o = text_bg; text_bg = c; return o; }
uint8_t gfx_SetTextTransparentColor(uint8_t c) { uint8_t o = text_tr; text_tr = c; return o; }
void gfx_SetMonospaceFont(uint8_t spacing) { (void)spacing; }
void gfx_PrintStringXY(const char *s, int x, int y)
{
    int bad = 0;
    cost.calls++;
    for (; *s; s++, x += 8 * tscale_w) {
        unsigned char ch = (unsigned char)*s;
        int gx, gy;
        cost.chars++;
        if (ch < 32 || ch > 127) ch = '?';
        for (gy = 0; gy < 8 * tscale_h; gy++) {
            for (gx = 0; gx < 8 * tscale_w; gx++) {
                int on = stub_font[ch - 32][gy / tscale_h] & (0x80 >> (gx / tscale_w));
                int px = x + gx, py = y + gy;
                uint8_t c = on ? text_fg : text_bg;
                if (!on && text_bg == text_tr) continue;
                if (px < 0 || px >= 320 || py < 0 || py >= 240) { bad = 1; continue; }
                target()[py][px] = c;
                cost.text_px++;
            }
        }
    }
    if (bad) {
        text_overflow++;
        if (text_overflow < 10) fprintf(stderr, "TEXT OVERFLOW at y=%d\n", y);
    }
}
void gfx_Sprite(const gfx_sprite_t *spr, int x, int y)
{
    int i, j;
    cost.calls++;
    for (j = 0; j < spr->height; j++) {
        int py = y + j;
        if (py < cy0 || py >= cy1) continue;
        cost.rows++;
        for (i = 0; i < spr->width; i++) {
            int px = x + i;
            if (px < cx0 || px >= cx1) continue;
            target()[py][px] = spr->data[j * spr->width + i];
            cost.sprite_px++;
        }
    }
}
void gfx_TransparentSprite(const gfx_sprite_t *spr, int x, int y)
{
    int i, j;
    cost.calls++;
    for (j = 0; j < spr->height; j++) {
        int py = y + j;
        if (py < cy0 || py >= cy1) continue;
        cost.rows++;
        for (i = 0; i < spr->width; i++) {
            int px = x + i;
            uint8_t c = spr->data[j * spr->width + i];
            if (px < cx0 || px >= cx1) continue;
            cost.sprite_px++;
            if (c != stub_transp) target()[py][px] = c;
        }
    }
}
gfx_sprite_t *gfx_GetSprite(gfx_sprite_t *spr, int x, int y)
{
    int i, j;
    for (j = 0; j < spr->height; j++)
        for (i = 0; i < spr->width; i++) {
            int px = x + i, py = y + j;
            spr->data[j * spr->width + i] = (px >= 0 && px < 320 && py >= 0 && py < 240) ? target()[py][px] : 0;
        }
    return spr;
}
void gfx_SetClipRegion(int xmin, int ymin, int xmax, int ymax)
{
    cx0 = xmin < 0 ? 0 : xmin;
    cy0 = ymin < 0 ? 0 : ymin;
    cx1 = xmax > 320 ? 320 : xmax;
    cy1 = ymax > 240 ? 240 : ymax;
}

uint8_t (*stub_draw_buffer(void))[320] { return target(); }
uint8_t (*stub_vbuf(void))[240][320] { cost.direct += 8; return &vram[scr_idx ^ 1]; }
void gfx_Wait(void) {}

/* ---------------- keypad ---------------- */
/* keypad matrix ghosting: rows sharing a pressed column get connected */
void stub_ghost(uint8_t *kb)
{
    int ch = 1, a, b;
    while (ch) {
        ch = 0;
        for (a = 1; a < 8; a++)
            for (b = 1; b < 8; b++)
                if (a != b && (kb[a] & kb[b])) {
                    uint8_t u = (uint8_t)(kb[a] | kb[b]);
                    if (kb[a] != u) { kb[a] = u; ch = 1; }
                    if (kb[b] != u) { kb[b] = u; ch = 1; }
                }
    }
}
int stub_ghost_on = 1;
void kb_Scan(void) { stub_feed_keys(stub_kb_data); if (stub_ghost_on) stub_ghost(stub_kb_data); }

/* ---------------- fileioc ---------------- */
typedef struct { char name[9]; uint8_t data[4096]; size_t size; int used; int archived; } var_t;
static var_t vars[4];
static struct { int var; size_t pos; int open; } handles[6];

uint8_t ti_Open(const char *name, const char *mode)
{
    int v = -1, i, h;
    for (i = 0; i < 4; i++) if (vars[i].used && strcmp(vars[i].name, name) == 0) v = i;
    if (mode[0] == 'r' && v < 0) return 0;
    if (mode[0] == 'w') {
        if (v < 0) for (i = 0; i < 4; i++) if (!vars[i].used) { v = i; break; }
        if (v < 0) return 0;
        vars[v].used = 1;
        snprintf(vars[v].name, sizeof vars[v].name, "%s", name);
        vars[v].size = 0;
        vars[v].archived = 0;
    }
    for (h = 1; h < 6; h++) if (!handles[h].open) {
        handles[h].open = 1; handles[h].var = v; handles[h].pos = 0;
        stub_files_open++;
        return (uint8_t)h;
    }
    return 0;
}
int ti_Close(uint8_t h) { if (h && h < 6 && handles[h].open) { handles[h].open = 0; stub_files_open--; } return 1; }
size_t ti_Write(const void *data, size_t size, size_t count, uint8_t h)
{
    var_t *v = &vars[handles[h].var];
    size_t n = size * count;
    stub_writes++;
    if (!handles[h].open || handles[h].pos + n > sizeof v->data) return 0;
    memcpy(v->data + handles[h].pos, data, n);
    handles[h].pos += n;
    if (handles[h].pos > v->size) v->size = handles[h].pos;
    return count;
}
size_t ti_Read(void *data, size_t size, size_t count, uint8_t h)
{
    var_t *v = &vars[handles[h].var];
    size_t n = size * count;
    if (!handles[h].open || handles[h].pos + n > v->size) return 0;
    memcpy(data, v->data + handles[h].pos, n);
    handles[h].pos += n;
    return count;
}
int ti_SetArchiveStatus(uint8_t archive, uint8_t h) { vars[handles[h].var].archived = archive; return 1; }
int stub_files_open = 0;
size_t stub_var_size(const char *name)
{
    int i;
    for (i = 0; i < 4; i++) if (vars[i].used && strcmp(vars[i].name, name) == 0) return vars[i].size;
    return 0;
}
int stub_var_archived(const char *name)
{
    int i;
    for (i = 0; i < 4; i++) if (vars[i].used && strcmp(vars[i].name, name) == 0) return vars[i].archived;
    return -1;
}
