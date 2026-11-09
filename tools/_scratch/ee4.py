import sys, zlib, struct
d = open(sys.argv[1],'rb').read()
ee = d[0x1F040:0x1F040+4032]
print("EEPROM 前 16:", ee[:16].hex(' '))
for name, off in (("分区0 当前",0x000), ("分区1 上次",0x300), ("分区2 工厂",0x600)):
    h = ee[off:off+12]
    if h[:4] != b'CPRM':
        print("%-10s 头不是 CPRM: %s" % (name, h[:4])); continue
    ver, crc = struct.unpack_from('<II', h, 4)
    payload = ee[off+12: off+12+551]
    calc = zlib.crc32(payload) & 0xFFFFFFFF
    print("%-10s version=%-3d crc=0x%08X payload-crc=0x%08X  %s" %
          (name, ver, crc, calc, "MATCH" if calc==crc else "MISMATCH"))
print()
print("工厂 ID 区 0xDC0-0xE10:")
seg = ee[0xDC0:0xE10]
print("  可打印:", ''.join(chr(b) if 32<=b<127 else '.' for b in seg))
print()
print("整片 EEPROM 是否全 0xFF:", all(b == 0xFF for b in ee))
print("整片 EEPROM 是否全 0x00:", all(b == 0x00 for b in ee))
nz = sum(1 for b in ee if b not in (0x00, 0xFF))
print("非 0/0xFF 字节数: %d / %d" % (nz, len(ee)))
# fcal 相关：看 payload 里是否有明显的浮点标定
import struct as st
p0 = ee[12:12+551]
vals = st.unpack_from('<32f', p0, 20)
print("\n分区0 payload[20:148] 当作 float 看:", ["%.6g" % v for v in vals[:8]])
