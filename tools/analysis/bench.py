"""Summarise an AI-driven benchmark race from fm3.log (session 33; tools/benchmark.ps1 runs it).

Usage: python -I tools/analysis/bench.py LOG [SETTLE_S] [SECONDS] [OUT.json]

The window starts SETTLE_S seconds (default 10) after the race is released ("switch: FM3_FREEZE_TIME off") and lasts
SECONDS (default 120). It reads the light performance log's line a second (`perf lite:`, REGPU_PERF_LITE=1) and the
long-frame lines inside the window. Prints a summary and writes it as JSON when OUT is given.
"""
import json
import re
import statistics
import sys

LITE = re.compile(r'\[\s*([\d.]+)\] perf lite: ([\d.]+) fps, ([\d.]+) ms/frame \(p99 ([\d.]+) ms, max ([\d.]+) ms\) \| (\d+) draws/frame'
                  r' \| GPU ([\d.]+) ms/frame \| draw thread (-?\d+)% busy(?: \| (\d+) long)?')
LONG = re.compile(r'\[\s*([\d.]+)\] long frame \d+: ([\d.]+) ms')
RELEASE = re.compile(r'\[\s*([\d.]+)\] switch: FM3_FREEZE_TIME off')
SIZE = re.compile(r'gpu: ReGPU at (\d+x\d+), msaa (\d+)')


def main():
    path = sys.argv[1]
    settle = float(sys.argv[2]) if len(sys.argv) > 2 else 10.0
    seconds = float(sys.argv[3]) if len(sys.argv) > 3 else 120.0
    out = sys.argv[4] if len(sys.argv) > 4 else None
    text = open(path, errors='ignore').read()
    rel = RELEASE.search(text)
    if not rel:
        sys.exit('no race release (switch: FM3_FREEZE_TIME off) in the log')
    t0 = float(rel.group(1)) + settle
    t1 = t0 + seconds
    rows = [tuple(float(x) if x is not None else 0.0 for x in m.groups()) for m in LITE.finditer(text)]
    rows = [r for r in rows if t0 <= r[0] < t1]
    if not rows:
        sys.exit('no perf lite lines in the window (is REGPU_PERF_LITE on?)')
    longs = [float(m.group(2)) for m in LONG.finditer(text) if t0 <= float(m.group(1)) < t1]
    size = SIZE.search(text)
    fps = [r[1] for r in rows]
    result = {
        'resolution': size.group(1) if size else '?',
        'msaa': int(size.group(2)) if size else 0,
        'window_s': [round(t0, 1), round(t1, 1)],
        'seconds_measured': len(rows),
        'fps_avg': round(statistics.mean(fps), 1),
        'fps_lowest_second': round(min(fps), 1),
        'frame_ms_p99_median': round(statistics.median(r[3] for r in rows), 2),
        'frame_ms_worst': round(max(r[4] for r in rows), 2),
        'long_frames': len(longs),
        'long_frames_over_10ms': sum(1 for x in longs if x > 10),
        'draws_avg': round(statistics.mean(r[5] for r in rows)),
        'gpu_ms_avg': round(statistics.mean(r[6] for r in rows), 2),
        'draw_thread_busy_avg': round(statistics.mean(r[7] for r in rows), 1),
    }
    width = max(len(k) for k in result)
    for k, v in result.items():
        print(f'{k:<{width}}  {v}')
    if out:
        with open(out, 'w') as f:
            json.dump(result, f, indent=2)


if __name__ == '__main__':
    main()
