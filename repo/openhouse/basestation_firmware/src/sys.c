#include "sys.h"
#include "board.h"

static volatile uint32_t ms_ticks;

__attribute__((weak)) void sys_tick_20ms(void) {}

void SysTick_Handler(void)
{
    if (++ms_ticks % 20 == 0) sys_tick_20ms();  /* UI refresh, independent of main loop */
}

/* Same register sequence as stock SystemInit @0x1F1C: 12 MHz crystal, SYSPLL
 * M=4 P=2 -> 48 MHz main clock, USB PLL also at 48 MHz. */
void sys_clock_init(void)
{
    PDRUNCFG &= ~(1u << 5);
    SYSOSCCTRL = 0;
    for (volatile int i = 0; i < 200; i++) {}
    SYSPLLCLKSEL = 1;
    SYSPLLCLKUEN = 1; SYSPLLCLKUEN = 0; SYSPLLCLKUEN = 1;
    while (!(SYSPLLCLKUEN & 1)) {}
    SYSPLLCTRL = 0x23;
    PDRUNCFG &= ~(1u << 7);
    while (!(SYSPLLSTAT & 1)) {}
    MAINCLKSEL = 3;
    MAINCLKUEN = 1; MAINCLKUEN = 0; MAINCLKUEN = 1;
    while (!(MAINCLKUEN & 1)) {}
    SYSAHBCLKDIV = 1;
    PDRUNCFG &= ~((1u << 10) | (1u << 8));
    USBPLLCLKSEL = 1;
    USBPLLCLKUEN = 1; USBPLLCLKUEN = 0; USBPLLCLKUEN = 1;
    while (!(USBPLLCLKUEN & 1)) {}
    USBPLLCTRL = 0x23;
    while (!(USBPLLSTAT & 1)) {}
    USBCLKSEL = 0; USBCLKUEN = 0; USBCLKUEN = 1;
    USBCLKDIV = 1;
    SYSAHBCLKCTRL |= AHB_IOCON | AHB_GPIO;
}

void sys_tick_init(void)
{
    SYST_RVR = CPU_HZ / 1000 - 1;
    SYST_CVR = 0;
    SYST_CSR = 7;
}

uint32_t sys_ms(void) { return ms_ticks; }

void sys_delay_ms(uint32_t ms)
{
    uint32_t t = ms_ticks;
    while (ms_ticks - t < ms) {}
}
