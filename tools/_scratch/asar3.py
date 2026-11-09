import re
p = r"A:\Program Files\HanaAgent\resources\app.asar"
text = open(p,'rb').read().decode('utf-8','ignore')
# 抓所有形如 "zh": "....压缩...." 的标签
pat = re.compile(r'"zh"\s*:\s*"([^"]{0,80}压缩[^"]{0,80})"')
seen = []
for m in pat.finditer(text):
    s = m.group(1)
    if s not in seen:
        seen.append(s)
print("含「压缩」的中文标签 %d 条：" % len(seen))
for s in seen:
    print("   ", s)
