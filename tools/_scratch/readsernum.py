import sys
d = open(sys.argv[1],'rb').read()
print("镜像: %d 字节" % len(d))
print("sector 31 魔数:", d[0x1F000:0x1F008])
if d[0x1F000:0x1F008] != b'LHEEDUMP':
    print("没有 EEPROM 快照 —— 这台还没跑过 lhbs？")
    sys.exit()
ee = d[0x1F040:0x1F040+4032]
for name, off in (("分区0 当前",0x000), ("分区1 上次",0x300), ("分区2 工厂",0x600)):
    magic = int.from_bytes(ee[off+12:off+16],'little')
    raw = ee[off+16:off+32]
    s = raw.split(b'\x00')[0].decode('ascii','replace')
    print("  %-10s param_magic=0x%08X(%s)  sys.name=%r" % (name, magic, "OK" if magic==1634492787 else "BAD", s))
print()
print("===== 前 8 个向量字 =====")
for i in range(8):
    print("  [%d] 0x%08x" % (i, int.from_bytes(d[i*4:i*4+4],'little')))
