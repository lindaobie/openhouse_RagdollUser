/* LPC11U3x counter/timers. Index 0..3 = CT16B0, CT16B1, CT32B0, CT32B1, matching
 * the stock timer.c numbering (timer_init @0xD0C4). */
#pragma once
#include <stdint.h>

typedef struct {
    volatile uint32_t IR, TCR, TC, PR, PC, MCR, MR[4], CCR, CR[3], _r0, EMR,
                      _r1[12], CTCR, PWMC;
} lpc_timer_t;

#define TMR_CT16B0  ((lpc_timer_t *)0x4000C000)
#define TMR_CT16B1  ((lpc_timer_t *)0x40010000)
#define TMR_CT32B0  ((lpc_timer_t *)0x40014000)
#define TMR_CT32B1  ((lpc_timer_t *)0x40018000)

/* IR bits */
#define IR_MR(n)    (1u << (n))
#define IR_CR(n)    (1u << (4 + (n)))   /* CT32B0 CAP1 is CR2 on LPC11U3x */
/* MCR bits for match n */
#define MCR_I(n)    (1u << (3 * (n)))
#define MCR_R(n)    (2u << (3 * (n)))
/* CCR bits for capture register n */
#define CCR_RISE(n) (1u << (3 * (n)))
#define CCR_I(n)    (4u << (3 * (n)))

void timers_sync_start(void);   /* stock timers_sync @0xCFA4: CT32B0+CT32B1 in lockstep */

static inline uint32_t timebase_now(void) { return TMR_CT32B0->TC; }
