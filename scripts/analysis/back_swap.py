#!/usr/bin/env python3
"""Mide el desorden de la linea defensiva (unums 2-5) en logs .rcg (ULG6).

Orden esperado de izquierda a derecha (y creciente): 4, 2, 3, 5
  (4 = SideBack izq, 2 = CB izq, 3 = CB der, 5 = SideBack der)

Metricas por partido (solo play_on):
  - %ciclos con inversion de orden adyacente (>3 m) en la linea de backs
  - %ciclos con el 4 en banda CONTRARIA (y > +10) y el 5 en contraria (y < -10)
  - episodios sostenidos (>=50 ciclos seguidos) de banda cruzada
Autodetecta el lado de RoboTech (sirve con collect_logs alternando lados).

Uso: back_swap.py '<glob1>' ['<glob2>' ...]
"""
import sys, re, glob, os
from collections import defaultdict
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rcg_side import side_info

show_re = re.compile(r'^\(show (\d+)')
pm_re   = re.compile(r'^\(playmode \d+ ([a-z_]+)')
ball_re = re.compile(r'\(\(b\) ([-\d.]+) ([-\d.]+)')

def analyze(path):
    side, sgn, us_re, _opp_re, _ = side_info(path)
    play_on = False
    stats = dict(cycles=0, inv=0, w4=0, w5=0, inv_def=0, cyc_def=0,
                 episodes4=0, episodes5=0)
    run4 = run5 = 0
    with open(path, errors='ignore') as f:
        for line in f:
            if line.startswith('(playmode'):
                m = pm_re.match(line)
                if m: play_on = (m.group(1) == 'play_on')
                continue
            if not (play_on and line.startswith('(show')):
                continue
            bm = ball_re.search(line)
            if not bm: continue
            bx = float(bm.group(1)) * sgn
            us = {int(u): (float(x) * sgn, float(y) * sgn)
                  for u, x, y in us_re.findall(line)}
            if not all(u in us for u in (2, 3, 4, 5)):
                continue
            y = {u: us[u][1] for u in (2, 3, 4, 5)}
            stats['cycles'] += 1
            defensive = bx < -10.0
            if defensive: stats['cyc_def'] += 1
            # inversiones de orden adyacente esperado 4 < 2 < 3 < 5 (margen 3 m)
            inv = (y[4] > y[2] + 3) or (y[2] > y[3] + 3) or (y[3] > y[5] + 3)
            if inv:
                stats['inv'] += 1
                if defensive: stats['inv_def'] += 1
            wrong4 = y[4] > 10.0
            wrong5 = y[5] < -10.0
            if wrong4: stats['w4'] += 1
            if wrong5: stats['w5'] += 1
            run4 = run4 + 1 if wrong4 else 0
            run5 = run5 + 1 if wrong5 else 0
            if run4 == 50: stats['episodes4'] += 1
            if run5 == 50: stats['episodes5'] += 1
    return side, stats

def pct(a, b): return 100.0 * a / b if b else 0.0

totals = defaultdict(float); nfiles = 0
paths = []
for arg in sys.argv[1:]:
    paths += sorted(glob.glob(arg))
for p in paths:
    side, s = analyze(p)
    if s['cycles'] < 1000: continue
    nfiles += 1
    print(f"[{side}] {os.path.basename(os.path.dirname(p))}/{os.path.basename(p)}: "
          f"inv={pct(s['inv'],s['cycles']):.1f}%  "
          f"inv_def={pct(s['inv_def'],s['cyc_def']):.1f}%  "
          f"4cruzado={pct(s['w4'],s['cycles']):.1f}%  5cruzado={pct(s['w5'],s['cycles']):.1f}%  "
          f"episodios(4/5)={s['episodes4']}/{s['episodes5']}")
    for k in ('cycles','inv','w4','w5','inv_def','cyc_def','episodes4','episodes5'):
        totals[k] += s[k]
print(f"\n== AGREGADO {nfiles} partidos ==")
print(f"inversion de orden linea backs : {pct(totals['inv'],totals['cycles']):.1f}% de ciclos play_on")
print(f"   en fase defensiva          : {pct(totals['inv_def'],totals['cyc_def']):.1f}%")
print(f"P4 en banda contraria          : {pct(totals['w4'],totals['cycles']):.1f}%")
print(f"P5 en banda contraria          : {pct(totals['w5'],totals['cycles']):.1f}%")
print(f"episodios sostenidos           : P4={totals['episodes4']:.0f}  P5={totals['episodes5']:.0f}")
