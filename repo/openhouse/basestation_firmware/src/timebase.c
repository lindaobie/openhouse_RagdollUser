#include "timebase.h"
#include "timers.h"
#include "rotor.h"
#include "params.h"
#include "board.h"
#include "carrier.h"
#include "sys.h"
#include "multi.h"

static uint32_t period_cur;         /* stock 0x1000066C+0xC */
static volatile uint32_t edges;
static volatile uint32_t last_edge;     /* CT32B1 time of last reference edge */
static volatile uint32_t last_lock_ms;  /* last time emission was permitted */
static volatile bool tdm_flag = true;   /* stock 0x1000067D: 1 = sweep revolution */

/* multi-station round-robin (ai/firmware/MULTISTATION.md) */
static volatile uint32_t rev;           /* revolution counter, see multi_sweeps_in() */
static volatile bool flash_rev = true;  /* this station flashes in this revolution */
static volatile bool emitting;          /* last emission_update() result */
static volatile uint8_t sf_confirm;     /* consecutive superframe confirmations */
volatile uint32_t sf_realign_count;

/* A slave sweeps only with the round confirmed (the master's sweeps where its round
 * expects them, twice running); it flashes as soon as it holds the master's phase. */
static bool multi_sweeps_now(void) { return multi_sweeps_in(rev) && (MC->slot == 0 || sf_confirm >= 2); }

/* ---- optical slave (stock timebase_capture_isr @0xC8B0, correlator @0x5ED0-
 * 0x6180, classifier @0xB514 + table @0x11EDC, phase callback @0xAAB8) ---- */
#define PIN_OPT_IN      PIN(1, 4)       /* CT32B1_CAP0, photodiode receiver */
#define PIN_OPT_SEL     PIN(1, 15)      /* high = optical receiver selected */
enum { CS_DISABLED, CS_SEARCHING, CS_SYNCING, CS_LOCKED, CS_LOST };
static const char *const corr_state_names[] = { "disabled", "searching", "syncing", "locked", "lost" };
static volatile uint32_t rise_t, last_width, pulses_seen, pulses_used;
static struct { uint32_t last; uint32_t n; double sum_ms; float est; } corr;
static float ph_f, ph_prev, ph_integ;  /* timebase phase loop (stock 0x10000280+0x18..) */
static volatile int32_t mr1_extra, last_phase_err;
static volatile uint8_t phase_good;     /* consecutive phase errors < 20 us */

/* diagnostics: last corrections (lhbs) */
typedef struct { uint8_t kind; int32_t e, extra; uint32_t a, b; } tb_trace_t;
static tb_trace_t trace[16];
static volatile uint8_t trace_pos;
volatile uint32_t tb_guard_count;
static void tb_trace(uint8_t kind, int32_t e, int32_t extra, uint32_t a, uint32_t b)
{
    tb_trace_t *x = &trace[trace_pos++ & 15];
    x->kind = kind; x->e = e; x->extra = extra; x->a = a; x->b = b;
}

static bool slave_mode(void)
{
    /* cable sync is dropped by design: slave-cable channels follow optically too */
    return P.timebase_mode == TB_SLAVE_OPTICAL || P.timebase_mode == TB_SLAVE_CABLE;
}

/* stock classifier table @0x11EDC: width (48 MHz ticks) -> pulse type, tol 250 */
static int classify(uint32_t w)
{
    static const uint16_t width[8] = { 3000, 3500, 4000, 4500, 5000, 5500, 6000, 6500 };
    static const uint8_t type[8]   = { 2,    4,    3,    5,    6,    8,    7,    9 };
    for (int i = 0; i < 8; i++)
        if (w + 250 >= width[i] && w <= width[i] + 250u) return type[i];
    return 1;
}

static void capture_irq(bool on)
{
    TMR_CT32B1->IR = IR_CR(0);          /* drop a capture latched while masked */
    if (on) TMR_CT32B1->CCR |= CCR_I(0); else TMR_CT32B1->CCR &= ~CCR_I(0);
}

/* stock cc64: mask our own emissions from the receiver (slave + optical.mask) */
void timebase_mask_own(bool masked)
{
    if (!slave_mode() || !P.timebase_optical_mask) masked = false;
    capture_irq(!masked);
}

/* stock callback @0xAAB8: correlator -> timebase phase/period */
static void corr_callback(uint32_t t, bool nonskip, bool first)
{
    uint32_t offset = multi_active() ? MC->slot * MC->stagger : P.timebase_optical_offset;
    int32_t e = (int32_t)(t - last_edge) - (int32_t)(period_cur >> 2) + (int32_t)offset;
    if (multi_active() && nonskip && !first) {
        /* the master sweeps in revolutions r = 0 mod count: align our counter */
        const uint32_t count = (uint32_t)multi_count();
        multi_master_sweep(rev % count == 0);
        if (rev % count) {
            rev -= rev % count;
            sf_confirm = 0;
            sf_realign_count++;
        } else if (sf_confirm < 3) {
            sf_confirm++;
        }
    }
    last_phase_err = e;
    /* multi slave: sweep only once the stagger is held (flashes 250 us apart) */
    {
        int32_t a = e < 0 ? -e : e;
        if (a > 1920) phase_good = 0;                       /* > 40 us: stop */
        else if (a < 960 && phase_good < 20) phase_good++;  /* < 20 us */
    }
    float k = P.timebase_k;
    float f = (1.0f - k) * (float)e + k * ph_f;
    int32_t deriv = (int32_t)(f - ph_prev);
    ph_prev = f;
    ph_f = f;
    if (!first) {
        float half = (float)(period_cur >> 1);
        ph_integ += f;
        while (ph_integ >= half) ph_integ -= half;
        while (ph_integ <= -half) ph_integ += half;
    } else {
        ph_f = ph_prev = ph_integ = 0;
        mr1_extra = e;                  /* jump straight onto the master */
        deriv = 0;
        tdm_flag = !nonskip;            /* sweep when the master skips */
    }
    period_cur = (uint32_t)corr.est;
    mr1_extra += (int32_t)(P.timebase_p * ph_f + P.timebase_i * ph_integ + P.timebase_d * (float)deriv);
    tb_trace(first ? 'F' : 'C', e, mr1_extra, t, last_edge);
}

/* stock correlator_add @0x5F64. lhbs, multi: `first` = the first sync flash of its
 * group. Searching, stock takes any two consecutive flashes one period apart; with
 * other slaves already flashing 250-750 us after the master, consecutive flashes are
 * never a period apart (purple for minutes, locking only when a flash was missed, and
 * then onto whichever station). The master flashes first in every group: searching
 * looks at first flashes only. Once syncing, the period check rejects the others. */
static void corr_pulse(uint32_t t, bool nonskip, bool first)
{
    if (!P.corr_enable) { P.corr_status = CS_DISABLED; return; }
    if (P.corr_status == CS_DISABLED) P.corr_status = CS_SEARCHING;
    if (multi_active() && !first && (P.corr_status == CS_SEARCHING || P.corr_status == CS_LOST)) return;
    uint32_t per = P.timebase_period, tol = P.corr_tolerance;
    uint32_t dt = t - corr.last;
    if (dt > per + tol || dt < per - tol) {
        if ((P.corr_status == CS_SYNCING || P.corr_status == CS_LOCKED) && corr.est > 0) {
            uint32_t est = (uint32_t)corr.est;
            int32_t r = (int32_t)(dt % est);
            if (r > (int32_t)(est / 2)) r -= (int32_t)est;
            if ((uint32_t)(r < 0 ? -r : r) >= 2 * tol) return;    /* stray pulse */
            P.corr_missed += dt / est;                             /* missed periods */
        }
        corr.last = t;
        return;
    }
    switch (P.corr_status) {
    case CS_SEARCHING:
    case CS_LOST:
        corr.est = (float)dt;
        corr.n = 0; corr.sum_ms = 0;
        P.corr_status = CS_SYNCING;
        break;
    case CS_SYNCING:
        if (corr.n >= P.corr_settle) {
            corr.est = (float)(corr.sum_ms / corr.n * 48000.0);
            corr.n = 0; corr.sum_ms = 0;
            corr_callback(t, nonskip, true);
            P.corr_status = CS_LOCKED;
        }
        break;
    case CS_LOCKED:
        corr.est = (float)((1.0 - P.corr_k) * (double)dt + P.corr_k * (double)corr.est);
        corr_callback(t, nonskip, false);
        break;
    }
    P.corr_period = corr.est;
    corr.n++;
    corr.sum_ms += (double)dt / 48000.0;
    corr.last = t;
    pulses_used++;
}

/* stock correlator_tick @0x5EF0, every timebase edge */
static void corr_tick(uint32_t now)
{
    if (multi_take_relock() && slave_mode()) {      /* locked onto a slave: search again */
        corr.n = 0; corr.sum_ms = 0;
        corr.last = 0;
        P.corr_status = CS_SEARCHING;
        return;
    }
    if ((P.corr_status == CS_SYNCING || P.corr_status == CS_LOCKED) &&
        now - corr.last > P.corr_timeout * P.timebase_period) {
        corr.n = 0; corr.sum_ms = 0;
        corr.last = 0;
        P.corr_status = CS_LOST;
    }
}

/* Multi: whose flash this is. Every station flashes a quarter period after its own
 * timebase edge, slot s * stagger after the master (slaves hold that by the phase
 * loop): so relative to our own edge and slot, slot s' flashes at s' * stagger, at
 * both index events of a revolution. -1: not on the slot grid. */
static int flash_slot(uint32_t t)
{
    const int32_t half = (int32_t)(period_cur >> 1);
    const int32_t stagger = (int32_t)MC->stagger;
    int32_t d = (int32_t)(t - last_edge) - (int32_t)(period_cur >> 2) + MC->slot * stagger;
    d %= half;
    if (d < -half / 4) d += half;
    if (d >= half - half / 4) d -= half;
    if (d < -stagger / 4) return -1;
    const int32_t s = (d + stagger / 2) / stagger;
    const int32_t err = d - s * stagger;
    if (s >= MULTI_SLOTS || err > 2880 || err < -2880) return -1;   /* 60 us */
    return (int)s;
}

static void optical_edge(uint32_t t)
{
    if (gpio_read(PIN_OPT_IN)) { rise_t = t; return; }    /* light on: pulse start */
    uint32_t w = t - rise_t;                              /* falling edge: pulse end */
    last_width = w;
    pulses_seen++;
    int type = classify(w);
    /* multi: first flash of its group (stations flash within 1 ms; groups are a half
     * period apart). Our own flashes are masked from the receiver. */
    static uint32_t last_sync;
    bool first = true;
    if (type >= 2) {
        first = rise_t - last_sync > 57600;          /* 1.2 ms */
        last_sync = rise_t;
    }
    if (type >= 2 && multi_active()) {
        const int s = flash_slot(rise_t);
        if (s >= 0 && s != MC->slot) multi_seen(s, type >= 6);
        /* a slave decodes the master's frame: data bit of every master flash (ALL) */
        if (s == 0 && MC->slot != 0 && P.corr_status == CS_LOCKED)
            multi_master_bit(type == 3 || type == 5 || type == 7 || type == 9);
    }
    if (slave_mode() && (type == 2 || type == 3 || type == 6 || type == 7))  /* axis-0 syncs only */
        corr_pulse(rise_t, type <= 5, first);
}

/* stock timebase_select_input @0xCBA8: optical receiver for slave modes */
void timebase_select_input(void)
{
    lpc_timer_t *t = TMR_CT32B1;
    uint32_t s = irq_save();
    period_cur = P.timebase_period ? P.timebase_period : 800000;
    mr1_extra = 0;
    ph_f = ph_prev = ph_integ = 0;
    corr.n = 0; corr.sum_ms = 0; corr.est = 0;
    rev = 0;
    sf_confirm = 0;
    flash_rev = true;
    P.corr_status = CS_DISABLED;
    irq_restore(s);
    if (slave_mode() || multi_active()) {          /* a multi master listens for its slaves */
        gpio_output(PIN_OPT_SEL, true);
        *iocon(PIN_OPT_IN) = IOCON_FUNC(1) | (1u << 5);   /* CT32B1_CAP0, hysteresis */
        t->CCR = CCR_RISE(0) | (2u << 0) | CCR_I(0);      /* both edges + irq */
        if (slave_mode() && P.corr_status == CS_DISABLED) P.corr_status = CS_SEARCHING;
    } else {
        gpio_output(PIN_OPT_SEL, false);
        t->CCR = 0;
        P.corr_status = CS_DISABLED;
    }
    t->IR = IR_CR(0);
}

void timebase_dump_trace(void (*out)(const char *fmt, ...))
{
    out("guards %u, trace (oldest first): kind e extra a b\n", tb_guard_count);
    for (int i = 0; i < 16; i++) {
        tb_trace_t *x = &trace[(trace_pos + i) & 15];
        if (x->kind) out("  %c %d %d %u %u\n", x->kind, x->e, x->extra, x->a, x->b);
    }
}

uint32_t timebase_rev(void) { return rev; }
uint8_t  timebase_sf_confirm(void) { return sf_confirm; }

const char *timebase_corr_state(void) { return corr_state_names[P.corr_status < 5 ? P.corr_status : 0]; }
uint32_t timebase_pulses_seen(void) { return pulses_seen; }
uint32_t timebase_pulses_used(void) { return pulses_used; }
uint32_t timebase_last_width(void) { return last_width; }
int32_t  timebase_phase_err(void) { return last_phase_err; }

static bool tdm_mode(void)
{
    return P.timebase_mode == TB_MASTER_TDM || P.timebase_mode == TB_SLAVE_OPTICAL ||
           P.timebase_mode == TB_SLAVE_CABLE;
}

/* stock 0x1000066D: timebase permits emission - masters always, slaves once
 * the correlator is locked to a master. */
bool timebase_permits_emission(void)
{
    if (slave_mode())
        /* Not sf_confirm (that gates sweeping, multi_sweeps_now): a slave that stopped
         * flashing whenever the round changed under it dropped out of the master's
         * round, which changed it back - joining took minutes (4 stations). */
        return P.corr_status == CS_LOCKED && (!multi_active() || phase_good >= 20);
    return true;
}
static volatile bool running;

/* Stock timers_sync @0xCFA4: stop, reset and restart CT32B0 and CT32B1 back to
 * back so rotor captures (CT32B0) and timebase edges (CT32B1) share one clock. */
void timers_sync_start(void)
{
    TMR_CT32B0->TCR = 0;
    TMR_CT32B1->TCR = 0;
    TMR_CT32B0->TCR = 2; while (TMR_CT32B0->TC) {}
    TMR_CT32B1->TCR = 2; while (TMR_CT32B1->TC) {}
    TMR_CT32B0->TCR = 0;
    TMR_CT32B1->TCR = 0;
    TMR_CT32B0->TCR = 1;
    TMR_CT32B1->TCR = 1;
}

void timebase_init(void)
{
    SYSAHBCLKCTRL |= AHB_CT32B1;
    lpc_timer_t *t = TMR_CT32B1;
    t->TCR = 0;
    t->PR = 0;
    t->MCR = 0;
    t->EMR = 0;                     /* sync-out MAT1 (P0_14) never driven: optical only */
    t->CCR = 0;                     /* optical capture enabled in phase 3 */
    t->IR = 0xFF;
    period_cur = P.timebase_period ? P.timebase_period : 800000;
    timebase_select_input();
    nvic_set_priority(19, 1);       /* stock: CT32B1 priority 1 */
    NVIC_ISER = 1u << 19;
}

void timebase_start(void)
{
    lpc_timer_t *t = TMR_CT32B1;
    t->MR[1] = t->TC + period_cur;
    t->IR = IR_MR(1);
    t->MCR = MCR_I(1);
    running = true;
}

/* Stock timebase_mr1_isr @0xCC8C, master-async case: re-arm one period later,
 * feed both rotor PLLs a reference edge, run the rotor control loop.
 * (Sync gate P1_0 is deliberately not touched in phase 2.) */
void CT32B1_IRQHandler(void)
{
    lpc_timer_t *t = TMR_CT32B1;
    uint32_t ir = t->IR;
    t->IR = ir;
    if (ir & IR_CR(0)) optical_edge(t->CR[0]);
    if (!(ir & IR_MR(1))) return;
    uint32_t now = t->MR[1];
    int32_t extra = mr1_extra;
    uint32_t next = now + period_cur + extra;   /* stock: phase correction consumed */
    /* lhbs guard: a correction beyond half a period, or a next edge already in
     * the past, would stall the timebase until the 32-bit counter wraps */
    if (extra > (int32_t)(period_cur / 2) || extra < -(int32_t)(period_cur / 2) ||
        (int32_t)(next - t->TC) < 1000) {
        tb_guard_count++;
        next = t->TC + period_cur;
    }
    t->MR[1] = next;
    tb_trace('E', 0, extra, now, next);
    mr1_extra = 0;
    if (slave_mode()) corr_tick(now);
    edges++;
    last_edge = now;
    if (multi_active()) {
        rev++;
        multi_rev_start();
        if (rev % (uint32_t)multi_count() == 0) {
            const int before = multi_count();
            multi_superframe();
            if (multi_count() != before) rev = 0;   /* the new round starts here */
        }
        /* ALL: every station flashes every revolution (and sends an OOTX bit
         * each time). MASTER+ACTIVE: the master every revolution, others only
         * when they sweep; the gate is set ahead of this revolution's index
         * edges. Measured (openhouse captures/ms-a): the gate does not stop a
         * slave's flash, so MASTER+ACTIVE slaves still flash, with no OOTX bit. */
        flash_rev = MC->policy == MP_ALL || MC->slot == 0 || multi_sweeps_now() ||
                    !multi_mask_known() || !(multi_mask_live() & (1u << MC->slot));  /* seen to join */
        gpio_write(PIN_SYNC_GATE, emitting && flash_rev);
    } else {
        tdm_flag = tdm_mode() ? !tdm_flag : true;   /* stock: alternate in TDM modes */
        flash_rev = true;
    }
    rotor_reference_edge(now, P.timebase_offset);
    rotor_control();
}

uint32_t timebase_period(void) { return period_cur; }
uint32_t timebase_edges(void) { return edges; }

int timebase_skip(void) { return multi_active() ? !multi_sweeps_now() : !tdm_flag; }
bool timebase_flash_this_rev(void) { return flash_rev; }

/* stock task @0xCFE8, every 500 ms: re-arm a stalled timebase, enter stand-by
 * after 60 s without timebase lock. */
void timebase_supervisor(void)
{
    lpc_timer_t *t = TMR_CT32B1;
    if (TMR_CT32B1->TC - last_edge > 480000000u) {     /* 10 s: "timebase error, kicking" */
        t->MR[1] = t->TC + period_cur;
        last_edge = t->TC;
    }
    /* lhbs: a slave's receiver keeps listening in stand-by (rotors and emitter off,
     * as while searching): the master's flash back, locked onto, wakes it. Stock
     * waits for the button (or Bluetooth), so a master that came back after its slaves
     * timed out (reflashed, power cycled) left them all dark. Nor does a slave go to
     * stand-by while it hears a master (syncing, or locked and settling in). */
    const bool hears = slave_mode() && (P.corr_status == CS_SYNCING || P.corr_status == CS_LOCKED);
    if (P.sys_standby && slave_mode() && P.corr_status == CS_LOCKED) timebase_exit_standby();
    if (timebase_permits_emission() || P.sys_standby || hears) last_lock_ms = sys_ms();
    else if (sys_ms() - last_lock_ms > 60000u) P.sys_standby = true;
}

void timebase_exit_standby(void)
{
    P.sys_standby = false;
    last_lock_ms = sys_ms();
}

volatile bool sync_requested = true;   /* stock: on whenever conditions allow */

bool emission_active(void) { return emitting; }

bool emission_update(void)
{
    bool locked = rotor[0].locked && rotor[1].locked;
    bool on = sync_requested && rotors_armed && P.sys_emission_enable && P.ootx_enable &&
              !P.sys_standby && timebase_permits_emission() &&
              (locked || !P.ootx_lock_required);
    gpio_write(PIN_EMITTER_PWR_N, !on);
    gpio_write(PIN_SYNC_GATE, on && flash_rev);
    carrier_enable(on && P.carrier_enable);
    emitting = on;
    return on;
}
