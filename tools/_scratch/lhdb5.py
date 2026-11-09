import json, datetime, collections
p = r"j:\app_game\steam\config\lighthouse\lighthousedb.json"
d = json.load(open(p, encoding='utf-8'))
rows = {}
for e in d['base_stations']:
    c = e['config']; s = c['serialNumber']
    seen, fw, mode, faults, reset = 0, None, None, None, None
    for st in e['dynamic_states']:
        t = int(st.get('time_last_seen') or 0)
        ds = st.get('dynamic_state', {})
        if t >= seen:
            seen = t
            fw = ds.get('firmware_version'); mode = ds.get('basestation_mode')
            faults = ds.get('faults'); reset = ds.get('reset_count')
    rows[s] = (seen, fw, mode, faults, reset, c.get('modelId'), c.get('ootxVersion'))
top = sorted(rows.items(), key=lambda kv: -kv[1][0])[:12]
print("%-12s %-12s %-22s %-5s %-4s %s" % ("serial", "LHB-", "last seen", "fw", "mode", "faults/reset/model"))
for s, (seen, fw, mode, faults, reset, model, ootx) in top:
    when = datetime.datetime.fromtimestamp(seen).strftime('%Y-%m-%d %H:%M') if seen else '?'
    print("%-12d LHB-%08X  %-22s %-5s %-4s %s/%s/%s" % (s, s, when, fw, mode, faults, reset, model))
print()
print("总计 %d 个不同的基站记录" % len(rows))
