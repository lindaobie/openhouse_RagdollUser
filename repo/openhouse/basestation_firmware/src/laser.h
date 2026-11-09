#pragma once
#include <stdint.h>
#include <stdbool.h>

extern volatile bool lasers_requested;
extern volatile uint32_t laser_on_count[2], laser_kill_count;

bool    laser_init(void);
bool    laser_driver_ok(void);
uint8_t laser_driver_id(void);
uint8_t isl_read(uint8_t reg);
void    lasers_off(void);
void    laser_index_edge(int n, uint32_t t, uint32_t period);
void    laser_match_event(int n);
void    laser_safety_check(void);
void    laser_watchdog(void);
void    laser_poll(void);
/* Around a settings save on a channel change: true puts the boot drive currents in the
 * parameters, false puts the regulated ones back. */
void    laser_boot_currents_for_save(bool saving);
extern volatile bool apc_enabled, laser_fault;
extern volatile uint16_t apc_adc[2];
extern volatile float apc_detected[2], apc_power[2];
