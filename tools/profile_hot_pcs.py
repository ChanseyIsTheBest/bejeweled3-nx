#!/usr/bin/env python3
"""Where a function's console profile samples fall, instruction by instruction (docs/optimising-functions.md): sums the
raw "pc image+0x... xN" lines of the given profiles inside one function of the build's map and prints the sampled
offsets, hottest first, with their AArch64 disassembly (devkitA64's objdump).

  python tools/profile_hot_pcs.py MAP ELF SECTION PROFILE [PROFILE ...] [--top 20]

SECTION is a substring of the function's .text section name in the map: n_8245AAD86Native (a localized copy),
h_82453938 (a hand-written native), __imp__sub_825652B0 (generated code). The map and ELF must be the build that wrote
the profile (out/nx/elf keeps one pair per delivered NRO).
"""
import argparse
import re
import subprocess
from collections import Counter

OBJDUMP = r'C:\devkitPro\devkitA64\bin\aarch64-none-elf-objdump.exe'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('map')
    ap.add_argument('elf')
    ap.add_argument('section')
    ap.add_argument('profiles', nargs='+')
    ap.add_argument('--top', type=int, default=20)
    a = ap.parse_args()
    lines = open(a.map, encoding='utf-8', errors='replace').read().splitlines()
    start = size = None
    for i, line in enumerate(lines):
        if line.strip().startswith('.text') and a.section in line:
            m = re.match(r'\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)', lines[i + 1])
            if m:
                start, size = int(m.group(1), 16), int(m.group(2), 16)
                break
    if start is None:
        raise SystemExit(f'{a.section}: no such .text section in {a.map}')
    pcs = Counter()
    for p in a.profiles:
        for line in open(p, encoding='utf-8', errors='replace'):
            m = re.search(r'pc image\+0x([0-9a-f]+) x(\d+)', line)
            if m:
                pcs[int(m.group(1), 16)] += int(m.group(2))
    hits = {pc: n for pc, n in pcs.items() if start <= pc < start + size}
    print(f'{a.section}: {start:#x}, {size:#x} bytes, {sum(hits.values())} samples')
    dis = subprocess.run([OBJDUMP, '-d', '--no-show-raw-insn', f'--start-address={start:#x}',
                          f'--stop-address={start + size:#x}', a.elf], capture_output=True, text=True).stdout
    insn = {}
    for line in dis.splitlines():
        m = re.match(r'\s*([0-9a-f]+):\s+(.*)', line)
        if m:
            insn[int(m.group(1), 16)] = re.sub(r'<[^>]*>', '', m.group(2)).strip()
    for pc, n in sorted(hits.items(), key=lambda kv: -kv[1])[:a.top]:
        print(f'  +{pc - start:#06x} x{n:4d}  {insn.get(pc, "?")}')


if __name__ == '__main__':
    main()
