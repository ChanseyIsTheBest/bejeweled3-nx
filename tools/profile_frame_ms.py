#!/usr/bin/env python3
"""Per 10 s window of a console profile: game frames a second, the main thread's CPU % and its CPU milliseconds a game
frame, and the CPU clock (docs/optimising-functions.md). Runs only compare at the same clock and a similar scene.

  python tools/profile_frame_ms.py PROFILE [PROFILE ...]
"""
import re
import sys

for path in sys.argv[1:]:
    print(path)
    fps = clock = None
    for line in open(path, encoding='utf-8', errors='replace'):
        m = re.match(r'==== ([\d.]+) s \| mode: (\w+) \| game ([\d.]+) fps', line)
        if m:
            fps = float(m.group(3))
            continue
        m = re.search(r'clocks: CPU ([\d.]+) MHz', line)
        if m:
            clock = float(m.group(1))
            continue
        m = re.search(r'"Main XThread[^"]*": CPU ([\d.]+)%', line)
        if m and fps:
            cpu = float(m.group(1))
            ms = cpu * 10.0 / fps if fps < 1000 else 0.0
            print(f'  {fps:6.1f} fps  main CPU {cpu:5.1f}%  {ms:5.2f} ms a frame  CPU {clock} MHz')
