/* Host-side stand-in for CEdev's keypadc.h (test harness only). */
#ifndef STUB_KEYPADC_H
#define STUB_KEYPADC_H
#include <stdint.h>

extern uint8_t stub_kb_data[8];
void kb_Scan(void);
#define kb_Data stub_kb_data

#define kb_Graph    (1<<0)
#define kb_Trace    (1<<1)
#define kb_Zoom     (1<<2)
#define kb_Window   (1<<3)
#define kb_Yequ     (1<<4)
#define kb_2nd      (1<<5)
#define kb_Mode     (1<<6)
#define kb_Del      (1<<7)
#define kb_Math     (1<<6)
#define kb_Alpha    (1<<7)
#define kb_0        (1<<0)
#define kb_1        (1<<1)
#define kb_4        (1<<2)
#define kb_2        (1<<1)
#define kb_5        (1<<2)
#define kb_3        (1<<1)
#define kb_Add      (1<<1)
#define kb_Sub      (1<<2)
#define kb_Comma    (1<<4)
#define kb_Apps     (1<<6)
#define kb_LParen   (1<<4)
#define kb_Prgm     (1<<6)
#define kb_RParen   (1<<4)
#define kb_Vars     (1<<6)
#define kb_Stat     (1<<7)
#define kb_Enter    (1<<0)
#define kb_Clear    (1<<6)
#define kb_Down     (1<<0)
#define kb_Left     (1<<1)
#define kb_Right    (1<<2)
#define kb_Up       (1<<3)
#endif
