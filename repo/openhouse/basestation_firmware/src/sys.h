#pragma once
#include <stdint.h>
#include <stdbool.h>

void     sys_clock_init(void);          /* stock SystemInit @0x1F1C */
void     sys_tick_init(void);
uint32_t sys_ms(void);
void     sys_delay_ms(uint32_t ms);
void     sys_tick_20ms(void);           /* weak hook called from SysTick */

/* Interrupt masking that nests (stock pattern around every IAP call). */
static inline uint32_t irq_save(void)
{
    uint32_t primask;
    __asm volatile ("mrs %0, primask\n cpsid i" : "=r"(primask) :: "memory");
    return primask;
}
static inline void irq_restore(uint32_t primask)
{
    __asm volatile ("msr primask, %0" :: "r"(primask) : "memory");
}
