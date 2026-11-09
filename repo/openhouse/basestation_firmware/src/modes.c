#include <stdint.h>
#include "modes.h"
#include "params.h"
#include "multi.h"
#include "laser.h"

/* OOTX sync pulse-width sets (stock flash @0xE590, 48 MHz ticks), in parameter
 * order j0 j1 k0 k1 j2 j3 k2 k3 = index [skip<<2 | axis<<1 | data]. */
static const uint16_t ootx_sets[3][8] = {
    [OOTX_SHORT]  = { 1500, 2000, 1750, 2250, 2500, 2500, 2500, 2500 },
    [OOTX_MEDIUM] = { 3000, 4000, 3500, 4500, 5000, 6000, 5500, 6500 },
    [OOTX_LONG]   = { 7200, 8400, 12000, 13200, 14000, 14000, 14000, 14000 },
};

/* The eight mode entries are separate named parameters; gather pointers once. */
typedef struct {
    uint32_t *period;
    uint8_t  *tb_mode;
    int32_t  *tb_offset;
    uint8_t  *ootx;
    uint8_t  *label;
    uint32_t *carrier;
} mode_ref_t;

#define MODE_REF(n) { &P.mode_##n##_period, &P.mode_##n##_timebase_mode, \
    &P.mode_##n##_timebase_offset, &P.mode_##n##_ootx, &P.mode_##n##_label, &P.mode_##n##_carrier }
static const mode_ref_t modes[8] = {
    MODE_REF(0), MODE_REF(1), MODE_REF(2), MODE_REF(3),
    MODE_REF(4), MODE_REF(5), MODE_REF(6), MODE_REF(7),
};

static uint16_t *const ootx_live[8] = {
    &P.ootx_j0, &P.ootx_j1, &P.ootx_k0, &P.ootx_k1,
    &P.ootx_j2, &P.ootx_j3, &P.ootx_k2, &P.ootx_k3,
};

int mode_count(void)
{
    int n = P.mode_count;
    return n < 1 ? 1 : n > 8 ? 8 : n;
}

static int cur(void) { return P.mode_current % mode_count(); }

void mode_apply(int m)
{
    const mode_ref_t *r = &modes[m];
    P.timebase_period = *r->period;
    P.timebase_mode = *r->tb_mode;
    P.timebase_offset = *r->tb_offset;
    P.carrier_frequency = *r->carrier;
    unsigned set = *r->ootx < 3 ? *r->ootx : OOTX_MEDIUM;
    for (int i = 0; i < 8; i++) *ootx_live[i] = ootx_sets[set][i];
}

/* stock button_event @0x3430: next channel, apply, save. lhbs: when multi
 * channels are enabled (the default) the cycle continues A, b, c, 1, 2, 3, 4, then
 * back to A. */
void mode_next(void)
{
    if (multi_active()) {
        if (MC->slot + 1 < MULTI_SLOTS) {
            multi_enter(MC->slot + 1);
        } else {
            multi_leave();
            P.mode_current = 0;
            mode_apply(0);
        }
    } else if (MC->enable && P.mode_current + 1 >= mode_count()) {
        multi_enter(0);
    } else {
        P.mode_current = (P.mode_current + 1) % mode_count();
        mode_apply(P.mode_current);
    }
    laser_boot_currents_for_save(true);
    params_save(0);
    laser_boot_currents_for_save(false);
}

bool mode_matches_live(void)
{
    if (multi_active()) return true;        /* label = slot number */
    const mode_ref_t *r = &modes[cur()];
    if (P.timebase_period != *r->period || P.carrier_frequency != *r->carrier ||
        P.timebase_mode != *r->tb_mode || P.timebase_offset != *r->tb_offset)
        return false;
    unsigned set = *r->ootx < 3 ? *r->ootx : OOTX_MEDIUM;
    for (int i = 0; i < 8; i++)
        if (*ootx_live[i] != ootx_sets[set][i]) return false;
    return true;
}

char mode_label(void) { return multi_active() ? multi_label() : (char)*modes[cur()].label; }
