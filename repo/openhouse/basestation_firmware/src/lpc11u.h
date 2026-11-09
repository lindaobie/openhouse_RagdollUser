/* Minimal LPC11U3x register map - only what this firmware touches.
 * Addresses from UM10462 and cross-checked against the stock lighthouse_tx image. */
#pragma once
#include <stdint.h>

#define REG(a) (*(volatile uint32_t *)(a))

/* SYSCON 0x40048000 */
#define SYSPLLCTRL      REG(0x40048008)
#define SYSPLLSTAT      REG(0x4004800C)
#define USBPLLCTRL      REG(0x40048010)
#define USBPLLSTAT      REG(0x40048014)
#define SYSOSCCTRL      REG(0x40048020)
#define SYSRSTSTAT      REG(0x40048030)   /* bit0 POR 1 EXTRST 2 WDT 3 BOD 4 SYSRST */
#define SYSPLLCLKSEL    REG(0x40048040)
#define SYSPLLCLKUEN    REG(0x40048044)
#define USBPLLCLKSEL    REG(0x40048048)
#define USBPLLCLKUEN    REG(0x4004804C)
#define MAINCLKSEL      REG(0x40048070)
#define MAINCLKUEN      REG(0x40048074)
#define SYSAHBCLKDIV    REG(0x40048078)
#define SYSAHBCLKCTRL   REG(0x40048080)
#define UARTCLKDIV      REG(0x40048098)
#define USBCLKSEL       REG(0x400480C0)
#define USBCLKUEN       REG(0x400480C4)
#define USBCLKDIV       REG(0x400480C8)
#define PDRUNCFG        REG(0x40048238)

#define AHB_GPIO   (1u << 6)
#define AHB_CT32B1 (1u << 10)
#define AHB_USART  (1u << 12)
#define AHB_USB    (1u << 14)
#define AHB_IOCON  (1u << 16)
#define AHB_RAM1   (1u << 26)

/* IOCON 0x40044000: PIO0_n at +4n, PIO1_n at +0x60+4n */
#define IOCON_P0(n)     REG(0x40044000 + 4 * (n))
#define IOCON_P1(n)     REG(0x40044060 + 4 * (n))
#define IOCON_FUNC(f)   ((f) & 7)
#define IOCON_PULLUP    (2u << 3)
#define IOCON_NOPULL    (0u << 3)

/* GPIO 0x50000000 (port register block at +0x2000) */
#define GPIO_DIR(p)     REG(0x50002000 + 4 * (p))
#define GPIO_PIN(p)     REG(0x50002100 + 4 * (p))
#define GPIO_SET(p)     REG(0x50002200 + 4 * (p))
#define GPIO_CLR(p)     REG(0x50002280 + 4 * (p))
#define GPIO_NOT(p)     REG(0x50002300 + 4 * (p))

/* GPIO pin interrupt block 0x4004C000 */
#define PINT_CIENR      REG(0x4004C00C)

/* USART 0x40008000 */
#define U0RBR           REG(0x40008000)
#define U0THR           REG(0x40008000)
#define U0DLL           REG(0x40008000)
#define U0DLM           REG(0x40008004)
#define U0FCR           REG(0x40008008)
#define U0LCR           REG(0x4000800C)
#define U0LSR           REG(0x40008014)
#define U0FDR           REG(0x40008028)

/* 32-bit timers (only MCR touched before ISP re-entry) */
#define CT32B0_MCR      REG(0x40014014)
#define CT32B1_MCR      REG(0x40018014)

/* USB device controller 0x40080000 */
#define USB_DEVCMDSTAT  REG(0x40080000)
#define USB_EPLISTSTART REG(0x40080008)
#define USB_DATABUFSTART REG(0x4008000C)
#define USB_EPSKIP      REG(0x40080014)
#define USB_INTSTAT     REG(0x40080020)
#define USB_INTEN       REG(0x40080024)

/* Cortex-M0 core */
#define SYST_CSR        REG(0xE000E010)
#define SYST_RVR        REG(0xE000E014)
#define SYST_CVR        REG(0xE000E018)
#define NVIC_ISER       REG(0xE000E100)
/* Cortex-M0: IPR is word access only, 2 priority bits at the top of each byte */
static inline void nvic_set_priority(int irq, unsigned prio)
{
    volatile uint32_t *ipr = (volatile uint32_t *)(0xE000E400 + 4 * (irq >> 2));
    unsigned sh = 8 * (irq & 3) + 6;
    *ipr = (*ipr & ~(3u << sh)) | ((prio & 3) << sh);
}
#define NVIC_IPR(irq) NVIC_IPR_is_word_only
#define NVIC_ICER       REG(0xE000E180)
#define SCB_AIRCR       REG(0xE000ED0C)

/* ROM IAP entry (Thumb) */
#define IAP_ENTRY       0x1FFF1FF1u
#define IAP_PREPARE         50
#define IAP_COPY_RAM2FLASH  51
#define IAP_ERASE           52
#define IAP_READ_PART_ID    54
#define IAP_READ_BOOT_VER   55
#define IAP_REINVOKE_ISP    57
#define IAP_READ_UID        58
#define IAP_EEPROM_WRITE    61
#define IAP_EEPROM_READ     62
