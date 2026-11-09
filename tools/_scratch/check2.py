import hashlib, re, sys, struct
dump = open(sys.argv[1],'rb').read()
rel  = open(sys.argv[2],'rb').read()
print("dump = %d 字节, release 镜像 = %d 字节" % (len(dump), len(rel)))
same = dump[:len(rel)] == rel
print("dump 前 %d 字节 == release: %s" % (len(rel), "完全一致" if same else "不一致"))
if same:
    print("SHA256(release) =", hashlib.sha256(rel).hexdigest())
else:
    n=len(rel); d=[i for i in range(n) if dump[i]!=rel[i]][:12]; print("  差异偏移:", d)
print("\n--- sector 31 (0x1F000) ---")
sec = dump[0x1F000:0x20000]
print("前 8 字节:", sec[:8], " 是否 LHEEDUMP:", sec[:8]==b'LHEEDUMP')
if sec[:8]==b'LHEEDUMP':
    hdr = sec[8:0x40]
    print("header 里的字段:", hdr[:24].hex(' '))
    ee = sec[0x40:0x40+4032]
    print("EEPROM 前 16 字节:", ee[:16].hex(' '))
    if ee[:4]==b'CPRM':
        ver, crc = struct.unpack_from('<II', ee, 4)
        print("  分区0: CPRM version=%d crc32=0x%08X" % (ver, crc))
        # 分区 2 (factory) 在 0x600
        f = ee[0x600:0x600+12]
        if f[:4]==b'CPRM':
            v2,c2 = struct.unpack_from('<II', f, 4)
            print("  分区2(factory): version=%d crc32=0x%08X" % (v2,c2))
    # 找 sys.name 附近的 u32 序列号
    m = re.search(rb'sys\.name', ee)
    print("  'sys.name' 出现在 EEPROM 偏移:", hex(m.start()) if m else "未找到")
else:
    print("  没有 EEPROM 快照（刷完还没正常上电启动过）")
print("\n--- 构建时间 ---")
for name, buf in (("dump", dump[:len(rel)]), ("release", rel)):
    m = re.search(rb'lhbs [A-Z][a-z]{2} [ 0-9]\d \d{4} \d\d:\d\d:\d\d', buf)
    print("  %-8s %s" % (name, m.group().decode() if m else "(无)"))
