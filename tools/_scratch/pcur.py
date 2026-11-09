import re, sys
src = open(r"J:\workspace_AI_Agent\baibaizi\research\openhouse\basestation_firmware\src\params_table.h", encoding='utf-8', errors='replace').read()
pat = re.compile(r'P\(\s*(\w+)\s*,\s*"([^"]+)"\s*,\s*(PT_\w+)\s*,\s*(\w+)\s*,\s*(\d+)\s*,\s*(0x[0-9A-Fa-f]+)')
entries = []
for m in pat.finditer(src):
    var, name, typ, ctype, size, flags = m.group(1), m.group(2), m.group(3), m.group(4), int(m.group(5)), int(m.group(6),16)
    entries.append((var, name, typ, size, flags))
print("解析到 %d 个参数" % len(entries))
off = 0
want = {}
for var, name, typ, size, flags in entries:
    if name == 'sys.param_magic':
        print("  %-28s off=%-4d size=%-3d stored=%s" % (name, off, size, not (flags & 1)))
    if name in ('sys.name','laser.0.current','laser.1.current','laser.0.pwr','laser.1.pwr','sys.version'):
        want[name] = (off, size, typ)
    if not (flags & 1):
        off += size
print("payload 总大小 = %d 字节（EEPROM 里是 551）" % off)
print()
for k,(o,s,t) in want.items():
    print("  %-18s payload 偏移 %-4d 大小 %-2d 类型 %s" % (k, o, s, t))

# 读 1 号机的 EEPROM
dump = open(sys.argv[1],'rb').read()
ee = dump[0x1F040:0x1F040+4032]
for part, base in (("当前",0x000), ("上次",0x300), ("工厂",0x600)):
    payload = ee[base+12: base+12+551]
    print()
    print("分区 %s:" % part)
    for k,(o,s,t) in want.items():
        raw = payload[o:o+s]
        if t == 'PT_U8': v = raw[0]
        elif t == 'PT_U32': v = int.from_bytes(raw,'little')
        elif t == 'PT_FLOAT': v = int.from_bytes(raw,'little')
        else: v = raw
        print("   %-18s %s" % (k, v))
