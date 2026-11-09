import re
p = r"A:\Program Files\HanaAgent\resources\app.asar"
text = open(p,'rb').read().decode('utf-8','ignore')
for key in ("重新压缩", "freshCompact", "fresh-compact"):
    print("=" * 18, key, "=" * 18)
    n = 0
    for m in re.finditer(re.escape(key), text):
        s = max(0, m.start()-260); e = min(len(text), m.end()+260)
        print("---", n, "---")
        print(text[s:e].replace("\n"," "))
        n += 1
        if n >= 3: break
    if n == 0: print("  (没有)")
