import hashlib, re, sys
dump = open(sys.argv[1],'rb').read()
rel  = open(sys.argv[2],'rb').read()
print("你读出来的镜像 : %d 字节" % len(dump))
print("官方 release   : %d 字节" % len(rel))
print("SHA256(dump)   :", hashlib.sha256(dump).hexdigest())
print("SHA256(release):", hashlib.sha256(rel).hexdigest())
print()
print("dump 前 46512 字节 SHA256:", hashlib.sha256(dump[:len(rel)]).hexdigest())
print("是否与 release 前 %d 字节完全一致: %s" % (len(rel), "是" if dump[:len(rel)] == rel else "否"))
if dump[:len(rel)] != rel:
    n = min(len(rel), len(dump)); d=[i for i in range(n) if dump[i]!=rel[i]][:12]
    print("  差异偏移:", d)
print()
# lhbs 的构建时间戳
for name, buf in (("dump", dump), ("release", rel)):
    m = re.search(rb'lhbs [A-Z][a-z]{2} [ 0-9]\d \d{4} \d\d:\d\d:\d\d', buf)
    print("%-8s 构建时间: %s" % (name, m.group().decode() if m else "(没有)"))
# 向量表
sp = int.from_bytes(dump[0:4],'little'); rst = int.from_bytes(dump[4:8],'little')
s = sum(int.from_bytes(dump[i:i+4],'little') for i in range(0,32,4)) & 0xFFFFFFFF
print("\nSP=%08x  Reset=%08x  向量校验和=%08x  %s" % (sp, rst, s, "OK" if s==0 else "BAD"))
# 尾部 0xFF 填充
tail = len(dump) - 1
while tail > 0 and dump[tail] == 0xFF: tail -= 1
print("最后一个非 0xFF 字节位置: 0x%05X (%d)，之后全是 0xFF" % (tail, tail))
# LHEEDUMP 快照
idx = dump.find(b'LHEEDUMP')
print("\nLHEEDUMP 快照: %s" % ("找到，偏移 0x%05X" % idx if idx >= 0 else "没有（说明刷完还没正常上电启动过）"))
if idx >= 0:
    ee = dump[idx+0x40: idx+0x40+4032]
    print("  EEPROM 前 12 字节:", ee[:12].hex(' '))
    if ee[:4] == b'CPRM':
        import struct
        ver, crc = struct.unpack_from('<II', ee, 4)
        print("  分区头: CPRM 版本=%d crc=0x%08X" % (ver, crc))
