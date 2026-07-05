#!/usr/bin/env python3
"""Gate O1: ocupación central en fase ofensiva, sobre logs .rcg (ULG6).

Métricas (posesión nuestra aprox.: nuestro jugador más cercano al balón <1.2m
y más cerca que el rival; fase ofensiva: balón en x>15):
  - media de compañeros en ZONA CENTRAL de llegada (x>25, |y|<12), sin contar
    al portador
  - distribución (0/1/2/3+)
  - toques profundos (x>36): centrales (|y|<14) vs banda
Autodetecta el lado de RoboTech. Uso: central_support.py '<glob>' [...]
"""
import sys, re, glob, os
from collections import Counter
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rcg_side import side_info

pm_re   = re.compile(r'^\(playmode \d+ ([a-z_]+)')
ball_re = re.compile(r'\(\(b\) ([-\d.]+) ([-\d.]+)')

tot = Counter(); dist = Counter(); n_sum = 0
for arg in sys.argv[1:]:
    for path in sorted(glob.glob(arg)):
        side, sgn, us_re, opp_re, _ = side_info(path)
        play_on = False
        for line in open(path, errors='ignore'):
            if line.startswith('(playmode'):
                m = pm_re.match(line)
                if m: play_on = (m.group(1) == 'play_on')
                continue
            if not (play_on and line.startswith('(show')):
                continue
            bm = ball_re.search(line)
            if not bm: continue
            bx, by = float(bm.group(1)) * sgn, float(bm.group(2)) * sgn
            if bx < 15: continue
            us  = {int(u): (float(x) * sgn, float(y) * sgn)
                   for u, x, y in us_re.findall(line)}
            opp = [(float(x) * sgn, float(y) * sgn) for _, x, y in opp_re.findall(line)]
            if not us or not opp: continue
            du_min, holder = 99.0, None
            for u, (x, y) in us.items():
                d = ((x - bx)**2 + (y - by)**2)**0.5
                if d < du_min: du_min, holder = d, u
            do = min(((x - bx)**2 + (y - by)**2)**0.5 for x, y in opp)
            if not (du_min < 1.2 and du_min < do):
                continue   # no es posesión nuestra clara
            tot['cyc'] += 1
            n = sum(1 for u, (x, y) in us.items()
                    if u != holder and u >= 2 and x > 25 and abs(y) < 12)
            n_sum += n
            dist[min(n, 3)] += 1
            if bx > 36:
                tot['deep'] += 1
                if abs(by) < 14: tot['deep_central'] += 1

if tot['cyc']:
    print(f"ciclos de posesión ofensiva (x>15): {tot['cyc']}")
    print(f"compañeros centrales (x>25,|y|<12) MEDIA: {n_sum/tot['cyc']:.2f}")
    print("distribución:", {k: f"{100*v/tot['cyc']:.0f}%" for k, v in sorted(dist.items())})
if tot['deep']:
    print(f"toques/posesión profunda (x>36): {tot['deep']}  centrales: "
          f"{tot['deep_central']} ({100*tot['deep_central']/tot['deep']:.0f}%)")
