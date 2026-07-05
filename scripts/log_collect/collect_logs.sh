#!/bin/bash
# =============================================================================
# collect_logs.sh — junta DATOS para el DNN.
#
# Juega partidos COMPLETOS de RoboTech contra varios rivales y GUARDA cada
# replay (.rcg) de forma PERSISTENTE (NUNCA borra) para tener más entradas de
# entrenamiento. Más partidos = más datos.  A diferencia del harness A/B, este
# NO hace `rm -rf`: cada .rcg queda en  $OUTROOT/<rival>/<fecha>_mN.rcg .
#
# ---- USO --------------------------------------------------------------------
#   scripts/log_collect/collect_logs.sh [N] [rival1 rival2 ...]
#
#     N        partidos por rival        (default 10)
#     rivalX   nombres de la tabla OPPONENTS de abajo. Si no pones ninguno,
#              corre contra TODOS los configurados.
#
#   Ejemplos:
#     collect_logs.sh                      # 10 partidos vs cada rival
#     collect_logs.sh 20                   # 20 vs cada rival
#     collect_logs.sh 15 YuShan2025 RoboCIn   # 15 vs esos dos
#     collect_logs.sh 5 FRA-UNIted SRBIAU2D   # solo los nativos (sin sudo)
#
#   Variables de entorno opcionales:
#     OUTROOT   dónde guardar (default ~/rc/logs/dnn_data)
#     HALF      medio tiempo en seg-sim   (default 300 = partido completo 6000c)
#     SYNCH     true=rápido / false=tiempo real (default true)
#     DOCKER    comando docker            (default "sudo docker")
#     IMAGE     imagen para rivales docker(default ubuntu:22.04)
#     RCSS_SERVER  ruta a rcssserver      (default: la del PATH)
# =============================================================================
set -u

# --- localizar repo (scripts/log_collect/ -> repo) ---------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../.." && pwd)"
OURBIN="$REPO/build/bin"
BT="$HOME/rc/teams/base_teams"

SRV="${RCSS_SERVER:-$(command -v rcssserver)}"
OUTROOT="${OUTROOT:-$HOME/rc/logs/dnn_data}"
HALF="${HALF:-300}"
SYNCH="${SYNCH:-true}"
DOCKER="${DOCKER:-sudo docker}"
IMAGE="${IMAGE:-ubuntu:22.04}"
KEEPALIVE=900
OURNAME="RoboTech"

# --- TABLA DE RIVALES:  name | runtime | dir | launcher | argstyle -----------
#   runtime  = native | docker
#   launcher = script que lanza TODO el equipo (background + return)
#   argstyle = hp   -> se le pasa  -h localhost -p 6000
#              none -> hardcodea localhost:6000 (se llama sin args)
#   synch    = true  -> synch_mode (rápido, ~24 s/partido)
#              false -> TIEMPO REAL (~10 min/partido). FRA-UNIted NO implementa
#                       el ack (done) de synch: en synch pierde TODO ciclo desde
#                       el 1 y el server aborta ("Waiting too long for clients").
# (los rápidos de synch primero; FRA-UNIted al final por ser tiempo real/lento)
OPPONENTS=(
  "SRBIAU2D|native|$BT/SRBIAU2D|./start.sh|hp|true"
  "HELIOS2025|docker|$BT/HELIOS2025|./start.sh|none|true"
  "RoboCIn|docker|$BT/RoboCIn|./startAll|none|true"
  "TitasdaRobotica|docker|$BT/TitasdaRobotica|./start.sh|hp|true"
  "YuShan2025|docker|$BT/YuShan2025|./start.sh|hp|true"
  "FRA-UNIted|docker|$BT/FRA-UNIted|./startlocal.sh|none|false"
)

# --- parseo de args ----------------------------------------------------------
N=10
if [ $# -gt 0 ] && [[ "$1" =~ ^[0-9]+$ ]]; then N="$1"; shift; fi
SELECT=("$@")   # rivales pedidos (vacío = todos)

# --- validaciones ------------------------------------------------------------
[ -x "$SRV" ] || { echo "ERROR: rcssserver no encontrado. Exporta RCSS_SERVER=/ruta/rcssserver"; exit 1; }
[ -x "$OURBIN/sample_player" ] || { echo "ERROR: compila nuestro equipo ($OURBIN/sample_player no existe)"; exit 1; }
[ -x "$OURBIN/start.sh" ]      || { echo "ERROR: falta $OURBIN/start.sh"; exit 1; }

want() {  # ¿este rival está en la selección? (vacío = todos)
  [ ${#SELECT[@]} -eq 0 ] && return 0
  local x; for x in "${SELECT[@]}"; do [ "$x" = "$1" ] && return 0; done
  return 1
}

# --- ¿algún rival docker en la tanda? -> keepalive de sudo -------------------
need_docker=false
for row in "${OPPONENTS[@]}"; do
  IFS='|' read -r name rt dir launch style osynch <<< "$row"
  want "$name" || continue
  [ "$rt" = "docker" ] && need_docker=true
done
SUDO_KEEPALIVE_PID=""
if [ "$need_docker" = true ] && [ "${DOCKER%% *}" = "sudo" ]; then
  echo ">> Rivales docker en la tanda -> autenticando sudo (una vez)…"
  sudo -v || { echo "ERROR: sudo requerido para docker"; exit 1; }
  ( while true; do sudo -n true; sleep 50; done ) 2>/dev/null &
  SUDO_KEEPALIVE_PID=$!
fi

local_srv=""   # PID del rcssserver del partido en curso (para poder matarlo)

cleanup_all() {
  [ -n "${SUDO_KEEPALIVE_PID:-}" ] && kill "$SUDO_KEEPALIVE_PID" 2>/dev/null
  [ -n "${local_srv:-}" ] && kill "$local_srv" 2>/dev/null          # server actual por PID
  $DOCKER rm -f rt_opp >/dev/null 2>&1
  pkill -f "$OURBIN/sample_player" 2>/dev/null
  pkill -f "$OURBIN/sample_coach"  2>/dev/null
  [ -n "$SRV" ] && pkill -f "$SRV" 2>/dev/null                       # rcssserver huerfano
}

# Ctrl-C / kill: limpiar y SALIR (antes solo limpiaba y el bucle seguia lanzando
# partidos -> parecia que "abria varios simuladores y no paraba").
on_interrupt() {
  echo ""; echo ">> Senal recibida (Ctrl-C) — deteniendo y limpiando…"
  cleanup_all
  exit 130
}
trap on_interrupt INT TERM   # interrupcion -> limpia y sale de verdad
trap cleanup_all  EXIT       # salida normal  -> solo limpia

kill_match() {   # limpia entre partidos (server por PID, no por -f)
  [ -n "${1:-}" ] && kill "$1" 2>/dev/null
  $DOCKER rm -f rt_opp >/dev/null 2>&1
  pkill -f "$OURBIN/sample_player" 2>/dev/null
  pkill -f "$OURBIN/sample_coach"  2>/dev/null
  sleep 1
}

launch_opponent() {   # $1 runtime  $2 dir  $3 launcher  $4 argstyle
  local rt="$1" dir="$2" launch="$3" style="$4" args=""
  [ "$style" = "hp" ] && args="-h localhost -p 6000"
  if [ "$rt" = "native" ]; then
    ( cd "$dir" && $launch $args >/dev/null 2>&1 )
  else
    $DOCKER rm -f rt_opp >/dev/null 2>&1
    $DOCKER run -d --rm --name rt_opp --network host \
        -v "$dir":/team -w /team "$IMAGE" \
        bash -c "$launch $args; sleep $KEEPALIVE" >/dev/null 2>&1
  fi
}

echo "REPO=$REPO"
echo "SERVER=$SRV ($($SRV --version 2>/dev/null | head -1))"
echo "OUTROOT=$OUTROOT   N=$N/rival   half=${HALF}s   synch=$SYNCH"
echo ""

# ============================ bucle principal ===============================
for row in "${OPPONENTS[@]}"; do
  IFS='|' read -r name rt dir launch style osynch <<< "$row"
  want "$name" || continue
  if [ ! -d "$dir" ]; then echo "!! $name: no existe $dir — saltado"; continue; fi

  OUT="$OUTROOT/$name"; mkdir -p "$OUT"
  CSV="$OUT/results.csv"; [ -f "$CSV" ] || echo "stamp,match,our_goals,opp_goals" > "$CSV"
  # synch efectivo: el de la tabla, salvo que el env SYNCH=false lo fuerce a real-time
  eff_synch="${osynch:-true}"; [ "$SYNCH" = "false" ] && eff_synch="false"
  mode=$([ "$eff_synch" = "true" ] && echo "synch~24s" || echo "REALTIME~10min")
  echo "════════ $name ($rt, $mode) — $N partidos → $OUT ════════"

  for i in $(seq 1 "$N"); do
    kill_match ""
    stamp="$(date +%Y%m%d_%H%M%S)"
    tag="${stamp}_m${i}"

    "$SRV" server::auto_mode=true server::synch_mode=$eff_synch \
        server::half_time=$HALF server::nr_normal_halfs=2 server::nr_extra_halfs=0 \
        server::penalty_shoot_outs=false \
        server::game_logging=true server::text_logging=false \
        server::game_log_dir="'$OUT'" server::game_log_fixed=true \
        server::game_log_fixed_name="'$tag'" server::game_log_compression=0 \
        > "$OUT/${tag}.out" 2>&1 &
    local_srv=$!
    sleep 2

    # ALTERNAR LADOS: partidos impares nosotros IZQUIERDA, pares DERECHA
    # (el primero en conectar queda left; cancela el sesgo de lado).
    # CWD=build/bin para cargar pesos+formaciones. El parseo del marcador de
    # abajo ya es side-aware (usa "'L' vs 'R'" del .out), no necesita cambios.
    if [ $((i % 2)) -eq 1 ]; then
      ( cd "$OURBIN" && ./start.sh -h localhost -p 6000 -t "$OURNAME" >/dev/null 2>&1 )
      sleep 1
      launch_opponent "$rt" "$dir" "$launch" "$style"
    else
      launch_opponent "$rt" "$dir" "$launch" "$style"
      sleep 1
      ( cd "$OURBIN" && ./start.sh -h localhost -p 6000 -t "$OURNAME" >/dev/null 2>&1 )
    fi

    # esperar a que el server termine solo (auto_mode); tope generoso
    for s in $(seq 1 900); do kill -0 $local_srv 2>/dev/null || break; sleep 1; done
    kill_match "$local_srv"

    # marcador (best-effort; el dato REAL es el .rcg)
    out="$OUT/${tag}.out"
    vs=$(grep -oE "'[^']*' vs '[^']*'" "$out" 2>/dev/null | tail -1)
    left=$(echo "$vs" | sed -E "s/'([^']*)' vs '([^']*)'/\1/")
    score=$(grep -E "Score:" "$out" 2>/dev/null | tail -1)
    L=$(echo "$score" | sed -E 's/.*Score:[[:space:]]*([0-9]+)[[:space:]]*-[[:space:]]*([0-9]+).*/\1/')
    R=$(echo "$score" | sed -E 's/.*Score:[[:space:]]*([0-9]+)[[:space:]]*-[[:space:]]*([0-9]+).*/\2/')
    if [ "$left" = "$OURNAME" ]; then og=$L; pg=$R; else og=$R; pg=$L; fi
    [ -z "${og:-}" ] && og="?"; [ -z "${pg:-}" ] && pg="?"
    rcg_ok=$([ -f "$OUT/${tag}.rcg" ] && echo "rcg✓" || echo "rcg✗")
    echo "  [$name $i/$N] us=$og opp=$pg  $rcg_ok  ($tag)"
    echo "$tag,$i,$og,$pg" >> "$CSV"
  done
  echo ""
done

cleanup_all; trap - EXIT INT TERM
echo "================================================================"
echo "LISTO. Replays (.rcg) para el DNN en:  $OUTROOT/<rival>/"
echo "Conteo por rival:"
for row in "${OPPONENTS[@]}"; do
  IFS='|' read -r name rt dir launch style osynch <<< "$row"
  want "$name" || continue
  n=$(ls "$OUTROOT/$name"/*.rcg 2>/dev/null | wc -l)
  printf "  %-16s %s replays\n" "$name" "$n"
done
