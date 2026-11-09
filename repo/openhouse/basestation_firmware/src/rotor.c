/* Rotor control: reimplementation of stock rotor_init @0xB988, rotor_reset
 * @0xB8D4, rotor_index_isr @0xB720, rotor_control @0xBCFC and the DPLL
 * @0x7DE0-0x7FF8. Laser event scheduling and OOTX sync pulses (also driven
 * from the index capture in stock) are added in later phases.
 *
 * Hardware:
 *   rotor r coarse PWM  CT16B1 MATr   (P0_21 / P0_22)   = rotor.r.pwm
 *   rotor r fine PWM    CT16B0 MATr   (P1_13 / P1_14)   = 0xFFF - (pll_out + 0x7FF)
 *   rotor 0 index       CT32B0 CAP1 (CR2)  P1_29
 *   rotor 1 index       CT32B0 CAP0 (CR0)  P1_28
 * PWM: period 0xFFF (11.7 kHz); output low until TC reaches MR, so MR=0xFFF is
 * 0 % drive and MR=0 is 100 %. */
#include "rotor.h"
#include "timers.h"
#include "timebase.h"
#include "params.h"
#include "board.h"
#include "sys.h"
#include "ootx.h"
#include "laser.h"

#define PWM_MAX         0xFFF
#define PWM_MID         0x7FF
#define INDEX_MIN       4800u           /* 100 us: shorter = sync backscatter */
#define RUNNING_WINDOW  48000000u       /* index within 1 s = running */
#define INTEG_LIMIT     100000000
#define KICK_TIMEOUT_MS 10000u          /* lhbs safety addition */

rotor_state_t rotor[2];
volatile bool rotors_armed = true;   /* stock: rotors start at boot */
volatile uint32_t sync_late_count;
volatile bool rotor_lock_event;     /* main loop rebuilds the OOTX frame */

/* per-rotor views of the parameter block */
typedef struct {
    bool *enable; uint16_t *pwm; float *pwm_m, *pwm_b; bool *pwm_auto; float *pwm_i;
    bool *pll_enable; float *p, *i, *d; int32_t *offset; float *tolerance;
    uint32_t *settle; bool *debug;
} rotor_cfg_t;

#define CFG(n) { &P.rotor_##n##_enable, &P.rotor_##n##_pwm, &P.rotor_##n##_pwm_m, \
    &P.rotor_##n##_pwm_b, &P.rotor_##n##_pwm_auto, &P.rotor_##n##_pwm_i, \
    &P.rotor_##n##_pll_enable, &P.rotor_##n##_pll_p, &P.rotor_##n##_pll_i, \
    &P.rotor_##n##_pll_d, &P.rotor_##n##_pll_offset, &P.rotor_##n##_tolerance, \
    &P.rotor_##n##_settle, &P.rotor_##n##_debug }
static const rotor_cfg_t cfg[2] = { CFG(0), CFG(1) };

static void pwm_write(int r, uint16_t fine, uint16_t coarse)
{
    TMR_CT16B0->MR[r] = fine;
    TMR_CT16B1->MR[r] = coarse;
    rotor[r].mr_fine = fine;
    rotor[r].mr_coarse = coarse;
}

static void pwm_timer_init(lpc_timer_t *t)
{
    t->TCR = 0;
    t->PR = 0;
    t->MR[3] = PWM_MAX;
    t->MCR = MCR_R(3);
    t->MR[0] = PWM_MAX;
    t->MR[1] = PWM_MAX;
    t->PWMC = 3;                        /* MAT0, MAT1 in PWM mode */
    t->TCR = 2;
    t->TCR = 1;
}

/* ---- DPLL (stock @0x7DE0 loop filter, @0x7E98 phase detector) ---- */
static void pll_reset(int r)
{
    rotor_state_t *s = &rotor[r];
    s->pll_state = 0;
    s->pll_t_first = s->pll_t_last = 0;
    s->pll_err = s->pll_integ = s->pll_deriv = s->pll_out = 0;
}

static void pll_filter(int r, int32_t e)
{
    rotor_state_t *s = &rotor[r];
    int32_t integ = s->pll_integ + e;
    if (integ < -INTEG_LIMIT) integ = -INTEG_LIMIT;
    if (integ > INTEG_LIMIT) integ = INTEG_LIMIT;
    s->pll_integ = integ;
    s->pll_deriv = e - s->pll_err;
    s->pll_err = e;
    s->pll_out = (int32_t)((float)e * *cfg[r].p + (float)integ * *cfg[r].i +
                           (float)s->pll_deriv * *cfg[r].d);
}

enum { EDGE_REF = 0, EDGE_INDEX = 1 };

static void pll_edge(int r, int kind, uint32_t t, int32_t tb_offset)
{
    rotor_state_t *s = &rotor[r];
    s->pll_t_last = t;
    int32_t off = *cfg[r].offset;
    switch (s->pll_state) {
    case 0:
        s->pll_state = kind == EDGE_REF ? 1 : 2;
        s->pll_t_first = t;
        break;
    case 1:                             /* reference came first */
        if (kind == EDGE_INDEX) {
            pll_filter(r, off + (int32_t)(t - s->pll_t_first) + tb_offset);
            s->pll_state = 0;
        }
        break;
    case 2:                             /* index came first */
        if (kind == EDGE_REF) {
            pll_filter(r, -((int32_t)(t - s->pll_t_first) - off - tb_offset));
            s->pll_state = 0;
        }
        break;
    }
}

static int fine_pwm(int r)              /* stock pll_fine_pwm @0x7FB0 */
{
    int v = rotor[r].pll_out + PWM_MID;
    return v < 0 ? 0 : v > PWM_MAX ? PWM_MAX : v;
}

/* stock pll_integrator_adjust @0x7FF8: compensate a coarse PWM step */
static void pll_integ_adjust(int r, int32_t step)
{
    rotor[r].pll_integ = (int32_t)((float)rotor[r].pll_integ + (float)step / *cfg[r].i);
}

void rotor_reference_edge(uint32_t t, int32_t offset)
{
    pll_edge(0, EDGE_REF, t, offset);
    pll_edge(1, EDGE_REF, t, offset);
}

/* ---- index capture (stock rotor_index_isr @0xB720, timing part) ---- */
static void index_edge(int r, uint32_t t)
{
    rotor_state_t *s = &rotor[r];
    uint32_t period = t - s->last_index;
    if (period < INDEX_MIN) return;

    /* Sync flash (stock @0xB750-0xB7C6): the flash starts on this index edge in
     * hardware; schedule its end with CT32B0 MR0 -> MAT0 (P0_18). Every flash
     * carries one OOTX bit; the width encodes skip/axis/data. Emission itself is
     * gated by P1_0 (see emission_update), the bit stream always advances. */
    /* OOTX bits only advance on flashes actually emitted (multi-station: a
     * station is dark in revolutions it does not sweep) */
    int bit = timebase_flash_this_rev() ? ootx_next_bit() : 0;
    uint32_t w = ootx_pulse_width(r, bit, timebase_skip());
    lpc_timer_t *tm = TMR_CT32B0;
    if ((int32_t)(t + w - tm->TC) < 250) {      /* too late: end the flash now */
        tm->EMR |= 1;
        tm->EMR &= ~1u;
        sync_late_count++;
    } else {
        tm->MR[0] = t + w;
    }

    pll_edge(r, EDGE_INDEX, t, P.timebase_offset);
    s->last_period = period;
    s->last_index = t;
    s->index_count++;
    laser_index_edge(r, t, period);     /* schedule this revolution's sweep window */
}

void CT32B0_IRQHandler(void)
{
    lpc_timer_t *t = TMR_CT32B0;
    uint32_t ir = t->IR;
    t->IR = ir;
    if (ir & IR_MR(0)) {                            /* stock @0xC5FC: sync flash over */
        t->EMR &= ~1u;
        timebase_mask_own(false);
    }
    if ((ir & IR_MR(1)) && (t->MCR & MCR_I(1))) laser_match_event(0);
    if ((ir & IR_MR(2)) && (t->MCR & MCR_I(2))) laser_match_event(1);
    if (ir & IR_CR(2)) index_edge(0, t->CR[2]);     /* CAP1 -> rotor 0 */
    if (ir & IR_CR(0)) index_edge(1, t->CR[0]);     /* CAP0 -> rotor 1 */
}

/* ---- control loop, once per timebase edge (stock rotor_control @0xBCFC) ---- */
static void lock_check(int r)
{
    rotor_state_t *s = &rotor[r];
    uint32_t expect = timebase_period();
    int32_t diff = (int32_t)(s->last_period - expect);
    if (diff < 0) diff = -diff;
    float ppm = (float)diff / (float)expect * 1e6f;
    if (ppm > *cfg[r].tolerance) {
        s->lock_count = 0;
        s->locked = false;
    } else if (s->lock_count < *cfg[r].settle || s->locked) {
        s->lock_count++;
    } else {
        s->locked = true;
        ootx_lock_events++;             /* stock: counter + OOTX frame rebuild */
        rotor_lock_event = true;
    }
}

void rotor_control(void)
{
    uint32_t now = timebase_now();
    for (int r = 0; r < 2; r++) {
        rotor_state_t *s = &rotor[r];
        const rotor_cfg_t *c = &cfg[r];
        s->running = now - s->last_index < RUNNING_WINDOW && s->index_count > 1;
        if (!s->running) {
            s->last_period = 0;
            /* lhbs: hold the PLL in reset while stopped. Stock keeps the first
             * reference edge forever, so a late start yields a huge first error
             * (harmless in stock only because its rotors start at boot). */
            pll_reset(r);
        }

        if (!*c->enable || !rotors_armed || s->fault || P.sys_standby) {
            pwm_write(r, PWM_MAX, PWM_MAX);             /* stopped */
            s->kick_ms = 0;
        } else if (!s->running || s->start_count) {
            /* stock start-up: full drive on both PWMs until index pulses arrive */
            if (s->start_count) s->start_count--;
            pwm_write(r, 0, 0);
            s->kick_ms += timebase_period() / 48000u;
            if (s->kick_ms > KICK_TIMEOUT_MS) s->fault = true;
        } else if (*c->pll_enable) {
            s->kick_ms = 0;
            int mr_fine = PWM_MAX - fine_pwm(r);
            /* auto-tune: keep the fine PWM centred by nudging the coarse PWM */
            s->fine_avg = (float)(0.99 * (double)s->fine_avg + 0.01 * (double)mr_fine);
            if (*c->pwm_auto) {
                float x = (s->fine_avg - 2047.0f) * *c->pwm_i + s->coarse_acc;
                if (x < 0.0f) x = 0.0f;
                if (x > 4095.0f) x = 4095.0f;
                s->coarse_acc = x;
                int step = (int)(x - (float)*c->pwm);
                if (step) {
                    *c->pwm = (uint16_t)(int)x;
                    pll_integ_adjust(r, step * 10);
                    mr_fine = (int)((double)mr_fine + (double)step * 10.0);
                }
            }
            s->saturated = mr_fine <= 0 || mr_fine >= PWM_MAX;
            if (mr_fine < 0) mr_fine = 0;
            pwm_write(r, (uint16_t)mr_fine, *c->pwm);
        }
        if (*c->pll_enable) lock_check(r);
    }
    laser_safety_check();
}

void rotor_stop_all(void)
{
    lasers_off();
    rotors_armed = false;
    pwm_write(0, PWM_MAX, PWM_MAX);
    pwm_write(1, PWM_MAX, PWM_MAX);
}

void rotor_init(void)
{
    SYSAHBCLKCTRL |= (1u << 7) | (1u << 8) | (1u << 9);    /* CT16B0, CT16B1, CT32B0 */

    /* index inputs: no pull, hysteresis, capture function */
    *iocon(PIN(1, 29)) = IOCON_FUNC(2) | (1u << 5);         /* CT32B0_CAP1 */
    *iocon(PIN(1, 28)) = IOCON_FUNC(1) | (1u << 5);         /* CT32B0_CAP0 */

    pwm_timer_init(TMR_CT16B1);         /* coarse */
    pwm_timer_init(TMR_CT16B0);         /* fine */
    *iocon(PIN(0, 21)) = IOCON_FUNC(1);                     /* CT16B1_MAT0 */
    *iocon(PIN(0, 22)) = IOCON_FUNC(2) | (1u << 7);         /* CT16B1_MAT1, digital */
    *iocon(PIN(1, 13)) = IOCON_FUNC(2);                     /* CT16B0_MAT0 */
    *iocon(PIN(1, 14)) = IOCON_FUNC(2);                     /* CT16B0_MAT1 */

    lpc_timer_t *t = TMR_CT32B0;
    t->TCR = 0;
    t->PR = 0;
    t->MCR = 0;
    t->EMR = 0;
    t->CCR = CCR_RISE(0) | CCR_I(0) | CCR_RISE(2) | CCR_I(2);
    t->EMR = 2u << 4;                   /* MAT0: set on MR0 match (sync end) */
    t->MCR = MCR_I(0);                  /* MR0 irq clears it again */
    *iocon(PIN(0, 18)) = IOCON_FUNC(2); /* CT32B0_MAT0 */
    t->IR = 0xFF;

    for (int r = 0; r < 2; r++) {       /* stock rotor_reset @0xB8D4 */
        pll_reset(r);
        rotor[r].running = rotor[r].locked = false;
        rotor[r].start_count = 1;
        rotor[r].coarse_acc = (float)*cfg[r].pwm;
        rotor[r].fine_avg = 2047.0f;
    }
    /* lhbs: CT32B0 above CT32B1 (stock: 2, below it). A sync flash ends in this
     * handler (MR0 is set on the index capture), so it must not wait: a slave
     * processing the master's flash in CT32B1 (phase loop, soft float, up to 175 us)
     * delayed slot 2's next index event, 250 us later, by up to 170 us, past the end
     * of its 62.5 us flash: the flash came out 59-69 us and decoded as the wrong axis
     * (ai/firmware/MULTISTATION.md). CT32B1 captures are latched by hardware. */
    nvic_set_priority(18, 0);
    NVIC_ISER = 1u << 18;
}
