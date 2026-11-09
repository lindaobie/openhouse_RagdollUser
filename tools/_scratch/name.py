import sys
d = open(sys.argv[1],'rb').read()
ee = d[0x1F040:0x1F040+4032]
# 表顺序: sys.param_magic (U32, 4B), sys.name (STRING, 16B) -> payload 偏移 4
# 分区头 12 字节, 所以 EEPROM 内偏移 = 12 + 4 = 16
for name, off in (("分区0 当前",0x000), ("分区1 上次",0x300), ("分区2 工厂",0x600)):
    magic = int.from_bytes(ee[off+12:off+16],'little')
    raw = ee[off+16:off+32]
    s = raw.split(b'\x00')[0].decode('ascii','replace')
    print("%-10s param_magic=0x%08X(%s)  sys.name=%r  hex=%s" %
          (name, magic, "OK" if magic==1634492787 else "BAD", s, raw.hex(' ')))
