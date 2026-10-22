#!/usr/bin/env python3
"""Write-backs of only the registers that changed: one exact transformation of the localized copies
(docs/hand-natives.md, 2026-10-22). tools/localize_native.py applies it to every copy it makes (rewrite_text below);
run on its own it makes app/src/cpu/hot/h_<ADDR>.h from a copy.

A localized copy (app/generated/hot/n_<ADDR>.h) keeps the function's registers in C++ locals l_X and wrote all of them
back to the context at every return and before every fully synced call (BEJEWELED3_SYNC_OUT), changed or not; the
compiler has to keep those stores (the context is memory the callee reads, and guest stores may alias it as far as it
knows). The console profile of the 10:21 NRO put the copies' samples on those stores and on the matching entry loads;
on the 10:41 NRO the 17 copies written this way took 16-42 % less time a call.

Every BEJEWELED3_SYNC_OUT() becomes stores of the registers that may differ from the context at that point, from a
may-differ dataflow over the copy's statements:
  - at entry no register differs (every l_X was loaded from ctx.X);
  - a statement that assigns l_X (or its loop local ll_X: a reference to it when loop locals are off) makes X differ;
  - a fully synced call (BEJEWELED3_SYNC_OUT; call; BEJEWELED3_SYNC_IN) leaves none differing (all reloaded);
  - a smart-synced call leaves the registers it writes back or reloads equal; the others keep their state (the copy
    itself relies on the generator's reach analysis: the callee neither reads nor writes them);
  - a conditional trap block keeps the state (the union of its two branches);
  - control flow: labels, gotos, conditional gotos and the return blocks, as the copy has them (anything else: the
    copy is left as it is).
A register that cannot differ holds in ctx exactly the value the full write-back would store, so leaving its store out
leaves the context as the copy leaves it: every other statement of the copy is unchanged. The entry loads of registers
that are then never stored nor read become dead and the compiler drops them.

  python tools/hand_native_dirty_sync.py ADDR [ADDR ...]
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HOT = os.path.join(ROOT, 'app', 'generated', 'hot')
OUT = os.path.join(ROOT, 'app', 'src', 'cpu', 'hot')

ASSIGN = r'(?:=(?!=)|\+=|-=|\*=|/=|%=|\|=|&=|\^=|<<=|>>=|\+\+|--)'


class Unsupported(ValueError):
    pass


def rewrite_text(addr, text):
    """The copy's text with its write-backs limited; returns (text, write-backs, register stores kept, of them)."""
    m = re.search(r'#define BEJEWELED3_SYNC_OUT\(\) do \{ (.*?) \} while \(0\)', text)
    if not m:
        raise Unsupported(f'{addr}: no BEJEWELED3_SYNC_OUT definition')
    regs = re.findall(r'ctx\.(\w+) = l_\1;', m.group(1))
    if not regs:
        raise Unsupported(f'{addr}: no registers in BEJEWELED3_SYNC_OUT')
    regset = set(regs)
    lines = text.split('\n')
    out = list(lines)
    funcs = [i for i, l in enumerate(lines)
             if re.match(r'inline void (Native|NativeLogged)\(PPCContext& ctx, uint8_t\* base\) \{$', l)]
    if not funcs:
        raise Unsupported(f'{addr}: no Native body')
    changed = kept = 0
    for start in funcs:
        end = next(j for j in range(start + 1, len(lines)) if lines[j] == '}')
        body = list(range(start + 1, end))
        labels = {}
        for i in body:
            lm = re.match(r'^(loc_\w+):\s*$', lines[i])
            if lm:
                labels[lm.group(1)] = i

        def writes(line):
            w = set()
            for mm in re.finditer(r'\bl{1,2}_(\w+?)\b(?:\.\w+)?\s*' + ASSIGN, line):
                w.add(mm.group(1))
            for mm in re.finditer(r'(?:\+\+|--)l{1,2}_(\w+)\b', line):
                w.add(mm.group(1))
            for mm in re.finditer(r'REX_F(?:ABS|NEG|NABS)\(\s*l{1,2}_(\w+)\s*,', line):
                w.add(mm.group(1))
            for mm in re.finditer(r'REX_LOOP_(?:OUT|IN)\(\s*ll_(\w+)\s*,\s*l_(\w+)\s*\)', line):
                w.add(mm.group(1))
                w.add(mm.group(2))
            # Every bare use of a register local (not followed by a field) must be one of the forms above.
            for mm in re.finditer(r'\b(l{1,2}_\w+)\b(?!\.)', line):
                n = re.escape(mm.group(1))
                if not re.search(r'REX_(?:FABS|FNEG|FNABS|LOOP_OUT|LOOP_IN|LOOP_LOCAL)\([^;]*\b' + n + r'\b|\b' + n +
                                 r'\s*' + ASSIGN + r'|=\s*' + n + r'\s*;|ctx\.\w+ = ' + n + ';', line):
                    raise Unsupported(f'{addr}: unrecognized use of {mm.group(1)}: {line.strip()[:160]}')
            return {r for r in w if r in regset}

        def kind(line):
            s = line.strip()
            if 'g_smart_sync.active' in s:
                return 'smart'
            if s.startswith('PPCRegister l_') and re.fullmatch(r'PPCRegister l_(\w+) = ctx\.\1;', s):
                return 'decl'
            if 'BEJEWELED3_SYNC_OUT();' in s and 'return;' in s:
                return 'cond_return' if s.startswith('if') else 'return'
            if 'BEJEWELED3_SYNC_OUT();' in s and 'BEJEWELED3_SYNC_IN();' in s:
                return 'cond_sync' if s.startswith('if') else 'sync'
            if s == 'BEJEWELED3_SYNC_OUT();':
                return 'tail'
            if re.fullmatch(r'goto (loc_\w+);', s):
                return 'goto'
            if re.fullmatch(r'if \(.*\) goto (loc_\w+);', s):
                return 'cond_goto'
            if re.search(r'\bgoto\b|\breturn\b|BEJEWELED3_SYNC|\bswitch\b|\bcase\b|\bwhile\b|\bfor\b|\bbreak\b|\bcontinue\b', s):
                raise Unsupported(f'{addr}: unhandled control flow: {s[:160]}')
            return 'plain'

        kinds = {i: kind(lines[i]) for i in body}

        def succ(i):
            k = kinds[i]
            nxt = [i + 1] if i + 1 < end else []
            if k in ('return', 'tail'):
                return []
            target = re.search(r'goto (loc_\w+);', lines[i])
            if k in ('goto', 'cond_goto') and target.group(1) not in labels:
                raise Unsupported(f'{addr}: goto to an unknown label {target.group(1)}')
            if k == 'goto':
                return [labels[target.group(1)]]
            if k == 'cond_goto':
                return nxt + [labels[target.group(1)]]
            return nxt

        def transfer(i, d):
            k = kinds[i]
            line = lines[i]
            if k == 'smart':
                synced = set(re.findall(r'ctx\.(\w+) = l_\1;', line)) | set(re.findall(r'l_(\w+) = ctx\.\1;', line))
                return d - synced
            if k == 'sync':
                return frozenset()
            if k in ('decl', 'cond_sync', 'cond_return', 'goto', 'cond_goto', 'return', 'tail'):
                return d
            return d | writes(line)

        state = {body[0]: frozenset()}
        work = [body[0]]
        while work:
            i = work.pop()
            d_out = frozenset(transfer(i, state[i]))
            for j in succ(i):
                if j >= end:
                    continue
                old = state.get(j)
                new = d_out if old is None else old | d_out
                if new != old:
                    state[j] = new
                    work.append(j)
        for i in body:
            if 'BEJEWELED3_SYNC_OUT()' not in lines[i] or i not in state or kinds[i] == 'decl':
                continue
            d = state[i]
            kept += sum(1 for r in regs if r in d)
            stores = ' '.join(f'ctx.{r} = l_{r};' for r in regs if r in d)
            out[i] = lines[i].replace('BEJEWELED3_SYNC_OUT()',
                                      f'do {{ {stores} }} while (0)' if stores else 'do {} while (0)')
            changed += 1
    return '\n'.join(out), changed, kept, len(regs) * changed


def transform(addr):
    text = open(os.path.join(HOT, f'n_{addr}.h'), encoding='utf-8').read()
    try:
        body_text, changed, kept, full = rewrite_text(addr, text)
    except Unsupported as e:
        sys.exit(f'hand_native_dirty_sync: {e}')
    body_text = body_text.replace(f'namespace bejeweled3::hot::n_{addr}', f'namespace bejeweled3::hot::h_{addr}')
    rest = body_text[body_text.index('#pragma once') + len('#pragma once'):]
    header = (f'// h_{addr}.h: hand-written native of sub_{addr}: its localized copy (app/generated/hot/n_{addr}.h) with '
              f'every write-back\n// limited to the registers that may differ from the context there ({changed} '
              f'write-backs).\n// GENERATED by tools/hand_native_dirty_sync.py from the copy (re-run it after the copy '
              f'changes); the audit is the tool\'s docstring.\n#pragma once\n')
    open(os.path.join(OUT, f'h_{addr}.h'), 'w', encoding='utf-8', newline='\n').write(header + rest)
    print(f'h_{addr}.h: {changed} write-backs: {kept} register stores instead of {full}')


if __name__ == '__main__':
    for a in sys.argv[1:]:
        transform(a.upper())
