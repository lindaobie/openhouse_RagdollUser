/* USB CDC-ACM serial port using the LPC11U3x boot-ROM USB device stack.
 * Not part of the stock firmware (stock has no USB code at all): lhbs addition
 * so a closed unit has a debug console. Shows up as /dev/ttyACM* on Linux.
 * Data path follows NXP LPCOpen's cdc_vcom example. */
#include <string.h>
#include "usb_cdc.h"
#include "usbd_rom.h"
#include "board.h"
#include "sys.h"
#include "ui.h"

/* LED1 diagnostics while bringing USB up: G = line coding, B = DTR, R = data in */
static unsigned diag;
static void diag_set(unsigned bit) { diag |= bit; }    /* kept for debugging */

#define EP_INT_IN    0x82
#define EP_BULK_IN   0x81
#define EP_BULK_OUT  0x01
#define EP_SIZE      64

#define USB_REG_BASE 0x40080000u
#define USB_RAM_BASE 0x20004000u
#define USB_RAM_SIZE 0x800u

/* ---- descriptors (must be word aligned for the ROM) ---- */
__attribute__((aligned(4))) static const uint8_t dev_desc[] = {
    18, 1, 0x00, 0x02,          /* USB 2.0 */
    0x02, 0x00, 0x00,           /* class CDC */
    64,                         /* EP0 size */
    0xC9, 0x1F, 0x83, 0x00,     /* VID 0x1FC9 (NXP), PID 0x0083 (NXP VCOM example) */
    0x00, 0x01,                 /* bcdDevice 1.00 */
    1, 2, 3,                    /* manufacturer, product, serial strings */
    1,
};

#define CFG_LEN (9 + 9 + 5 + 5 + 4 + 5 + 7 + 9 + 7 + 7)
__attribute__((aligned(4))) static const uint8_t cfg_desc[] = {
    9, 2, CFG_LEN & 0xff, CFG_LEN >> 8, 2, 1, 0, 0x80, 50,     /* bus powered, 100 mA */
    /* interface 0: communications */
    9, 4, 0, 0, 1, 0x02, 0x02, 0x01, 0,
    5, 0x24, 0x00, 0x10, 0x01,                  /* header, CDC 1.10 */
    5, 0x24, 0x01, 0x00, 1,                     /* call management */
    4, 0x24, 0x02, 0x02,                        /* ACM: line coding/state */
    5, 0x24, 0x06, 0, 1,                        /* union: master 0, slave 1 */
    7, 5, EP_INT_IN, 0x03, 16, 0, 2,            /* interrupt IN */
    /* interface 1: data */
    9, 4, 1, 0, 2, 0x0A, 0x00, 0x00, 0,
    7, 5, EP_BULK_OUT, 0x02, EP_SIZE, 0, 0,
    7, 5, EP_BULK_IN, 0x02, EP_SIZE, 0, 0,
    0,                                          /* ROM looks for a terminating 0 */
};

#define USTR(...) __VA_ARGS__
__attribute__((aligned(4))) static const uint8_t str_desc[] = {
    4, 3, 0x09, 0x04,                                           /* LANGID en-US */
    12, 3, 'l',0,'h',0,'r',0,'w',0,' ',0,                       /* "lhrw " */
    36, 3, 'L',0,'i',0,'g',0,'h',0,'t',0,'h',0,'o',0,'u',0,'s',0,'e',0,' ',0,
           '1',0,'.',0,'0',0,' ',0,'B',0,'S',0,                 /* "Lighthouse 1.0 BS" */
    10, 3, 'l',0,'h',0,'b',0,'s',0,                             /* "lhbs" */
};

/* ---- buffers ---- */
static USBD_HANDLE_T h_usb, h_cdc;
static volatile bool connected;
static volatile bool tx_busy;

#define RING 512
static uint8_t rx_ring[RING], tx_ring[RING];
static volatile uint16_t rx_head, rx_tail, tx_head, tx_tail;
/* ReadEP/WriteEP copy to/from the ROM's own endpoint buffers (verified in the ROM
 * dump: both call its memcpy @0x1FFF3EF4), so these live in normal SRAM. Carving
 * them out of 'free' USB RAM overlapped buffers the ROM allocates later. */
static uint8_t rx_pkt[EP_SIZE] __attribute__((aligned(4)));
static uint8_t tx_pkt[EP_SIZE] __attribute__((aligned(4)));

/* called with IRQs masked or from the USB ISR */
static void tx_kick(void)
{
    if (tx_busy || !connected || tx_head == tx_tail) return;
    unsigned n = 0;
    while (n < EP_SIZE && tx_tail != tx_head) {
        tx_pkt[n++] = tx_ring[tx_tail];
        tx_tail = (tx_tail + 1) % RING;
    }
    tx_busy = true;
    USBD_API->hw->WriteEP(h_usb, EP_BULK_IN, tx_pkt, n);
}

static ErrorCode_t bulk_in(USBD_HANDLE_T h, void *data, uint32_t ev)
{
    (void)h; (void)data;
    if (ev == USB_EVT_IN) { tx_busy = false; tx_kick(); }
    return LPC_OK;
}

static ErrorCode_t bulk_out(USBD_HANDLE_T h, void *data, uint32_t ev)
{
    (void)data;
    if (ev == USB_EVT_OUT) {
        diag_set(C_RED);
        uint32_t n = USBD_API->hw->ReadEP(h, EP_BULK_OUT, rx_pkt);
        for (uint32_t i = 0; i < n; i++) {
            uint16_t next = (rx_head + 1) % RING;
            if (next == rx_tail) break;             /* drop on overflow */
            rx_ring[rx_head] = rx_pkt[i];
            __asm volatile ("" ::: "memory");
            rx_head = next;
        }
    }
    return LPC_OK;
}

/* The host sets the line coding when it opens the port: use it as "connected". */
static ErrorCode_t set_line_code(USBD_HANDLE_T h, void *lc)
{
    (void)h; (void)lc;
    diag_set(C_GREEN);
    connected = true;
    tx_kick();
    return LPC_OK;
}

static ErrorCode_t set_ctrl_line(USBD_HANDLE_T h, uint16_t state)
{
    (void)h;
    if (state & 1) diag_set(C_BLUE);
    connected = true;                               /* don't drop on DTR low */
    tx_kick();
    return LPC_OK;
}

static ErrorCode_t configured(USBD_HANDLE_T h)
{
    (void)h;
    connected = true;       /* data flows as soon as the host opens the port */
    return LPC_OK;
}

void USB_IRQHandler(void) { USBD_API->hw->ISR(h_usb); }

bool usb_cdc_init(void)
{
    /* pins: P0_3 = USB_VBUS, P0_6 = USB_CONNECT (SoftConnect, as the ROM ISP uses) */
    IOCON_P0(3) = IOCON_FUNC(1);
    IOCON_P0(6) = IOCON_FUNC(1);
    SYSAHBCLKCTRL |= AHB_USB | (1u << 27);          /* USB + USB RAM */
    PDRUNCFG &= ~((1u << 10) | (1u << 8));          /* USB PAD + USB PLL (already up) */

    memset((void *)USB_RAM_BASE, 0, USB_RAM_SIZE);
    USBD_API_INIT_PARAM_T up;
    memset(&up, 0, sizeof up);
    up.USB_Configure_Event = configured;
    up.usb_reg_base = USB_REG_BASE;
    up.mem_base = USB_RAM_BASE;
    up.mem_size = USB_RAM_SIZE;
    up.max_num_ep = 3;

    USB_CORE_DESCS_T d = {
        (uint8_t *)dev_desc, (uint8_t *)str_desc,
        (uint8_t *)cfg_desc, (uint8_t *)cfg_desc, 0,
    };
    if (USBD_API->hw->Init(&h_usb, &d, &up) != LPC_OK) return false;

    USBD_CDC_INIT_PARAM_T cp;
    memset(&cp, 0, sizeof cp);
    cp.mem_base = up.mem_base;          /* Init advanced these past its own use */
    cp.mem_size = up.mem_size;
    cp.cif_intf_desc = (uint8_t *)&cfg_desc[9];
    cp.dif_intf_desc = (uint8_t *)&cfg_desc[9 + 9 + 5 + 5 + 4 + 5 + 7];
    cp.SetLineCode = set_line_code;
    cp.SetCtrlLineState = set_ctrl_line;
    if (USBD_API->cdc->init(h_usb, &cp, &h_cdc) != LPC_OK) return false;

    USBD_API->core->RegisterEpHandler(h_usb, USB_EP_INDEX_IN(EP_BULK_IN), bulk_in, 0);
    USBD_API->core->RegisterEpHandler(h_usb, USB_EP_INDEX_OUT(EP_BULK_OUT), bulk_out, 0);

    nvic_set_priority(22, 3);           /* lowest: never delay timing IRQs */
    NVIC_ISER = 1u << 22;               /* USB_IRQ */
    USBD_API->hw->Connect(h_usb, 1);
    return true;
}

bool usb_cdc_connected(void) { return connected; }

/* Detach cleanly so the host notices before the boot ROM's own USB ISP drive
 * enumerates (otherwise ISP re-entry hangs with our ttyACM still present). */
void usb_cdc_shutdown(void)
{
    if (!h_usb) return;
    NVIC_ICER = 1u << 22;
    USBD_API->hw->Connect(h_usb, 0);            /* release D+ pull-up */
    connected = false;
    sys_delay_ms(500);
    /* Return the controller to its reset state: the ROM ISP does not clear the
     * device address / enable bits we leave behind, so the host could never
     * enumerate its mass-storage drive. */
    USB_INTEN = 0;
    USB_DEVCMDSTAT = 0;                         /* DEV_EN=0, address 0, DCON=0 */
    USB_INTSTAT = 0xFFFFFFFFu;                  /* write-1-to-clear */
    USB_EPSKIP = 0;
    USB_EPLISTSTART = 0;
    USB_DATABUFSTART = 0;
    /* leave USB and USB-RAM clocks on: the ROM ISP needs both (USB RAM is on
     * after reset and stock never touches it) */
}

int usb_cdc_getc(void)
{
    if (rx_head == rx_tail) return -1;
    int c = rx_ring[rx_tail];
    rx_tail = (rx_tail + 1) % RING;
    return c;
}

void usb_cdc_write(const char *s, unsigned n)
{
    if (!connected) return;
    while (n--) {
        uint16_t next = (tx_head + 1) % RING;
        uint32_t t0 = sys_ms();
        while (next == tx_tail) {                   /* ring full: wait, but never hang */
            uint32_t m = irq_save(); tx_kick(); irq_restore(m);
            if (!connected || sys_ms() - t0 > 50) return;
        }
        tx_ring[tx_head] = (uint8_t)*s++;
        __asm volatile ("" ::: "memory");       /* byte must land before head moves */
        tx_head = next;
    }
    uint32_t m = irq_save();
    tx_kick();
    irq_restore(m);
}
