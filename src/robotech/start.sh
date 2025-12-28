#!/bin/sh
#

HOST="localhost"
PORT="6000"
TEAM="RoboTech"
UNUM=0


DIR=$(dirname "$0")
PLAYER="${DIR}/sample_player"
COACH="${DIR}/sample_coach"
PCONF="${DIR}/player.conf"
CCONF="${DIR}/coach.conf"
FORMDIR="${DIR}/formations-dt"


export LD_LIBRARY_PATH="${DIR}/lib${LD_LIBRARY_PATH:+:}$LD_LIBRARY_PATH"


usage() {
  cat << EOF
Usage: $(basename "$0") [options]

Options:
  -h, --host HOST         servidor (default: $HOST)
  -p, --port PORT         puerto (default: $PORT)
  -t, --teamname NAME     nombre de equipo (default: $TEAM)
  -u, --unum NUM          número uniforme (1–11 jugador, 12 coach)  ← OBLIGATORIO
  --help                  muestra esta ayuda
EOF
  exit 1
}


while [ $# -gt 0 ]; do
  case "$1" in
    -h|--host)
      HOST="$2"; shift 2;;
    -p|--port)
      PORT="$2"; shift 2;;
    -t|--teamname)
      TEAM="$2"; shift 2;;
    -u|--unum)
      UNUM="$2"; shift 2;;
    --help)
      usage;;
    *)
      echo "Opción inválida: $1" >&2
      usage;;
  esac
done


if [ "$UNUM" -lt 1 ] || [ "$UNUM" -gt 12 ]; then
  echo "Error: debes pasar --unum N (1–11 jugador, 12 coach)" >&2
  usage
fi


COMMON_OPTS="--player-config \"$PCONF\" --config_dir \"$FORMDIR\" -h \"$HOST\" -p \"$PORT\" -t \"$TEAM\""

COACH_OPTS="--coach-config \"$CCONF\" -h \"$HOST\" -p \"$PORT\" -t \"$TEAM\" --use_team_graphic off"

cd "$DIR" || exit 1


if [ "$UNUM" -eq 12 ]; then
  echo "▶ Iniciando COACH (unum=12)…"
  exec $COACH $COACH_OPTS
else
  echo "▶ Iniciando PLAYER #$UNUM…"
  exec $PLAYER $COMMON_OPTS --unum "$UNUM"
fi

