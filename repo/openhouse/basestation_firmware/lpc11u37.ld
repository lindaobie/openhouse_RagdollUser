/* LPC11U37 (128 KB flash / 8 KB SRAM0). The image is far smaller than 64 KB,
 * so the same binary also fits the 96 KB LPC11U36 should a board carry one. */
MEMORY
{
  FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 128K
  RAM   (rwx) : ORIGIN = 0x10000000, LENGTH = 8K - 32   /* top 32 B reserved for IAP */
}

_estack = ORIGIN(RAM) + LENGTH(RAM);
/* Reset-surviving mailbox (ISP request) at the end of SRAM1. Not in SRAM0: the
 * boot ROM's reset code writes 0x3456ABCD near the top of SRAM0 (literal at
 * 0x1FFF013C), and it references SRAM1 only at 0x20000000. */
_mailbox = 0x200007F0;

SECTIONS
{
  .text :
  {
    KEEP(*(.vectors))
    . = 0x2FC;
    KEEP(*(.crp))             /* CRP word: must stay 0xFFFFFFFF (no protection) */
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
  } > FLASH

  .data : AT(ADDR(.text) + SIZEOF(.text))
  {
    _sdata = .;
    *(.data*)
    . = ALIGN(4);
    _edata = .;
  } > RAM
  _sidata = LOADADDR(.data);

  .bss (NOLOAD) :
  {
    _sbss = .;
    *(.bss*) *(COMMON)
    . = ALIGN(4);
    _ebss = .;
  } > RAM

  /DISCARD/ : { *(.ARM.exidx*) *(.ARM.attributes) *(.comment) }
}
