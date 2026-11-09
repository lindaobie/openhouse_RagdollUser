#include "ui.h"
#include "board.h"
#include "sys.h"
#include "iap.h"

/* 7-segment fonts copied from stock: digits @0x10768, letters @0x10778. */
static const uint8_t font_digit[10] = { 0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f };
static const uint8_t font_alpha[26] = {
    0x77,0x7c,0x58,0x5e,0x79,0x71,0x3d,0x76,0x10,0x1e,0x75,0x38,0x15,
    0x54,0x5c,0x73,0x67,0x50,0x6d,0x70,0x3e,0x1c,0x2a,0x76,0x6e,0x5b };

static void disp_shift(uint8_t segs)    /* stock disp_shift_bits @0xD7B0 */
{
    gpio_clr(PIN_DISP_LATCH);
    gpio_clr(PIN_DISP_CLK);
    for (int i = 7; i >= 0; i--) {
        gpio_write(PIN_DISP_DATA, segs & (1u << i));
        gpio_toggle(PIN_DISP_CLK);
        gpio_toggle(PIN_DISP_CLK);
    }
    gpio_toggle(PIN_DISP_LATCH);
    gpio_toggle(PIN_DISP_LATCH);
}

void disp_char(char c, bool flipped)
{
    uint8_t s = 0;
    if (c >= '0' && c <= '9') s = font_digit[c - '0'];
    else if ((c | 0x20) >= 'a' && (c | 0x20) <= 'z') s = font_alpha[(c | 0x20) - 'a'];
    else if (c == '-') s = 0x40;
    if (flipped)    /* rotate 180 deg: a<->d, b<->e, c<->f (stock does the same) */
        s = (s & 0xC0) | ((s & 7) << 3) | ((s & 0x3F) >> 3);
    disp_shift(s);
}

void disp_clear(void) { disp_shift(0); }

static const uint8_t led_pins[2][3] = {
    { PIN_LED0_R, PIN_LED0_G, PIN_LED0_B },
    { PIN_LED1_R, PIN_LED1_G, PIN_LED1_B },
};

/* Active low, open-drain emulation: on = output low, off = input. */
void led_set(int led, unsigned colour)
{
    for (int c = 0; c < 3; c++) {
        int p = led_pins[led][c];
        if (colour & (1u << c)) gpio_output(p, false);
        else gpio_input(p);
    }
}

void ui_init(void)
{
    *iocon(PIN_BUTTON) = IOCON_FUNC(0) | IOCON_PULLUP;
    gpio_input(PIN_BUTTON);
    gpio_output(PIN_DISP_DATA, false);
    gpio_output(PIN_DISP_CLK, false);
    gpio_output(PIN_DISP_LATCH, false);
    gpio_output(PIN_DISP_OE, false);
    for (int l = 0; l < 2; l++)
        for (int c = 0; c < 3; c++)
            *iocon(led_pins[l][c]) = IOCON_FUNC(0) | IOCON_NOPULL;
    gpio_output(PIN_IND_ROTOR0, false);
    gpio_output(PIN_IND_ROTOR1, false);
    gpio_output(PIN_IND_TIMEBASE, false);
    gpio_output(PIN_IND_STATUS_N, true);
    disp_clear();
    led_set(0, C_OFF);
    led_set(1, C_OFF);
}

bool button_poll(void)
{
    static bool was_down;
    static uint32_t t_down;
    bool down = !gpio_read(PIN_BUTTON);
    bool fire = false;
    uint32_t now = sys_ms();

    if (down && !was_down) t_down = now;
    if (down && now - t_down > 3000) {      /* lhbs addition: hold 3 s -> USB ISP */
        disp_char('P', false);
        led_set(0, C_PURPLE);
        sys_delay_ms(200);
        iap_reinvoke_isp();
    }
    if (!down && was_down && now - t_down > 50 && now - t_down < 1000)
        fire = true;                        /* stock: release within 1 s of press */
    was_down = down;
    return fire;
}
