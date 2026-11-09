#pragma once
#include <stdint.h>
void uart_sniff_init(void);
void uart_sniff_poll(void);
void uart_sniff_dump(void (*out)(const char *fmt, ...));
extern volatile uint32_t uart_rx_total;
