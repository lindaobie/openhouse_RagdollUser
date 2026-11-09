#!/usr/bin/env python3
"""Reads openhouse's live telemetry and records per-device / per-basestation coverage.

    python coverage.py [seconds] [out.csv]

Run it while SteamVR + openhouse are running, then walk the tracked device
around the room: the point is where each base stops seeing it, so a run with
the device sitting still only tells you about one spot.

The clock starts when a tracked device appears, not when the script does.

How to read the numbers (include/lh/session.hpp, src/runtime/device_tracker.cpp)
--------------------------------------------------------------------------------
`coverage[].frames` is the number of frames that base lit in the last 0.5 s, and
a base with no frames in that window is dropped from the list entirely -- its
absence is the finding, not a zero. Sweeps per second are therefore frames / 0.5.

A multi-station array splits 120 sweeps/s between its stations, so three bases
give 40/s each and four give 30/s each. A base well under its share is either
occluded from wherever the device is, or not sweeping at all.

Output columns: t, device, base, frames_window, sensors, bases_seen
"""
import csv
import json
import socket
import sys
from collections import defaultdict

HOST, PORT = '127.0.0.1', 36840
WINDOW = 0.5  # seconds of light the driver counts in `frames`


def connect():
    while True:
        try:
            return socket.create_connection((HOST, PORT), timeout=5)
        except OSError:
            print('waiting for the driver on %s:%d ...' % (HOST, PORT))
            import time
            time.sleep(2)


def main():
    duration = float(sys.argv[1]) if len(sys.argv) > 1 else 900.0
    path = sys.argv[2] if len(sys.argv) > 2 else 'coverage.csv'

    sock = connect()
    sock.settimeout(5)
    stream = sock.makefile('r', encoding='utf-8', newline='\n')

    rows = []
    devices = {}       # serial -> role
    t0 = None
    last_print = -1e9

    print('recording %.0f s to %s (Ctrl-C to stop)' % (duration, path))
    print('waiting for a tracked device ...')

    try:
        while True:
            line = stream.readline()
            if not line:
                print('driver went away, reconnecting...')
                sock.close()
                sock = connect()
                sock.settimeout(5)
                stream = sock.makefile('r', encoding='utf-8', newline='\n')
                continue

            try:
                msg = json.loads(line)
            except ValueError:
                continue
            if msg.get('type') != 'state':
                continue

            live = [d for d in msg.get('devices', []) if d.get('connected')]

            # A run that starts before anything is tracked burns its window on empty
            # samples, so hold the clock until a device is actually being seen.
            if t0 is None:
                if not any(d.get('basesSeen', 0) >= 1 for d in live):
                    continue
                t0 = msg['t']
                print('  got %s, starting the clock' %
                      ', '.join(d.get('serial', '?') for d in live))

            now = msg['t']
            elapsed = now - t0

            for dev in live:
                serial = dev['serial']
                devices[serial] = dev.get('role', '?')
                for cov in dev.get('coverage', []):
                    rows.append((round(elapsed, 2), serial, cov['base'],
                                 cov.get('frames', 0), cov.get('sensors', 0),
                                 dev.get('basesSeen', 0)))

            if elapsed - last_print >= 30:
                last_print = elapsed
                print('  %5.0f s  %6d samples  %d devices' % (elapsed, len(rows), len(devices)))
                for dev in live:
                    seen = ' '.join('%s=%.0f/s' % (c['base'][-8:], c['frames'] / WINDOW)
                                    for c in dev.get('coverage', []))
                    print('    %-10s %-8s basesSeen=%d  %s' %
                          (dev['serial'], dev.get('role', '?'), dev.get('basesSeen', 0), seen))

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
        writer.writerow(['t', 'device', 'base', 'frames_window', 'sensors', 'bases_seen'])
        writer.writerows(rows)

    print('wrote %d rows to %s' % (len(rows), path))
    summarise(rows, devices)


def summarise(rows, devices):
    if not rows:
        print('\nno coverage recorded: nothing was tracked long enough.')
        return

    span = max(r[0] for r in rows)
    # Each sample carries a 0.5 s window, so the mean of the windows is a rate.
    per = defaultdict(list)
    times = defaultdict(set)
    sample_times = defaultdict(set)
    for t, dev, base, frames, _sensors, _seen in rows:
        per[(dev, base)].append(frames)
        times[(dev, base)].add(t)
        sample_times[dev].add(t)

    print('\nmean sweeps/s over %.0f s  (a base absent from a sample lit nothing in it)' % span)
    for dev in sorted({d for (d, _b) in per}):
        cells = [(b, sum(v) / len(v) / WINDOW) for (d, b), v in per.items() if d == dev]
        total = sum(c[1] for c in cells) or 1.0
        n = len(sample_times[dev]) or 1
        print('  %s (%s), %d samples' % (dev, devices.get(dev, '?'), n))
        for base, rate in sorted(cells, key=lambda c: -c[1]):
            print('    %-14s %6.1f/s  %5.1f%% of the light   lit in %3.0f%% of samples'
                  % (base, rate, 100 * rate / total, 100.0 * len(times[(dev, base)]) / n))


if __name__ == '__main__':
    main()
