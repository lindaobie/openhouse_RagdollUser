import sys, re
p = r"A:\Program Files\HanaAgent\resources\app.asar"
data = open(p,'rb').read()
text = data.decode('utf-8', 'ignore')
for key in ("压缩并更新会话", "压缩会话"):
    print("=" * 20, key, "=" * 20)
    n = 0
    for m in re.finditer(re.escape(key), text):
        s = max(0, m.start()-350); e = min(len(text), m.end()+350)
        seg = text[s:e].replace("\n", " ")
        print("---", n, "---")
        print(seg)
        n += 1
        if n >= 4: break
    if n == 0: print("  (没有)")
