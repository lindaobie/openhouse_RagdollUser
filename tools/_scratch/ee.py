import sys, zlib, struct, re
d = open(sys.argv[1],'rb').read()
ee = d[0x1F040:0x1F040+4032]
print("EEPROM 4032 字节，前 12:", ee[:12].hex(' '))
for name, off in (("分区0(用户当前)",0x000), ("分区1(用户上次)",0x300), ("分区2(工厂)",0x600)):
    h = ee[off:off+12]
    if h[:4] != b'CPRM':
        print("%-16s 头不对: %s" % (name, h[:4])); continue
    ver, crc = struct.unpack_from('<II', h, 4)
    payload = ee[off+12: off+12+551]
    calc = zlib.crc32(payload) & 0xFFFFFFFF
    print("%-16s version=%-3d crc=0x%08X  payload-crc=0x%08X  %s" %
          (name, ver, crc, calc, "MATCH" if calc==crc else "MISMATCH"))
print()
print("===== 0xDC0-0xE10 的 ASCII 区（工厂 ID）=====")
seg = ee[0xDC0:0xE10]
print(" ", seg.hex(' '))
print("  可打印:", ''.join(chr(b) if 32<=b<127 else '.' for b in seg))
print()
print("===== 在 EEPROM 里找贴纸号 FABBHAA01321 =====")
print("  ", "找到" if b'FABBHAA01321' in ee else "没找到")
print()
print("===== ASCII 形式的 8 位十六进制串 =====")
hits = sorted(set(m.group().decode() for m in re.finditer(rb'(?<![0-9A-Fa-f])[0-9A-Fa-f]{8}(?![0-9A-Fa-f])', ee) if re.search(rb'[0-9]', m.group())))
for h in hits[:20]: print("   ", h)
