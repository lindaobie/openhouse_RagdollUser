import sys, re, hashlib
d = open(sys.argv[1],'rb').read()
print("大小: %d 字节" % len(d))
print("SHA256: %s" % hashlib.sha256(d).hexdigest())
s = sum(int.from_bytes(d[i:i+4],'little') for i in range(0,32,4)) & 0xFFFFFFFF
print("向量校验和: 0x%08X  %s" % (s, "OK" if s==0 else "BAD"))
m = re.search(rb'lhbs [A-Z][a-z]{2} [ 0-9]\d \d{4} \d\d:\d\d:\d\d', d)
print("lhbs 标记: %s" % (m.group().decode() if m else "没有 —— 是原厂镜像"))
m2 = re.search(rb'(?i)(20\d\d-\d\d-\d\d \d\d:\d\d:\d\d)', d)
print("构建时间串: %s" % (m2.group().decode() if m2 else "无"))
print("LHEEDUMP 快照: %s" % ("有" if d[0x1F000:0x1F008]==b'LHEEDUMP' else "没有（符合预期：写入前读的）"))
