import re
p = r"A:\Program Files\HanaAgent\resources\app.asar"
text = open(p,'rb').read().decode('utf-8','ignore')
for key in ("压缩并更新", "Compact and update", "compactAndUpdate", "compact-and-update"):
    print("=" * 16, key, "=" * 16)
    n = 0
    for m in re.finditer(re.escape(key), text):
        s = max(0, m.start()-320); e = min(len(text), m.end()+320)
        print("---", n, "---")
        print(text[s:e].replace("\n", " "))
        n += 1
        if n >= 3: break
    if n == 0: print("  (没有)")
