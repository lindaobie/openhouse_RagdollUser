import json
p = r"j:\app_game\steam\config\lighthouse\lighthousedb.json"
d = json.load(open(p, encoding='utf-8'))
target = int('52D9BF61', 16)
hit = None
for e in d['base_stations']:
    if e['config'].get('serialNumber') == target:
        hit = e; break
print("查找 serial 0x%08X = %d" % (target, target))
if hit:
    c = hit['config']
    print("  在 lighthousedb 里找到")
    print("  LHB-%08X  modelId=%s ootxVersion=%s" % (c['serialNumber'], c.get('modelId'), c.get('ootxVersion')))
    for st in hit['dynamic_states'][:3]:
        ds = st.get('dynamic_state', {})
        print("    fw=%s mode=%s faults=%s reset=%s last_seen=%s" %
              (ds.get('firmware_version'), ds.get('basestation_mode'), ds.get('faults'),
               ds.get('reset_count'), st.get('time_last_seen')))
else:
    print("  没有找到——这台基站可能从未和这个 SteamVR 配对过")
    print("  （数据库里最近 4 台是: 411E7A16 / 1DBFB323 / 7E1946CD / 3E6C2F4F）")
