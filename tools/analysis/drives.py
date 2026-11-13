"""Compare race drives from fm3.log files with the perf log on (session 30).

Usage: python -I tools/analysis/drives.py out/run/fm3.drive30-ringoff.log out/run/fm3.drive30-packed.log ...

For each log: the race part (perf lines with >= 2000 draws a frame), its average fps, draws, the draw thread's time
a frame ("renderer draw") and the draw worker's busy share. Then fps and draw-thread time a draw per bucket of 100
draws a frame, side by side: drives differ in camera and traffic, so compare equal draw counts, not whole drives.
The perf log is on only when the game is started from the launcher (settings.ini perf_log=1 -> REGPU_PERF);
play.cmd does not set it.
"""
import collections
import re
import sys

PERF = re.compile(r'\[\s*([\d.]+)\] perf: ([\d.]+) fps, ([\d.]+) ms/frame \| renderer draw ([\d.]+) ms.*\| (\d+) draws/frame')
BUSY = re.compile(r'\[\s*([\d.]+)\] perf: draw worker: (\d+)% busy')
MIN_DRAWS = 2000


def read(path):
    rows, busy = [], []
    with open(path, errors='ignore') as f:
        for line in f:
            m = PERF.search(line)
            if m:
                t, fps, ms, draw_ms, draws = map(float, m.groups())
                if draws >= MIN_DRAWS:
                    rows.append((t, fps, ms, draw_ms, draws))
                continue
            m = BUSY.search(line)
            if m:
                busy.append((float(m.group(1)), int(m.group(2))))
    if rows:
        busy = [b for t, b in busy if t >= rows[0][0]]
    return rows, busy


def main():
    logs = sys.argv[1:]
    if not logs:
        print(__doc__)
        return
    buckets = {}
    for path in logs:
        rows, busy = read(path)
        if not rows:
            print(f'{path}: no race lines (perf log off? started with play.cmd?)')
            continue
        n = len(rows)
        print(f'{path}: {n} lines {rows[0][0]:.0f}-{rows[-1][0]:.0f} s, fps {sum(r[1] for r in rows) / n:.1f}, '
              f'draws {sum(r[4] for r in rows) / n:.0f}, renderer draw {sum(r[3] for r in rows) / n:.2f} ms a frame, '
              f'draw worker busy {sum(busy) / max(len(busy), 1):.0f}%')
        b = collections.defaultdict(list)
        for t, fps, ms, draw_ms, draws in rows:
            b[int(draws) // 100 * 100].append((fps, draw_ms * 1000 / draws))
        buckets[path] = b
    keys = sorted(set().union(*[set(b) for b in buckets.values()])) if buckets else []
    print('draws  ' + '  '.join(f'{p[-30:]:>30}' for p in buckets))
    for k in keys:
        cells = []
        for b in buckets.values():
            v = b.get(k)
            cells.append(f'{len(v):3d}x {sum(x[0] for x in v) / len(v):6.1f} fps {sum(x[1] for x in v) / len(v):5.2f} us' if v else '')
        print(f'{k:5d}  ' + '  '.join(f'{c:>30}' for c in cells))


if __name__ == '__main__':
    main()
