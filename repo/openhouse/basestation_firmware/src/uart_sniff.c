/* UART on P1_27 (TXD) / P1_26 (RXD), 115200 8N1 - stock console port.
 * lhbs: receive-only logger to discover what (if anything) the Bluetooth
 * power-management chip sends to the main MCU. Stock uart_init @0xDBF8. */
#include "uart_sniff.h"
#include "board.h"
#include "sys.h"
#include "console.h"
#include <string.h>

#define U0RBR REG(0x40008000)
#define U0DLL REG(0x40008000)
#define U0DLM REG(0x40008004)
#define U0FCR REG(0x40008008)
#define U0LCR REG(0x4000800C)
#define U0LSR REG(0x40008014)
#define U0THR REG(0x40008000)

volatile uint32_t uart_cmds_ok, uart_cmds_bad;

static void tx(const char *s)
{
    while (*s) {
        uint32_t t0 = sys_ms();
        while (!(U0LSR & 0x20)) if (sys_ms() - t0 > 2) return;
        U0THR = (uint8_t)*s++;
    }
}

/* Stock console subset. The Bluetooth power-management chip sends
 * "param set sys.standby 0|1" about once per second (observed on hardware). */
static void uart_line(char *l)
{
    while (*l == ' ') l++;
    if (!strncmp(l, "param set ", 10)) {
        char *name = l + 10, *val = strchr(name, ' ');
        if (val) {
            *val++ = 0;
            if (console_param_set(name, val)) { uart_cmds_ok++; return; }
        }
        uart_cmds_bad++;
        tx("Invalid parameter.\r\n");
    } else if (!strncmp(l, "param get ", 10)) {
        char val[24];
        if (console_param_format(l + 10, val, sizeof val)) {
            tx(l + 10); tx("\t"); tx(val); tx("\r\n");      /* stock: "%s\t" value "\r\n" */
            uart_cmds_ok++;
        } else {
            uart_cmds_bad++;
            tx("Invalid parameter.\r\n");
        }
    } else if (*l) {
        uart_cmds_bad++;
    }
}

#define LOG 256
static uint8_t buf[LOG];
static uint32_t ts[LOG];
static volatile uint16_t head, count;
volatile uint32_t uart_rx_total;

void uart_sniff_init(void)
{
    *iocon(PIN_UART_RX) = IOCON_FUNC(2) | IOCON_PULLUP;
    *iocon(PIN_UART_TX) = IOCON_FUNC(2) | IOCON_PULLUP;
    SYSAHBCLKCTRL |= AHB_USART;
    REG(0x40048098) = 1;                    /* UARTCLKDIV */
    U0LCR = 0x83;
    U0DLM = 0;
    U0DLL = CPU_HZ / 16 / 115200;           /* stock divisor (26) */
    U0LCR = 0x03;
    U0FCR = 0x07;
}

void uart_sniff_poll(void)
{
    while (U0LSR & 1) {
        uint8_t c = U0RBR;
        uart_rx_total++;
        buf[head] = c;
        ts[head] = sys_ms();
        head = (head + 1) % LOG;
        if (count < LOG) count++;

        static char line[64];
        static unsigned n;
        if (c == '\r' || c == '\n') {
            if (n) { line[n] = 0; uart_line(line); n = 0; tx("\r\nlhtx> "); }
        } else if (c >= ' ' && n < sizeof line - 1) {
            line[n++] = (char)c;
        }
    }
}

void uart_sniff_dump(void (*out)(const char *fmt, ...))
{
    out("UART RX total %u bytes, commands ok %u / rejected %u, last %u bytes:\n",
        uart_rx_total, uart_cmds_ok, uart_cmds_bad, count);
    unsigned start = (head + LOG - count) % LOG;
    for (unsigned i = 0; i < count; i += 16) {
        out("[%u ms]", ts[(start + i) % LOG]);
        for (unsigned j = i; j < i + 16 && j < count; j++) out(" %02x", buf[(start + j) % LOG]);
        out("  |");
        for (unsigned j = i; j < i + 16 && j < count; j++) {
            uint8_t c = buf[(start + j) % LOG];
            out("%c", c >= 32 && c < 127 ? c : '.');
        }
        out("|\n");
    }
}
