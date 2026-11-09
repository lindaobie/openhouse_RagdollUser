import json
p = r"j:\app_game\steam\config\lighthouse\lighthousedb.json"
d = json.load(open(p, encoding='utf-8'))
t = int("1DA59AC0", 16)
hit = next((e for e in d['base_stations'] if e['config'].get('serialNumber') == t), None)
print("LHB-1DA59AC0 = 0x%08X = %d" % (t, t))
if hit:
    c = hit['config']
    print("  在 lighthousedb 里找到，modelId=%s ootxVersion=%s" % (c.get('modelId'), c.get('ootxVersion')))
    for st in hit['dynamic_states'][:2]:
        ds = st.get('dynamic_state', {})
        print("    fw=%s mode=%s faults=%s reset=%s last_seen=%s" %
              (ds.get('firmware_version'), ds.get('basestation_mode'), ds.get('faults'),
               ds.get('reset_count'), st.get('time_last_seen')))
else:
    print("  不在库里（这台没在这台电脑上配对过）")
