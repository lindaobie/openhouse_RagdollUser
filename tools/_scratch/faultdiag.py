import re, sys, struct
src = open(r"J:\workspace_AI_Agent\baibaizi\research\openhouse\basestation_firmware\src\params_table.h", encoding='utf-8', errors='replace').read()
pat = re.compile(r'P\(\s*(\w+)\s*,\s*"([^"]+)"\s*,\s*(PT_\w+)\s*,\s*(\w+)\s*,\s*(\d+)\s*,\s*(0x[0-9A-Fa-f]+)')
ents = [(m.group(2), m.group(3), int(m.group(5)), int(m.group(6),16)) for m in pat.finditer(src)]
off = 0; table = {}
for name, typ, size, flags in ents:
    if not (flags & 1):
        table[name] = (off, size, typ)
        off += size
print("payload 计算大小 = %d 字节" % off)

def val(payload, name):
    o, s, t = table[name]
    raw = payload[o:o+s]
    if t == 'PT_U8': return raw[0]
    if t == 'PT_BOOL': return bool(raw[0])
    if t == 'PT_U16': return int.from_bytes(raw,'little')
    if t == 'PT_U32': return int.from_bytes(raw,'little')
    if t == 'PT_I32': return int.from_bytes(raw,'little', signed=True)
    if t == 'PT_FLOAT': return struct.unpack('<f', raw)[0]
    if t == 'PT_STRING': return raw.split(b'\x00')[0].decode('ascii','replace')
    return raw.hex()

d = open(sys.argv[1],'rb').read()
ee = d[0x1F040:0x1F040+4032]
keys = ['sys.name','laser.apc','laser.enable','laser.interlock',
        'laser.0.bias','laser.0.current','laser.0.pwr','laser.0.pwr.b','laser.0.pwr.m','laser.0.pwr.gain',
        'laser.1.bias','laser.1.current','laser.1.pwr','laser.1.pwr.b','laser.1.pwr.m','laser.1.pwr.gain',
        'rotor.0.pwm','rotor.1.pwm']
for pname, base in (("当前 0x000",0x000), ("上次 0x300",0x300), ("工厂 0x600",0x600)):
    payload = ee[base+12: base+12+551]
    print()
    print("=== 分区 %s ===" % pname)
    for k in keys:
        if k in table:
            print("  %-20s %s" % (k, val(payload, k)))
