#!/usr/bin/env python3
"""
RoboTech Field Evaluation Data Extractor
=========================================
Extracts training data from RCG (game log) files to train a neural network
that evaluates field positions (replaces heuristic sample_field_evaluator).

Data flow:
  rcg log files → this script → CSV files → trainer.py → .h5 → DecodeKerasModel.py → .txt → C++

Each sample represents a game state where our team had the ball.
The label is: did our team score within the next N cycles? (binary classification)

Usage:
  python3 extract_from_logs.py /path/to/logs/ /path/to/output/

The output CSV has columns for:
  - Ball position (x, y, vx, vy)
  - All 22 player positions (x, y per player)
  - Ball holder info
  - Game score differential
  - Cycle number (normalized)
  - Label: 1 if our team scored within SCORE_HORIZON cycles, 0 otherwise
"""

import os
import sys
import gzip
import re
import csv
import math
from collections import defaultdict

# How many cycles ahead to look for a goal (defines "good position")
# 2026-07-13: 300 → 150. Con 300 el crédito se asignaba a jugadas de hace
# 30 segundos (ruido puro); 150 ≈ una posesión larga.
SCORE_HORIZON = 150

# Minimum cycle interval between samples (avoid redundant data)
SAMPLE_INTERVAL = 10


def parse_rcg_v5(filepath):
    """
    Parse RCG v5 (text-based) log file.
    Returns list of (cycle, show_data) tuples.
    """
    frames = []
    opener = gzip.open if filepath.endswith('.gz') else open

    with opener(filepath, 'rt', errors='replace') as f:
        for line in f:
            line = line.strip()
            if not line.startswith('(show'):
                continue

            # Extract cycle number
            m = re.match(r'\(show\s+(\d+)\s+', line)
            if not m:
                continue
            cycle = int(m.group(1))

            # Extract ball: ((bx by) (bvx bvy) ...)
            ball_m = re.search(r'\(\(b\)\s+([-\d.e]+)\s+([-\d.e]+)\s+([-\d.e]+)\s+([-\d.e]+)', line)
            if not ball_m:
                continue

            ball_x = float(ball_m.group(1))
            ball_y = float(ball_m.group(2))
            ball_vx = float(ball_m.group(3))
            ball_vy = float(ball_m.group(4))

            # Extract players: ((side unum) type state x y vx vy body neck [pointto] ...)
            players = {}
            for pm in re.finditer(
                r'\(\(([lr])\s+(\d+)\)\s+(\d+)\s+(\S+)\s+'
                r'([-\d.e]+)\s+([-\d.e]+)\s+([-\d.e]+)\s+([-\d.e]+)\s+'
                r'([-\d.e]+)\s+([-\d.e]+)',
                line
            ):
                side = pm.group(1)
                unum = int(pm.group(2))
                px = float(pm.group(5))
                py = float(pm.group(6))
                pvx = float(pm.group(7))
                pvy = float(pm.group(8))
                players[(side, unum)] = (px, py, pvx, pvy)

            frames.append({
                'cycle': cycle,
                'ball': (ball_x, ball_y, ball_vx, ball_vy),
                'players': players
            })

    return frames


def detect_our_side(filepath, team_name='RoboTech'):
    """
    Detect which side (l or r) our team is on by reading the (team ...) line.
    Format: (team CYCLE TeamL TeamR ScoreL ScoreR)
    """
    opener = gzip.open if filepath.endswith('.gz') else open
    with opener(filepath, 'rt', errors='replace') as f:
        for line in f:
            m = re.match(r'\(team\s+\d+\s+(\S+)\s+(\S+)', line)
            if m:
                team_l = m.group(1)
                team_r = m.group(2)
                if team_name.lower() in team_l.lower():
                    return 'l'
                elif team_name.lower() in team_r.lower():
                    return 'r'
                return 'l'
    return 'l'


def find_goal_cycles(filepath):
    """
    Find cycles where goals were scored from RCG log.
    Format: (playmode CYCLE goal_l) or (playmode CYCLE goal_r)
    Returns list of (cycle, scoring_side) tuples.
    """
    goals = []
    opener = gzip.open if filepath.endswith('.gz') else open

    with opener(filepath, 'rt', errors='replace') as f:
        for line in f:
            m = re.match(r'\(playmode\s+(\d+)\s+goal_(l|r)\)', line)
            if m:
                goals.append((int(m.group(1)), m.group(2)))
    return goals


def find_closest_player_to_ball(frame, side):
    """Find which player of `side` is closest to the ball."""
    bx, by = frame['ball'][0], frame['ball'][1]
    best_dist = float('inf')
    best_unum = 0
    for (s, unum), (px, py, _, _) in frame['players'].items():
        if s != side:
            continue
        d = math.sqrt((px - bx) ** 2 + (py - by) ** 2)
        if d < best_dist:
            best_dist = d
            best_unum = unum
    return best_unum, best_dist


def extract_features(frame, our_side='l'):
    """
    Extract 48-dimensional feature vector from a frame.
    Features:
      [0-3]   Ball: x, y, vx, vy (normalized)
      [4-25]  Our 11 players: x, y each (sorted by unum)
      [26-47] Opp 11 players: x, y each (sorted by unum)
    """
    bx, by, bvx, bvy = frame['ball']
    features = [
        bx / 52.5,     # normalize to ~[-1, 1]
        by / 34.0,
        bvx / 3.0,     # max ball speed
        bvy / 3.0,
    ]

    opp_side = 'r' if our_side == 'l' else 'l'

    # Our players (unum 1-11)
    for unum in range(1, 12):
        key = (our_side, unum)
        if key in frame['players']:
            px, py, _, _ = frame['players'][key]
            features.extend([px / 52.5, py / 34.0])
        else:
            features.extend([0.0, 0.0])  # missing player

    # Opponent players (unum 1-11)
    for unum in range(1, 12):
        key = (opp_side, unum)
        if key in frame['players']:
            px, py, _, _ = frame['players'][key]
            features.extend([px / 52.5, py / 34.0])
        else:
            features.extend([0.0, 0.0])

    return features


def process_log_file(filepath):
    """
    Process a single RCG file and return training samples.
    Automatically detects which side RoboTech is on.
    Each sample: (features, label)
    Label: 1 if our team scored within SCORE_HORIZON cycles
    """
    print(f"Processing: {filepath}")

    our_side = detect_our_side(filepath)
    print(f"  RoboTech is on side: {our_side}")

    frames = parse_rcg_v5(filepath)
    if not frames:
        print(f"  No frames found, skipping")
        return []

    # 2026-07-13 ETIQUETA SIMÉTRICA: la red vieja solo veía "¿anotamos?" y
    # aprendió a empujar jugadas que también regalaban contragolpes (−9 netGD
    # medido). Ahora el PRÓXIMO gol dentro del horizonte decide la etiqueta:
    #   nuestro → 1.0 | rival → 0.0 | ninguno → 0.5 (neutral)
    # El C++ ya interpreta 0.5 como neutro ((score-0.5)*60), cero cambios allí.
    goals = sorted(find_goal_cycles(filepath))
    print(f"  Found {len(goals)} goals total "
          f"({sum(1 for _, s in goals if s == our_side)} by RoboTech)")

    samples = []
    last_sample_cycle = -SAMPLE_INTERVAL

    for frame in frames:
        cycle = frame['cycle']

        # Sample at intervals
        if cycle - last_sample_cycle < SAMPLE_INTERVAL:
            continue

        # Only sample when our team has (or is near) the ball
        closest_unum, closest_dist = find_closest_player_to_ball(frame, our_side)
        if closest_dist > 2.0:  # ~kickable area
            continue

        # Próximo gol dentro del horizonte (el primero manda)
        label = 0.5
        for gc, gside in goals:
            if gc <= cycle:
                continue
            if gc - cycle > SCORE_HORIZON:
                break
            label = 1.0 if gside == our_side else 0.0
            break

        features = extract_features(frame, our_side)
        samples.append(features + [label])
        last_sample_cycle = cycle

    print(f"  Extracted {len(samples)} samples ({sum(1 for s in samples if s[-1] == 1)} positive)")
    return samples


def main():
    if len(sys.argv) < 3:
        print("Usage: python3 extract_from_logs.py <logs_dir> <output_dir> [exclude_dir1 exclude_dir2 ...]")
        print("  logs_dir:    directory containing .rcg or .rcg.gz files")
        print("  output_dir:  directory to write CSV output")
        print("  exclude_dirN: subdirectory names to skip (e.g. LOGS_ANTES_300526)")
        print()
        print("Example:")
        print("  python3 extract_from_logs.py /home/robotechtecnl/ data/ LOGS_ANTES_300526 Descargas")
        sys.exit(1)

    logs_dir = sys.argv[1]
    output_dir = sys.argv[2]
    # Any extra arguments are directory names to exclude from the walk
    excluded_dirs = set(sys.argv[3:]) if len(sys.argv) > 3 else set()
    if excluded_dirs:
        print(f"Excluding subdirectories: {excluded_dirs}")

    os.makedirs(output_dir, exist_ok=True)

    # Find all RCG files (skip files with "null" in name — formation tests without opponent)
    rcg_files = []
    skipped = 0
    skipped_dirs = 0
    for root, dirs, files in os.walk(logs_dir):
        # Prune excluded directories so os.walk won't descend into them.
        # dirs[:] = ... modifies the list in-place, which is what os.walk requires.
        before = len(dirs)
        dirs[:] = [d for d in dirs if d not in excluded_dirs]
        skipped_dirs += before - len(dirs)

        for f in files:
            if f.endswith('.rcg') or f.endswith('.rcg.gz'):
                if 'null' in f.lower():
                    skipped += 1
                    continue
                rcg_files.append(os.path.join(root, f))

    if skipped_dirs:
        print(f"Skipped {skipped_dirs} excluded subdirectorie(s)")

    if not rcg_files:
        print(f"No .rcg files found in {logs_dir} (skipped {skipped} null matches)")
        sys.exit(1)

    print(f"Found {len(rcg_files)} log files (skipped {skipped} null/test matches)")

    # Build header
    header = ['ball_x', 'ball_y', 'ball_vx', 'ball_vy']
    for i in range(1, 12):
        header.extend([f'our_{i}_x', f'our_{i}_y'])
    for i in range(1, 12):
        header.extend([f'opp_{i}_x', f'opp_{i}_y'])
    header.append('label')

    # Process all files (side auto-detected per file)
    all_samples = []
    for rcg_file in sorted(rcg_files):
        samples = process_log_file(rcg_file)
        all_samples.extend(samples)

    if not all_samples:
        print("No samples extracted. Check log format.")
        sys.exit(1)

    # Write CSV
    output_file = os.path.join(output_dir, 'field_eval_data.csv')
    with open(output_file, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(header)
        writer.writerows(all_samples)

    pos = sum(1 for s in all_samples if s[-1] == 1.0)
    neg = sum(1 for s in all_samples if s[-1] == 0.0)
    neu = len(all_samples) - pos - neg
    print(f"\nTotal: {len(all_samples)} samples | anotamos={pos} "
          f"({100*pos/len(all_samples):.1f}%) encajamos={neg} "
          f"({100*neg/len(all_samples):.1f}%) neutral={neu}")
    print(f"Output: {output_file}")


if __name__ == '__main__':
    main()
