/* Channel display, RGB LEDs and mode button. */
#pragma once
#include <stdint.h>
#include <stdbool.h>

/* LED colour bits as in stock (table @0xE520): R1 G2 B4 Y3 P5 A6 W7 */
enum { C_OFF, C_RED, C_GREEN, C_YELLOW, C_BLUE, C_PURPLE, C_AQUA, C_WHITE };

void ui_init(void);
void disp_char(char c, bool flipped);   /* stock disp_puts @0xD68C */
void disp_clear(void);                  /* stock disp_clear @0xD514 */
void led_set(int led, unsigned colour); /* stock rgb_led_set @0xC688 */

/* Returns true once per short press (release < 1 s after press), matching the
 * stock edge logic in button_event @0x3430. Also handles the 3 s ISP hold. */
bool button_poll(void);
