#!/usr/bin/env python3
"""bejeweled3-nx: an input script (bejeweled3_input_script) that plays a board at random: after the given menu prefix,
cursor steps and swaps (A, then a direction 8 frames later) until the last frame. Random swaps are legal often enough
to keep a game going, and the A of each swap also dismisses a dialog.

  python tools/random_play_script.py --prefix "960:START,1560:A,2160:A" --start 2800 --end 12000 [--seed 1]
"""
import argparse
import random

DIRS = ['UP', 'DOWN', 'LEFT', 'RIGHT']


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--prefix', default='')
    ap.add_argument('--start', type=int, required=True)
    ap.add_argument('--end', type=int, required=True)
    ap.add_argument('--seed', type=int, default=1)
    a = ap.parse_args()
    rng = random.Random(a.seed)
    events = [e for e in a.prefix.split(',') if e]
    f = a.start
    while f < a.end:
        if rng.random() < 0.45:
            events.append(f'{f}:A')
            events.append(f'{f + 8}:{rng.choice(DIRS)}')
            f += rng.randint(70, 110)
        else:
            events.append(f'{f}:{rng.choice(DIRS)}')
            f += rng.randint(20, 40)
    print(','.join(events))


if __name__ == '__main__':
    main()
