import re, sys
PAT = re.compile(rb'[ -~]{6,}')
def text(p):
    d = open(p,'rb').read()
    out = set()
    for m in PAT.finditer(d):
        s = m.group().decode('ascii', 'ignore')
        # keep only things that look like real strings: lower case, and a dot/underscore/space
        if not re.search(r'[a-z]', s): continue
        if not re.search(r'[ ._>:/]', s): continue
        if len(re.findall(r'[A-Za-z]', s)) < 4: continue
        out.add(s.strip())
    return d, out
a, sa = text(sys.argv[1]); b, sb = text(sys.argv[2])
print("release %d bytes, %d real strings" % (len(a), len(sa)))
print("local   %d bytes, %d real strings" % (len(b), len(sb)))
oa, ob = sorted(sa-sb), sorted(sb-sa)
print("\n--- 只在 release ---")
for s in oa: print("   -", s[:90])
print("\n--- 只在 local ---")
for s in ob: print("   +", s[:90])
print("\n共有 %d" % len(sa & sb))
