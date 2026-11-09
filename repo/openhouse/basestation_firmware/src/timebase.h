/* Timebase (stock timebase.c). Phase 2: internal master only - one reference
 * edge per period on CT32B1 MR1. Optical slave sync arrives in phase 3; cable
 * sync is not supported (purely optical design). */
#pragma once
#include <stdint.h>
#include <stdbool.h>

void     timebase_init(void);       /* stock timebase_init @0xCAAC (subset) */
void     timebase_start(void);
uint32_t timebase_period(void);     /* current nominal period, ticks */
uint32_t timebase_edges(void);      /* reference edges since start */

/* skip bit for the current revolution: TDM modes alternate it every timebase
 * edge (stock flag @0x1000067D); single-station masters never skip. */
int  timebase_skip(void);
bool timebase_flash_this_rev(void);     /* false: stay dark this revolution (multi) */
uint32_t timebase_rev(void);
uint8_t  timebase_sf_confirm(void);
extern volatile uint32_t sf_realign_count;
bool timebase_permits_emission(void);
void timebase_supervisor(void);         /* every 500 ms */
void timebase_exit_standby(void);
void timebase_select_input(void);       /* call after a channel change */
void timebase_mask_own(bool masked);    /* hide own flashes/sweeps from the receiver */
const char *timebase_corr_state(void);
uint32_t timebase_pulses_seen(void), timebase_pulses_used(void), timebase_last_width(void);
int32_t  timebase_phase_err(void);
void     timebase_dump_trace(void (*out)(const char *fmt, ...));

/* Emission control (stock standby_outputs @0x3220 + P1_0 handling in
 * timebase_mr1_isr): emitter power P0_23 (active low), sync gate P1_0, carrier. */
extern volatile bool sync_requested;    /* lhbs: console 'sync on' */
bool emission_update(void);             /* call periodically; returns emitting */
bool emission_active(void);             /* last state set by emission_update */
