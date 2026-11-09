/* Lighthouse 1.0 board: pin map and GPIO helpers.
 * Derivation of every pin: ai/firmware/HARDWARE.md section 2. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "lpc11u.h"

#define CPU_HZ          48000000u

/* Pins are written P(port, bit) */
#define PIN(port, bit)  (((port) << 5) | (bit))
#define PIN_PORT(p)     ((p) >> 5)
#define PIN_BIT(p)      ((p) & 31)

#define PIN_BUTTON      PIN(0, 1)   /* active low, ROM ISP strap */
#define PIN_UART_RX     PIN(1, 26)
#define PIN_UART_TX     PIN(1, 27)

#define PIN_DISP_DATA   PIN(1, 22)  /* shared with ISL58303 SDIO */
#define PIN_DISP_CLK    PIN(1, 20)  /* shared with ISL58303 SCLK */
#define PIN_DISP_LATCH  PIN(1, 24)
#define PIN_DISP_OE     PIN(1, 23)

#define PIN_LED0_R      PIN(1, 16)
#define PIN_LED0_G      PIN(1, 17)
#define PIN_LED0_B      PIN(1, 18)
#define PIN_LED1_R      PIN(1, 9)
#define PIN_LED1_G      PIN(1, 19)
#define PIN_LED1_B      PIN(1, 11)

#define PIN_IND_ROTOR0  PIN(1, 7)   /* internal indicators, active high */
#define PIN_IND_ROTOR1  PIN(1, 6)
#define PIN_IND_TIMEBASE PIN(1, 8)
#define PIN_IND_STATUS_N PIN(1, 10) /* active low */

#define PIN_LASER0_EN   PIN(1, 12)  /* active high */
#define PIN_LASER1_EN   PIN(0, 16)  /* active high */
#define PIN_SYNC_GATE   PIN(1, 0)
#define PIN_CARRIER_EN  PIN(0, 20)
#define PIN_EMITTER_PWR_N PIN(0, 23) /* stock standby_outputs @0x3220: cleared only when
                                      * emission enabled and rotors locked */

static inline void gpio_set(int p)   { GPIO_SET(PIN_PORT(p)) = 1u << PIN_BIT(p); }
static inline void gpio_clr(int p)   { GPIO_CLR(PIN_PORT(p)) = 1u << PIN_BIT(p); }
static inline void gpio_write(int p, bool v) { if (v) gpio_set(p); else gpio_clr(p); }
static inline void gpio_toggle(int p) { GPIO_NOT(PIN_PORT(p)) = 1u << PIN_BIT(p); }
static inline bool gpio_read(int p)  { return GPIO_PIN(PIN_PORT(p)) & (1u << PIN_BIT(p)); }
static inline void gpio_output(int p, bool v) { gpio_write(p, v); GPIO_DIR(PIN_PORT(p)) |= 1u << PIN_BIT(p); }
static inline void gpio_input(int p) { GPIO_DIR(PIN_PORT(p)) &= ~(1u << PIN_BIT(p)); }
static inline volatile uint32_t *iocon(int p)
{
    return PIN_PORT(p) ? &IOCON_P1(PIN_BIT(p)) : &IOCON_P0(PIN_BIT(p));
}
