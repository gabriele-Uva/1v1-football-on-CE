/* Hooks the game calls when built with -DFBL_HOST_TEST. */
#ifndef TEST_HOOKS_H
#define TEST_HOOKS_H
#include <stdint.h>
#include <stdbool.h>

int test_p1_ai(void);
int test_p1_diff(void);
int test_headless(void);
void test_chk24(long v, int line);
void test_screen(int id);
void test_on_goal(uint8_t scorer, uint8_t half, int half_ticks);
void test_on_super(uint8_t who, uint8_t power);
void test_on_match_end(uint8_t t0, uint8_t t1, uint8_t s0, uint8_t s1, unsigned long ticks);
void test_on_tour_end(uint8_t round, bool champion);
void test_after_restore(uint8_t crowd_jump, int shaking);
void test_bg_ready(uint8_t crowd_jump);
void test_on_load(uint8_t tour_active, uint8_t tour_round, unsigned played);
void test_on_watchdog(int x, int y);
void test_ball_tick(int x, int y, int vx, int vy);
void test_force_teams(int *t0, int *t1);
void test_on_series(uint8_t w0, uint8_t w1, uint8_t need);
void test_on_panic(void);
void test_match_frame(void);
void test_phase(int k);
void test_logic_frame(void);
void test_wcost(int w);
void test_rows(int n);
void test_memop(int bytes);
void test_bg_proc(void);
void test_bg_decoded(unsigned n);
void test_bgc_full(void);
void test_set_widget(uint8_t w, int x0, int y0, int x1, int y1);
void fbl_test_decode(const uint8_t *k, const uint8_t *pk, uint8_t who, int8_t dir, uint8_t *out6);
#endif
