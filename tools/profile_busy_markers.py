#!/usr/bin/env python3
"""Two console runs compared without call counts (docs/optimising-functions.md, "Measuring"): over each run's busy
windows (main thread at least --busy % of the window), the main thread's CPU milliseconds a game frame, and the same
for marker functions whose code did not change between the runs. A marker's time a frame is the work the scene asked
of it, so the markers say how much heavier one run was than the other; the total says what the frame cost.

  python tools/profile_busy_markers.py "LABEL|PROFILE|MAP" [...] [--markers 82453938,822DFF80,8255EF98] [--busy 80]

Use markers whose code is the same in every build compared (a hand-written native nobody touched, say), and that run
in the scene measured (the effect functions for Lightning).
"""
import argparse
import os
import re
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(spec, markers, busy_min):
    label, prof, mp = spec.split('|')
    lines = open(prof, encoding='utf-8', errors='replace').read().split('\n')
    head, windows, cur = lines[:3], [], None
    for line in lines[3:]:
        if line.startswith('===='):
            cur = [line]
            windows.append(cur)
        elif cur is not None:
            cur.append(line)
    busy, frames, cpu_ms = [], 0.0, 0.0
    for w in windows:
        m = re.match(r'==== ([\d.]+) s \| mode: \w+ \| game ([\d.]+) fps', w[0])
        c = next((re.search(r'"Main XThread[^"]*": CPU ([\d.]+)%', l) for l in w if 'Main XThread' in l), None)
        if not m or not c:
            continue
        secs, fps, cpu = float(m.group(1)), float(m.group(2)), float(c.group(1))
        if cpu >= busy_min and fps < 1000:
            busy.append(w)
            frames += fps * secs
            cpu_ms += min(cpu, 100.0) / 100.0 * secs * 1000.0
    if not busy:
        return f'{label}: no window with the main thread at {busy_min:.0f} % or more'
    with tempfile.NamedTemporaryFile('w', suffix='.log', delete=False, encoding='utf-8') as tmp:
        tmp.write('\n'.join(head + [l for w in busy for l in w]))
    try:
        out = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'switch_hot_functions.py'), tmp.name, '--map',
                              mp, '--thread', 'Main XThread', '--top', '3000'], capture_output=True, text=True,
                             cwd=ROOT).stdout
    finally:
        os.unlink(tmp.name)
    total = int(re.search(r'(\d+) busy samples', out).group(1))
    per = {}
    for line in out.split('Main XThread', 1)[1].splitlines():
        mm = re.match(r'\s+([\d.]+)%\s+(\d+)\s+(\S+)\s+(\S+)', line)
        a = mm and re.search(r'(?:sub_|n_|h_)([0-9A-F]{8})', mm.group(4))
        if a:
            per[a.group(1)] = per.get(a.group(1), 0) + int(mm.group(2))
    ms_frame = cpu_ms / frames
    marks = ', '.join(f'{a} {per.get(a, 0) / total * ms_frame:.2f}' for a in markers)
    return (f'{label}: {len(busy)} busy windows, {frames:.0f} frames, main thread {ms_frame:.2f} ms a frame; '
            f'markers (ms a frame): {marks}')


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('runs', nargs='+')
    ap.add_argument('--markers', default='82453938,822DFF80,8255EF98')
    ap.add_argument('--busy', type=float, default=80.0)
    a = ap.parse_args()
    markers = [m.strip().upper() for m in a.markers.split(',') if m.strip()]
    for spec in a.runs:
        print(run(spec, markers, a.busy))


if __name__ == '__main__':
    main()
