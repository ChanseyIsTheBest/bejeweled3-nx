#!/usr/bin/env python3
"""Makes app/src/cpu/hot/h_824D8FF8.h (the hand-written native of PopCap's textured, alpha-blended span rasterizer,
docs/hand-natives.md) from its localized copy app/generated/hot/n_824D8FF8.h by three exact transformations:

  1. The stack slots of its frame (r1 + 80 .. r1 + 175) are C++ locals: every load and store of one becomes the local,
     the slots are read from memory once after the two indirect calls at the start (no slot is touched before them)
     and every one is written back at the function's single exit (loc_824D9BF8), so the memory after the call is the
     copy's (a slot this call never wrote gets its own old value back). The pixel loops spill about a dozen values a
     blended pixel to those slots. Slot 388 (in the caller's frame, stored before the calls) stays a real store and
     is read once after the calls.
  2. The loop barriers (compiler fences) go: the loops are bounded and wait on no other thread.
  3. rex::ppc::divwu becomes FastDivwu: the same quotient (0 for a zero divisor) by a multiplication with a 64-bit
     reciprocal for divisors up to 256 (Lemire et al. 2019: floor(n / d) == (M * n) >> 64 for every 32-bit n with
     M = floor((2^64 - 1) / d) + 1, d >= 2), the hardware divide otherwise. The blend divides three times a pixel by
     256 - ((256 - alpha) * (256 - dest alpha) >> 8), 9..256.

  python tools/hand_native_824D8FF8.py   (re-run after the copy changes; the build does not run it)
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'app', 'generated', 'hot', 'n_824D8FF8.h')
DST = os.path.join(ROOT, 'app', 'src', 'cpu', 'hot', 'h_824D8FF8.h')
KEEP = {388}  # stays guest memory (see above)

lines = open(SRC, encoding='utf-8').read().splitlines()
body_start = next(i for i, l in enumerate(lines) if l.startswith('namespace bejeweled3::hot::n_824D8FF8'))
body_end = next(i for i, l in enumerate(lines) if l.startswith('}  // namespace bejeweled3::hot::n_824D8FF8'))
body = lines[body_start:body_end + 1]

slots = {}  # offset -> 32 or 64
for l in body:
    for size, off in re.findall(r'REX_(?:LOAD|STORE)_D_U(32|64)\(l_r1\.u32, (\d+)', l):
        off = int(off)
        if off in KEEP:
            continue
        if slots.setdefault(off, int(size)) != int(size):
            sys.exit(f'slot {off} is accessed with two sizes')
ranges = sorted((o, o + s // 8) for o, s in slots.items())
for (a0, a1), (b0, b1) in zip(ranges, ranges[1:]):
    if b0 < a1:
        sys.exit(f'slots {a0} and {b0} overlap')

out = []
calls_seen = 0
inserted_init = False
for l in body:
    s = l.strip()
    if s in ('BEJEWELED3_LOOP_BARRIER();', 'REX_LOOP_BARRIER();'):
        continue
    l = l.replace('namespace bejeweled3::hot::n_824D8FF8', 'namespace bejeweled3::hot::h_824D8FF8')
    l = l.replace('rex::ppc::divwu(', 'FastDivwu(')
    if s == 'loc_824D9BF8:':
        out.append(l)
        for off in sorted(slots):
            out.append(f'\tREX_STORE_D_U{slots[off]}(l_r1.u32, {off}, s_{off});')
        continue
    m = re.fullmatch(r'(\s*)REX_STORE_D_U(32|64)\(l_r1\.u32, (\d+), (.*)\);', l)
    if m and int(m.group(3)) not in KEEP:
        t = 'uint32_t' if m.group(2) == '32' else 'uint64_t'
        l = f'{m.group(1)}s_{m.group(3)} = {t}({m.group(4)});'
    else:
        def load(mm):
            off = int(mm.group(2))
            if off in KEEP:
                return f'uint64_t(s_{off})'
            return f'uint64_t(s_{off})'
        l = re.sub(r'REX_LOAD_D_U(32|64)\(l_r1\.u32, (\d+)\)', load, l)
    if 'REX_LOAD_D_U' in l and 'l_r1.u32' in l:
        sys.exit(f'unhandled stack access: {l}')
    if 'REX_STORE_D_U' in l and 'l_r1.u32' in l and not any(f'l_r1.u32, {k},' in l for k in KEEP) \
            and 'chain' not in l.lower() and 'REX_STORE_D_U32(l_r1.u32, 388' not in l and not s.startswith('REX_STORE_D_U32(l_r1.u32, ') \
            and not re.search(r's_\d+\)?;$', l):
        sys.exit(f'unhandled stack store: {l}')
    out.append(l)
    if 'REX_CALL_INDIRECT_FUNC' in l:
        calls_seen += 1
        if calls_seen == 2:
            decl = []
            for off in sorted(slots):
                t = 'uint32_t' if slots[off] == 32 else 'uint64_t'
                decl.append(f'\t{t} s_{off} = REX_LOAD_D_U{slots[off]}(l_r1.u32, {off});')
            for off in sorted(KEEP):
                decl.append(f'\tconst uint32_t s_{off} = REX_LOAD_D_U32(l_r1.u32, {off});')
            out.extend(decl)
            inserted_init = True
if not inserted_init or calls_seen != 2:
    sys.exit(f'expected two indirect calls before the slots, saw {calls_seen}')
# No slot may be touched before the two calls.
first_call = next(i for i, l in enumerate(out) if 'REX_CALL_INDIRECT_FUNC' in l)
for l in out[:first_call]:
    if re.search(r'\bs_\d+\b', l):
        sys.exit(f'slot used before the calls: {l}')

header = f'''// h_824D8FF8.h: hand-written native of sub_824D8FF8, one of PopCap's software triangle rasterizers (a textured
// triangle, the texel modulated by a colour and alpha-blended into a 32-bit image with its own alpha: Lightning's
// 4/5-match effect, 24 % of the console's main thread while it plays). Reference: app/generated/hot/n_824D8FF8.h.
// GENERATED by tools/hand_native_824D8FF8.py from the copy (re-run it after the copy changes): the copy's statements
// with its {len(slots)} stack slots as locals, without its loop barriers, and with FastDivwu for divwu. The audit of each
// transformation is the tool's docstring; the rest of the code is the copy's own.
#pragma once

// n_824D8FF8.h (the copy) is included just before this file by app/generated/hot/localized_hooks.inc.

namespace bejeweled3::hot::h_824D8FF8_detail {{
// floor((2^64 - 1) / d) + 1 for d = 2..256: (M * n) >> 64 is floor(n / d) for every 32-bit n.
struct Reciprocals {{
  uint64_t m[257];
  constexpr Reciprocals() : m{{}} {{
    for (uint32_t d = 2; d <= 256; ++d) m[d] = ~uint64_t(0) / d + 1;
  }}
}};
inline constexpr Reciprocals kReciprocals{{}};
}}  // namespace bejeweled3::hot::h_824D8FF8_detail
'''
helper = '''
// rex::ppc::divwu's quotient (0 for a zero divisor).
[[gnu::always_inline]] inline uint32_t FastDivwu(uint32_t n, uint32_t d) {
  if (d >= 2 && d <= 256) [[likely]] {
    return uint32_t((static_cast<unsigned __int128>(h_824D8FF8_detail::kReciprocals.m[d]) * n) >> 64);
  }
  return rex::ppc::divwu(n, d);
}
'''
# The helper goes right after the namespace line.
ns = next(i for i, l in enumerate(out) if l.startswith('namespace bejeweled3::hot::h_824D8FF8'))
out[ns + 1:ns + 1] = helper.splitlines()
open(DST, 'w', encoding='utf-8', newline='\n').write(header + '\n'.join(out) + '\n')
print(f'{DST}: {len(slots)} slots ({", ".join(f"{o}/{s}" for o, s in sorted(slots.items()))}), '
      f'{sum("FastDivwu(" in l for l in out) - 1} divisions')
