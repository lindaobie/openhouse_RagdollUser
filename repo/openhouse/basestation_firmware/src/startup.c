/* Reset/vector table for LPC11U3x. Word 7 (checksum) is filled in after
 * linking by tools/lpcsum.py; the boot ROM will not start the image otherwise. */
#include <stdint.h>
#include "lpc11u.h"
#include "board.h"
#include "iap.h"
#include "sys.h"

extern uint32_t _estack, _sidata, _sdata, _edata, _sbss, _ebss;
int main(void);
void SysTick_Handler(void);
void USB_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void CT32B0_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));
void CT32B1_IRQHandler(void) __attribute__((weak, alias("Default_Handler")));

void Default_Handler(void) { for (;;) {} }

/* Runs before anything else: put every emitter-related line into its known
 * OFF state, because the laser driver is an external chip whose configuration
 * survives an MCU reset (e.g. after ISP re-entry from the stock firmware). */
static void safe_outputs(void)
{
    SYSAHBCLKCTRL |= AHB_GPIO | AHB_IOCON;
    gpio_output(PIN_LASER0_EN, false);
    gpio_output(PIN_LASER1_EN, false);
    gpio_output(PIN_SYNC_GATE, false);
    gpio_output(PIN_CARRIER_EN, false);
    gpio_output(PIN_EMITTER_PWR_N, true);   /* stock: low only when locked + emitting */
}

extern uint32_t _mailbox;
uint32_t boot_mailbox, boot_rststat;        /* diagnostics, shown by 'status' */

void Reset_Handler(void)
{
    safe_outputs();
    SYSAHBCLKCTRL |= AHB_RAM1;
    uint32_t mb = _mailbox, rs = SYSRSTSTAT;
    if (iap_isp_requested()) {              /* 'isp' command / 3 s button hold */
        sys_clock_init();
        /* Clear USB RAM: a soft reset keeps our CDC endpoint list/buffers there,
         * and the ROM's USB ISP does not fully reinitialise it. */
        SYSAHBCLKCTRL |= (1u << 27);
        for (volatile uint32_t *p = (uint32_t *)0x20004000; p < (uint32_t *)0x20004800; p++) *p = 0;
        /* Let the crystal/USB PLL settle: lhfw's working ISP entry happened
         * seconds after clock start-up, this path is microseconds after. ~300 ms. */
        for (volatile uint32_t i = 0; i < 3600000; i++) {}
        iap_reinvoke_isp_now();
    }
    uint32_t *s = &_sidata, *d = &_sdata;
    while (d < &_edata) *d++ = *s++;
    for (d = &_sbss; d < &_ebss; ) *d++ = 0;
    boot_mailbox = mb;
    boot_rststat = rs;
    SYSRSTSTAT = 0x1F;                      /* write-1-to-clear */
    main();
    for (;;) {}
}

__attribute__((section(".vectors"), used))
void (*const vectors[48])(void) = {
    (void (*)(void))&_estack,
    Reset_Handler,
    Default_Handler,        /* NMI */
    Default_Handler,        /* HardFault */
    0, 0, 0,
    0,                      /* checksum, patched post-link */
    0, 0, 0,
    Default_Handler,        /* SVCall */
    0, 0,
    Default_Handler,        /* PendSV */
    SysTick_Handler,
    [16 ... 16 + 17] = Default_Handler,
    [16 + 18] = CT32B0_IRQHandler,      /* rotor index capture */
    [16 + 19] = CT32B1_IRQHandler,      /* timebase */
    [16 + 20 ... 16 + 21] = Default_Handler,
    [16 + 22] = USB_IRQHandler,         /* USB_IRQ */
    [16 + 23 ... 47] = Default_Handler,
};

__attribute__((section(".crp"), used))
const uint32_t crp_word = 0xFFFFFFFFu;
