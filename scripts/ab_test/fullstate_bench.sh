#!/bin/bash
# =============================================================================
# fullstate_bench.sh — BENCHMARK del costo del RUIDO (solo medición local).
#
# Corre N partidos completos vs SRBIAU2D (nativo) donde NUESTRO equipo juega
# con VISIÓN PERFECTA: el server manda fullstate al lado de RoboTech
# (side-aware: alterna lados y el flag lo sigue) y nuestros jugadores arrancan
# con `--fullstate override` (use_fullstate=true → el WorldModel se actualiza
# con la verdad exacta). El rival juega normal (su cliente ignora fullstate).
#
# La comparación contra una tanda normal del MISMO build responde: ¿cuánto nos
# cuesta el ruido de percepción? (techo de lo que un denoiser puede recuperar)
#
# PROHIBIDO en competencia — jamás poner --fullstate override en la entrega.
#
# Uso: scripts/ab_test/fullstate_bench.sh [N]   (default 6)
#   FSMODE=override  (default) vision perfecta DECIDE (benchmark del techo)
#   FSMODE=reference juego NORMAL + verdad de referencia (auditoria de ruido:
#                    los agentes escriben build/bin/noise_audit.csv)
# =============================================================================
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../.." && pwd)"
OURBIN="$REPO/build/bin"
OPP_DIR="$HOME/rc/teams/base_teams/SRBIAU2D"
SRV="${RCSS_SERVER:-$(command -v rcssserver)}"
OUT="${OUTDIR:-$HOME/rc/logs/fullstate_bench}"
N="${1:-6}"
FSMODE="${FSMODE:-override}"
HALF=300

[ -x "$SRV" ] || { echo "ERROR: rcssserver no encontrado"; exit 1; }
[ -x "$OURBIN/sample_player" ] || { echo "ERROR: compila el equipo"; exit 1; }
mkdir -p "$OUT"
CSV="$OUT/results.csv"; [ -f "$CSV" ] || echo "stamp,match,side,our_goals,opp_goals" > "$CSV"

srv_pid=""
cleanup() {
  [ -n "${srv_pid:-}" ] && kill "$srv_pid" 2>/dev/null
  pkill -f "$OURBIN/sample_player" 2>/dev/null
  pkill -f "$OURBIN/sample_coach"  2>/dev/null
}
trap 'cleanup; exit 130' INT TERM
trap cleanup EXIT

for i in $(seq 1 "$N"); do
  cleanup; sleep 1
  stamp="$(date +%Y%m%d_%H%M%S)"; tag="${stamp}_fs${i}"

  # lados alternados; el flag fullstate sigue a RoboTech
  if [ $((i % 2)) -eq 1 ]; then side=l; fsopt="server::fullstate_l=true server::fullstate_r=false";
  else side=r; fsopt="server::fullstate_l=false server::fullstate_r=true"; fi

  "$SRV" server::auto_mode=true server::synch_mode=true $fsopt \
      server::half_time=$HALF server::nr_normal_halfs=2 server::nr_extra_halfs=0 \
      server::penalty_shoot_outs=false \
      server::game_logging=true server::text_logging=false \
      server::game_log_dir="'$OUT'" server::game_log_fixed=true \
      server::game_log_fixed_name="'$tag'" server::game_log_compression=0 \
      > "$OUT/${tag}.out" 2>&1 &
  srv_pid=$!
  sleep 2

  if [ "$side" = "l" ]; then
    ( cd "$OURBIN" && ./start.sh -h localhost -p 6000 -t RoboTech --fullstate "$FSMODE" >/dev/null 2>&1 )
    sleep 1
    ( cd "$OPP_DIR" && ./start.sh -h localhost -p 6000 >/dev/null 2>&1 )
  else
    ( cd "$OPP_DIR" && ./start.sh -h localhost -p 6000 >/dev/null 2>&1 )
    sleep 1
    ( cd "$OURBIN" && ./start.sh -h localhost -p 6000 -t RoboTech --fullstate "$FSMODE" >/dev/null 2>&1 )
  fi

  for s in $(seq 1 900); do kill -0 $srv_pid 2>/dev/null || break; sleep 1; done
  cleanup

  out="$OUT/${tag}.out"
  score=$(grep -E "Score:" "$out" 2>/dev/null | tail -1)
  L=$(echo "$score" | sed -E 's/.*Score:[[:space:]]*([0-9]+)[[:space:]]*-[[:space:]]*([0-9]+).*/\1/')
  R=$(echo "$score" | sed -E 's/.*Score:[[:space:]]*([0-9]+)[[:space:]]*-[[:space:]]*([0-9]+).*/\2/')
  if [ "$side" = "l" ]; then og=$L; pg=$R; else og=$R; pg=$L; fi
  echo "  [FS $i/$N side=$side] us=${og:-?} opp=${pg:-?}  ($tag)"
  echo "$tag,$i,$side,${og:-?},${pg:-?}" >> "$CSV"
done
echo "LISTO → $OUT"
