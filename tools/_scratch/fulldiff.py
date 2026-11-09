import re, sys, struct
src = open(r"J:\workspace_AI_Agent\baibaizi\research\openhouse\basestation_firmware\src\params_table.h", encoding='utf-8', errors='replace').read()
pat = re.compile(r'P\(\s*(\w+)\s*,\s*"([^"]+)"\s*,\s*(PT_\w+)\s*,\s*(\w+)\s*,\s*(\d+)\s*,\s*(0x[0-9A-Fa-f]+)')
ents = [m.groups() for m in pat.finditer(src)]
table = []; off = 0
for var, name, typ, ctype, size, flags in ents:
    size = int(size); flags = int(flags, 16)
    if not (flags & 1):
        table.append((name, off, size, typ)); off += size

def val(payload, o, s, t):
    raw = payload[o:o+s]
    if t == 'PT_U8': return raw[0]
    if t == 'PT_BOOL': return bool(raw[0])
    if t == 'PT_U16': return int.from_bytes(raw,'little')
    if t == 'PT_U32': return int.from_bytes(raw,'little')
    if t == 'PT_I32': return int.from_bytes(raw,'little', signed=True)
    if t == 'PT_FLOAT': return round(struct.unpack('<f', raw)[0], 6)
    if t == 'PT_STRING': return raw.split(b'\x00')[0].decode('ascii','replace')
    return raw.hex()

d = open(sys.argv[1],'rb').read()
ee = d[0x1F040:0x1F040+4032]
p0 = ee[12:12+551]           # 用户当前
p2 = ee[0x600+12:0x600+12+551]  # 工厂

diffs = []
for name, o, s, t in table:
    a = val(p0, o, s, t); b = val(p2, o, s, t)
    if isinstance(a, float) and isinstance(b, float):
        same = abs(a-b) <= max(abs(b)*1e-6, 1e-6)
    else:
        same = a == b
    if not same:
        diffs.append((name, a, b))

print("用户分区 vs 工厂分区，不一致的参数 %d 个：" % len(diffs))
print()
print("  %-26s %-16s %-16s" % ("参数", "用户当前", "工厂"))
print("  " + "-"*64)
for name, a, b in diffs:
    print("  %-26s %-16s %-16s" % (name, a, b))
