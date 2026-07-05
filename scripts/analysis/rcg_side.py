#!/usr/bin/env python3
"""Helper compartido: autodetecta de qué lado juega RoboTech en un .rcg (ULG6)
y da los regex/signo para normalizar coordenadas como si SIEMPRE fuéramos el
equipo IZQUIERDO (atacando +x). Para lado derecho se rota 180°: (x,y)→(−x,−y),
que preserva la semántica de bandas y profundidad.

Uso:
    from rcg_side import side_info
    side, sgn, us_re, opp_re, our_goal_pm = side_info(path)
    # coord normalizada = valor * sgn ; gol nuestro = playmode our_goal_pm
"""
import re

TEAM_RE = re.compile(r'^\(team \d+ (\S+) (\S+)')

def player_re(side):
    return re.compile(
        r'\(\(%s (\d+)\) \d+ 0x[0-9a-f]+ ([-\d.]+) ([-\d.]+)' % side)

def is_ours(name):
    # collect_logs usa "RoboTech"; el harness A/B usa "RT_ON"/"RT_OFF"
    return name.startswith('RoboTech') or name.startswith('RT_')

def side_info(path, our_name=None):
    side = 'l'
    ours = ( (lambda n: our_name in n) if our_name else is_ours )
    with open(path, errors='ignore') as f:
        for i, line in enumerate(f):
            m = TEAM_RE.match(line)
            if m:
                left, right = m.group(1), m.group(2)
                if ours(right) and not ours(left):
                    side = 'r'
                break
            # el header (server/player params) puede ocupar cientos de lineas
            # antes del primer (team ...); en logs reales aparecio en la ~534
            if i > 5000:
                break
    opp = 'r' if side == 'l' else 'l'
    sgn = 1.0 if side == 'l' else -1.0
    our_goal_pm = 'goal_l' if side == 'l' else 'goal_r'
    return side, sgn, player_re(side), player_re(opp), our_goal_pm
