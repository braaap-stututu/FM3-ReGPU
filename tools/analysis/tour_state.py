"""Which FM3 screen a 2560x1440 debug-server shot shows, from a few pixel probes (session 28, tools/tour.ps1).

  python -I tools/analysis/tour_state.py shot.png [--probes]

Prints one word: mainmenu, assists, envselect (also manufacturer and car select), trackselect, menu (a dark side
menu with its first item highlighted: pre-race Start race, pause Resume, post-race Continue), or other. --probes
prints the probe colours too. Probe points were read off session 28's hand-made shots (out/perf/s28tour);
coordinates scale with the shot's width.
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pngstat import read_png  # noqa: E402

# name: (x, y) at 2560x1440
PROBES = {
    'assists_frame': (256, 185),      # the assists dialog's dark frame
    'select_box': (1280, 341),        # the yellow cursor box's top edge (environment / track select)
    'select_box2': (1062, 500),       # its left edge
    'x_icon': (614, 1344),            # the X button in track select's footer (LOAD CAR SETUP)
    'header': (1600, 60),             # the light grey header bar of the select screens
    'menu_red': (400, 430),           # first item of a side menu, highlighted red
    'menu_red2': (560, 430),
    'main_red': (1200, 770),          # main menu: its first item (Forza Motorsport 3) highlighted red
}


def colour(img, name):
    w, h, bpp, rows = img
    x, y = PROBES[name]
    x, y = x * w // 2560, y * h // 1440
    r = rows[y]
    return tuple(r[x * bpp:x * bpp + 3])


def near(c, ref, tol=40):
    return all(abs(a - b) <= tol for a, b in zip(c, ref))


def state(img):
    c = {n: colour(img, n) for n in PROBES}
    grey_header = near(c['header'], (208, 208, 208), 25)
    yellow = lambda p: p[0] > 180 and 120 < p[1] < 210 and p[2] < 80  # noqa: E731
    red = lambda p: p[0] > 190 and p[1] < 80 and p[2] < 60  # noqa: E731
    if near(c['assists_frame'], (64, 64, 64), 20) and min(c['header']) > 230:
        return 'assists', c
    if red(c['main_red']) and min(c['menu_red']) > 230 and min(c['header']) > 230:
        return 'mainmenu', c
    if grey_header and (yellow(c['select_box']) or yellow(c['select_box2'])):
        return ('trackselect' if max(c['x_icon']) < 200 else 'envselect'), c
    if red(c['menu_red']) and red(c['menu_red2']):
        return 'menu', c
    return 'other', c


if __name__ == '__main__':
    for p in [a for a in sys.argv[1:] if not a.startswith('--')]:
        s, c = state(read_png(p))
        print(s if '--probes' not in sys.argv else '%-12s %s %s' % (s, os.path.basename(p), c))
