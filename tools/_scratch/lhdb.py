import json, sys, re
p = r"j:\app_game\steam\config\lighthouse\lighthousedb.json"
d = json.load(open(p, encoding='utf-8'))
print("顶层键:", list(d.keys())[:12])
found = {}
def walk(o, path=""):
    if isinstance(o, dict):
        for k, v in o.items():
            if isinstance(k, str) and re.fullmatch(r'(LHB-[0-9A-Fa-f]{8}|[0-9A-Fa-f]{8})', k):
                found.setdefault(k, path or "/")
            walk(v, path + "/" + str(k))
    elif isinstance(o, list):
        for i, v in enumerate(o):
            walk(v, path + "/%d" % i)
walk(d)
print("\n找到的序列号 (%d 个):" % len(found))
for k in sorted(found):
    print("   ", k, "  <-", found[k])
# 也看看结构提示
for key in ("tracked_stations","tracking_stations","base_stations","tracking"):
    if key in d:
        v = d[key]
        print("\n%s: %s" % (key, type(v).__name__), (len(v) if hasattr(v,'__len__') else ''))
