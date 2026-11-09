/* Rotor (spinning mirror motor) control - stock rotor.c + pll.c. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    /* PLL (stock pll state @0x1000058C + 0x20*r) */
    uint8_t  pll_state;         /* 0 idle, 1 ref seen first, 2 index seen first */
    uint32_t pll_t_first;
    uint32_t pll_t_last;
    int32_t  pll_err;           /* last phase error (ticks) */
    int32_t  pll_integ;
    int32_t  pll_deriv;
    int32_t  pll_out;           /* fine PWM offset around mid-scale */
    /* rotor (stock rotor state @0x100004BC + 0x68*r, runtime part) */
    uint32_t last_index;        /* CT32B0 timestamp of last index edge */
    uint32_t last_period;       /* ticks, 0 when stopped */
    uint32_t lock_count;
    int32_t  start_count;
    float    fine_avg;          /* filtered fine match value (auto-tune input) */
    float    coarse_acc;        /* auto-tune accumulator (float coarse PWM) */
    bool     running;
    bool     locked;
    bool     saturated;
    uint16_t mr_fine, mr_coarse;/* last values written to the PWM match registers */
    uint32_t kick_ms;           /* lhbs safety: time spent kicking without index */
    bool     fault;
    uint32_t index_count;
} rotor_state_t;

extern rotor_state_t rotor[2];
extern volatile bool rotors_armed;  /* lhbs: motors only run after 'spin' */
extern volatile uint32_t sync_late_count;
extern volatile bool rotor_lock_event;

void rotor_init(void);                          /* stock rotor_init @0xB988 + rotor_reset @0xB8D4 */
void rotor_reference_edge(uint32_t t, int32_t offset);  /* stock pll_edge(r,0,..) */
void rotor_control(void);                       /* stock rotor_control @0xBCFC */
void rotor_stop_all(void);
