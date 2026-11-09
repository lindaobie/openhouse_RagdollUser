import json, re, collections
p = r"j:\app_game\steam\config\lighthouse\lighthousedb.json"
d = json.load(open(p, encoding='utf-8'))
bs = d['base_stations']
cfg = bs[0]['config']
print("config 前 900 字符:")
print(cfg[:900])
print("\n===== 扫 serial =====")
s = re.findall(r"'serial':\s*(\d+)", cfg)
print("第一条的 serial:", s)
