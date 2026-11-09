#include "iap.h"
#include "board.h"
#include "sys.h"
#include "usb_cdc.h"

typedef void (*iap_fn)(uint32_t *cmd, uint32_t *res);
static uint32_t cmd[5], res[5];

static uint32_t iap(uint32_t c, uint32_t a, uint32_t b, uint32_t d, uint32_t e)
{
    uint32_t s = irq_save();
    cmd[0] = c; cmd[1] = a; cmd[2] = b; cmd[3] = d; cmd[4] = e;
    ((iap_fn)IAP_ENTRY)(cmd, res);
    irq_restore(s);
    return res[0];
}

uint32_t iap_eeprom_read(uint32_t addr, void *dst, uint32_t len)
{
    return iap(IAP_EEPROM_READ, addr, (uint32_t)dst, len, CPU_HZ / 1000);
}

uint32_t iap_eeprom_write(uint32_t addr, const void *src, uint32_t len)
{
    return iap(IAP_EEPROM_WRITE, addr, (uint32_t)src, len, CPU_HZ / 1000);
}

/* Erase a 4 KB sector and program it in 1 KB chunks produced by fill(),
 * so callers need only a 1 KB RAM buffer. */
uint32_t iap_flash_write_sector_chunked(unsigned sector, uint32_t *buf1k,
                                        void (*fill)(uint32_t *buf, unsigned chunk, void *ctx), void *ctx)
{
    uint32_t rc;
    if ((rc = iap(IAP_PREPARE, sector, sector, 0, 0))) return rc;
    if ((rc = iap(IAP_ERASE, sector, sector, CPU_HZ / 1000, 0))) return rc;
    for (unsigned c = 0; c < 4; c++) {
        fill(buf1k, c, ctx);
        if ((rc = iap(IAP_PREPARE, sector, sector, 0, 0))) return rc;
        if ((rc = iap(IAP_COPY_RAM2FLASH, sector * 4096u + c * 1024u, (uint32_t)buf1k, 1024,
                      CPU_HZ / 1000))) return rc;
    }
    return 0;
}

uint32_t iap_flash_write_sector(unsigned sector, const uint32_t *src4k)
{
    uint32_t rc;
    if ((rc = iap(IAP_PREPARE, sector, sector, 0, 0))) return rc;
    if ((rc = iap(IAP_ERASE, sector, sector, CPU_HZ / 1000, 0))) return rc;
    if ((rc = iap(IAP_PREPARE, sector, sector, 0, 0))) return rc;
    return iap(IAP_COPY_RAM2FLASH, sector * 4096u, (uint32_t)src4k, 4096, CPU_HZ / 1000);
}

#define ISP_MAGIC 0x15B00715u
extern uint32_t _mailbox;

void iap_reinvoke_isp(void)
{
    usb_cdc_shutdown();                     /* let the host see us leave */
    SYSAHBCLKCTRL |= AHB_RAM1;
    _mailbox = ISP_MAGIC;
    __asm volatile ("dsb");
    SCB_AIRCR = 0x05FA0004;                 /* SYSRESETREQ: RAM is preserved */
    for (;;) {}
}

bool iap_isp_requested(void)
{
    SYSAHBCLKCTRL |= AHB_RAM1;
    if (_mailbox != ISP_MAGIC) return false;
    _mailbox = 0;
    return true;
}

/* Stock iap_reinvoke_isp @0x8FC0 (call only from a clean post-reset state). */
void iap_reinvoke_isp_now(void)
{
    __asm volatile ("cpsid i");
    SYSAHBCLKCTRL |= AHB_USB | AHB_CT32B1 | AHB_GPIO | AHB_IOCON;
    CT32B0_MCR = 0;
    CT32B1_MCR = 0;
    SYSAHBCLKDIV = 1;
    SYST_CSR &= ~3u;
    PINT_CIENR = 0xff;
    for (int i = 0; i < 32; i++) NVIC_ICER = 1u << i;
    __asm volatile ("msr msp, %0" :: "r"(0x10001FE0u));
    cmd[0] = IAP_REINVOKE_ISP;
    ((iap_fn)IAP_ENTRY)(cmd, res);
    for (;;) {}
}
