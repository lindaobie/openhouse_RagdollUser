import re, sys
d = open(sys.argv[1],'rb').read()
pat = re.compile(rb'[ -~]{5,}')
hits = []
for m in pat.finditer(d):
    s = m.group().decode('ascii','ignore')
    if re.search(r'(?i)lighthouse|htc|steam|201\d|\d+\.\d+\.\d+|lhtx|build', s):
        hits.append(s)
seen=set()
for s in hits:
    if s in seen: continue
    seen.add(s)
    print('  ', s[:100])
print('--- 共 %d 条 ---' % len(seen))
