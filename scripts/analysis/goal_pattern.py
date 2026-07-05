#!/usr/bin/env python3
"""Patron de goles ENCAJADOS + %carreras perdidas, sobre logs .rcg (ULG6).

Por cada gol del rival: rebobina la trayectoria del balon y reporta: carril de
entrada a nuestro tercio (y en x=-25), ciclos desde que el balon cruzo x=-20
hasta el gol, y distancia media del back mas cercano al balon durante esa
penetracion. Ademas mide %ciclos defensivos con carrera perdida (rival mas
cerca del balon que nuestro defensa mas cercano, margen 1m).
Autodetecta el lado de RoboTech; coordenadas normalizadas a "atacamos +x".

Uso: goal_pattern.py '<glob1>' ['<glob2>' ...]
"""
import sys, re, glob, os
from collections import deque, Counter
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rcg_side import side_info

show_re = re.compile(r'^\(show (\d+)')
pm_re   = re.compile(r'^\(playmode \d+ ([a-z_]+)')
ball_re = re.compile(r'\(\(b\) ([-\d.]+) ([-\d.]+)')

tot_def = tot_lost = 0
goals = []
for arg in sys.argv[1:]:
    for path in sorted(glob.glob(arg)):
        side, sgn, us_re, opp_re, our_goal_pm = side_info(path)
        their_goal_pm = 'goal_r' if our_goal_pm == 'goal_l' else 'goal_l'
        hist = deque(maxlen=200)   # (t, bx, by, min_back_dist)
        with open(path, errors='ignore') as f:
            for line in f:
                if line.startswith('(playmode'):
                    m = pm_re.match(line)
                    if m and m.group(1) == their_goal_pm and hist:
                        t_goal = hist[-1][0]
                        run = list(hist)
                        idx = None
                        for i in range(len(run)-1, -1, -1):
                            if run[i][1] > -20:
                                idx = i
                                break
                        pen = run[idx+1:] if idx is not None else run
                        if not pen: pen = run[-10:]
                        lane_y = None
                        for h in pen:
                            if h[1] <= -25: lane_y = h[2]; break
                        if lane_y is None: lane_y = pen[-1][2]
                        lane = 'IZQ' if lane_y < -12 else ('DER' if lane_y > 12 else 'CENTRO')
                        dists = [h[3] for h in pen if h[3] is not None]
                        goals.append(dict(
                            f=os.path.basename(path), t=t_goal, lane=lane,
                            ciclos=len(pen), half=1 if t_goal <= 3000 else 2,
                            back_dist=sum(dists)/len(dists) if dists else -1))
                    continue
                if not line.startswith('(show'):
                    continue
                sm = show_re.match(line); bm = ball_re.search(line)
                if not (sm and bm): continue
                t = int(sm.group(1))
                bx, by = float(bm.group(1)) * sgn, float(bm.group(2)) * sgn
                backs = [(float(x) * sgn, float(y) * sgn)
                         for u, x, y in us_re.findall(line) if 2 <= int(u) <= 5]
                mind = min(((x-bx)**2 + (y-by)**2)**0.5 for x, y in backs) if backs else None
                hist.append((t, bx, by, mind))
                if bx < -8 and backs:
                    opps = [(float(x) * sgn, float(y) * sgn)
                            for _, x, y in opp_re.findall(line)]
                    if opps:
                        tot_def += 1
                        do = min(((x-bx)**2 + (y-by)**2)**0.5 for x, y in opps)
                        if do < mind - 1.0: tot_lost += 1

print(f"GOLES ENCAJADOS: {len(goals)}")
print("  por carril:", dict(Counter(g['lane'] for g in goals)))
print("  por mitad :", dict(Counter(g['half'] for g in goals)))
fast = [g for g in goals if g['ciclos'] <= 45]
print(f"  penetraciones rapidas (<=45c desde x=-20): {len(fast)}/{len(goals)}")
if goals:
    print(f"  dist media back mas cercano durante la jugada: "
          f"{sum(g['back_dist'] for g in goals)/len(goals):.1f} m")
for g in goals:
    print(f"    {g['f']} c{g['t']} {g['lane']:6s} {g['ciclos']:3d}c mitad{g['half']} backdist={g['back_dist']:.1f}")
if tot_def:
    print(f"\nCARRERAS PERDIDAS: {tot_lost}/{tot_def} = {100.0*tot_lost/tot_def:.0f}% de ciclos defensivos")
