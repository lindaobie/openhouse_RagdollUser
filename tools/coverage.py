#!/usr/bin/env python3
"""Reads openhouse's live telemetry and records per-device / per-basestation coverage.

    python coverage.py [seconds] [out.csv]

Run it while SteamVR + openhouse are running and you are actually playing.
Ctrl-C stops early; the CSV is still written.

Output columns: t, device, base, frames_delta, sensors, bases_seen
`frames_delta` is the number of new frames in that sample period (50 ms at 20 Hz),
so summing a window and dividing by its length gives frames per second.

Protocol: include/lh/telemetry.hpp in the openhouse repo. Newline-delimited JSON,
about 20 state messages a second on 127.0.0.1:36840.
"""
import csv
import json
import socket
import sys
import time

HOST, PORT = '127.0.0.1', 36840


def connect():
    while True:
        try:
            return socket.create_connection((HOST, PORT), timeout=5)
        except OSError:
            print('waiting for the driver on %s:%d ...' % (HOST, PORT))
            time.sleep(2)


def main():
    duration = float(sys.argv[1]) if len(sys.argv) > 1 else 900.0
    path = sys.argv[2] if len(sys.argv) > 2 else 'coverage.csv'

    sock = connect()
    sock.settimeout(5)
    stream = sock.makefile('r', encoding='utf-8', newline='\n')

    prev = {}        # (device, base) -> cumulative frames
    devices = {}     # serial -> role
    rows = []
    t0 = None
    last_print = 0.0

    print('recording %.0f s to %s (Ctrl-C to stop)' % (duration, path))

    try:
        while True:
            line = stream.readline()
            if not line:
                print('driver went away, reconnecting...')
                sock.close()
                sock = connect()
                sock.settimeout(5)
                stream = sock.makefile('r', encoding='utf-8', newline='\n')
                prev.clear()
                continue

            try:
                msg = json.loads(line)
            except ValueError:
                continue
            if msg.get('type') != 'state':
                continue

            now = msg['t']
            if t0 is None:
                t0 = now
            elapsed = now - t0

            for dev in msg.get('devices', []):
                if not dev.get('connected'):
                    continue
                serial = dev['serial']
                devices[serial] = dev.get('role', '?')
                for cov in dev.get('coverage', []):
                    key = (serial, cov['base'])
                    old = prev.get(key)
                    prev[key] = cov['frames']
                    if old is not None and elapsed > 2:
                        rows.append((round(elapsed, 2), serial, cov['base'],
                                     cov['frames'] - old, cov.get('sensors', 0),
                                     dev.get('basesSeen', 0)))

            if elapsed - last_print >= 30:
                last_print = elapsed
                print('  %5.0f s  %6d samples  %d devices' % (elapsed, len(rows), len(devices)))
                # a live per-device line so you can see it working
                for dev in msg.get('devices', []):
                    if not dev.get('connected'):
                        continue
                    print('    %-10s %-14s basesSeen=%d' %
                          (dev['serial'], dev.get('role', '?'), dev.get('basesSeen', 0)))

            if elapsed >= duration:
                break
    except KeyboardInterrupt:
        pass
    finally:
        try:
            sock.close()
        except OSError:
            pass

    with open(path, 'w', newline='', encoding='utf-8') as fh:
        writer = csv.writer(fh)
        writer.writerow(['t', 'device', 'base', 'frames_delta', 'sensors', 'bases_seen'])
        writer.writerows(rows)

    print('wrote %d rows to %s' % (len(rows), path))

    # quick summary: frames per second per (device, base)
    total = {}
    for t, dev, base, delta, _sensors, _seen in rows:
        total[(dev, base)] = total.get((dev, base), 0) + delta
    span = max((r[0] for r in rows), default=1.0)
    print('\nframes/s over the whole run (%.0f s):' % span)
    bases = sorted({b for (_d, b) in total})
    print('%-12s %s' % ('device', ' '.join('%10s' % b for b in bases)))
    for dev in sorted({d for (d, _b) in total}):
        cells = ['%10.1f' % (total.get((dev, b), 0) / span) for b in bases]
        print('%-12s %s' % (dev, ' '.join(cells)))


if __name__ == '__main__':
    main()
