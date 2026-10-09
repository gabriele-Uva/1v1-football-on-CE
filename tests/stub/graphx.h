/* Host-side stand-in for CEdev's graphx.h (test harness only).
 * Signatures mirror the real header; drawing goes to a fake 8bpp framebuffer. */
#ifndef STUB_GRAPHX_H
#define STUB_GRAPHX_H
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef unsigned int uint24_t;
typedef int int24_t;

typedef struct gfx_sprite_t {
    uint8_t width;
    uint8_t height;
    uint8_t data[];
} gfx_sprite_t;

typedef enum { gfx_8bpp = 0x27 } gfx_mode_t;
typedef enum { gfx_screen = 0, gfx_buffer } gfx_location_t;
typedef enum { gfx_text_clip = 1, gfx_text_noclip } gfx_text_options_t;

#define gfx_RGBTo1555(r,g,b) \
((uint16_t)(((((r) & 255) >> 3) << 10) | ((((g) & 255) >> 3) << 5) | (((b) & 255) >> 3)))

#define gfx_SetDrawBuffer() gfx_SetDraw(gfx_buffer)
#define gfx_SetDrawScreen() gfx_SetDraw(gfx_screen)

void gfx_Begin(void);
void gfx_End(void);
uint8_t gfx_SetColor(uint8_t index);
uint8_t gfx_SetTransparentColor(uint8_t index);
void gfx_SetPalette(const void *palette, uint24_t size, uint8_t offset);
void gfx_FillScreen(uint8_t index);
void gfx_SetPixel(uint24_t x, uint8_t y);
void gfx_Line(int x0, int y0, int x1, int y1);
void gfx_HorizLine(int x, int y, int length);
void gfx_HorizLine_NoClip(uint24_t x, uint8_t y, uint24_t length);
void gfx_VertLine(int x, int y, int length);
void gfx_Rectangle(int x, int y, int width, int height);
void gfx_Rectangle_NoClip(uint24_t x, uint8_t y, uint24_t width, uint8_t height);
void gfx_FillRectangle(int x, int y, int width, int height);
void gfx_FillRectangle_NoClip(uint24_t x, uint8_t y, uint24_t width, uint8_t height);
void gfx_Circle(int x, int y, uint24_t radius);
void gfx_FillCircle(int x, int y, uint24_t radius);
void gfx_FillTriangle(int x0, int y0, int x1, int y1, int x2, int y2);
void gfx_SetDraw(uint8_t location);
void gfx_SwapDraw(void);
void gfx_SetTextScale(uint8_t width_scale, uint8_t height_scale);
void gfx_PrintStringXY(const char *string, int x, int y);
uint8_t gfx_SetTextFGColor(uint8_t color);
uint8_t gfx_SetTextBGColor(uint8_t color);
uint8_t gfx_SetTextTransparentColor(uint8_t color);
void gfx_SetMonospaceFont(uint8_t spacing);
void gfx_Sprite(const gfx_sprite_t *sprite, int x, int y);
gfx_sprite_t *gfx_GetSprite(gfx_sprite_t *sprite_buffer, int x, int y);
void gfx_SetClipRegion(int xmin, int ymin, int xmax, int ymax);

#endif
