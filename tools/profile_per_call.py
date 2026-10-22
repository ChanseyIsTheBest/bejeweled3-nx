#!/usr/bin/env python3
"""Cost per call of the natives in console runs, independent of how the game was played (docs/optimising-functions.md):
a function's share of the main thread's profile samples, times the main thread's CPU seconds (the profile's per-window
CPU %), divided by the calls the game log counted (its "[hot] 10 s: native calls" reports).

  python tools/profile_per_call.py "LABEL|PROFILE|MAP|LOG" [...] [--addrs 82453938,822DFF80] [--top 40]

One column per run. Without --addrs: the functions with the most main-thread time in the first run. Compare only
functions called millions of times with similar work a call; a function that loops over the effects on screen costs
what the scene costs. Functions whose code did not change between the runs show how much the runs differ by themselves.
"""
import argparse
import os
import re
import subprocess
import sys
from collections import Counter

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(spec):
    label, prof, mp, log = spec.split('|')
    out = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'switch_hot_functions.py'), prof, '--map', mp,
                          '--thread', 'Main XThread', '--top', '3000'], capture_output=True, text=True, cwd=ROOT).stdout
    samples = Counter()
    total = int(re.search(r'(\d+) busy samples', out).group(1))
    for line in out.split('Main XThread', 1)[1].splitlines():
        m = re.match(r'\s+([\d.]+)%\s+(\d+)\s+(\S+)\s+(\S+)', line)
        a = m and re.search(r'(?:sub_|n_|h_)([0-9A-F]{8})', m.group(4))
        if a:
            samples[a.group(1)] += int(m.group(2))
    cpu_s = window = 0.0
    for line in open(prof, encoding='utf-8', errors='replace'):
        w = re.match(r'==== ([\d.]+) s', line)
        if w:
            window = float(w.group(1))
            continue
        c = re.search(r'"Main XThread[^"]*": CPU ([\d.]+)%', line)
        if c:
            cpu_s += min(float(c.group(1)), 100.0) / 100.0 * window
    calls = Counter()
    for line in open(log, encoding='utf-8', errors='replace'):
        if '[hot] 10 s: native calls:' in line:
            for k, n in re.findall(r'bejeweled3_cpu_native_sub_([0-9A-F]{8}) (\d+)', line):
                calls[k] += int(n)
    return label, samples, total, cpu_s, calls


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('runs', nargs='+')
    ap.add_argument('--addrs', default='')
    ap.add_argument('--top', type=int, default=40)
    a = ap.parse_args()
    cols = [run(s) for s in a.runs]
    if a.addrs:
        addrs = [x.strip().upper() for x in a.addrs.split(',') if x.strip()]
    else:
        _, s, _, _, c = cols[0]
        addrs = [k for k, _ in s.most_common() if c[k]][:a.top]
    print('main thread: ' + '; '.join(f'{l} {cpu:.0f} CPU s, {t} samples' for l, _, t, cpu, _ in cols))
    print(f'{"function":10s}' + ''.join(f'{l:>24s}' for l, *_ in cols))
    for addr in addrs:
        row = f'{addr:10s}'
        for _, s, t, cpu, calls in cols:
            if calls[addr] and s[addr]:
                row += f'{s[addr] / t * cpu / calls[addr] * 1e9:11.0f} ns ({calls[addr] / 1e6:6.1f} M)'
            else:
                row += f'{"-":>24s}'
        print(row)


if __name__ == '__main__':
    main()
