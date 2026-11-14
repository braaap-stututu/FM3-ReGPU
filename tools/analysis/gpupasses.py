"""GPU time per render pass from REGPU_GPU_PASSES=1 logs, averaged, and two runs side by side (session 33).

Usage: python -I tools/analysis/gpupasses.py A.log [B.log] [--from SECONDS] [--top N]

ReGPU logs a block every 2 s ("gpu passes: X ms/frame over N frames", then the top passes: "gpu passes: ms  draws
label"). This averages each pass label over the blocks after --from (log seconds; default: the last two thirds of the
log) and prints them, or with two logs the passes of both (matched by label) with the difference: e.g. 4x vs 2x MSAA
on the frozen grid, to see which passes grow with the sample count.
"""
import argparse
import re
import statistics
from collections import defaultdict

HEAD = re.compile(r'\[\s*([\d.]+)\] gpu passes: ([\d.]+) ms/frame over (\d+) frames')
ROW = re.compile(r'\[\s*([\d.]+)\] gpu passes:\s+([\d.]+) ms\s+(\d+) draws\s+(.*)$')


def load(path, start):
    blocks, cur = [], None
    for line in open(path, errors='ignore'):
        m = HEAD.search(line)
        if m:
            cur = {'t': float(m.group(1)), 'total': float(m.group(2)), 'rows': {}}
            blocks.append(cur)
            continue
        m = ROW.search(line)
        if m and cur is not None:
            cur['rows'][m.group(4).strip()] = (float(m.group(2)), float(m.group(3)))
    if not blocks:
        raise SystemExit(f'{path}: no "gpu passes" blocks (REGPU_GPU_PASSES=1?)')
    if start is None:
        start = blocks[0]['t'] + (blocks[-1]['t'] - blocks[0]['t']) / 3
    blocks = [b for b in blocks if b['t'] >= start]
    per = defaultdict(list)
    for b in blocks:
        for label, (ms, draws) in b['rows'].items():
            per[label].append((ms, draws))
    n = len(blocks)
    # A pass missing from a block (out of the top list) counts as 0 there.
    avg = {k: (sum(x[0] for x in v) / n, statistics.mean(x[1] for x in v)) for k, v in per.items()}
    return statistics.mean(b['total'] for b in blocks), avg, n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('logs', nargs='+')
    ap.add_argument('--from', dest='start', type=float, default=None)
    ap.add_argument('--top', type=int, default=20)
    a = ap.parse_args()
    runs = [load(p, a.start) for p in a.logs[:2]]
    if len(runs) == 1:
        total, avg, n = runs[0]
        print(f'GPU {total:.3f} ms/frame ({n} blocks)')
        for k, (ms, d) in sorted(avg.items(), key=lambda kv: -kv[1][0])[:a.top]:
            print(f'  {ms:7.3f} ms  {d:6.0f} draws  {k}')
        return
    (ta, A, na), (tb, B, nb) = runs
    print(f'GPU A {ta:.3f} ms/frame ({na} blocks), B {tb:.3f} ({nb} blocks), B - A {tb - ta:+.3f} ms ({(tb - ta) / ta * 100:+.1f}%)')
    print(f'  {"A ms":>7} {"B ms":>7} {"B-A":>7}  {"draws":>6}  pass')
    keys = sorted(set(A) | set(B), key=lambda k: -max(A.get(k, (0, 0))[0], B.get(k, (0, 0))[0]))[:a.top]
    for k in keys:
        ma, da = A.get(k, (0.0, 0))
        mb, db = B.get(k, (0.0, 0))
        print(f'  {ma:7.3f} {mb:7.3f} {mb - ma:+7.3f}  {max(da, db):6.0f}  {k}')


if __name__ == '__main__':
    main()
