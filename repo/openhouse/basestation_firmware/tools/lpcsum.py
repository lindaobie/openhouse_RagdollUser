#!/usr/bin/env python3
"""LPC11Uxx vector-table checksum: word 7 (offset 0x1C) makes sum(words 0..7) == 0.
The boot ROM refuses to run user code (and stays in ISP) if this is wrong.

usage: lpcsum.py check FILE        exit 0 if valid
       lpcsum.py fix FILE [OUT]    rewrite word 7 in place (or to OUT)
"""
import struct, sys

def checksum(img: bytes) -> int:
    return (-sum(struct.unpack_from('<7I', img, 0))) & 0xFFFFFFFF

def is_valid(img: bytes) -> bool:
    return sum(struct.unpack_from('<8I', img, 0)) & 0xFFFFFFFF == 0

def fix(img: bytes) -> bytes:
    b = bytearray(img)
    struct.pack_into('<I', b, 0x1C, checksum(b))
    return bytes(b)

def main(argv):
    if len(argv) < 3 or argv[1] not in ('check', 'fix'):
        sys.exit(__doc__)
    img = open(argv[2], 'rb').read()
    if argv[1] == 'check':
        ok = is_valid(img)
        print(f"{argv[2]}: word7={struct.unpack_from('<I', img, 0x1C)[0]:#010x} "
              f"expected={checksum(img):#010x} {'OK' if ok else 'BAD'}")
        sys.exit(0 if ok else 1)
    out = argv[3] if len(argv) > 3 else argv[2]
    open(out, 'wb').write(fix(img))
    print(f"{out}: word7 := {checksum(img):#010x}")

if __name__ == '__main__':
    main(sys.argv)
