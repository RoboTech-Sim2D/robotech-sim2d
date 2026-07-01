#!/bin/bash
# =============================================================================
# A/B validation of the DNN field evaluator AGAINST AN EXTERNAL OPPONENT.
#
# Answers: "does field_eval_weights.txt (DNN) improve our results vs a real
# opponent, not just vs our own baseline?"  Runs two conditions against the
# SAME opponent and compares goal difference:
#     Condition ON : our team WITH    field_eval_weights.txt
#     Condition OFF: our team WITHOUT field_eval_weights.txt
# Sides are alternated every match to cancel left/right bias.
#
# Portable: no hard-coded personal paths. Auto-detects the repo, and takes the
# opponent launcher + optional server/lib paths as arguments / env vars.
#
# ---- REQUIREMENTS (on the machine that runs this) ----------------------------
#   * Our team already built:  <repo>/build/bin/sample_player + start.sh
#   * Weights present in <repo>/src/ :  field_eval_weights.txt, unmark_dnn_weights.txt
#   * rcssserver on PATH, or pass its path as env RCSS_SERVER=/path/to/rcssserver
#   * The opponent team launcher (its own start.sh) that accepts -h HOST -p PORT
#
# ---- USAGE -------------------------------------------------------------------
#   ./run_ab_vs_opponent.sh "<opponent_start_cmd>" [N_matches] [half_secs]
#
#   <opponent_start_cmd>  e.g.  "/home/user/helios/build/bin/start.sh"
#                         (quoted; it is called with  -h localhost -p 6000)
#   N_matches   matches PER condition (default 10 -> 20 total)
#   half_secs   half length in sim-seconds (default 300 = full 6000-cycle match)
#
#   Optional env:
#     RCSS_SERVER   path to rcssserver         (default: found on PATH)
#     OUR_LIBPATH   dir with our librcsc .so   (default: taken from our start.sh)
#     WORKDIR       output dir                 (default: <repo>/ab_results_vs_opp)
# =============================================================================
set -u

OPP_CMD="${1:?Pass the opponent start command as arg 1 (quoted)}"
N="${2:-10}"
HALF="${3:-300}"

# --- locate our repo relative to this script (scripts/ab_test/ -> repo root) ---
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$SCRIPT_DIR/../.." && pwd)"
BIN="$REPO/build/bin"

SRV="${RCSS_SERVER:-$(command -v rcssserver)}"
[ -x "$SRV" ] || { echo "ERROR: rcssserver not found. Set RCSS_SERVER=/path/to/rcssserver"; exit 1; }
[ -x "$BIN/sample_player" ] || { echo "ERROR: build our team first ($BIN/sample_player missing)"; exit 1; }
[ -f "$REPO/src/field_eval_weights.txt" ] || { echo "ERROR: $REPO/src/field_eval_weights.txt missing"; exit 1; }
[ -f "$REPO/src/unmark_dnn_weights.txt" ]  || { echo "ERROR: $REPO/src/unmark_dnn_weights.txt missing"; exit 1; }

WORK="${WORKDIR:-$REPO/ab_results_vs_opp}"
rm -rf "$WORK"; mkdir -p "$WORK/run_on" "$WORK/run_off" "$WORK/logs"
# unmark DNN identical in both; only run_on gets the field-eval DNN under test
cp "$REPO/src/unmark_dnn_weights.txt" "$WORK/run_on/"
cp "$REPO/src/unmark_dnn_weights.txt" "$WORK/run_off/"
cp "$REPO/src/field_eval_weights.txt" "$WORK/run_on/"

echo "REPO=$REPO"
echo "SERVER=$SRV ($($SRV --version 2>/dev/null | head -1))"
echo "OPPONENT=$OPP_CMD"
echo "N=$N per condition, half=$HALF s"
echo ""

run_condition() {   # $1 = ON|OFF ; $2 = our run dir ; $3 = our team name
  local tag="$1" rundir="$2" myname="$3"
  local ourG=0 oppG=0 ourW=0 oppW=0 draw=0
  echo "======== Condition $tag : $myname vs opponent ========"
  for i in $(seq 1 "$N"); do
    pkill -f sample_player 2>/dev/null; pkill -f sample_coach 2>/dev/null; pkill -f rcssserver 2>/dev/null
    sleep 1
    local name="${tag}_m${i}"
    "$SRV" server::auto_mode=true server::synch_mode=true \
        server::half_time=$HALF server::nr_normal_halfs=2 server::nr_extra_halfs=0 \
        server::penalty_shoot_outs=false \
        server::game_logging=true server::text_logging=false \
        server::game_log_dir="$WORK/logs" server::game_log_fixed=true \
        server::game_log_fixed_name="$name" server::game_log_compression=0 \
        > "$WORK/logs/${name}.out" 2>&1 &
    local srvpid=$!
    sleep 2
    if [ $((i % 2)) -eq 1 ]; then           # odd: our team left
      ( cd "$rundir" && "$BIN/start.sh" -h localhost -p 6000 -t "$myname" >/dev/null 2>&1 ); sleep 1
      ( eval "$OPP_CMD -h localhost -p 6000" >/dev/null 2>&1 )
    else                                    # even: our team right
      ( eval "$OPP_CMD -h localhost -p 6000" >/dev/null 2>&1 ); sleep 1
      ( cd "$rundir" && "$BIN/start.sh" -h localhost -p 6000 -t "$myname" >/dev/null 2>&1 )
    fi
    for s in $(seq 1 300); do kill -0 $srvpid 2>/dev/null || break; sleep 1; done
    kill $srvpid 2>/dev/null; pkill -f sample_player 2>/dev/null; pkill -f sample_coach 2>/dev/null

    local out="$WORK/logs/${name}.out"
    local vs left_name score L R
    vs=$(grep -oE "'[^']*' vs '[^']*'" "$out" | tail -1)
    left_name=$(echo "$vs" | sed -E "s/'([^']*)' vs '([^']*)'/\1/")
    score=$(grep -E "Score:" "$out" | tail -1)
    L=$(echo "$score" | sed -E 's/.*Score:[[:space:]]*([0-9]+)[[:space:]]*-[[:space:]]*([0-9]+).*/\1/')
    R=$(echo "$score" | sed -E 's/.*Score:[[:space:]]*([0-9]+)[[:space:]]*-[[:space:]]*([0-9]+).*/\2/')
    if [ "$left_name" = "$myname" ]; then og=$L; pg=$R; else og=$R; pg=$L; fi
    ourG=$((ourG+og)); oppG=$((oppG+pg))
    if [ $og -gt $pg ]; then ourW=$((ourW+1)); elif [ $pg -gt $og ]; then oppW=$((oppW+1)); else draw=$((draw+1)); fi
    echo "  [$tag $i/$N] us=$og opp=$pg   (W=$ourW L=$oppW D=$draw  goals us=$ourG opp=$oppG)"
    echo "$tag,$i,$og,$pg" >> "$WORK/results.csv"
  done
  echo "  >>> $tag TOTAL: W=$ourW L=$oppW D=$draw  goals us=$ourG opp=$oppG  netGD=$((ourG-oppG))"
  echo "$tag $ourW $oppW $draw $ourG $oppG $((ourG-oppG))" >> "$WORK/summary.txt"
}

echo "cond,match,our_goals,opp_goals" > "$WORK/results.csv"
: > "$WORK/summary.txt"

run_condition ON  "$WORK/run_on"  "RT_ON"
run_condition OFF "$WORK/run_off" "RT_OFF"

pkill -f rcssserver 2>/dev/null; pkill -f sample_player 2>/dev/null; pkill -f sample_coach 2>/dev/null
echo ""
echo "================ VERDICT (vs external opponent) ================"
awk '{printf "  %-3s: W=%s L=%s D=%s  goals %s-%s  netGD=%+d\n",$1,$2,$3,$4,$5,$6,$7}' "$WORK/summary.txt"
gd_on=$(awk '$1=="ON"{print $7}'  "$WORK/summary.txt")
gd_off=$(awk '$1=="OFF"{print $7}' "$WORK/summary.txt")
echo "  ---------------------------------------------------------------"
echo "  DNN effect vs this opponent:  netGD(ON) - netGD(OFF) = $((gd_on - gd_off))"
echo "  (>0 means the DNN helps against this opponent; <=0 means it does not)"
echo "  Full data: $WORK/results.csv   Replays: $WORK/logs/*.rcg"
