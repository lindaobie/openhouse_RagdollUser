import json, collections
p = r"j:\app_game\steam\config\lighthouse\lighthousedb.json"
d = json.load(open(p, encoding='utf-8'))
bs = d['base_stations']
print("base_stations 条数:", len(bs))
print("第一条的键:", sorted(bs[0].keys()) if isinstance(bs[0], dict) else type(bs[0]))
print()
for e in bs[:3]:
    print(json.dumps({k: (str(v)[:60] if not isinstance(v,(int,float,bool)) else v) for k,v in e.items()}, ensure_ascii=False)[:600])
    print("---")
# 找所有可能的序列号字段
keys = collections.Counter()
for e in bs:
    if isinstance(e, dict):
        for k in e: keys[k]+=1
print("字段出现次数:", dict(keys))
