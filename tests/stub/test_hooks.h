/* Hooks the game calls when built with -DFBL_HOST_TEST. */
#ifndef TEST_HOOKS_H
#define TEST_HOOKS_H
#include <stdint.h>
#include <stdbool.h>

int test_p1_ai(void);
int test_p1_diff(void);
void test_chk24(long v, int line);
void test_screen(int id);
void test_on_goal(uint8_t scorer, uint8_t half, int half_ticks);
void test_on_super(uint8_t who, uint8_t power);
void test_on_match_end(uint8_t t0, uint8_t t1, uint8_t s0, uint8_t s1, unsigned long ticks);
void test_on_tour_end(uint8_t round, bool champion);
void test_after_restore(uint8_t crowd_cheer);
void test_bg_ready(uint8_t crowd_cheer);
void test_on_load(uint8_t tour_active, uint8_t tour_round, unsigned played);
#endif
