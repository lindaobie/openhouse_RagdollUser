import json, collections
p = r"j:\app_game\steam\config\lighthouse\lighthousedb.json"
d = json.load(open(p, encoding='utf-8'))
bs = d['base_stations']
e = bs[0]
print("config 的键:", sorted(e['config'].keys()))
print()
for k, v in e['config'].items():
    if k == 'baseCalibration':
        print("  baseCalibration: %.1f 个浮点" % len(v))
    elif k == 'modelMatrix':
        print("  modelMatrix: %.1f 个浮点" % len(v))
    else:
        print("  %s = %r" % (k, v))
print()
print("dynamic_states[0] 的键:", sorted(e['dynamic_states'][0].keys()))
for k, v in e['dynamic_states'][0].items():
    print("  %s = %r" % (k, v))
