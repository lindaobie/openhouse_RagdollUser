import json, socket, sys, time
s = socket.create_connection(('127.0.0.1', 36840), timeout=5)
f = s.makefile('r', encoding='utf-8', newline='\n')
last = None
t0 = time.time()
while time.time() - t0 < 6:
    line = f.readline()
    if not line: break
    try: m = json.loads(line)
    except ValueError: continue
    if m.get('type') == 'state': last = m
if not last:
    print("没收到 state 消息"); sys.exit()
print("===== 基站 =====")
for b in last.get('bases', []):
    print("  %-14s 频道 %-3s 状态 %-12s seen=%s placed=%s locked=%s status=%s faults=%s" % (
        b.get('serial'), b.get('channel'), ('learned' if b.get('learned') else ('known' if b.get('placed') else '?')),
        b.get('seen'), b.get('placed'), b.get('locked'), b.get('status'), b.get('faults')))
print()
print("===== 房间 =====")
r = last.get('room', {})
print("  id=%s settled=%s learned=%s seen=%s epoch=%s" % (r.get('id'), r.get('settled'), r.get('learned'), r.get('seen'), r.get('epoch')))
print()
print("===== 设备与覆盖 =====")
for d in last.get('devices', []):
    if not d.get('connected'): continue
    print("  %s  role=%s  state=%s  basesSeen=%s  primary=%s" % (
        d.get('serial'), d.get('role'), d.get('state'), d.get('basesSeen'), d.get('primary')))
    for c in d.get('coverage', []):
        print("      %-14s frames=%-8s sensors=%s" % (c.get('base'), c.get('frames'), c.get('sensors')))
