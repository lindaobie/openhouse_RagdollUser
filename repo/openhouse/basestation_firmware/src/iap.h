/* NXP boot-ROM IAP: EEPROM, flash and ISP re-entry (stock iap.c @0x8DD4-0x9074). */
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define EEPROM_SIZE     4032u   /* top 64 bytes of the 4 KB EEPROM are reserved */

uint32_t iap_eeprom_read(uint32_t addr, void *dst, uint32_t len);
uint32_t iap_eeprom_write(uint32_t addr, const void *src, uint32_t len);
uint32_t iap_flash_write_sector(unsigned sector, const uint32_t *src4k);
uint32_t iap_flash_write_sector_chunked(unsigned sector, uint32_t *buf1k,
                                        void (*fill)(uint32_t *buf, unsigned chunk, void *ctx), void *ctx);
/* Enter the USB ISP bootloader. Sets a reset-surviving request word and resets;
 * startup then jumps to the ROM before USB is touched (a direct jump after our
 * USB stack ran leaves the ROM's USB drive unable to enumerate). */
__attribute__((noreturn)) void iap_reinvoke_isp(void);
bool iap_isp_requested(void);           /* called first thing in Reset_Handler */
__attribute__((noreturn)) void iap_reinvoke_isp_now(void);
