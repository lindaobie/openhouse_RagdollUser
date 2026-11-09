#pragma once
#include <stdint.h>
void     ootx_build(void);
int      ootx_next_bit(void);
uint32_t ootx_pulse_width(int axis, int data, int skip);
uint32_t ootx_serial(void);
uint16_t ootx_frames_sent(void);
void     ootx_refresh_accel(void);   /* main loop */
uint16_t ootx_bits(void);
uint32_t ootx_crc32(const uint8_t *p, unsigned n);
const uint8_t *ootx_frame(void);
extern uint8_t ootx_accel[3];
extern uint16_t ootx_lock_events;
