import re, sys
def strs(p):
    d = open(p,'rb').read()
    return d, set(m.group().decode() for m in re.finditer(rb'[ -~]{5,}', d))
a, sa = strs(sys.argv[1])
b, sb = strs(sys.argv[2])
print("release %d bytes, %d strings" % (len(a), len(sa)))
print("local   %d bytes, %d strings" % (len(b), len(sb)))
only_a = sorted(sa - sb); only_b = sorted(sb - sa)
print("\n只在 release 里 (%d):" % len(only_a))
for s in only_a[:40]: print("   -", s)
print("\n只在 local 里 (%d):" % len(only_b))
for s in only_b[:40]: print("   +", s)
com = sorted(sa & sb)
print("\n共有 %d 条" % len(com))
print("前 8 个向量的十六进制:")
for name, d in (("release", a), ("local", b)):
    print("  %-8s SP=%08x  Reset=%08x" % (name, int.from_bytes(d[0:4],'little'), int.from_bytes(d[4:8],'little')))
