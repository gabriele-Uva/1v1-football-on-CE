#ifndef STUB_INTERNAL_H
#define STUB_INTERNAL_H
#include <stdint.h>
#include <stddef.h>

typedef struct {
    long calls, fill_px, rows, sprite_px, slow_px, text_px, chars, direct, memops, mem_bytes;
} cost_t;
#define NCOST 10

extern uint8_t vram[2][240][320];
extern uint16_t stub_pal[256];
extern int scr_idx;
extern long violations, text_overflow;
extern cost_t cost;
extern int stub_files_open;

uint8_t (*stub_draw_buffer(void))[320];
void stub_on_swap(void);
void stub_feed_keys(uint8_t *kb);
size_t stub_var_size(const char *name);
int stub_var_archived(const char *name);
extern int stub_gfx_ended, stub_ghost_on;
extern long stub_writes;
void stub_ghost(uint8_t *kb);
#endif
