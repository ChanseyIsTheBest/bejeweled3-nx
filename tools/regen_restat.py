#!/usr/bin/env python3
"""After a regeneration (the build's codegen target, then tools/post_codegen.py): makes the build trees see the
generated sources as current without recompiling the ones whose content did not change.

Why (2026-10-22): the codegen command in generated/rexglue.cmake has every generated .cpp as an output, and ninja runs
it again whenever one output is older than an input (the manifest, overrides.toml, perf_overrides.toml, default.xex,
the generator executable). tools/post_codegen.py keeps the old time of a file whose content it did not change (so
that file does not compile again), which leaves such outputs older than the inputs: every build ran the generator and
the passes once more, and the passes rewrote bejeweled3_pch.h while the build compiled it ("has been modified since the
precompiled header was built").

What it does, for every generated .cpp (generated/default/sources.cmake) older than the newest input:
  - each object compiled from it in out/pc and out/nx that was up to date with it (not older than the file nor than
    any generated header) moves to now + 2 s, so the build still sees it as current;
  - the file moves to now.
Headers are left alone (clang records the precompiled header's time and refuses a changed one). Then the stamp moves to
now + 2 s. A file the passes changed has a new time already: its objects are older and compile again, as they should.

  python -P tools/regen_restat.py [--builds out/pc out/nx]
"""
import argparse
import glob
import os
import re
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
GEN = os.path.join(ROOT, 'app', 'generated', 'default')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--builds', nargs='*', default=['out/pc', 'out/nx'])
    a = ap.parse_args()
    sources = re.findall(r'CMAKE_CURRENT_LIST_DIR\}/([\w.]+\.cpp)', open(os.path.join(GEN, 'sources.cmake')).read())
    inputs = [os.path.join(ROOT, 'app', f) for f in ('bejeweled3_manifest.toml', 'overrides.toml', 'perf_overrides.toml')]
    inputs += glob.glob(os.path.join(ROOT, 'sdk', 'out', '*', 'rexglue*.exe'))
    newest_input = max(os.path.getmtime(p) for p in inputs if os.path.exists(p))
    # An object also has to be newer than every generated header (a .cpp the passes left alone can include a header
    # they changed: inline copies, the hot hooks); the precompiled header's own rebuild is newer than the objects anyway.
    headers = [p for d in (GEN, os.path.join(ROOT, 'app', 'generated', 'hot'))
               for p in glob.glob(os.path.join(d, '*.h')) + glob.glob(os.path.join(d, '*.inc'))]
    newest_header = max((os.path.getmtime(p) for p in headers), default=0.0)
    now = time.time()
    moved = objs = 0
    for name in sources:
        path = os.path.join(GEN, name)
        if not os.path.exists(path):
            continue
        prev = os.path.getmtime(path)
        if prev >= newest_input:
            continue
        for build in a.builds:
            for obj in glob.glob(os.path.join(ROOT, build, 'CMakeFiles', 'bejeweled3_recomp.dir', 'generated', 'default',
                                              name + '.o*')):
                if os.path.getmtime(obj) >= max(prev, newest_header):
                    os.utime(obj, (now + 2, now + 2))
                    objs += 1
        os.utime(path, (now, now))
        moved += 1
    stamp = os.path.join(GEN, 'codegen.build.stamp')
    if os.path.exists(stamp):
        os.utime(stamp, (now + 2, now + 2))
    print(f'regen_restat: {moved} of {len(sources)} generated sources moved to now, {objs} up-to-date objects with them')


if __name__ == '__main__':
    main()
