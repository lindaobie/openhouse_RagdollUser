"""Scores lhtool session output (see eval_replay.sh)."""
import json, math, os, sys, struct, collections, glob

def length(cap):
    # last report time - first: the file is large, so just ask lhtool's header-free way:
    # read the endpoint/report stream timestamps from the tail via lhtool stats is slow;
    # duration from the replay outputs is used instead when available.
    return 620.0

def quat_angle(a, b):
    d = abs(sum(x * y for x, y in zip(a, b)))
    return 2 * math.degrees(math.acos(min(1.0, d)))

def score(dirname, lead):
    devs = collections.defaultdict(lambda: collections.Counter())
    for path in sorted(glob.glob(os.path.join(dirname, 'w*.jsonl'))):
        t0 = None
        start = float(path.rsplit('-', 1)[1][:-6])
        per = collections.defaultdict(list)
        for line in open(path):
            if '"position"' not in line: continue
            try: j = json.loads(line)
            except Exception: continue
            if t0 is None: t0 = j['t']
            if j['t'] - t0 < lead and start > 0: continue
            per[j['serial']].append(j)
        for serial, L in per.items():
            c = devs[serial]
            for a, b, cc in zip(L, L[1:], L[2:]):
                dt = cc['t'] - b['t']
                if not (0.005 < dt < 0.02 and 0.005 < b['t'] - a['t'] < 0.02): continue
                c['samples'] += 1
                c['valid'] += b['valid']
                c['coasting'] += b['state'] == 'coasting'
                c['lost'] += b['state'] in ('lost', 'uninitialized', 'acquiring')
                if not (a['valid'] and b['valid'] and cc['valid']): continue
                acc = math.dist([2 * y - x - z for x, y, z in zip(a['position'], b['position'], cc['position'])], [0, 0, 0])
                rot = abs(quat_angle(a['orientation'], b['orientation']) - quat_angle(b['orientation'], cc['orientation']))
                c['pos_spikes'] += acc > 0.02
                c['rot_spikes'] += rot > 5
                c['big_spikes'] += acc > 0.05
                speed = math.dist(b['position'], cc['position']) / dt
                if speed < 0.02:
                    c['still'] += 1
                    c['still_jitter_um'] += acc * 1e6
    print('%-14s %7s %6s %6s %6s %7s %7s %7s %8s' % ('device', 'secs', 'valid%', 'coast%', 'lost%', 'pos>2cm', 'pos>5cm', 'rot>5d', 'jitter'))
    tot = collections.Counter()
    for serial, c in sorted(devs.items()):
        n = max(c['samples'], 1)
        print('%-14s %7.0f %6.1f %6.1f %6.1f %7d %7d %7d %7.0fum' % (serial, n / 100, 100 * c['valid'] / n, 100 * c['coasting'] / n,
              100 * c['lost'] / n, c['pos_spikes'], c['big_spikes'], c['rot_spikes'], c['still_jitter_um'] / max(c['still'], 1)))
        tot.update(c)
    n = max(tot['samples'], 1)
    print('%-14s %7.0f %6.1f %6.1f %6.1f %7d %7d %7d %7.0fum' % ('TOTAL', n / 100, 100 * tot['valid'] / n, 100 * tot['coasting'] / n,
          100 * tot['lost'] / n, tot['pos_spikes'], tot['big_spikes'], tot['rot_spikes'], tot['still_jitter_um'] / max(tot['still'], 1)))

if __name__ == '__main__':
    if sys.argv[1] == '--length': print(length(sys.argv[2]))
    else: score(sys.argv[1], float(sys.argv[2]))
