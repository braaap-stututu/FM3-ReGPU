"""Compare two sets of benchmark runs (A/B), plainly and at equal draw counts (session 33).

Usage: python -I tools/analysis/bench_compare.py --a A1.series.csv A2.series.csv ... --b B1.series.csv ... [--bucket 100] [--min 3]
       (a run may also be given as its fm3.log or .json: the .series.csv next to it is used, made from the log if missing)

The AI races differently every run, so two runs are never at the same places with the same traffic: the draw count a
frame differed by 12% between same-settings 4K runs, and fps follows it. So besides the plain comparison (mean fps of
each side's runs) this compares the seconds of both sides that had the same load: every second (a `perf lite` line)
goes into a bucket by its draws a frame (--bucket wide); in each bucket with at least --min seconds on both sides the
mean frame time (1000 / fps) of A and B are compared; the buckets are weighted by min(seconds A, seconds B). GPU time a
frame is compared the same way. Prints both, the per-bucket table and how much of each side the buckets cover.
"""
import argparse
import csv
import os
import statistics
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def series_path(path):
    if path.endswith('.series.csv'):
        return path
    base = path[:-5] if path.endswith('.json') else path[:-4] if path.endswith('.log') else path
    csv_path = base + '.series.csv'
    if not os.path.exists(csv_path) and path.endswith('.log'):
        subprocess.run([sys.executable, '-I', os.path.join(HERE, 'bench.py'), path, '10', '120', base + '.json'],
                       check=True, stdout=subprocess.DEVNULL)
    return csv_path


def load(paths):
    runs = []
    for p in paths:
        with open(series_path(p)) as f:
            rows = [{k: float(v) for k, v in r.items()} for r in csv.DictReader(f)]
        runs.append((os.path.basename(p), rows))
    return runs


def pct(a, b):
    return (b - a) / a * 100.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--a', nargs='+', required=True)
    ap.add_argument('--b', nargs='+', required=True)
    ap.add_argument('--bucket', type=int, default=100)
    ap.add_argument('--min', type=int, default=3)
    args = ap.parse_args()
    A, B = load(args.a), load(args.b)

    print('Plain (each run\'s mean fps):')
    for name, runs in (('A', A), ('B', B)):
        means = [statistics.mean(r['fps'] for r in rows) for _, rows in runs]
        draws = [statistics.mean(r['draws_per_frame'] for r in rows) for _, rows in runs]
        sd = statistics.stdev(means) if len(means) > 1 else 0.0
        print(f'  {name}: {statistics.mean(means):7.1f} fps (sd {sd:.1f}) over {len(means)} runs: '
              + ', '.join(f'{m:.1f}' for m in means) + f'; draws/frame {statistics.mean(draws):.0f}')
    a_plain = statistics.mean(statistics.mean(r['fps'] for r in rows) for _, rows in A)
    b_plain = statistics.mean(statistics.mean(r['fps'] for r in rows) for _, rows in B)
    print(f'  B vs A: {pct(a_plain, b_plain):+.2f}% fps')

    def buckets(runs):
        out = {}
        for _, rows in runs:
            for r in rows:
                k = int(r['draws_per_frame'] // args.bucket)
                out.setdefault(k, []).append(r)
        return out

    ba, bb = buckets(A), buckets(B)
    keys = sorted(k for k in set(ba) & set(bb) if len(ba[k]) >= args.min and len(bb[k]) >= args.min)
    if not keys:
        print('\nAt equal draws: no bucket has enough seconds on both sides (try a wider --bucket or lower --min).')
        return
    print(f'\nAt equal draws ({args.bucket}-draw buckets, >= {args.min} s on both sides):')
    print('  draws/frame   s A   s B   ms A    ms B    fps diff   GPU ms A  GPU ms B')
    wsum = wms = wgpu = 0.0
    for k in keys:
        ra, rb = ba[k], bb[k]
        ms_a = statistics.mean(1000.0 / r['fps'] for r in ra)
        ms_b = statistics.mean(1000.0 / r['fps'] for r in rb)
        g_a = statistics.mean(r['gpu_ms'] for r in ra)
        g_b = statistics.mean(r['gpu_ms'] for r in rb)
        w = min(len(ra), len(rb))
        wsum += w
        wms += w * (ms_a - ms_b) / ms_b * 100.0  # + = B faster (A's frame longer)
        wgpu += w * (g_b - g_a) / g_a * 100.0
        print(f'  {k * args.bucket:5d}-{(k + 1) * args.bucket - 1:<5d} {len(ra):5d} {len(rb):5d} {ms_a:6.3f}  {ms_b:6.3f}  {(ms_a - ms_b) / ms_b * 100:+7.2f}%   {g_a:7.3f}   {g_b:7.3f}')
    cover_a = sum(len(ba[k]) for k in keys) / sum(len(v) for v in ba.values()) * 100
    cover_b = sum(len(bb[k]) for k in keys) / sum(len(v) for v in bb.values()) * 100
    print(f'  B vs A at equal draws: {wms / wsum:+.2f}% fps, GPU time a frame {wgpu / wsum:+.2f}%'
          f'  (buckets cover {cover_a:.0f}% of A\'s seconds, {cover_b:.0f}% of B\'s)')


if __name__ == '__main__':
    main()
