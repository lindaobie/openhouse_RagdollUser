/* Multi-station (2-4 base station) mode - lhbs extension, see ai/firmware/MULTISTATION.md.
 * Configuration lives in the parameter payload's reserved slot (stock
 * 'reserved1', 120 bytes, zero in stock EEPROMs and preserved by stock), so the
 * stock fields never hold non-stock values.
 *
 * Each station's slot (1-4 on the button, 0-3 inside) is set by hand; how many take
 * turns sweeping is not: the master sees which slots flash, announces that set in its
 * OOTX frame, and every station sweeps in the round of the stations present (three
 * stations: 60 sweeps/s shared by three, not four turns with one empty). */
#pragma once
#include <stdint.h>
#include <stdbool.h>

enum { MP_MASTER_ACTIVE = 0, MP_ALL = 1 };
enum { MULTI_SLOTS = 4 };

typedef struct __attribute__((packed)) {
    uint8_t  magic[2];      /* 'M','S' */
    uint8_t  version;       /* 1 */
    uint8_t  enable;        /* multi channels included in the button cycle (default on) */
    uint8_t  active;        /* currently on a multi channel */
    uint8_t  n;             /* unused (was the fixed station count; now found at runtime) */
    uint8_t  slot;          /* this station's slot, 0 = master */
    uint8_t  policy;        /* MP_ALL (default) or MP_MASTER_ACTIVE */
    uint32_t stagger;       /* flash offset per slot, ticks (12000 = 250 us) */
} multi_cfg_t;

extern multi_cfg_t *const MC;

void multi_init(void);              /* validate config after params_load */
bool multi_active(void);
char multi_label(void);             /* '1'..'4' */
void multi_enter(int slot);         /* switch to multi channel `slot` (live params) */
void multi_leave(void);
uint8_t multi_ootx_mode(void);      /* OOTX byte 31 in multi mode */
uint8_t multi_ootx_mask(void);      /* OOTX byte 33 in multi mode: slots in the round */

/* the round (timebase ISR) */
int  multi_count(void);             /* stations taking turns, >= 1 */
bool multi_sweeps_in(uint32_t rev); /* this station sweeps in revolution `rev` */
void multi_superframe(void);        /* rev % count == 0 edge: guards count down; master applies */
void multi_master_sweep(bool aligned); /* slave: the master's sweeping flash; aligned = expected */
void multi_seen(int slot, bool skip); /* a sync flash in `slot`'s phase was seen (skip: not sweeping) */
void multi_rev_start(void);         /* each timebase edge: a new revolution's evidence */
void multi_master_bit(int bit);     /* slave: an OOTX bit from the master's flash */
void multi_reset(void);             /* channel change */

/* main loop */
void multi_poll(void);              /* slave: decode a completed master frame */
void multi_task(void);              /* every 100 ms: master: who is present */
bool multi_take_round_changed(void); /* slave: the round changed since asked (rebuild OOTX) */

/* status */
uint8_t  multi_mask_live(void);
uint8_t  multi_mask_announced(void);
bool     multi_mask_known(void);
uint8_t  multi_guard(void);
uint32_t multi_seen_age_ms(int slot);
uint32_t multi_blind_sweeps(void); /* turns swept without that revolution's master flash */
uint32_t multi_frames_decoded(void);
uint32_t multi_frames_bad(void);
uint32_t multi_frames_wrong(void);    /* slave: decoded a slave's frame as the master's */
bool     multi_take_relock(void);     /* timebase: restart the correlator (locked on a slave) */
