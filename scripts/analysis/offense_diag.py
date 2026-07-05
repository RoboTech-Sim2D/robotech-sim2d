#!/usr/bin/env python3
"""Diagnostico ofensivo sobre logs .rcg (ULG6). Autodetecta el lado de RoboTech;
coordenadas normalizadas a "atacamos +x".

Por partido: goles nuestros; remates (balon rapido hacia la porteria rival
proyectado a cruzar la linea); %ciclos en tercio/area rival; y donde PERDEMOS
el balon en campo rival. Posesion aproximada: ultimo equipo con jugador a
<1.2m del balon.

Uso: offense_diag.py '<glob1>' ['<glob2>' ...]
"""
import sys, re, glob, os
from collections import Counter
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from rcg_side import side_info

show_re = re.compile(r'^\(show (\d+)')
pm_re   = re.compile(r'^\(playmode \d+ ([a-z_]+)')
ball_re = re.compile(r'\(\(b\) ([-\d.]+) ([-\d.]+) ([-\d.]+) ([-\d.]+)')

GOAL_X, GOAL_HALF = 52.5, 7.01

tot = Counter(); loss_zone = Counter(); shots_detail = []
npart = 0
for arg in sys.argv[1:]:
    for path in sorted(glob.glob(arg)):
        npart += 1
        side, sgn, us_re, opp_re, our_goal_pm = side_info(path)
        play_on = False
        poss = None
        last_shot_t = -100
        with open(path, errors='ignore') as f:
            for line in f:
                if line.startswith('(playmode'):
                    m = pm_re.match(line)
                    if m:
                        pm = m.group(1)
                        play_on = (pm == 'play_on')
                        if pm == our_goal_pm: tot['goles'] += 1
                    continue
                if not (play_on and line.startswith('(show')):
                    continue
                sm = show_re.match(line); bm = ball_re.search(line)
                if not (sm and bm): continue
                t = int(sm.group(1))
                bx, by = float(bm.group(1)) * sgn, float(bm.group(2)) * sgn
                vx, vy = float(bm.group(3)) * sgn, float(bm.group(4)) * sgn
                us  = [(float(x) * sgn, float(y) * sgn) for _, x, y in us_re.findall(line)]
                opp = [(float(x) * sgn, float(y) * sgn) for _, x, y in opp_re.findall(line)]
                if not us or not opp: continue
                du = min(((x-bx)**2+(y-by)**2)**0.5 for x, y in us)
                do = min(((x-bx)**2+(y-by)**2)**0.5 for x, y in opp)
                new_poss = poss
                if du < 1.2 and du < do: new_poss = 'us'
                elif do < 1.2 and do < du: new_poss = 'opp'
                if poss == 'us' and new_poss == 'opp' and bx > 0:
                    zone = ('area' if bx > 36 and abs(by) < 20 else
                            'tercio' if bx > 17 else 'medio')
                    band = 'izq' if by < -12 else ('der' if by > 12 else 'centro')
                    loss_zone[f"{zone}/{band}"] += 1
                    tot['perdidas_campo_rival'] += 1
                poss = new_poss
                tot['ciclos'] += 1
                if bx > 17: tot['tercio_rival'] += 1
                if bx > 36 and abs(by) < 20: tot['area_rival'] += 1
                spd = (vx*vx + vy*vy)**0.5
                if spd > 1.6 and vx > 0.4 and bx > 25 and t - last_shot_t > 20:
                    steps = 0; px, py, pvx, pvy = bx, by, vx, vy
                    while px < GOAL_X and steps < 40 and (pvx*pvx+pvy*pvy) > 0.01:
                        px += pvx; py += pvy; pvx *= 0.94; pvy *= 0.94; steps += 1
                    if px >= GOAL_X and abs(py) < GOAL_HALF + 4.0:
                        on = abs(py) < GOAL_HALF
                        tot['remates'] += 1
                        if on: tot['remates_al_arco'] += 1
                        shots_detail.append((os.path.basename(path), t,
                                             round(bx,1), round(by,1),
                                             'ON' if on else 'off'))
                        last_shot_t = t

n = max(1, npart)
print(f"partidos: {npart}")
for k in ('goles','remates','remates_al_arco','perdidas_campo_rival'):
    print(f"  {k:22s} {tot[k]:4d}  ({tot[k]/n:.1f}/partido)")
if tot['ciclos']:
    print(f"  %ciclos tercio rival    {100.0*tot['tercio_rival']/tot['ciclos']:.1f}%")
    print(f"  %ciclos area rival      {100.0*tot['area_rival']/tot['ciclos']:.1f}%")
print("\nPERDIDAS en campo rival por zona/banda:")
for z, c in loss_zone.most_common(12):
    print(f"    {z:15s} {c}")
print("\nREMATES:")
for s in shots_detail:
    print("   ", *s)
