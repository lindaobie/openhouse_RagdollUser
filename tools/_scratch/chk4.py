import sys, re, hashlib
d = open(sys.argv[1],'rb').read()
print("大小: %d" % len(d))
print("SHA256: %s" % hashlib.sha256(d).hexdigest())
s = sum(int.from_bytes(d[i:i+4],'little') for i in range(0,32,4)) & 0xFFFFFFFF
print("向量校验和: 0x%08X %s" % (s, "OK" if s==0 else "BAD"))
m = re.search(rb'lhbs [A-Z][a-z]{2} [ 0-9]\d \d{4}', d)
print("lhbs 标记:", m.group().decode() if m else "无（原厂镜像）")
m2 = re.search(rb'20\d\d-\d\d-\d\d \d\d:\d\d:\d\d', d)
print("原厂构建时间:", m2.group().decode() if m2 else "无")
