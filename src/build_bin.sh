#!/bin/bash
# =============================================================================
# build_bin.sh — arma el paquete de COMPETENCIA de RoboTech.
#
# Molde = base_teams/SRBIAU2D (nuestro gemelo: mismos sample_player/sample_coach,
# formations-dt/keeper/taker, unmark_dnn_weights.txt, team_logo.xpm, todo en la
# RAÍZ). Formato `start` común a RoboCIn/SRBIAU2D/Titans: recibe HOST BASEDIR NUM,
# hace `cd $BASEDIR` y lanza UN agente (1=portero, 12=coach, resto=jugador).
#
#   * El directorio se llama igual que el equipo:  RoboTech/
#   * Rutas relativas (no depende de esta máquina); lib de agentes en lib/.
#   * Pesos DNN en la raíz (el player los carga por CWD, y `start` hace cd al dir).
#   * Logo cargado por el coach con --team_graphic_file (el usuario lo pidió).
#   * localStartAll para verificar (como indica el reglamento).
#   * Comprime en  RoboTech.tar.gz.
#
# Uso:   bash src/build_bin.sh
# Salida: <repo>/release/RoboTech/  +  <repo>/release/RoboTech.tar.gz
# =============================================================================
set -eu

TEAM="RoboTech"
COUNTRY="mexico"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/.." && pwd)"
BIN="$REPO/build/bin"
OUTROOT="$REPO/release"
DEST="$OUTROOT/$TEAM"

[ -x "$BIN/sample_player" ] || { echo "ERROR: falta $BIN/sample_player (compila: make -C build)"; exit 1; }
[ -x "$BIN/sample_coach" ]  || { echo "ERROR: falta $BIN/sample_coach"; exit 1; }

echo ">> Limpiando destino…"
rm -rf "$DEST" "$OUTROOT/$TEAM.tar.gz"
mkdir -p "$DEST/lib"

echo ">> Copiando binarios, configs y formación (a la RAÍZ)…"
cp "$BIN/sample_player" "$BIN/sample_coach" "$DEST/"
cp "$BIN/player.conf"   "$BIN/coach.conf"   "$DEST/"
# Solo formations-dt: es la única que usan start/start.sh (config_dir=formations-dt).
# keeper/taker son de keepaway (modo entrenamiento), no de competición.
cp -r "$BIN/formations-dt" "$DEST/"

echo ">> Copiando pesos DNN (raíz = CWD tras 'cd \$BASEDIR')…"
cp "$BIN/field_eval_weights.txt" "$BIN/unmark_dnn_weights.txt" "$DEST/" 2>/dev/null || \
  echo "   (aviso: sin pesos DNN → correría solo heurística)"

echo ">> Copiando logo del equipo (mismo nombre que dice coach.conf: robotech_logo.xpm)…"
cp "$BIN/robotech_logo.xpm" "$DEST/"

echo ">> Copiando librcsc (copias reales dereferenciadas)…"
LIBFILE="$(ldd "$BIN/sample_player" | awk '/librcsc/{print $3; exit}')"
[ -n "${LIBFILE:-}" ] && [ -e "$LIBFILE" ] || { echo "ERROR: no pude resolver librcsc con ldd"; exit 1; }
REAL="$(readlink -f "$LIBFILE")"
SONAME="$(basename "$LIBFILE")"                 # librcsc.so.18 (soname que pide el binario)
cp "$REAL" "$DEST/lib/$(basename "$REAL")"      # librcsc.so.18.0.0
cp "$REAL" "$DEST/lib/$SONAME"                  # librcsc.so.18
cp "$REAL" "$DEST/lib/librcsc.so"               # librcsc.so
echo "   librcsc: $SONAME  desde  $(dirname "$LIBFILE")"

echo ">> Generando 'start' (HOST BASEDIR NUM, molde SRBIAU2D)…"
cat > "$DEST/start" <<'START_EOF'
#!/bin/sh
HOST=$1
BASEDIR=$2
NUM=$3

LIBPATH=./lib
if [ x"$LIBPATH" != x ]; then
  if [ x"$LD_LIBRARY_PATH" = x ]; then
    LD_LIBRARY_PATH=$LIBPATH
  else
    LD_LIBRARY_PATH=$LIBPATH:$LD_LIBRARY_PATH
  fi
  export LD_LIBRARY_PATH
fi

teamname="RoboTech"

player="./sample_player"
coach="./sample_coach"

config="./player.conf"
coach_config="./coach.conf"
config_dir="./formations-dt"

opt="--player-config ${config} --config_dir ${config_dir}"
opt="${opt} -h ${HOST} -t ${teamname}"

coachopt="--coach-config ${coach_config}"
coachopt="${coachopt} -h ${HOST} -t ${teamname}"
coachopt="${coachopt} --use_team_graphic on --team_graphic_file ./robotech_logo.xpm"

cd $BASEDIR

case $NUM in
    1)
        $player $opt -g
        ;;
    12)
        $coach $coachopt
        ;;
    *)
        $player $opt
        ;;
esac
START_EOF
chmod +x "$DEST/start"

echo ">> Generando 'start.sh' (el helios de siempre, con -h/-p/-t; lib relativa)…"
# Copia src/start.sh (launcher helios completo) pero: se sitúa en su propio dir
# (para que ./lib y los pesos DNN por CWD resuelvan) y usa la lib EMBEBIDA.
{
  head -1 "$REPO/src/start.sh"                        # #!/bin/sh
  echo 'cd "$(dirname "$0")" 2>/dev/null || exit 1'   # CWD = dir del paquete
  tail -n +2 "$REPO/src/start.sh" \
    | sed -e 's#^LIBPATH=.*#LIBPATH="./lib"#' \
          -e 's#^DIR=.*#DIR="."#'
} > "$DEST/start.sh"
chmod +x "$DEST/start.sh"

echo ">> Generando 'localStartAll' (verificación local: 11 jugadores + coach)…"
cat > "$DEST/localStartAll" <<'LSA_EOF'
#!/bin/sh
./start 127.0.0.1 . 1 &
sleep 2
i=2
while [ $i -le 12 ]; do
  ./start 127.0.0.1 . $i &
  sleep 0.3
  i=`expr $i + 1`
done
LSA_EOF
chmod +x "$DEST/localStartAll"

echo ">> Generando 'kill'…"
cat > "$DEST/kill" <<'K_EOF'
#!/bin/sh
killall -TERM sample_player
killall -TERM sample_coach
sleep 2
killall -KILL sample_player
killall -KILL sample_coach
K_EOF
chmod +x "$DEST/kill"

printf -- '---\ncountry: %s\n' "$COUNTRY" > "$DEST/team.yml"
chmod +x "$DEST/sample_player" "$DEST/sample_coach"

echo ">> Comprimiendo  $TEAM.tar.gz …"
( cd "$OUTROOT" && tar czvfp "$TEAM.tar.gz" "$TEAM/" >/dev/null )

echo ""
echo "✅ Paquete listo:"
echo "   Carpeta : $DEST"
echo "   Archivo : $OUTROOT/$TEAM.tar.gz"
echo ""
echo "   Contenido raíz:"
( cd "$DEST" && ls -1 | sed 's/^/     /' )
